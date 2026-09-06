// SPDX-License-Identifier: MIT
#include "yolo/core.hpp"
#include "yolo/hardware.hpp"
#include <stdexcept>
namespace yolo {
std::vector<GpuInfo> opencl_devices(bool) { return {}; }
std::unique_ptr<Backend> opencl_backend(int, int, int) {
  throw std::runtime_error("OpenCL backend not built");
}
std::vector<Hash> opencl_hashes(const Header &, uint64_t, uint32_t, int, int) {
  throw std::runtime_error("OpenCL backend not built");
}
} // namespace yolo
