// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/amd.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
namespace fs = std::filesystem;
void require(bool b) {
  if (!b)
    throw std::runtime_error("AMD sensor test failed");
}
int main() {
  auto root = fs::temp_directory_path() / ("supryolo-hwmon-" + std::to_string(getpid()));
  try {
    auto device = root / "0000:03:00.0", hw = device / "hwmon" / "hwmon42";
    fs::create_directories(hw);
    auto write = [](const fs::path &p, const std::string &s) { std::ofstream(p) << s; };
    write(device / "vendor", "0x1002");
    write(hw / "name", "amdgpu");
    write(hw / "temp1_input", "85000");
    write(hw / "pwm1", "128");
    write(hw / "pwm1_max", "255");
    write(hw / "power1_average", "150500000");
    write(hw / "power1_cap", "200000000");
    write(device / "pp_dpm_sclk", "0: 300Mhz\n1: 2400Mhz *\n");
    write(hw / "freq2_input", "1000000000");
    yolo::GpuInfo d{0, "AMD fixture", "0000:03:00.0", yolo::GpuApi::opencl, 0, true};
    auto v = yolo::amd_readings(d, root);
    require(v.temperature == 85 && v.fan_percent == 50 && v.core_mhz == 2400 &&
            v.memory_mhz == 1000 && v.watts == 150.5 && v.power_limit == 200);
    write(hw / "power1_cap_min", "100000000");
    write(hw / "power1_cap_max", "250000000");
    write(hw / "power1_cap_default", "200000000");
    write(hw / "pwm1_enable", "2");
    write(device / "power_dpm_force_performance_level", "auto");
    auto plan = yolo::amd_control_plan(d, 2400, {}, 150, 70, root);
    require(plan.writes.size() == 5);
    // Planning performs no writes; invalid later settings cannot partially apply power.
    require(yolo::amd_readings(d, root).power_limit == 200);
    bool invalid = false;
    try {
      yolo::amd_control_plan(d, 2345, {}, 150, 70, root);
    } catch (const std::exception &) {
      invalid = true;
    }
    require(invalid && yolo::amd_readings(d, root).power_limit == 200);
    yolo::amd_apply(plan);
    require(yolo::amd_readings(d, root).power_limit == 150);
    auto reset = yolo::amd_control_plan(d, 0, 0, 0, 0, root);
    yolo::amd_apply(reset);
    require(yolo::amd_readings(d, root).power_limit == 200);
    auto failing_fan = yolo::amd_control_plan(d, {}, {}, {}, 50, root);
    fs::remove(hw / "pwm1");
    fs::create_directory(hw / "pwm1");
    bool failed = false;
    try {
      yolo::amd_apply(failing_fan);
    } catch (const std::exception &) {
      failed = true;
    }
    std::string policy;
    std::ifstream(hw / "pwm1_enable") >> policy;
    require(failed && policy == "2");
    fs::remove(hw / "pwm1");
    write(hw / "pwm1", "128");
    fs::remove(hw / "temp1_input");
    require(!yolo::amd_readings(d, root).temperature);
    d.pci_bus = "../escape";
    require(!yolo::amd_readings(d, root).error.empty());
    fs::remove_all(root);
    std::cout << "PASS AMD PCI mapping, hwmon units, DPM clocks and missing sensors\n";
  } catch (const std::exception &e) {
    fs::remove_all(root);
    std::cerr << e.what() << '\n';
    return 1;
  }
}
