// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/core.hpp"
#include <algorithm>
#include <stdexcept>
namespace yolo {
Hash compact_target(const std::string &nbits) {
  auto bytes = unhex(nbits);
  if (bytes.size() != 4)
    throw std::runtime_error("invalid nbits");
  unsigned exponent = bytes[0],
           mantissa = (unsigned(bytes[1]) << 16) | (unsigned(bytes[2]) << 8) | bytes[3];
  if ((mantissa & 0x800000) || !mantissa || exponent > 34)
    throw std::runtime_error("invalid compact target");
  Hash out{};
  if (exponent <= 3) {
    mantissa >>= 8 * (3 - exponent);
    for (int i = 31; i >= 28; --i) {
      out[i] = mantissa & 255;
      mantissa >>= 8;
    }
  } else
    for (int i = 0; i < 3; ++i) {
      int position = 32 - int(exponent) + i;
      unsigned byte = (mantissa >> (16 - i * 8)) & 255;
      if (position < 0) {
        if (byte)
          throw std::runtime_error("compact target overflow");
      } else if (position < 32)
        out[position] = byte;
    }
  if (std::all_of(out.begin(), out.end(), [](uint8_t b) { return b == 0; }))
    throw std::runtime_error("zero compact target");
  return out;
}

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
