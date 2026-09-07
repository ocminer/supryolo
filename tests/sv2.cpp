// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/sv2.hpp"
#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace yolo;
using namespace yolo::sv2;
void check(bool b) {
  if (!b)
    throw std::runtime_error("SV2 test failed");
}
template <class F> void rejects(F f) {
  bool rejected = false;
  try {
    f();
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected);
}
int main() {
  auto b = unhex("0000000000009d43a50f45138b80444e8b5711cb49143d2a76f5a9101e22963e");
  Hash hidden;
  std::copy(b.begin(), b.end(), hidden.begin());
  b = unhex("96a12f9a5d9b4b27ef056eded78de44989488aff7fd6b8ea20f937ee95e27074");
  Hash root;
  std::copy(b.begin(), b.end(), root.begin());
  // Commitment independently derived from the pool's public block 968704 vector.
  auto cb = unhex("62a14cabbc8332aec619d1e99de5c49964a7fe0c5c176134d5e05f48933431b9");
  Hash commitment;
  std::copy(cb.begin(), cb.end(), commitment.begin());
  auto extra = unhex("74e09d6a0200000000000000");
  check(standard_work(hidden, commitment, extra, 0x6a9e0a02) ==
        work_header(hidden, root, 0x6a9e0a02));
  rejects([&] { standard_work(hidden, commitment, Bytes(4), 0); });
  auto real_prev = hidden;
  real_prev[0] = 1;
  rejects([&] { standard_work(real_prev, commitment, extra, 0); });
  auto h = work_header(hidden, root, 0x6a9e0a02);
  store_le(h.data() + 32, 0x11223344);
  auto hash = blake2b256(h);
  std::reverse(hash.begin(), hash.end());
  check(hex(hash) == "c1864f57ff74e5b8532f081497dc166804aa1d1f67fc27871adbf07804a15b70");
  b = unhex("53a864ec548e019000000000020a9e6a");
  std::copy(b.begin(), b.end(), h.begin() + 32);
  hash = blake2b256(h);
  std::reverse(hash.begin(), hash.end());
  check(hex(hash) == "7cfe56452a2a0819313373d079d4b998f2b3deedb8c80eff0300000000000000");
  auto f = submit_standard(1, 2, 3, 0xddccbbaa, 0x11223344, 0x20000000);
  check(f.extension == 0x8000 && f.type == 0x1a);
  check(hex(f.payload) == "010000000200000003000000aabbccdd4433221100000020");
  Frame job{0x8000, 0x15, unhex("010000000200000001020a9e6a00000020")};
  job.payload.insert(job.payload.end(), root.begin(), root.end());
  auto j = std::get<NewJob>(decode(job));
  check(j.channel == 1 && j.job == 2 && j.ntime == 0x6a9e0a02 && j.root == root);
  for (size_t n = 0; n < job.payload.size(); ++n) {
    auto truncated = job;
    truncated.payload.resize(n);
    rejects([&] { decode(truncated); });
  }
  auto bad = job;
  bad.payload.push_back(0);
  rejects([&] { decode(bad); });
  bad = job;
  bad.payload[8] = 2;
  rejects([&] { decode(bad); });
  bad = job;
  bad.extension = 0;
  rejects([&] { decode(bad); });
  job.payload.erase(job.payload.begin() + 9, job.payload.begin() + 13);
  job.payload[8] = 0;
  check(!std::get<NewJob>(decode(job)).ntime);
  Frame target{0x8000, 0x21, Bytes(36)};
  target.payload[0] = 1;
  target.payload[4] = 2;
  auto t = std::get<Target>(decode(target));
  check(t.channel == 1 && t.target[31] == 2 && t.target[0] == 0);
  target.payload[4] = 0;
  rejects([&] { decode(target); });
  auto p = partition(0xfffffff0, 0, 0xffffffff, 128);
  check(p.nonce == 0xffffffff && p.count == 1 && p.ntime == 0xfffffff0);
  p = partition(0xfffffff0, 0, uint64_t{1} << 32, 128);
  check(p.nonce == 0 && p.count == 128 && p.ntime == 0xfffffff1);
  std::set<uint32_t> times;
  for (unsigned slot = 0; slot < 192; ++slot)
    for (uint64_t cursor : {uint64_t{0}, ((uint64_t{1} << 24) - 1) << 32})
      check(times.insert(partition(0xf1234567, slot, cursor, 128).ntime).second);
  rejects([] { partition(0, 256, 0, 1); });
  rejects([] { partition(0, 0, uint64_t{1} << 56, 1); });
  rejects([] { setup(std::string(256, 'x'), 1, "test"); });
  auto ack =
      std::get<Accepted>(decode({0x8000, 0x1c, unhex("0100000009000000070000000000000001000000")}));
  check(ack.count == 7 && ack.last_sequence == 9 && ack.difficulty_sum == (uint64_t{1} << 32));
  std::cout << "SV2 codec, BTCB2 vectors, nonce partition tests passed\n";
}
