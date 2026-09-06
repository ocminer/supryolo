// SPDX-License-Identifier: MIT
#include "yolo/core.hpp"
#include <algorithm>
#include <stdexcept>
namespace yolo {
Header sia_work(const std::string &prev, const std::string &c1, const std::string &c2,
                const std::string &en1, const std::string &en2, const std::string &time) {
  auto p = unhex(prev), a = unhex(c1), b = unhex(c2), e1 = unhex(en1), e2 = unhex(en2),
       t = unhex(time);
  if (p.size() != 32 || a.size() != 39 || !b.empty() || e1.size() + e2.size() != 12 ||
      t.size() != 8)
    throw std::runtime_error("unsupported BTCB2 Sia job layout");
  // Sv1 prevhash is already the hidden hash, in direct byte order.
  Bytes leaf{0};
  leaf.insert(leaf.end(), a.begin(), a.end());
  leaf.insert(leaf.end(), e1.begin(), e1.end());
  leaf.insert(leaf.end(), e2.begin(), e2.end());
  auto root = blake2b256(leaf);
  Header w{};
  std::copy(p.begin(), p.end(), w.begin());
  std::copy(t.begin(), t.end(), w.begin() + 40);
  std::copy(root.begin(), root.end(), w.begin() + 48);
  return w;
}
} // namespace yolo
