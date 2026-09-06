// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace yolo {
struct GpuInfo {
  int index;
  std::string name, pci_bus;
};
std::vector<GpuInfo> cuda_devices();
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
