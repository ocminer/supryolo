// SPDX-License-Identifier: MIT
#include "yolo/amd.hpp"
#include "yolo/hardware.hpp"
#include <dlfcn.h>
#include <limits>
#include <stdexcept>
#include <utility>
namespace yolo {
std::vector<unsigned> broadcast_values(const std::vector<unsigned> &v, size_t count) {
  if (v.empty())
    return {};
  if (v.size() == 1)
    return std::vector<unsigned>(count, v[0]);
  if (v.size() != count)
    throw std::runtime_error("GPU control list must contain one value or one per selected GPU");
  return v;
}
// Stable NVML C ABI. Dynamic loading keeps CPU-only builds independent of CUDA SDK headers.
struct GpuManagement::Impl {
  using Handle = void *;
  void *library = nullptr;
  std::vector<GpuInfo> devices;
  std::vector<Handle> handles;
  std::string unavailable;
  bool initialized = false;
  template <typename F> F symbol(const char *name) {
    return library ? reinterpret_cast<F>(dlsym(library, name)) : nullptr;
  }
  std::string error(int code) {
    auto f = symbol<const char *(*)(int)>("nvmlErrorString");
    return f ? f(code) : "NVML unavailable";
  }
  void checked(int result, const std::string &operation) {
    if (result)
      throw std::runtime_error(operation + ": " + error(result));
  }
  template <typename F, typename... Args> void call(const char *name, Args... args) {
    auto f = symbol<F>(name);
    if (!f)
      throw std::runtime_error(std::string(name) + ": driver does not provide this operation");
    checked(f(args...), name);
  }
  explicit Impl(std::vector<GpuInfo> d) : devices(std::move(d)), handles(devices.size(), nullptr) {
    library = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) {
      unavailable = "NVML library unavailable";
      return;
    }
    auto init = symbol<int (*)()>("nvmlInit_v2");
    int result = init ? init() : -1;
    if (result) {
      unavailable = error(result);
      return;
    }
    initialized = true;
    auto get = symbol<int (*)(const char *, Handle *)>("nvmlDeviceGetHandleByPciBusId_v2");
    if (!get) {
      unavailable = "NVML PCI lookup unavailable";
      return;
    }
    for (size_t i = 0; i < devices.size(); ++i)
      if (!devices[i].amd && get(devices[i].pci_bus.c_str(), &handles[i]))
        handles[i] = nullptr;
  }
  ~Impl() {
    if (initialized) {
      auto f = symbol<int (*)()>("nvmlShutdown");
      if (f)
        f();
    }
    if (library)
      dlclose(library);
  }
  std::optional<unsigned> read(const char *name, Handle h) {
    unsigned x = 0;
    auto f = symbol<int (*)(Handle, unsigned *)>(name);
    if (f && f(h, &x) == 0)
      return x;
    return {};
  }
};
GpuManagement::GpuManagement(std::vector<GpuInfo> devices)
    : impl(std::make_unique<Impl>(std::move(devices))) {}
