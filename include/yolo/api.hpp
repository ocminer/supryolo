// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#pragma once
#include "yolo/ui.hpp"
#include <nlohmann/json.hpp>
namespace yolo {
nlohmann::json api_snapshot(const DashboardView &);
class ApiServer {
  struct Impl;
  std::unique_ptr<Impl> impl;
public:
  explicit ApiServer(unsigned port);
  ~ApiServer();
  void update(const DashboardView &);
};
}
