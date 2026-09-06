// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
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
    require(compact_target("1d00ffff") == difficulty_target("1"));
    require(hex(compact_target("03000001")) == std::string(63, '0') + "1");
    for (const auto &invalid : {"01000001", "1d80ffff", "23000001", "22010000"}) {
      bool rejected = false;
      try {
        compact_target(invalid);
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      require(rejected);
    }
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
    if (cpu_avx2_available()) {
      std::mt19937_64 rng(73);
      for (int trial = 0; trial < 32; ++trial) {
        Header h;
        for (auto &b : h)
          b = uint8_t(rng());
        uint32_t count = 257 + trial;
        uint64_t start = trial == 0   ? 0
                         : trial == 1 ? 0xfffffff0ULL
                         : trial == 2 ? UINT64_MAX - count + 1
                                      : rng() >> 1;
        auto hashes = cpu_avx2_hashes(h, start, count);
        for (size_t i = 0; i < hashes.size(); ++i) {
          store_le(h.data() + 32, start + i);
          require(hashes[i] == blake2b256(h));
        }
        Work w{h, {}};
        w.target.fill(255);
        w.target[0] = trial * 8;
        auto ref = cpu_backend("scalar"), fast = cpu_backend("avx2");
        require(ref->scan(w, start, count).nonces == fast->scan(w, start, count).nonces);
        store_le(w.header.data() + 32, start);
        w.target = blake2b256(w.header);
        require(fast->scan(w, start, 1).nonces == std::vector<uint64_t>{start});
        for (int j = 31; j >= 0; --j)
          if (w.target[j]-- != 0)
            break;
        require(fast->scan(w, start, 1).nonces.empty());
      }
      std::cout << "PASS AVX2 full hashes, tails, nonce boundaries and full targets\n";
    }
    if (argc > 1) {
      int device = std::stoi(argv[1]);
      std::mt19937_64 rng(42);
      for (int variant = 0; variant < 7; ++variant) {
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
        auto cpu = cpu_backend("scalar"), gpu = cuda_backend(device, 128, variant);
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
              << (argc > 1 ? ", 57344 GPU full hashes and candidate-set comparisons" : "") << "\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
