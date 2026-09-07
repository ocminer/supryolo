// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/sv2_session.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace yolo;
void check(bool b) {
  if (!b)
    throw std::runtime_error("SV2 session test failed");
}
Bytes read(size_t n) {
  Bytes b(n);
  std::cin.read(reinterpret_cast<char *>(b.data()), n);
  check(bool(std::cin));
  return b;
}
int main() {
  auto k = unhex("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
  Hash key;
  std::copy(k.begin(), k.end(), key.begin());
  sv2::Session s(key, "example.invalid", 14444, "test", 1, [](const Bytes &b) {
    std::cout.write(reinterpret_cast<const char *>(b.data()), b.size());
    std::cout.flush();
  });
  s.receive(read(234));
  auto next = [&]() {
    auto b = read(4);
    size_t n = 0;
    for (int i = 0; i < 4; ++i)
      n |= size_t(b[i]) << (8 * i);
    check(n < 65536);
    return s.receive(read(n));
  };
  next();
  next();
  next();
  check(!s.current);
  next();
  check(bool(s.current));
  auto first = *s.current;
  check(s.valid(first));
  check(first.network_target == compact_target("190f0b50"));
  check(meets(first.work.target, first.work.target));
  check(!meets(first.work.target, first.network_target));
  for (uint32_t i = 10; i < 13; ++i)
    s.submit(first, i, i);
  auto events = next();
  for (auto &e : events)
    check(!e.contains("id"));
  events = next();
  unsigned accepted = 0, stale = 0;
  for (auto &e : events) {
    if (e.value("result", false))
      ++accepted;
    else if (e.contains("error"))
      ++stale;
  }
  check(accepted == 2 && stale == 1);
  next();
  check(s.current->generation == first.generation);
  next();
  check(s.current->work.target == first.work.target);
  check(s.current->network_target == first.network_target);
  next();
  auto second = *s.current;
  check(second.network_target == compact_target("190e0000"));
  check(first.network_target == compact_target("190f0b50"));
  check(!s.valid(first) && s.valid(second) && second.work.target < first.work.target);
  next();
  auto third = *s.current;
  check(third.network_target == second.network_target);
  check(s.valid(second) && s.valid(third) && third.time32 == 20);
  next();
  check(s.current->work.target == third.work.target);
  check(s.current->network_target == third.network_target);
  std::cerr << "SV2 future jobs, target snapshots, stale epochs and delayed batch errors passed\n";
}
