// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/hardware.hpp"
#include <dlfcn.h>
#include <iostream>
#include <stdexcept>
void require(bool v) {
  if (!v)
    throw std::runtime_error("GPU control test failed");
}
int main() {
  try {
    auto lib = dlopen("libnvidia-ml.so.1", RTLD_NOW);
    require(lib);
    auto reset = reinterpret_cast<void (*)()>(dlsym(lib, "test_reset"));
    auto count = reinterpret_cast<unsigned (*)()>(dlsym(lib, "test_writes"));
    auto last = reinterpret_cast<unsigned (*)()>(dlsym(lib, "test_last"));
    auto deny = reinterpret_cast<void (*)()>(dlsym(lib, "test_deny"));
    auto missing = reinterpret_cast<void (*)()>(dlsym(lib, "test_missing_temp"));
    require(reset && count && last && deny && missing);
    {
      yolo::GpuManagement manager({{1, "test GPU1", "0000:07:00.0"}});
      reset();
      manager.apply({});
      require(count() == 0);
      yolo::GpuControls c;
      c.core_mhz = {2200};
      c.memory_mhz = {10000};
      c.power_watts = {400};
      c.fan_percent = {80};
      auto events = manager.apply(c);
      require(count() == 5 && last() == 1 && events.size() == 4);
      auto s = manager.sample(0);
      require(s.core_mhz == 2200 && s.memory_mhz == 10000 && s.power_limit == 400 &&
              s.fan_percent == 80 && s.temperature == 63);
      missing();
      require(!manager.sample(0).temperature);
      reset();
      c.core_mhz = {0};
      c.memory_mhz = {0};
      c.power_watts = {0};
      c.fan_percent = {0};
      manager.apply(c);
      require(count() == 5 && last() == 1);
      reset();
      deny();
      bool permission = false;
      try {
        manager.apply(c);
      } catch (const std::runtime_error &e) {
        permission = std::string(e.what()).find("Insufficient Permissions") != std::string::npos;
      }
      require(permission && count() == 0);
    }
    {
      yolo::GpuManagement manager({{0, "GPU0", "0000:03:00.0"}, {1, "GPU1", "0000:07:00.0"}});
      reset();
      yolo::GpuControls c;
      c.power_watts = {400, 9999};
      bool bad = false;
      try {
        manager.apply(c);
      } catch (...) {
        bad = true;
      }
      require(bad && count() == 0);
      bad = false;
      c.power_watts = {400, 450, 500};
      try {
        manager.apply(c);
      } catch (...) {
        bad = true;
      }
      require(bad && count() == 0);
    }
    dlclose(lib);
    std::cout << "PASS selected-device controls, limits, permission errors, resets and missing "
                 "telemetry\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
