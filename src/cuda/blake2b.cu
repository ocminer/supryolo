// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/core.hpp"
#include "yolo/hardware.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cuda_runtime.h>
#include <stdexcept>
namespace yolo {
namespace {
void check(cudaError_t e) {
  if (e != cudaSuccess)
    throw std::runtime_error(cudaGetErrorString(e));
}
constexpr unsigned capacity = 4096;
struct Params {
  uint64_t m[10];
  uint64_t target;
  uint64_t initial[16];
};
struct Hits {
  unsigned count;
  uint64_t nonces[capacity];
};
template <int R, int Mode> __device__ __forceinline__ uint64_t rotate(uint64_t x) {
  if constexpr ((Mode & 1) == 0)
    return (x >> R) | (x << (64 - R));
  else {
    uint32_t lo = uint32_t(x), hi = uint32_t(x >> 32), a, b;
    if constexpr (R == 32)
      return (uint64_t(lo) << 32) | hi;
    else {
      if constexpr (R < 32) {
        asm("shf.r.wrap.b32 %0, %1, %2, %3;" : "=r"(a) : "r"(lo), "r"(hi), "n"(R));
        asm("shf.r.wrap.b32 %0, %1, %2, %3;" : "=r"(b) : "r"(hi), "r"(lo), "n"(R));
      } else {
        asm("shf.r.wrap.b32 %0, %1, %2, %3;" : "=r"(a) : "r"(hi), "r"(lo), "n"(R - 32));
        asm("shf.r.wrap.b32 %0, %1, %2, %3;" : "=r"(b) : "r"(lo), "r"(hi), "n"(R - 32));
      }
      return (uint64_t(b) << 32) | a;
    }
  }
}
template <int Mode>
__device__ __forceinline__ void mix(uint64_t &a, uint64_t &b, uint64_t &c, uint64_t &d, uint64_t x,
                                    uint64_t y) {
  a += b + x;
  d = rotate<32, Mode>(d ^ a);
  c += d;
  b = rotate<24, Mode>(b ^ c);
  a += b + y;
  d = rotate<16, Mode>(d ^ a);
  c += d;
  b = rotate<63, Mode>(b ^ c);
}
template <int Mode>
__device__ __forceinline__ void hash80(const Params &p, uint64_t nonce, uint64_t *out) {
  constexpr uint64_t iv[] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                             0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                             0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  constexpr int s[12][16] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
                             {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
                             {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
                             {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
                             {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
                             {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
                             {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
                             {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
                             {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
                             {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
                             {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
                             {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};
  uint64_t m[16], v[16];
#pragma unroll
  for (int i = 0; i < 16; ++i) {
    m[i] = i < 10 ? p.m[i] : 0;
    v[i] = iv[i % 8];
  }
  m[4] = nonce;
  v[0] ^= 0x01010020;
  v[12] ^= 80;
  v[14] = ~v[14];
#pragma unroll
  for (int r = 0; r < 12; ++r) {
    if constexpr (Mode >= 2) {
      if (r == 0) {
#pragma unroll
        for (int i = 0; i < 16; ++i)
          v[i] = p.initial[i];
      }
    }
    if (Mode < 2 || r != 0) {
      mix<Mode>(v[0], v[4], v[8], v[12], m[s[r][0]], m[s[r][1]]);
      mix<Mode>(v[1], v[5], v[9], v[13], m[s[r][2]], m[s[r][3]]);
    }
    mix<Mode>(v[2], v[6], v[10], v[14], m[s[r][4]], m[s[r][5]]);
    if (Mode < 2 || r != 0)
      mix<Mode>(v[3], v[7], v[11], v[15], m[s[r][6]], m[s[r][7]]);
    mix<Mode>(v[0], v[5], v[10], v[15], m[s[r][8]], m[s[r][9]]);
    mix<Mode>(v[1], v[6], v[11], v[12], m[s[r][10]], m[s[r][11]]);
    mix<Mode>(v[2], v[7], v[8], v[13], m[s[r][12]], m[s[r][13]]);
    mix<Mode>(v[3], v[4], v[9], v[14], m[s[r][14]], m[s[r][15]]);
  }
#pragma unroll
  for (int i = 0; i < 4; ++i)
    out[i] = iv[i] ^ v[i] ^ v[i + 8] ^ (i == 0 ? 0x01010020 : 0);
}
__device__ __forceinline__ uint64_t swap(uint64_t x) {
  uint32_t lo = __byte_perm(uint32_t(x), 0, 0x0123), hi = __byte_perm(uint32_t(x >> 32), 0, 0x0123);
  return (uint64_t(lo) << 32) | hi;
}
template <int Mode, bool Dump, int Roll = 1>
__global__ void kernel(Params p, uint64_t start, uint32_t count, Hits *hits, uint64_t *hashes) {
  uint64_t base = uint64_t(blockIdx.x) * blockDim.x * Roll + threadIdx.x;
#pragma unroll
  for (int k = 0; k < Roll; ++k) {
    uint64_t i = base + uint64_t(k) * blockDim.x;
    if (i >= count)
      continue;
    uint64_t nonce = start + i;
    if constexpr (Mode >= 4) {
      // Keep the high nonce word uniform when this batch cannot cross a 32-bit boundary.
      // The general path remains necessary for arbitrary starts and diagnostic ranges.
      if (uint32_t(start) <= UINT32_MAX - (count - 1))
        nonce = (start & 0xffffffff00000000ULL) | uint32_t(uint32_t(start) + uint32_t(i));
    }
    uint64_t h[4];
    hash80<Mode>(p, nonce, h);
    if constexpr (Dump) {
      for (int j = 0; j < 4; ++j)
        hashes[4 * i + j] = h[j];
    } else if (swap(h[0]) <= p.target) {
      unsigned slot = atomicAdd(&hits->count, 1);
      if (slot < capacity)
        hits->nonces[slot] = nonce;
    }
  }
}
template <bool Dump>
void launch(int variant, Params p, uint64_t start, uint32_t count, int block, cudaStream_t stream,
            Hits *hits, uint64_t *hashes) {
  unsigned grid = (uint64_t(count) + block - 1) / block;
  switch (variant) {
  case 0:
    kernel<0, Dump><<<grid, block, 0, stream>>>(p, start, count, hits, hashes);
    break;
  case 1:
    kernel<1, Dump><<<grid, block, 0, stream>>>(p, start, count, hits, hashes);
    break;
  case 2:
    kernel<2, Dump><<<grid, block, 0, stream>>>(p, start, count, hits, hashes);
    break;
  case 3:
    kernel<3, Dump><<<grid, block, 0, stream>>>(p, start, count, hits, hashes);
    break;
  case 4:
    kernel<5, Dump><<<grid, block, 0, stream>>>(p, start, count, hits, hashes);
    break;
  case 5:
    kernel<5, Dump, 2><<<(uint64_t(count) + 2 * block - 1) / (2 * block), block, 0, stream>>>(
        p, start, count, hits, hashes);
    break;
  case 6:
    kernel<5, Dump, 4><<<(uint64_t(count) + 4 * block - 1) / (4 * block), block, 0, stream>>>(
        p, start, count, hits, hashes);
    break;
  default:
    throw std::runtime_error("invalid kernel variant");
  }
}
Params params(const Work &w) {
  Params p{};
  for (int i = 0; i < 10; ++i)
    p.m[i] = load_le(w.header.data() + i * 8);
  for (int i = 0; i < 8; ++i)
    p.target = (p.target << 8) | w.target[i];
  const uint64_t iv[] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                         0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                         0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  auto v = p.initial;
  for (int i = 0; i < 16; ++i)
    v[i] = iv[i % 8];
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
  g(0, 4, 8, 12, p.m[0], p.m[1]);
  g(1, 5, 9, 13, p.m[2], p.m[3]);
  g(3, 7, 11, 15, p.m[6], p.m[7]);
  return p;
}
class Cuda final : public Backend {
  int device, block, variant;
  cudaStream_t stream{};
  Hits *hits{};
  Hits *host{};
  std::string label;

public:
  Cuda(int d, int b, int v) : device(d), block(b), variant(v) {
    if (b < 32 || b > 1024 || b % 32 || v < 0 || v > 6)
      throw std::runtime_error("invalid CUDA configuration");
    check(cudaSetDevice(d));
    cudaDeviceProp prop;
    check(cudaGetDeviceProperties(&prop, d));
    label = prop.name;
    try {
      check(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
      check(cudaMalloc(&hits, sizeof(Hits)));
      check(cudaMallocHost(&host, sizeof(Hits)));
    } catch (...) {
      if (host)
        cudaFreeHost(host);
      if (hits)
        cudaFree(hits);
      if (stream)
        cudaStreamDestroy(stream);
      throw;
    }
  }
  ~Cuda() {
    cudaSetDevice(device);
    if (stream)
      cudaStreamSynchronize(stream);
    if (host)
      cudaFreeHost(host);
    if (hits)
      cudaFree(hits);
    if (stream)
      cudaStreamDestroy(stream);
  }
  std::string name() const override { return label; }
  Scan scan(const Work &w, uint64_t start, uint32_t count) override {
    if (!count || start > UINT64_MAX - (count - 1))
      throw std::runtime_error("nonce range overflow");
    check(cudaSetDevice(device));
    auto t = std::chrono::steady_clock::now();
    check(cudaMemsetAsync(hits, 0, sizeof(unsigned), stream));
    auto p = params(w);
    launch<false>(variant, p, start, count, block, stream, hits, nullptr);
    check(cudaGetLastError());
    check(cudaMemcpyAsync(host, hits, sizeof(Hits), cudaMemcpyDeviceToHost, stream));
    check(cudaStreamSynchronize(stream));
    if (host->count > capacity)
      throw std::runtime_error("CUDA candidate buffer overflow; reduce batch size");
    Scan result;
    result.hashes = count;
    Header h = w.header;
    for (unsigned i = 0; i < host->count; ++i) {
      auto n = host->nonces[i];
      store_le(h.data() + 32, n);
      auto digest = blake2b256(h);
      uint64_t top = 0;
      for (int j = 0; j < 8; ++j)
        top = (top << 8) | digest[j];
      if (top > p.target)
        throw std::runtime_error("CUDA hash validation failed");
      if (meets(digest, w.target))
        result.nonces.push_back(n);
    }
    result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    return result;
  }
};
} // namespace
std::unique_ptr<Backend> cuda_backend(int d, int b, int v) {
  return std::make_unique<Cuda>(d, b, v);
}
std::vector<Hash> cuda_hashes(const Header &header, uint64_t start, uint32_t count, int device,
                              int variant) {
  if (!count || count > 65536 || start > UINT64_MAX - (count - 1) || variant < 0 || variant > 6)
    throw std::runtime_error("invalid hash test range");
  check(cudaSetDevice(device));
  uint64_t *out;
  check(cudaMalloc(&out, size_t(count) * 32));
  Work w;
  w.header = header;
  std::vector<Hash> hashes(count);
  try {
    launch<true>(variant, params(w), start, count, 128, nullptr, nullptr, out);
    check(cudaGetLastError());
    check(cudaMemcpy(hashes.data(), out, size_t(count) * 32, cudaMemcpyDeviceToHost));
  } catch (...) {
    cudaFree(out);
    throw;
  }
  cudaFree(out);
  return hashes;
}
std::vector<GpuInfo> cuda_devices() {
  int n = 0;
  auto err = cudaGetDeviceCount(&n);
  if (err == cudaErrorNoDevice || err == cudaErrorInsufficientDriver)
    return {};
  check(err);
  std::vector<GpuInfo> out;
  for (int i = 0; i < n; ++i) {
    cudaDeviceProp p{};
    check(cudaGetDeviceProperties(&p, i));
    char pci[32]{};
    check(cudaDeviceGetPCIBusId(pci, sizeof(pci), i));
    out.push_back({i, p.name, pci});
  }
  return out;
}
} // namespace yolo
