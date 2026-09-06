// SPDX-License-Identifier: MIT
#include "yolo/amd.hpp"
#include <cerrno>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>
namespace yolo {
namespace {
std::optional<uint64_t> number(const std::filesystem::path &p) {
  std::ifstream f(p);
  uint64_t n;
  if (f >> n)
    return n;
  return {};
}
std::optional<unsigned> clock(const std::filesystem::path &p) {
  std::ifstream f(p);
  std::string line;
  while (std::getline(f, line))
    if (line.find('*') != std::string::npos) {
      std::istringstream in(line);
      unsigned level, mhz;
      char colon;
      if (in >> level >> colon >> mhz && colon == ':')
        return mhz;
    }
  return {};
}
} // namespace
GpuReadings amd_readings(const GpuInfo &d, const std::filesystem::path &root) {
  GpuReadings r;
  try {
    if (!std::regex_match(d.pci_bus,
                          std::regex("[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\\.[0-7]"))) {
      r.error = "AMD PCI mapping unavailable";
      return r;
    }
    auto device = root / d.pci_bus;
    std::ifstream vendor(device / "vendor");
    std::string id;
    vendor >> id;
    if (id != "0x1002") {
      r.error = "AMD PCI device not found in sysfs";
      return r;
    }
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(device / "hwmon", ec)) {
      auto hw = entry.path();
      std::ifstream name(hw / "name");
      std::string driver;
      name >> driver;
      if (driver != "amdgpu")
        continue;
      if (auto n = number(hw / "temp1_input"))
        r.temperature = unsigned(*n / 1000);
      auto fan = number(hw / "pwm1"), maximum = number(hw / "pwm1_max");
      if (fan && maximum && *maximum)
        r.fan_percent = unsigned(*fan * 100 / *maximum);
      auto power = number(hw / "power1_average");
      if (!power)
        power = number(hw / "power1_input");
      if (power)
        r.watts = *power / 1e6;
      if (auto n = number(hw / "power1_cap"))
        r.power_limit = *n / 1e6;
      if (auto n = number(hw / "freq1_input"))
        r.core_mhz = unsigned(*n / 1000000);
      if (auto n = number(hw / "freq2_input"))
        r.memory_mhz = unsigned(*n / 1000000);
      break;
    }
    if (!r.core_mhz)
      r.core_mhz = clock(device / "pp_dpm_sclk");
    if (!r.memory_mhz)
      r.memory_mhz = clock(device / "pp_dpm_mclk");
    if (auto n = number(device / "gpu_busy_percent"))
      r.utilization = unsigned(*n);
    if (!r.temperature && !r.watts)
      r.error = "AMD hwmon telemetry unavailable";
  } catch (const std::exception &) {
    r.error = "AMD sysfs telemetry unavailable";
  }
  return r;
}
} // namespace yolo
namespace yolo {
AmdPlan amd_control_plan(const GpuInfo &d, std::optional<unsigned> core,
                         std::optional<unsigned> memory, std::optional<unsigned> power,
                         std::optional<unsigned> fan, const std::filesystem::path &root) {
  AmdPlan p;
  if (!std::regex_match(d.pci_bus,
                        std::regex("[0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\\.[0-7]")))
    throw std::runtime_error("AMD PCI mapping unavailable");
  auto device = root / d.pci_bus;
  std::ifstream vendor(device / "vendor");
  std::string id;
  vendor >> id;
  if (id != "0x1002")
    throw std::runtime_error("AMD PCI vendor mismatch");
  std::filesystem::path hw;
  std::error_code ec;
  for (const auto &entry : std::filesystem::directory_iterator(device / "hwmon", ec)) {
    std::ifstream f(entry.path() / "name");
    std::string name;
    f >> name;
    if (name == "amdgpu") {
      hw = entry.path();
      break;
    }
  }
  auto add = [&](const std::filesystem::path &file, std::string value) {
    if (!std::filesystem::exists(file))
      throw std::runtime_error("AMD driver does not expose " + file.filename().string());
    p.writes.push_back({file, std::move(value)});
  };
  if (power) {
    if (hw.empty())
      throw std::runtime_error("AMD hwmon unavailable for power control");
    auto lo = number(hw / "power1_cap_min"), hi = number(hw / "power1_cap_max");
    auto requested = *power ? std::optional<uint64_t>(uint64_t(*power) * 1000000)
                            : number(hw / "power1_cap_default");
    if (!lo || !hi || !requested)
      throw std::runtime_error("AMD power limits/default unavailable");
    if (*requested < *lo || *requested > *hi)
      throw std::runtime_error("AMD power limit outside driver range");
    add(hw / "power1_cap", std::to_string(*requested));
    p.events.push_back("power limit " + std::to_string(*requested / 1000000) + " W applied");
  }
  if (core || memory) {
    bool reset = (core && !*core) || (memory && !*memory);
    if (reset && ((core && *core) || (memory && *memory)))
      throw std::runtime_error(
          "AMD clock reset applies to both domains; cannot combine reset and locked clocks");
    if (reset) {
      add(device / "power_dpm_force_performance_level", "auto");
      p.events.push_back("core and memory clock policy automatic applied");
    } else {
      auto level = [&](const std::filesystem::path &file, unsigned mhz) {
        std::ifstream f(file);
        std::string line;
        while (std::getline(f, line)) {
          std::istringstream in(line);
          unsigned index, value;
          char colon;
          if (in >> index >> colon >> value && colon == ':' && value == mhz)
            return std::to_string(index);
        }
        throw std::runtime_error("AMD clock must match a driver-advertised MHz level in " +
                                 file.filename().string());
      };
      std::optional<std::string> sc, mc;
      if (core)
        sc = level(device / "pp_dpm_sclk", *core);
      if (memory)
        mc = level(device / "pp_dpm_mclk", *memory);
      add(device / "power_dpm_force_performance_level", "manual");
      if (sc) {
        add(device / "pp_dpm_sclk", *sc);
        p.events.push_back("core clock DPM level " + std::to_string(*core) + " MHz applied");
      }
      if (mc) {
        add(device / "pp_dpm_mclk", *mc);
        p.events.push_back("memory clock DPM level " + std::to_string(*memory) + " MHz applied");
      }
    }
  }
  if (fan) {
    if (hw.empty() || *fan > 100)
      throw std::runtime_error("AMD fan control unavailable or invalid percentage");
    if (!*fan)
      add(hw / "pwm1_enable", "2");
    else {
      auto maximum = number(hw / "pwm1_max");
      if (!maximum || !*maximum)
        throw std::runtime_error("AMD fan PWM range unavailable");
      auto policy = number(hw / "pwm1_enable");
      if (!policy || *policy > 2)
        throw std::runtime_error("AMD current fan policy unavailable");
      p.fan_restore = AmdWrite{hw / "pwm1_enable", std::to_string(*policy)};
      add(hw / "pwm1_enable", "1");
      add(hw / "pwm1", std::to_string((*maximum * *fan + 50) / 100));
    }
    p.events.push_back(*fan ? "fan " + std::to_string(*fan) + "% applied"
                            : "automatic fan control applied");
  }
  return p;
}
void amd_apply(const AmdPlan &p) {
  auto write = [](const AmdWrite &w) {
    errno = 0;
    std::ofstream f(w.path);
    f << w.value << '\n';
    f.flush();
    if (!f)
      throw std::system_error(errno ? errno : EIO, std::generic_category(),
                              "AMD write failed for " + w.path.filename().string());
  };
  bool fan_changed = false;
  try {
    for (const auto &w : p.writes) {
      write(w);
      if (p.fan_restore && w.path == p.fan_restore->path)
        fan_changed = true;
    }
  } catch (const std::exception &e) {
    if (fan_changed) {
      try {
        write(*p.fan_restore);
      } catch (const std::exception &restore) {
        throw std::runtime_error(std::string(e.what()) +
                                 "; previous fan policy could not be restored: " + restore.what());
      }
    }
    throw;
  }
}
} // namespace yolo
