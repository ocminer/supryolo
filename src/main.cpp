// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/core.hpp"
#include "yolo/hardware.hpp"
#include "yolo/stratum.hpp"
#include "yolo/ui.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
namespace {
volatile std::sig_atomic_t demo_stop = 0;
void stop_demo(int) { demo_stop = 1; }
struct DemoSignals {
  void (*old_int)(int) = std::signal(SIGINT, stop_demo);
  void (*old_term)(int) = std::signal(SIGTERM, stop_demo);
  ~DemoSignals() {
    std::signal(SIGINT, old_int);
    std::signal(SIGTERM, old_term);
  }
};
uint64_t integer(const std::string &s, uint64_t maximum) {
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("expected nonnegative integer");
  size_t used;
  auto n = std::stoull(s, &used);
  if (used != s.size() || n > maximum)
    throw std::runtime_error("integer out of range");
  return n;
}
std::vector<unsigned> numbers(const std::string &text, unsigned maximum) {
  std::vector<unsigned> out;
  size_t pos = 0;
  do {
    auto end = text.find(',', pos);
    out.push_back(integer(text.substr(pos, end == std::string::npos ? end : end - pos), maximum));
    if (end == std::string::npos)
      break;
    pos = end + 1;
  } while (true);
  return out;
}
void usage() {
  std::cout
      << "supryolo/0.1.0\n"
         "Mine: --url stratum+tcp://de.b2pool.io:4444 --user ADDRESS.worker\n"
         "Devices: --gpu-device 0,1 (alias -d; default all), --no-gpu, --no-cpu\n"
         "GPU backend: --gpu-backend auto|cuda|opencl (auto: CUDA plus AMD OpenCL)\n"
         "CPU: --cpu-threads N (0 auto), --cpu (legacy CPU-only alias)\n"
         "CPU kernel: --cpu-variant auto|scalar|avx2 (runtime detection)\n"
         "GPU controls: --gpu-core-clock MHz --gpu-mem-clock MHz --powerlimit W\n"
         "              --gpu-fan-speed PERCENT (one value or comma-separated per selected GPU)\n"
         "              0 resets that setting; explicit settings persist until changed\n"
         "Display: --tui --no-tui --gpu-temp-warn 75 --gpu-temp-alarm 85\n"
         "API: --api-port N (loopback HTTP, 0 disables); --log-file PATH\n"
         "Inspect: --list-devices --tui-demo [--seconds 10]\n"
         "Benchmark: --benchmark --no-cpu --gpu-device 0 --seconds 30\n"
         "OpenCL tuning: auto selects AMD variant 3; --opencl-variant 0 (native), 1 "
         "(precomputed),\n"
         "               2 (AMD rotations), 3 (precomputed AMD rotations)\n"
         "Tuning: --block 0 (auto: CUDA 256, OpenCL 64) --batch 67108864 --variant 3 --cpu-batch "
         "16384\n"
         "Variants: 0 native, 1 PTX, 2 precomputed native, 3 precomputed PTX\n4 uniform nonce "
         "word, 5 two nonces/thread, 6 four nonces/thread\n";
}
} // namespace
int main(int argc, char **argv) {
  try {
    yolo::MineOptions o;
    bool bench = false, seconds_set = false, list = false, demo = false, help = false;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto value = [&]() {
        if (i + 1 >= argc)
          throw std::runtime_error("missing value for " + a);
        return std::string(argv[++i]);
      };
      if (a == "--benchmark")
        bench = true;
      else if (a == "--gpu-device" || a == "-d") {
        auto v = numbers(value(), 255);
        o.devices.assign(v.begin(), v.end());
        std::set<int> unique(o.devices.begin(), o.devices.end());
        if (unique.size() != o.devices.size())
          throw std::runtime_error("duplicate GPU device");
        o.devices_explicit = true;
      } else if (a == "--gpu-backend")
        o.gpu_mode = value();
      else if (a == "--no-cpu")
        o.no_cpu = true;
      else if (a == "--no-gpu")
        o.no_gpu = true;
      else if (a == "--cpu")
        o.no_gpu = true;
      else if (a == "--cpu-variant")
        o.cpu_variant = value();
      else if (a == "--cpu-threads")
        o.cpu_threads = integer(value(), 128);
      else if (a == "--block")
        o.block = integer(value(), 1024);
      else if (a == "--opencl-variant")
        o.opencl_variant = integer(value(), 3);
      else if (a == "--variant")
        o.variant = integer(value(), 6);
      else if (a == "--batch") {
        o.batch = integer(value(), 1U << 28);
        if (!o.batch)
          throw std::runtime_error("batch must be positive");
      } else if (a == "--cpu-batch") {
        o.cpu_batch = integer(value(), 1U << 20);
        if (!o.cpu_batch)
          throw std::runtime_error("CPU batch must be positive");
      } else if (a == "--seconds") {
        auto s = value();
        size_t used;
        o.seconds = std::stod(s, &used);
        if (used != s.size() || !std::isfinite(o.seconds) || o.seconds <= 0)
          throw std::runtime_error("seconds must be positive and finite");
        seconds_set = true;
      } else if (a == "--url")
        o.url = value();
      else if (a == "--user")
        o.user = value();
      else if (a == "--log-file")
        o.log_file = value();
      else if (a == "--api-port")
        o.api_port = integer(value(), 65535);
      else if (a == "--password")
        o.password = value();
      else if (a == "--gpu-core-clock")
        o.controls.core_mhz = numbers(value(), 10000);
      else if (a == "--gpu-mem-clock")
        o.controls.memory_mhz = numbers(value(), 100000);
      else if (a == "--powerlimit")
        o.controls.power_watts = numbers(value(), 2000);
      else if (a == "--gpu-fan-speed")
        o.controls.fan_percent = numbers(value(), 100);
      else if (a == "--gpu-temp-warn")
        o.warn_temperature = integer(value(), 120);
      else if (a == "--gpu-temp-alarm")
        o.alarm_temperature = integer(value(), 120);
      else if (a == "--tui")
        o.tui = "on";
      else if (a == "--no-tui")
        o.tui = "off";
      else if (a == "--list-devices")
        list = true;
      else if (a == "--tui-demo")
        demo = true;
      else if (a == "--version") {
        std::cout << "supryolo/0.1.0\n";
        return 0;
      } else if (a == "--help" || a == "-h")
        help = true;
      else
        throw std::runtime_error("unknown argument: " + a);
    }
    if (help || argc == 1) {
      usage();
      return 0;
    }
    if (o.cpu_variant != "auto" && o.cpu_variant != "scalar" && o.cpu_variant != "avx2")
      throw std::runtime_error("CPU variant must be auto, scalar or avx2");
    if (int(bench) + int(demo) + int(list) > 1)
      throw std::runtime_error("choose one inspection/benchmark mode");
    if (list && o.controls.requested())
      throw std::runtime_error("--list-devices cannot apply hardware settings");
    if (o.warn_temperature >= o.alarm_temperature || !o.warn_temperature)
      throw std::runtime_error("temperature thresholds require 0 < warn < alarm");
    if ((bench || demo || list) && !o.url.empty())
      throw std::runtime_error("inspection/benchmark mode cannot be combined with --url");
    if (demo) {
      if (o.controls.requested())
        throw std::runtime_error("demo cannot apply hardware settings");
      DemoSignals signals;
      yolo::Dashboard ui("on");
      yolo::DashboardView v;
      v.state = "DEMO - NO MINING";
      v.pool = "stratum+tcp://example.invalid:4444";
      v.worker = "matrix-demo";
      v.difficulty = "128";
      v.accepted = 42;
      v.rejected = 1;
      v.devices = {{"GPU0", "NVIDIA RTX 5090", 17.3e9, 0, 23, 0, 0, {}},
                   {"GPU1", "NVIDIA RTX 5090", 17.2e9, 0, 19, 1, 0, {}},
                   {"CPU", "CPU SIMD x8", 40e6, 0, 0, 0, 0, {}, {}}};
      v.devices[0].sensors.temperature = 62;
      v.devices[0].sensors.fan_percent = 65;
      v.devices[0].sensors.core_mhz = 2800;
      v.devices[0].sensors.memory_mhz = 15001;
      v.devices[0].sensors.watts = 570;
      v.devices[1].sensors.temperature = 86;
      v.devices[1].sensors.fan_percent = 95;
      v.devices[1].sensors.core_mhz = 2700;
      v.devices[1].sensors.memory_mhz = 15001;
      v.devices[1].sensors.watts = 575;
      ui.update(v);
      ui.event("DEMO ONLY: no devices or pool connection", yolo::Severity::warning);
      ui.event("Share accepted (GPU0)", yolo::Severity::success);
      ui.event("Share rejected (GPU1, above target)", yolo::Severity::error);
      ui.event("Vardiff retarget -> 128");
      ui.event("GPU1 86C ALARM", yolo::Severity::error);
      auto start = std::chrono::steady_clock::now();
      while (!demo_stop && !ui.quit_requested()) {
        v.elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (v.elapsed > (seconds_set ? o.seconds : 10))
          break;
        ui.update(v);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }
      return 0;
    }
    if (list) {
      auto ds = yolo::gpu_devices(o.gpu_mode);
      yolo::GpuManagement hw(ds);
      for (size_t i = 0; i < ds.size(); ++i) {
        auto r = hw.sample(i);
        std::cout << "GPU" << ds[i].index << " " << ds[i].name
                  << (ds[i].api == yolo::GpuApi::cuda ? " [CUDA]" : " [OpenCL]") << " PCI "
                  << ds[i].pci_bus
                  << " temp=" << (r.temperature ? std::to_string(*r.temperature) + "C" : "--")
                  << " fan=" << (r.fan_percent ? std::to_string(*r.fan_percent) + "%" : "--")
                  << " core=" << (r.core_mhz ? std::to_string(*r.core_mhz) : "--")
                  << " mem=" << (r.memory_mhz ? std::to_string(*r.memory_mhz) : "--")
                  << " power=" << (r.watts ? std::to_string(*r.watts) + "W" : "--") << '\n';
      }
      std::cout << "CPU " << yolo::cpu_backend(o.cpu_variant)->name()
                << " logical threads=" << std::thread::hardware_concurrency() << '\n';
      return 0;
    }
    if (o.no_cpu && o.no_gpu)
      throw std::runtime_error("No mining devices enabled (--no-cpu and --no-gpu)");
    if (o.no_gpu && o.devices_explicit)
      throw std::runtime_error("--gpu-device conflicts with --no-gpu");
    if (!o.url.empty())
      return yolo::mine(o);
    if (!bench) {
      usage();
      return 1;
    }
    auto all = o.no_gpu ? std::vector<yolo::GpuInfo>{} : yolo::gpu_devices(o.gpu_mode);
    std::vector<yolo::GpuInfo> selected;
    if (o.devices_explicit) {
      for (int id : o.devices) {
        auto it =
            std::find_if(all.begin(), all.end(), [&](const auto &d) { return d.index == id; });
        if (it == all.end())
          throw std::runtime_error("GPU device unavailable");
        selected.push_back(*it);
      }
    } else
      selected = all;
    unsigned cpu_threads =
        o.no_cpu ? 0
                 : (o.cpu_threads ? o.cpu_threads
                                  : std::max(1u, std::thread::hardware_concurrency() / 2));
    if (selected.empty() && !cpu_threads)
      throw std::runtime_error("No enabled devices available");
    yolo::GpuManagement hw(selected);
    for (auto &e : hw.apply(o.controls))
      std::cout << e << '\n';
    if (!seconds_set)
      o.seconds = 10;
    struct Result {
      std::string name, error;
      uint64_t hashes = 0;
      double seconds = 0;
    };
    std::vector<Result> results(selected.size() + cpu_threads);
    std::vector<std::jthread> threads;
    for (size_t i = 0; i < results.size(); ++i)
      threads.emplace_back([&, i]() {
        try {
          bool cpu = i >= selected.size();
          auto backend = cpu ? yolo::cpu_backend(o.cpu_variant)
                             : yolo::gpu_backend(selected[i], o.block, o.variant, o.opencl_variant);
          auto &r = results[i];
          r.name = cpu ? "CPU" : "GPU" + std::to_string(selected[i].index);
          r.name += " " + backend->name();
          yolo::Work w;
          for (size_t j = 0; j < w.header.size(); ++j)
            w.header[j] = uint8_t(j * 7 + 3);
          w.target = yolo::difficulty_target("128");
          uint32_t batch = cpu ? o.cpu_batch : o.batch;
          backend->scan(w, 0, batch);
          auto start = std::chrono::steady_clock::now();
          do {
            auto scan = backend->scan(w, r.hashes, batch);
            r.hashes += scan.hashes;
            r.seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
          } while (r.seconds < o.seconds);
        } catch (const std::exception &e) {
          results[i].error = e.what();
        }
      });
    for (auto &t : threads)
      t.join();
    double total = 0, cpu_rate = 0;
    bool failed = false;
    for (size_t i = 0; i < results.size(); ++i) {
      const auto &r = results[i];
      if (!r.error.empty()) {
        std::cerr << r.error << '\n';
        failed = true;
        continue;
      }
      double mh = r.hashes / r.seconds / 1e6;
      total += mh;
      if (i >= selected.size())
        cpu_rate += mh;
      else
        std::cout << r.name << " variant="
                  << (selected[i].api == yolo::GpuApi::opencl
                          ? (o.opencl_variant < 0 ? std::string("auto")
                                                  : std::to_string(o.opencl_variant))
                          : std::to_string(o.variant))
                  << " block=" << yolo::gpu_block(selected[i], o.block) << " batch=" << o.batch
                  << " hashes=" << r.hashes << " seconds=" << r.seconds << " MH/s=" << mh << '\n';
    }
    if (cpu_threads)
      std::cout << "CPU threads=" << cpu_threads << " MH/s=" << cpu_rate << '\n';
    std::cout << "Benchmark total MH/s=" << total << '\n';
    return failed ? 1 : 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
