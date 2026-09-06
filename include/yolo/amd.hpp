// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#pragma once
#include "yolo/hardware.hpp"
#include <filesystem>
namespace yolo {
// Read-only amdgpu telemetry; root injection is used only by filesystem fixtures.
GpuReadings amd_readings(const GpuInfo &,
                         const std::filesystem::path &root = "/sys/bus/pci/devices");
} // namespace yolo
namespace yolo {
struct AmdWrite {
  std::filesystem::path path;
  std::string value;
};
struct AmdPlan {
  std::vector<AmdWrite> writes;
  std::vector<std::string> events;
  std::optional<AmdWrite> fan_restore;
};
AmdPlan amd_control_plan(const GpuInfo &, std::optional<unsigned> core,
                         std::optional<unsigned> memory, std::optional<unsigned> power,
                         std::optional<unsigned> fan,
                         const std::filesystem::path &root = "/sys/bus/pci/devices");
void amd_apply(const AmdPlan &);
} // namespace yolo
