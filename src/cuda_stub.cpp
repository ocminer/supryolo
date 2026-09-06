// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/core.hpp"
#include "yolo/hardware.hpp"
#include <stdexcept>
namespace yolo {
std::vector<GpuInfo> cuda_devices() { return {}; }
std::unique_ptr<Backend> cuda_backend(int, int, int) {
  throw std::runtime_error("CUDA backend not built; use --cpu");
}
std::vector<Hash> cuda_hashes(const Header &, uint64_t, uint32_t, int, int) {
  throw std::runtime_error("CUDA backend not built");
}
} // namespace yolo
