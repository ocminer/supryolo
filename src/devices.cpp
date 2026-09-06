// SPDX-License-Identifier: MIT
#include "yolo/core.hpp"
#include "yolo/hardware.hpp"
#include <stdexcept>
namespace yolo {
std::vector<GpuInfo> gpu_devices(const std::string &mode) {
  if (mode != "auto" && mode != "cuda" && mode != "opencl")
    throw std::runtime_error("GPU backend must be auto, cuda or opencl");
  auto out = mode == "opencl" ? std::vector<GpuInfo>{} : cuda_devices();
  for (auto &d : out)
    d.backend_index = d.index;
  if (mode != "cuda")
    for (auto d : opencl_devices(mode == "auto")) {
      d.index = int(out.size());
      out.push_back(d);
    }
  return out;
}
int gpu_block(const GpuInfo &d, int configured) {
  return configured ? configured : d.api == GpuApi::cuda ? 256 : 64;
}
std::unique_ptr<Backend> gpu_backend(const GpuInfo &d, int block, int variant, int opencl_variant) {
  block = gpu_block(d, block);
  return d.api == GpuApi::cuda ? cuda_backend(d.backend_index, block, variant)
                               : opencl_backend(d.backend_index, block, opencl_variant);
}
} // namespace yolo
