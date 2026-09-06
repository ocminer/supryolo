// SPDX-License-Identifier: MIT
#include "yolo/core.hpp"
#include "yolo/stratum.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
void require(bool b) {
  if (!b)
    throw std::runtime_error("test failed");
}
int main(int argc, char **argv) {
  try {
    using namespace yolo;
    require(hex(blake2b256({})) ==
            "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8");
    require(hex(blake2b256(unhex("616263"))) ==
            "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319");
    require(hex(difficulty_target("1")) ==
            "00000000ffff0000000000000000000000000000000000000000000000000000");
    require(difficulty_target("128") == difficulty_target("1.28e2"));
    {
      using J = nlohmann::json;
      std::string cb =
          "0000001b11a8e2c16084cc487838df37dd867fed260104a09be4298db7a1e833ff4b0900000000";
      auto h = sia_work(std::string(64, '0'), cb, "", "01020304", "05060708090a0b0c",
                        "00000000c0ffee00");
      require(hex(std::span(h).subspan(48)) ==
              "b1bd2099bfadca5f276ea8fc0c3148aa6c775d61ed5d5a4ce9eb3d9fae01f178");
      StratumState state;
      state.receive({{"id", 1}, {"result", J::array({J::array(), "01020304", 8})}});
      state.receive({{"id", 3}, {"result", true}});
      state.receive({{"method", "mining.set_difficulty"}, {"params", J::array({128})}});
      J notify = {{"method", "mining.notify"},
                  {"params", J::array({"a", std::string(64, '0'), cb, "", J::array(), "20000000",
                                       "190f0b50", "00000000c0ffee00", true})}};
      auto a = *state.receive(notify);
      require(state.valid(a));
      require(a.work.target == difficulty_target("128"));
      state.receive({{"method", "mining.set_difficulty"}, {"params", J::array({256})}});
      require(state.current->work.target == a.work.target);
      state.receive({{"method", "mining.set_extranonce"}, {"params", J::array({"05060708", 8})}});
      require(state.valid(a));
      notify["params"][8] = false;
      auto b = *state.receive(notify);
      require(!state.valid(a));
      require(state.valid(b));
      require(b.work.header != a.work.header);
      require(b.work.target == difficulty_target("256"));
      notify["params"][0] = "b";
      auto c = *state.receive(notify);
      require(state.valid(b) && state.valid(c));
      notify["params"][8] = true;
      state.receive(notify);
      require(!state.valid(b) && !state.valid(c));
      bool bad = false;
      notify["params"][7] = "00000000";
      try {
        state.receive(notify);
      } catch (...) {
        bad = true;
      }
      require(bad);
    }
    if (argc > 1) {
      int device = std::stoi(argv[1]);
      std::mt19937_64 rng(42);
      for (int variant = 0; variant < 4; ++variant) {
        for (int trial = 0; trial < 8; ++trial) {
          Header h;
          for (auto &b : h)
            b = uint8_t(rng());
          uint64_t start = trial == 0   ? 0
                           : trial == 1 ? 0xfffffff0ULL
                           : trial == 2 ? UINT64_MAX - 1023
                                        : rng() >> 1;
          auto hashes = cuda_hashes(h, start, 1024, device, variant);
          for (size_t i = 0; i < hashes.size(); ++i) {
            store_le(h.data() + 32, start + i);
            require(blake2b256(h) == hashes[i]);
          }
        }
        Work w;
        for (auto &b : w.header)
          b = uint8_t(rng());
        w.target.fill(255);
        w.target[0] = 8;
        auto cpu = cpu_backend(), gpu = cuda_backend(device, 128, variant);
        auto a = cpu->scan(w, 0xfffff000ULL, 32768), b = gpu->scan(w, 0xfffff000ULL, 32768);
        std::sort(b.nonces.begin(), b.nonces.end());
        require(a.nonces == b.nonces);
        // Exercise equality and the low target limbs beyond the GPU's 64-bit filter.
        constexpr uint64_t nonce = 0x100000007ULL;
        store_le(w.header.data() + 32, nonce);
        w.target = blake2b256(w.header);
        require(gpu->scan(w, nonce, 1).nonces == std::vector<uint64_t>{nonce});
        auto equal_target = w.target;
        for (int i = 31; i >= 0; --i) {
          if (w.target[i]-- != 0)
            break;
        }
        require(std::equal(w.target.begin(), w.target.begin() + 8, equal_target.begin()));
        require(gpu->scan(w, nonce, 1).nonces.empty());
        // Every nonce qualifies: overflowing the result buffer must be explicit.
        w.target.fill(255);
        bool overflow = false;
        try {
          gpu->scan(w, 0, 4097);
        } catch (const std::runtime_error &e) {
          overflow = std::string(e.what()).find("candidate buffer overflow") != std::string::npos;
        }
        require(overflow);
      }
    }
    std::cout << "PASS scalar vectors, exact difficulty"
              << (argc > 1 ? ", 32768 GPU full hashes and candidate-set comparisons" : "") << "\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
