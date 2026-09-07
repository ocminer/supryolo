// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/sv2.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
namespace yolo::sv2 {
namespace {
struct Reader {
  std::span<const uint8_t> b;
  uint64_t integer(size_t n) {
    if (b.size() < n)
      throw std::runtime_error("truncated SV2 message");
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i)
      v |= uint64_t(b[i]) << (8 * i);
    b = b.subspan(n);
    return v;
  }
  uint32_t u32() { return uint32_t(integer(4)); }
  Bytes bytes(size_t n) {
    if (b.size() < n)
      throw std::runtime_error("truncated SV2 bytes");
    Bytes out(b.begin(), b.begin() + n);
    b = b.subspan(n);
    return out;
  }
  Hash hash(bool target = false) {
    auto v = bytes(32);
    Hash h;
    std::copy(v.begin(), v.end(), h.begin());
    if (target) {
      std::reverse(h.begin(), h.end());
      if (std::all_of(h.begin(), h.end(), [](auto x) { return x == 0; }))
        throw std::runtime_error("zero SV2 target");
    }
    return h;
  }
  Bytes short_bytes(size_t max) {
    auto n = integer(1);
    if (n > max)
      throw std::runtime_error("invalid SV2 byte string length");
    return bytes(n);
  }
  std::string string() {
    auto v = short_bytes(255);
    return {v.begin(), v.end()};
  }
  void end() {
    if (!b.empty())
      throw std::runtime_error("unexpected trailing SV2 bytes");
  }
};
struct Writer {
  Bytes b;
  void integer(uint64_t v, size_t n) {
    for (size_t i = 0; i < n; ++i)
      b.push_back(uint8_t(v >> (8 * i)));
  }
  void string(const std::string &s) {
    if (s.size() > 255)
      throw std::runtime_error("SV2 string exceeds 255 bytes");
    integer(s.size(), 1);
    b.insert(b.end(), s.begin(), s.end());
  }
};
} // namespace
Message decode(const Frame &f) {
  bool channel = f.type >= 0x15 && f.type <= 0x24;
  if (f.extension != (channel ? 0x8000 : 0))
    throw std::runtime_error("unsupported SV2 extension or channel bit");
  Reader r{f.payload};
  auto value = [&]() -> Message {
    switch (f.type) {
    case 1: {
      auto version = uint16_t(r.integer(2));
      auto flags = r.u32();
      return SetupSuccess{version, flags};
    }
    case 0x11: {
      auto request = r.u32(), c = r.u32();
      auto t = r.hash(true);
      auto p = r.short_bytes(32);
      auto g = r.u32();
      return OpenSuccess{request, c, t, p, g};
    }
    case 0x15: {
      auto c = r.u32(), j = r.u32();
      auto present = r.integer(1);
      if (present > 1)
        throw std::runtime_error("invalid SV2 optional ntime");
      std::optional<uint32_t> t;
      if (present)
        t = r.u32();
      auto v = r.u32();
      return NewJob{c, j, t, v, r.hash()};
    }
    case 0x20: {
      auto c = r.u32(), j = r.u32();
      auto h = r.hash();
      auto t = r.u32(), n = r.u32();
      return PrevHash{c, j, h, t, n};
    }
    case 0x21: {
      auto c = r.u32();
      return Target{c, r.hash(true)};
    }
    case 0x1c: {
      auto c = r.u32(), s = r.u32(), n = r.u32();
      return Accepted{c, s, n, r.integer(8)};
    }
    case 0x1d: {
      auto c = r.u32(), s = r.u32();
      return Rejected{c, s, r.string()};
    }
    case 2:
    case 0x12:
    case 0x18: {
      auto ref = r.u32();
      return Error{f.type, ref, r.string()};
    }
    default:
      throw std::runtime_error("unsupported SV2 message type " + std::to_string(f.type));
    }
  }();
  r.end();
  return value;
}
Frame setup(const std::string &host, uint16_t port, const std::string &agent) {
  Writer w;
  w.integer(0, 1);
  w.integer(2, 2);
  w.integer(2, 2);
  w.integer(1, 4);
  w.string(host);
  w.integer(port, 2);
  w.string("ocminer");
  w.string("supryolo");
  w.string(agent);
  w.string("");
  return {0, 0, std::move(w.b)};
}
Frame open_standard(uint32_t request, const std::string &user, float rate, const Hash &max_target) {
  if (!std::isfinite(rate) || rate < 0)
    throw std::runtime_error("invalid SV2 nominal hashrate");
  Writer w;
  w.integer(request, 4);
  w.string(user);
  w.integer(std::bit_cast<uint32_t>(rate), 4);
  w.b.insert(w.b.end(), max_target.rbegin(), max_target.rend());
  return {0, 0x10, std::move(w.b)};
}
Frame submit_standard(uint32_t c, uint32_t s, uint32_t j, uint32_t n, uint32_t t, uint32_t v) {
  Writer w;
  for (auto x : {c, s, j, n, t, v})
    w.integer(x, 4);
  return {0x8000, 0x1a, std::move(w.b)};
}
Header work_header(const Hash &hidden, const Hash &root, uint32_t t) {
  Header h{};
  std::copy(hidden.begin(), hidden.end(), h.begin());
  store_le(h.data() + 40, t);
  std::copy(root.begin(), root.end(), h.begin() + 48);
  return h;
}
Header standard_work(const Hash &hidden, const Hash &commitment,
                     std::span<const uint8_t> extranonce, uint32_t ntime) {
  if (extranonce.size() != 12 ||
      !std::all_of(hidden.begin(), hidden.begin() + 6, [](auto b) { return b == 0; }))
    throw std::runtime_error("invalid BTCB2 SV2 hidden hash or extranonce length");
  Bytes leaf(4, 0);
  leaf.insert(leaf.end(), commitment.begin(), commitment.end());
  leaf.insert(leaf.end(), 4, 0);
  leaf.insert(leaf.end(), extranonce.begin(), extranonce.end());
  return work_header(hidden, blake2b256(leaf), ntime);
}
Partition partition(uint32_t base, unsigned slot, uint64_t cursor, uint32_t batch) {
  // At most 256 slots, each owns 2^56 hashes; time addition is modulo 2^32.
  if (slot >= 256 || cursor >= (uint64_t{1} << 56) || !batch)
    throw std::runtime_error("SV2 nonce partition exhausted or invalid");
  auto nonce = uint32_t(cursor);
  return {nonce, base + (uint32_t(slot) << 24) + uint32_t(cursor >> 32),
          uint32_t(std::min<uint64_t>(batch, (uint64_t{1} << 32) - nonce))};
}
} // namespace yolo::sv2
