// SPDX-License-Identifier: MIT
#pragma once
#include "yolo/core.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <unordered_map>
namespace yolo {
struct Job {
  Work work;
  std::string id, en2, ntime;
  uint64_t generation{}, epoch{};
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
  std::string url, user, password = "x";
  std::vector<int> devices{0};
  int block = 128, variant = 3;
  uint32_t batch = 1 << 26;
  double seconds = 0;
  bool cpu = false;
};
int mine(const MineOptions &);
} // namespace yolo
