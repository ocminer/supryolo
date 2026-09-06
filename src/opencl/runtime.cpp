// SPDX-License-Identifier: MIT
#define CL_TARGET_OPENCL_VERSION 120
#include "opencl_kernel.hpp"
#include "yolo/core.hpp"
#include "yolo/hardware.hpp"
#include <CL/cl.h>
#include <CL/cl_ext.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <dlfcn.h>
#include <set>
#include <stdexcept>
namespace yolo {
namespace {
void check(cl_int code, const char *operation) {
  if (code != CL_SUCCESS)
    throw std::runtime_error(std::string(operation) + ": OpenCL error " + std::to_string(code));
}
struct Api {
  void *library = nullptr;
#define FUNCTION(name) decltype(&::name) name = nullptr;
  FUNCTION(clGetPlatformIDs)
  FUNCTION(clGetDeviceIDs)
  FUNCTION(clGetDeviceInfo)
  FUNCTION(clCreateContext)
  FUNCTION(clReleaseContext)
  FUNCTION(clCreateCommandQueue)
  FUNCTION(clReleaseCommandQueue) FUNCTION(clCreateProgramWithSource) FUNCTION(clBuildProgram)
      FUNCTION(clGetProgramBuildInfo) FUNCTION(clReleaseProgram) FUNCTION(clCreateKernel)
          FUNCTION(clReleaseKernel) FUNCTION(clGetKernelWorkGroupInfo) FUNCTION(clSetKernelArg)
              FUNCTION(clCreateBuffer) FUNCTION(clReleaseMemObject) FUNCTION(clEnqueueWriteBuffer)
                  FUNCTION(clEnqueueReadBuffer) FUNCTION(clEnqueueNDRangeKernel) FUNCTION(clFinish)
#undef FUNCTION
                      Api() {
    library = dlopen("libOpenCL.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library)
      return;
#define LOAD(name)                                                                                 \
  name = reinterpret_cast<decltype(name)>(dlsym(library, #name));                                  \
  if (!name) {                                                                                     \
    dlclose(library);                                                                              \
    library = nullptr;                                                                             \
    return;                                                                                        \
  }
    LOAD(clGetPlatformIDs)
    LOAD(clGetDeviceIDs)
    LOAD(clGetDeviceInfo)
    LOAD(clCreateContext)
    LOAD(clReleaseContext)
    LOAD(clCreateCommandQueue)
    LOAD(clReleaseCommandQueue) LOAD(clCreateProgramWithSource) LOAD(clBuildProgram)
        LOAD(clGetProgramBuildInfo) LOAD(clReleaseProgram) LOAD(clCreateKernel)
            LOAD(clReleaseKernel) LOAD(clGetKernelWorkGroupInfo) LOAD(clSetKernelArg)
                LOAD(clCreateBuffer) LOAD(clReleaseMemObject) LOAD(clEnqueueWriteBuffer)
                    LOAD(clEnqueueReadBuffer) LOAD(clEnqueueNDRangeKernel) LOAD(clFinish)
#undef LOAD
  }
  ~Api() {
    if (library)
      dlclose(library);
  }
};
Api &api() {
  static Api a;
  return a;
}
template <class T> bool query(cl_device_id d, cl_device_info key, T &out) {
  return api().clGetDeviceInfo(d, key, sizeof(T), &out, nullptr) == CL_SUCCESS;
}
std::string string_info(cl_device_id d, cl_device_info key) {
  size_t n = 0;
  check(api().clGetDeviceInfo(d, key, 0, nullptr, &n), "device info size");
  if (!n || n > 65536)
    throw std::runtime_error("invalid OpenCL device string");
  std::string s(n, '\0');
  check(api().clGetDeviceInfo(d, key, n, s.data(), nullptr), "device info");
  if (s.back() == '\0')
    s.pop_back();
  return s;
}
struct Device {
  cl_device_id handle;
  GpuInfo info;
};
std::vector<Device> enumerate() {
  auto &a = api();
  if (!a.library)
    return {};
  cl_uint n = 0;
  auto e = a.clGetPlatformIDs(0, nullptr, &n);
  if (e == -1001)
    return {};
  check(e, "platform enumeration");
  std::vector<cl_platform_id> platforms(n);
  if (n)
    check(a.clGetPlatformIDs(n, platforms.data(), nullptr), "platform list");
  std::vector<Device> out;
  std::set<std::string> seen;
  for (auto p : platforms) {
    cl_uint count = 0;
    e = a.clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 0, nullptr, &count);
    if (e == CL_DEVICE_NOT_FOUND)
      continue;
    check(e, "device enumeration");
    std::vector<cl_device_id> devices(count);
    if (count)
      check(a.clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, count, devices.data(), nullptr), "device list");
    for (auto d : devices) {
      cl_bool available = CL_FALSE, compiler = CL_FALSE, little = CL_FALSE;
      query(d, CL_DEVICE_AVAILABLE, available);
      query(d, CL_DEVICE_COMPILER_AVAILABLE, compiler);
      query(d, CL_DEVICE_ENDIAN_LITTLE, little);
      if (!available || !compiler || !little)
        continue;
      cl_uint vendor = 0;
      query(d, CL_DEVICE_VENDOR_ID, vendor);
      std::string pci;
      cl_device_pci_bus_info_khr bus{};
      if (query(d, CL_DEVICE_PCI_BUS_INFO_KHR, bus)) {
        char b[32];
        std::snprintf(b, sizeof b, "%04x:%02x:%02x.%x", bus.pci_domain, bus.pci_bus, bus.pci_device,
                      bus.pci_function);
        pci = b;
      } else if (vendor == 0x1002) {
        cl_device_topology_amd topology{};
        if (query(d, CL_DEVICE_TOPOLOGY_AMD, topology) &&
            topology.raw.type == CL_DEVICE_TOPOLOGY_TYPE_PCIE_AMD) {
          char b[32];
          std::snprintf(b, sizeof b, "0000:%02x:%02x.%x",
                        static_cast<unsigned char>(topology.pcie.bus),
                        static_cast<unsigned char>(topology.pcie.device),
                        static_cast<unsigned char>(topology.pcie.function));
          pci = b;
        }
      }
      // Several ICDs can expose the same physical AMD card. Mine it once.
      if (!pci.empty() && !seen.insert(pci).second)
        continue;
      auto name = string_info(d, CL_DEVICE_NAME);
      if (vendor == 0x1002) {
        try {
          auto board = string_info(d, CL_DEVICE_BOARD_NAME_AMD);
          if (!board.empty())
            name = board + " (" + name + ")";
        } catch (const std::runtime_error &) {
        }
      }
      int index = int(out.size());
      out.push_back({d, {index, name, pci, GpuApi::opencl, index, vendor == 0x1002}});
    }
  }
  return out;
}
class OpenCl final : public Backend {
  Api &a = api();
  cl_device_id device = nullptr;
  cl_context context = nullptr;
  cl_command_queue queue = nullptr;
  cl_program program = nullptr;
  cl_kernel scanner = nullptr, diagnostic = nullptr;
  cl_mem input = nullptr, hits = nullptr, nonces = nullptr;
  size_t block;
  std::string label;
  void release() {
    if (queue)
      a.clFinish(queue);
    if (nonces)
      a.clReleaseMemObject(nonces);
    if (hits)
      a.clReleaseMemObject(hits);
    if (input)
      a.clReleaseMemObject(input);
    if (diagnostic)
      a.clReleaseKernel(diagnostic);
    if (scanner)
      a.clReleaseKernel(scanner);
    if (program)
      a.clReleaseProgram(program);
    if (queue)
      a.clReleaseCommandQueue(queue);
    if (context)
      a.clReleaseContext(context);
  }
  template <class T> void arg(cl_kernel k, cl_uint n, const T &value) {
    check(a.clSetKernelArg(k, n, sizeof(T), &value), "kernel argument");
  }
  void prepare(cl_kernel k, const Header &h, uint64_t start, uint32_t count) {
    std::array<uint64_t, 26> data{};
    for (int i = 0; i < 10; ++i)
      data[i] = load_le(h.data() + 8 * i);
    constexpr uint64_t iv[] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                               0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                               0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
    auto v = data.data() + 10;
    for (int i = 0; i < 8; ++i)
      v[i] = v[i + 8] = iv[i];
    v[0] ^= 0x01010020;
    v[12] ^= 80;
    v[14] = ~v[14];
    auto g = [&](int a, int b, int c, int d, uint64_t x, uint64_t y) {
      v[a] += v[b] + x;
      v[d] = std::rotr(v[d] ^ v[a], 32);
      v[c] += v[d];
      v[b] = std::rotr(v[b] ^ v[c], 24);
      v[a] += v[b] + y;
      v[d] = std::rotr(v[d] ^ v[a], 16);
      v[c] += v[d];
      v[b] = std::rotr(v[b] ^ v[c], 63);
    };
    g(0, 4, 8, 12, data[0], data[1]);
    g(1, 5, 9, 13, data[2], data[3]);
    g(3, 7, 11, 15, data[6], data[7]);
    check(a.clEnqueueWriteBuffer(queue, input, CL_TRUE, 0, sizeof(data), data.data(), 0, nullptr,
                                 nullptr),
          "header upload");
    arg(k, 0, input);
    arg(k, 1, start);
    arg(k, 2, count);
  }
  void launch(cl_kernel k, uint32_t count) {
    size_t global = (size_t(count) + block - 1) / block * block;
    check(a.clEnqueueNDRangeKernel(queue, k, 1, nullptr, &global, &block, 0, nullptr, nullptr),
          "kernel launch");
  }

public:
  OpenCl(int index, int group, int variant) : block(group) {
    if (variant < -1 || variant > 3)
      throw std::runtime_error("OpenCL variant must be 0..3");
    if (group < 32 || group > 1024 || group % 32)
      throw std::runtime_error("invalid OpenCL workgroup size");
    auto ds = enumerate();
    if (index < 0 || size_t(index) >= ds.size())
      throw std::runtime_error("OpenCL device unavailable");
    device = ds[index].handle;
    if (variant == -1)
      variant = ds[index].info.amd &&
                        string_info(device, CL_DEVICE_EXTENSIONS).find("cl_amd_media_ops") !=
                            std::string::npos
                    ? 3
                    : 0;
    if (variant >= 2 &&
        string_info(device, CL_DEVICE_EXTENSIONS).find("cl_amd_media_ops") == std::string::npos)
      throw std::runtime_error("This OpenCL variant requires cl_amd_media_ops");
    label = ds[index].info.name + " OpenCL v" + std::to_string(variant);
    try {
      cl_int e = 0;
      context = a.clCreateContext(nullptr, 1, &device, nullptr, nullptr, &e);
      check(e, "create context");
      queue = a.clCreateCommandQueue(context, device, 0, &e);
      check(e, "create queue");
      const char *source = opencl_source;
      program = a.clCreateProgramWithSource(context, 1, &source, nullptr, &e);
      check(e, "create program");
      auto options = std::string("-cl-std=CL1.2 -DPRECOMPUTE=") + std::to_string(variant & 1) +
                     " -DAMD_ROTATE=" + std::to_string(variant >> 1);
      e = a.clBuildProgram(program, 1, &device, options.c_str(), nullptr, nullptr);
      if (e) {
        size_t n = 0;
        a.clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &n);
        std::string log(std::min(n, size_t(65536)), '\0');
        if (n <= log.size())
          a.clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log.size(), log.data(),
                                  nullptr);
        throw std::runtime_error("OpenCL kernel compilation failed: " + log);
      }
      scanner = a.clCreateKernel(program, "scan", &e);
      check(e, "scan kernel");
      diagnostic = a.clCreateKernel(program, "hashes", &e);
      check(e, "diagnostic kernel");
      for (auto k : {scanner, diagnostic}) {
        size_t maximum = 0;
        check(a.clGetKernelWorkGroupInfo(k, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof maximum,
                                         &maximum, nullptr),
              "workgroup limit");
        if (block > maximum)
          throw std::runtime_error("OpenCL workgroup exceeds kernel limit " +
                                   std::to_string(maximum));
      }
      input = a.clCreateBuffer(context, CL_MEM_READ_ONLY, 208, nullptr, &e);
      check(e, "input buffer");
      hits = a.clCreateBuffer(context, CL_MEM_READ_WRITE, 4, nullptr, &e);
      check(e, "counter buffer");
      nonces = a.clCreateBuffer(context, CL_MEM_WRITE_ONLY, 4096 * 8, nullptr, &e);
      check(e, "nonce buffer");
    } catch (...) {
      release();
      throw;
    }
  }
  ~OpenCl() { release(); }
  std::string name() const override { return label; }
  Scan scan(const Work &w, uint64_t start, uint32_t count) override {
    if (!count || start > UINT64_MAX - (count - 1))
      throw std::runtime_error("nonce range overflow");
    auto begin = std::chrono::steady_clock::now();
    prepare(scanner, w.header, start, count);
    uint64_t target = 0;
    for (int i = 0; i < 8; ++i)
      target = (target << 8) | w.target[i];
    arg(scanner, 3, target);
    arg(scanner, 4, hits);
    arg(scanner, 5, nonces);
    cl_uint n = 0;
    check(a.clEnqueueWriteBuffer(queue, hits, CL_TRUE, 0, 4, &n, 0, nullptr, nullptr),
          "reset hits");
    launch(scanner, count);
    check(a.clEnqueueReadBuffer(queue, hits, CL_TRUE, 0, 4, &n, 0, nullptr, nullptr), "read hits");
    if (n > 4096)
      throw std::runtime_error(
          "OpenCL candidate buffer overflow; reduce batch or increase difficulty");
    std::vector<uint64_t> candidates(n);
    if (n)
      check(a.clEnqueueReadBuffer(queue, nonces, CL_TRUE, 0, n * 8, candidates.data(), 0, nullptr,
                                  nullptr),
            "read candidates");
    Scan r;
    r.hashes = count;
    for (auto nonce : candidates) {
      if (nonce < start || nonce - start >= count)
        throw std::runtime_error("OpenCL nonce out of range");
      Header h = w.header;
      store_le(h.data() + 32, nonce);
      auto hash = blake2b256(h);
      uint64_t high = 0;
      for (int i = 0; i < 8; ++i)
        high = (high << 8) | hash[i];
      if (high > target)
        throw std::runtime_error("OpenCL candidate failed scalar validation");
      if (meets(hash, w.target))
        r.nonces.push_back(nonce);
    }
    std::sort(r.nonces.begin(), r.nonces.end());
    if (std::adjacent_find(r.nonces.begin(), r.nonces.end()) != r.nonces.end())
      throw std::runtime_error("duplicate OpenCL candidate");
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    return r;
  }
  std::vector<Hash> hashes(const Header &h, uint64_t start, uint32_t count) {
    if (!count || count > 65536 || start > UINT64_MAX - (count - 1))
      throw std::runtime_error("invalid OpenCL diagnostic range");
    cl_int e;
    auto buffer = a.clCreateBuffer(context, CL_MEM_WRITE_ONLY, size_t(count) * 32, nullptr, &e);
    check(e, "hash buffer");
    std::vector<Hash> out(count);
    try {
      prepare(diagnostic, h, start, count);
      arg(diagnostic, 3, buffer);
      launch(diagnostic, count);
      check(a.clEnqueueReadBuffer(queue, buffer, CL_TRUE, 0, size_t(count) * 32, out.data(), 0,
                                  nullptr, nullptr),
            "hash readback");
    } catch (...) {
      a.clReleaseMemObject(buffer);
      throw;
    }
    a.clReleaseMemObject(buffer);
    return out;
  }
};
} // namespace
std::vector<GpuInfo> opencl_devices(bool amd_only) {
  std::vector<GpuInfo> out;
  for (auto &d : enumerate())
    if (!amd_only || d.info.amd)
      out.push_back(d.info);
  return out;
}
std::unique_ptr<Backend> opencl_backend(int index, int block, int variant) {
  return std::make_unique<OpenCl>(index, block, variant);
}
std::vector<Hash> opencl_hashes(const Header &h, uint64_t start, uint32_t count, int index,
                                int variant) {
  return OpenCl(index, 128, variant).hashes(h, start, count);
}
} // namespace yolo
