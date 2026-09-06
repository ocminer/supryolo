// SPDX-License-Identifier: MIT
#include "yolo/core.hpp"
#include "yolo/stratum.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
uint64_t integer(const std::string &s, uint64_t maximum) {
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("expected nonnegative integer");
  size_t used;
  auto value = std::stoull(s, &used);
  if (used != s.size() || value > maximum)
    throw std::runtime_error("integer out of range");
  return value;
}
} // namespace
int main(int argc, char **argv) {
  try {
    yolo::MineOptions options;
    bool bench = false, batch_set = false, seconds_set = false;
    options.block = 256;
    options.variant = 3;
    options.batch = 1 << 26;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      auto value = [&]() {
        if (i + 1 >= argc)
          throw std::runtime_error("missing value for " + a);
        return std::string(argv[++i]);
      };
      if (a == "--benchmark")
        bench = true;
      else if (a == "-d") {
        auto v = value();
        options.devices.clear();
        std::set<int> seen;
        size_t pos = 0;
        do {
          auto end = v.find(',', pos);
          int d = integer(v.substr(pos, end == std::string::npos ? end : end - pos), 255);
          if (!seen.insert(d).second)
            throw std::runtime_error("duplicate device");
          options.devices.push_back(d);
          if (end == std::string::npos)
            break;
          pos = end + 1;
        } while (true);
      } else if (a == "--block")
        options.block = integer(value(), 1024);
      else if (a == "--variant")
        options.variant = integer(value(), 3);
      else if (a == "--batch") {
        options.batch = integer(value(), 1U << 28);
        batch_set = true;
        if (!options.batch)
          throw std::runtime_error("batch must be positive");
      } else if (a == "--seconds") {
        auto s = value();
        size_t used;
        options.seconds = std::stod(s, &used);
        if (used != s.size() || !std::isfinite(options.seconds) || options.seconds <= 0)
          throw std::runtime_error("seconds must be positive and finite");
        seconds_set = true;
      } else if (a == "--url")
        options.url = value();
      else if (a == "--user")
        options.user = value();
      else if (a == "--password")
        options.password = value();
      else if (a == "--cpu")
        options.cpu = true;
      else if (a == "--version") {
        std::cout << "supryolo/0.1.0-dev\n";
        return 0;
      } else if (a != "--help")
        throw std::runtime_error("unknown argument: " + a);
    }
    if (options.cpu && !batch_set)
      options.batch = 16384;
    if (bench && !options.url.empty())
      throw std::runtime_error("choose benchmark or pool mode");
    if (!options.url.empty())
      return yolo::mine(options);
    if (!bench) {
      std::cout << "supryolo/0.1.0-dev\n"
                   "Pool: --url stratum+tcp://de.b2pool.io:4444 --user ADDRESS.worker -d 0\n"
                   "Benchmark: --benchmark -d 0 [--seconds 10]\n"
                   "Options: -d 0,1 --cpu --block 256 --batch 67108864 --variant 3\n"
                   "Variants: 0 native, 1 PTX, 2 native precomputed, 3 PTX precomputed\n";
      return 0;
    }
    if (options.devices.size() != 1)
      throw std::runtime_error("benchmark one GPU at a time");
    if (!seconds_set)
      options.seconds = 10;
    auto backend = options.cpu
                       ? yolo::cpu_backend()
                       : yolo::cuda_backend(options.devices[0], options.block, options.variant);
    yolo::Work work;
    for (size_t i = 0; i < work.header.size(); ++i)
      work.header[i] = uint8_t(i * 7 + 3);
    work.target = yolo::difficulty_target("128");
    backend->scan(work, 0, options.batch);
    auto start = std::chrono::steady_clock::now();
    uint64_t hashes = 0;
    double elapsed = 0;
    do {
      auto result = backend->scan(work, hashes, options.batch);
      hashes += result.hashes;
      elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    } while (elapsed < options.seconds);
    std::cout << "device=" << options.devices[0] << " name=" << backend->name()
              << " variant=" << options.variant << " block=" << options.block
              << " batch=" << options.batch << " hashes=" << hashes << " seconds=" << elapsed
              << " MH/s=" << hashes / elapsed / 1e6 << '\n';
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
