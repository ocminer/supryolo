// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace yolo {
enum class GpuApi { cuda, opencl };
struct GpuInfo {
  int index;
  std::string name, pci_bus;
  GpuApi api = GpuApi::cuda;
  int backend_index = -1;
  bool amd = false;
};
std::vector<GpuInfo> cuda_devices();
std::vector<GpuInfo> opencl_devices(bool amd_only = true);
std::vector<GpuInfo> gpu_devices(const std::string &mode = "auto");
int gpu_block(const GpuInfo &, int configured);
class Backend;
std::unique_ptr<Backend> gpu_backend(const GpuInfo &, int block, int variant,
                                     int opencl_variant = -1);
std::unique_ptr<Backend> opencl_backend(int device, int block = 256, int variant = 0);

struct GpuReadings {
  std::optional<unsigned> temperature, fan_percent, core_mhz, memory_mhz, utilization;
  std::optional<double> watts, power_limit;
  std::string error;
};
struct GpuControls {
  std::vector<unsigned> core_mhz, memory_mhz, power_watts, fan_percent;
  bool requested() const {
    return !core_mhz.empty() || !memory_mhz.empty() || !power_watts.empty() || !fan_percent.empty();
  }
};
std::vector<unsigned> broadcast_values(const std::vector<unsigned> &, size_t count);
class GpuManagement {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  explicit GpuManagement(std::vector<GpuInfo> devices);
  ~GpuManagement();
  GpuReadings sample(size_t position);
  std::vector<std::string> apply(const GpuControls &);
};
} // namespace yolo