GpuManagement::~GpuManagement() = default;
GpuReadings GpuManagement::sample(size_t position) {
  auto &p = *impl;
  GpuReadings r;
  if (position < p.devices.size() && p.devices[position].amd)
    return amd_readings(p.devices[position]);
  if (position >= p.handles.size() || !p.handles[position]) {
    r.error = p.unavailable.empty() ? "GPU telemetry unavailable" : p.unavailable;
    return r;
  }
  auto h = p.handles[position];
  auto temp = p.symbol<int (*)(Impl::Handle, unsigned, unsigned *)>("nvmlDeviceGetTemperature");
  unsigned x = 0;
  if (temp && temp(h, 0, &x) == 0)
    r.temperature = x;
  r.fan_percent = p.read("nvmlDeviceGetFanSpeed", h);
  auto clock = p.symbol<int (*)(Impl::Handle, unsigned, unsigned *)>("nvmlDeviceGetClockInfo");
  if (clock && clock(h, 0, &x) == 0)
    r.core_mhz = x;
  if (clock && clock(h, 2, &x) == 0)
    r.memory_mhz = x;
  if (auto w = p.read("nvmlDeviceGetPowerUsage", h))
    r.watts = *w / 1000.0;
  if (auto w = p.read("nvmlDeviceGetPowerManagementLimit", h))
    r.power_limit = *w / 1000.0;
  struct Util {
    unsigned gpu, memory;
  };
  Util u{};
  auto util = p.symbol<int (*)(Impl::Handle, Util *)>("nvmlDeviceGetUtilizationRates");
  if (util && util(h, &u) == 0)
    r.utilization = u.gpu;
  if (!r.temperature && !r.watts)
    r.error = "Driver telemetry unavailable";
  return r;
}
std::vector<std::string> GpuManagement::apply(const GpuControls &c) {
  auto &p = *impl;
  size_t n = p.devices.size();
  std::vector<std::string> events;
  if (!c.requested())
    return events;
  if (!n)
    throw std::runtime_error("GPU controls supplied with no selected GPU");
  auto core = broadcast_values(c.core_mhz, n), mem = broadcast_values(c.memory_mhz, n),
       power = broadcast_values(c.power_watts, n), fan = broadcast_values(c.fan_percent, n);
  std::vector<unsigned> resolved_power(n), fan_count(n);
  std::vector<AmdPlan> amd_plans(n);
  // Validate all devices and limits before the first hardware write.
  for (size_t i = 0; i < n; ++i) {
    if (p.devices[i].amd) {
      auto value = [&](const std::vector<unsigned> &v) -> std::optional<unsigned> {
        return v.empty() ? std::nullopt : std::optional<unsigned>(v[i]);
      };
      amd_plans[i] =
          amd_control_plan(p.devices[i], value(core), value(mem), value(power), value(fan));
      continue;
    }
    auto h = p.handles[i];
    if (!h)
      throw std::runtime_error("Cannot configure GPU " + std::to_string(p.devices[i].index) +
                               ": NVML device unavailable");
    if (!core.empty() && core[i] > 10000)
      throw std::runtime_error("GPU core clock out of range (MHz)");
    if (!mem.empty() && mem[i] > 100000)
      throw std::runtime_error("GPU memory clock out of range (MHz)");
    if (!power.empty()) {
      unsigned lo = 0, hi = 0;
      p.call<int (*)(Impl::Handle, unsigned *, unsigned *)>(
          "nvmlDeviceGetPowerManagementLimitConstraints", h, &lo, &hi);
      if (power[i] > std::numeric_limits<unsigned>::max() / 1000)
        throw std::runtime_error("GPU power limit overflow");
      unsigned mw = power[i] * 1000;
      if (!power[i]) {
        auto d = p.read("nvmlDeviceGetPowerManagementDefaultLimit", h);
        if (!d)
          throw std::runtime_error("Default GPU power limit unavailable");
        mw = *d;
      }
      if (mw < lo || mw > hi)
        throw std::runtime_error("GPU " + std::to_string(p.devices[i].index) +
                                 " power limit outside driver range " + std::to_string(lo / 1000) +
                                 ".." + std::to_string(hi / 1000) + " W");
      resolved_power[i] = mw;
    }
    if (!fan.empty()) {
      if (fan[i] > 100)
        throw std::runtime_error("GPU fan speed must be 0 (automatic) or 1..100 percent");
      p.call<int (*)(Impl::Handle, unsigned *)>("nvmlDeviceGetNumFans", h, &fan_count[i]);
      if (!fan_count[i])
        throw std::runtime_error("GPU fan control unsupported on this device");
    }
  }
  for (size_t i = 0; i < n; ++i) {
    auto h = p.handles[i];
    std::string label = "GPU" + std::to_string(p.devices[i].index);
    try {
      if (p.devices[i].amd) {
        amd_apply(amd_plans[i]);
        for (const auto &event : amd_plans[i].events)
          events.push_back(label + " " + event);
        continue;
      }
      if (!power.empty()) {
        p.call<int (*)(Impl::Handle, unsigned)>("nvmlDeviceSetPowerManagementLimit", h,
                                                resolved_power[i]);
        events.push_back(label + " power limit " + std::to_string(resolved_power[i] / 1000) +
                         " W applied");
      }
      if (!core.empty()) {
        if (core[i])
          p.call<int (*)(Impl::Handle, unsigned, unsigned)>("nvmlDeviceSetGpuLockedClocks", h,
                                                            core[i], core[i]);
        else
          p.call<int (*)(Impl::Handle)>("nvmlDeviceResetGpuLockedClocks", h);
        events.push_back(label + " core clock " +
                         (core[i] ? std::to_string(core[i]) + " MHz" : "automatic") + " applied");
      }
      if (!mem.empty()) {
        if (mem[i])
          p.call<int (*)(Impl::Handle, unsigned, unsigned)>("nvmlDeviceSetMemoryLockedClocks", h,
                                                            mem[i], mem[i]);
        else
          p.call<int (*)(Impl::Handle)>("nvmlDeviceResetMemoryLockedClocks", h);
        events.push_back(label + " memory clock " +
                         (mem[i] ? std::to_string(mem[i]) + " MHz" : "automatic") + " applied");
      }
      if (!fan.empty()) {
        for (unsigned f = 0; f < fan_count[i]; ++f) {
          if (fan[i])
            p.call<int (*)(Impl::Handle, unsigned, unsigned)>("nvmlDeviceSetFanSpeed_v2", h, f,
                                                              fan[i]);
          else
            p.call<int (*)(Impl::Handle, unsigned)>("nvmlDeviceSetDefaultFanSpeed_v2", h, f);
        }
        events.push_back(label + " fan " + (fan[i] ? std::to_string(fan[i]) + "%" : "automatic") +
                         " applied");
      }
    } catch (const std::exception &e) {
      throw std::runtime_error(label + " control failed: " + e.what() +
                               ". Earlier successfully applied settings remain active; check "
                               "device settings. Root permission may be required.");
    }
  }
  return events;
}
} // namespace yolo
