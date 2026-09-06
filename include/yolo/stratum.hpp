// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#pragma once
#include "yolo/core.hpp"
#include "yolo/hardware.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
namespace yolo {
struct Job {
  Work work;
  std::string id, en2, ntime;
  uint64_t generation{}, epoch{};
  Hash network_target{};
};
// Pure session state: socket I/O and devices are deliberately outside this module.
class StratumState {
  std::string en1;
  size_t en2_size = 0;
  Hash next_target{};
  bool have_target = false;
  uint64_t generation = 0, epoch = 0;
  std::unordered_map<std::string, uint64_t> valid_ids;

public:
  bool authorized = false;
  std::optional<Job> current;
  std::optional<Job> receive(const nlohmann::json &message);
  bool valid(const Job &job) const;
};
struct MineOptions {
  std::string url, user, password = "x", tui = "auto", cpu_variant = "auto", gpu_mode = "auto";
  std::vector<int> devices;
  bool devices_explicit = false, no_cpu = false, no_gpu = false;
  unsigned cpu_threads = 0;
  int block = 0, variant = 3, opencl_variant = -1;
  uint32_t batch = 1 << 26, cpu_batch = 16384;
  double seconds = 0;
  unsigned warn_temperature = 75, alarm_temperature = 85;
  GpuControls controls;
};
int mine(const MineOptions &);
} // namespace yolo
