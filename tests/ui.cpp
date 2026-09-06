// SPDX-License-Identifier: MIT
#include "yolo/ui.hpp"
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
void require(bool v) {
  if (!v)
    throw std::runtime_error("TUI rendering test failed");
}
int main() {
  try {
    yolo::DashboardView v;
    v.pool = "evil\033[2J\nserver";
    v.state = "MINING";
    yolo::DeviceView d;
    d.label = "GPU0";
    d.sensors.temperature = 85;
    v.devices.push_back(d);
    auto on = yolo::render_dashboard(v, 100, 30, 0), off = yolo::render_dashboard(v, 100, 30, .25),
         on_again = yolo::render_dashboard(v, 100, 30, .5);
    require(on.find("ALARM") != std::string::npos && off.find("ALARM") == std::string::npos &&
            on_again.find("ALARM") != std::string::npos);
    require(on.find("\033[2J") == std::string::npos);
    std::regex escape("\033\\[[0-9;?]*[A-Za-z]");
    for (auto [w, h] : {std::pair{100, 30}, std::pair{80, 24}, std::pair{40, 10}}) {
      auto frame = std::regex_replace(yolo::render_dashboard(v, w, h, 0), escape, "");
      std::istringstream lines(frame);
      std::string line;
      int count = 0;
      while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        require(line.size() == size_t(w));
        ++count;
      }
      require(count == h);
    }
    require(yolo::console_safe("hello\033\n\r\a world") == "hello world");
    std::cout << "PASS TUI 2Hz alarm, resize boundaries and terminal text sanitization\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
