// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#pragma once
#include "yolo/hardware.hpp"
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>
namespace yolo {
enum class Severity { info, success, warning, error };
struct Event {
  std::string time, message;
  Severity severity = Severity::info;
};
struct DeviceView {
  std::string label, name;
  double hashes_per_second = 0;
  uint64_t hashes = 0, accepted = 0, rejected = 0, stale = 0;
  GpuReadings sensors;
};
struct DashboardView {
  std::string coin = "BTCB2", pool, worker, state = "CONNECTING", difficulty = "--";
  double elapsed = 0;
  uint64_t hashes = 0, accepted = 0, rejected = 0, stale = 0, block_candidates = 0, pending = 0;
  unsigned warn_temperature = 75, alarm_temperature = 85;
  std::vector<DeviceView> devices;
  std::deque<Event> events;
};
std::string console_safe(const std::string &);
std::string render_dashboard(const DashboardView &, int width, int height, double animation_time,
                             size_t device_offset = 0);
class Dashboard {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  // mode: auto, on, off. Output redirected to a file always uses plain logs.
  explicit Dashboard(const std::string &mode = "auto");
  ~Dashboard();
  void update(const DashboardView &);
  void event(std::string message, Severity severity = Severity::info);
  bool quit_requested() const;
  bool interactive() const;
};
} // namespace yolo
