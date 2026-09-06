// SPDX-License-Identifier: MIT
#include "yolo/core.hpp"
#include "yolo/hardware.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
void require(bool ok) {
  if (!ok)
    throw std::runtime_error("OpenCL validation failed");
}
int main(int argc, char **argv) {
  try {
    using namespace yolo;
    int device = argc > 1 ? std::stoi(argv[1]) : 0;
    int variant = argc > 2 ? std::stoi(argv[2]) : 0;
    std::mt19937_64 rng(189);
    uint64_t tested = 0;
    for (int trial = 0; trial < 8; ++trial) {
      Header h;
      for (auto &b : h)
        b = uint8_t(rng());
      uint32_t count = 1024 + trial;
      uint64_t start = trial == 0   ? 0
                       : trial == 1 ? 0xfffffff0ULL
                       : trial == 2 ? UINT64_MAX - count + 1
                                    : rng() >> 1;
      auto hashes = opencl_hashes(h, start, count, device, variant);
      require(hashes.size() == count);
      for (size_t i = 0; i < hashes.size(); ++i) {
        store_le(h.data() + 32, start + i);
        require(blake2b256(h) == hashes[i]);
        ++tested;
      }
    }
    auto cl = opencl_backend(device, 128, variant), reference = cpu_backend("scalar");
    Work w;
    for (auto &b : w.header)
      b = uint8_t(rng());
    w.target.fill(255);
    w.target[0] = 8;
    require(cl->scan(w, 0xfffff000ULL, 32769).nonces ==
            reference->scan(w, 0xfffff000ULL, 32769).nonces);
    constexpr uint64_t nonce = 0x100000007ULL;
    store_le(w.header.data() + 32, nonce);
    w.target = blake2b256(w.header);
    require(cl->scan(w, nonce, 1).nonces == std::vector<uint64_t>{nonce});
    auto equal = w.target;
    for (int i = 31; i >= 0; --i)
      if (w.target[i]-- != 0)
        break;
    require(std::equal(w.target.begin(), w.target.begin() + 8, equal.begin()));
    require(cl->scan(w, nonce, 1).nonces.empty());
    w.target.fill(255);
    bool overflow = false;
    try {
      cl->scan(w, 0, 4097);
    } catch (const std::runtime_error &e) {
      overflow = std::string(e.what()).find("candidate buffer overflow") != std::string::npos;
    }
    require(overflow);
    std::cout << "PASS " << tested
              << " OpenCL full hashes, nonce tails/boundaries, candidate sets, full target and "
                 "overflow\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
