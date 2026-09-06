// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
// Scalar reference implementation of the BLAKE2b specification (RFC 7693).
#include "yolo/core.hpp"
#include <algorithm>
#include <bit>
#include <openssl/bn.h>
#include <openssl/sha.h>
#include <stdexcept>
namespace yolo {
Bytes unhex(const std::string &s) {
  if (s.size() % 2)
    throw std::runtime_error("odd hex length");
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    throw std::runtime_error("invalid hex");
  };
  Bytes b(s.size() / 2);
  for (size_t i = 0; i < b.size(); ++i)
    b[i] = (nib(s[2 * i]) << 4) | nib(s[2 * i + 1]);
  return b;
}
std::string hex(std::span<const uint8_t> b) {
  const char *d = "0123456789abcdef";
  std::string s;
  s.reserve(b.size() * 2);
  for (auto v : b) {
    s += d[v >> 4];
    s += d[v & 15];
  }
  return s;
}
uint64_t load_le(const uint8_t *p) {
  uint64_t x = 0;
  for (int i = 7; i >= 0; --i)
    x = (x << 8) | p[i];
  return x;
}
void store_le(uint8_t *p, uint64_t x) {
  for (int i = 0; i < 8; ++i) {
    p[i] = x;
    x >>= 8;
  }
}
Hash blake2b256(std::span<const uint8_t> b) {
  if (b.size() > 128)
    throw std::runtime_error("reference BLAKE2b supports one block only");
  const uint64_t iv[] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                         0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                         0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
  const int sigma[10][16] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
                             {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
                             {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
                             {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
                             {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
                             {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
                             {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
                             {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
                             {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
                             {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0}};
  std::array<uint8_t, 128> block{};
  std::copy(b.begin(), b.end(), block.begin());
  uint64_t m[16], v[16], h[8];
  for (int i = 0; i < 16; ++i)
    m[i] = load_le(block.data() + 8 * i);
  for (int i = 0; i < 8; ++i) {
    h[i] = iv[i];
    v[i + 8] = iv[i];
  }
  h[0] ^= 0x01010020;
  std::copy(h, h + 8, v);
  v[12] ^= b.size();
  v[14] = ~v[14];
  auto g = [&](int a, int bb, int c, int d, uint64_t x, uint64_t y) {
    v[a] += v[bb] + x;
    v[d] = std::rotr(v[d] ^ v[a], 32);
    v[c] += v[d];
    v[bb] = std::rotr(v[bb] ^ v[c], 24);
    v[a] += v[bb] + y;
    v[d] = std::rotr(v[d] ^ v[a], 16);
    v[c] += v[d];
    v[bb] = std::rotr(v[bb] ^ v[c], 63);
  };
  for (int r = 0; r < 12; ++r) {
    auto s = sigma[r % 10];
    g(0, 4, 8, 12, m[s[0]], m[s[1]]);
    g(1, 5, 9, 13, m[s[2]], m[s[3]]);
    g(2, 6, 10, 14, m[s[4]], m[s[5]]);
    g(3, 7, 11, 15, m[s[6]], m[s[7]]);
    g(0, 5, 10, 15, m[s[8]], m[s[9]]);
    g(1, 6, 11, 12, m[s[10]], m[s[11]]);
    g(2, 7, 8, 13, m[s[12]], m[s[13]]);
    g(3, 4, 9, 14, m[s[14]], m[s[15]]);
  }
  Hash out;
  for (int i = 0; i < 4; ++i)
    store_le(out.data() + 8 * i, h[i] ^ v[i] ^ v[i + 8]);
  return out;
}
Hash tagged_sha256(const std::string &tag, std::span<const uint8_t> b) {
  Hash t, out;
  SHA256((const uint8_t *)tag.data(), tag.size(), t.data());
  Bytes pre(t.begin(), t.end());
  pre.insert(pre.end(), t.begin(), t.end());
  pre.insert(pre.end(), b.begin(), b.end());
  SHA256(pre.data(), pre.size(), out.data());
  return out;
}
bool meets(const Hash &h, const Hash &t) {
  return !std::lexicographical_compare(t.begin(), t.end(), h.begin(), h.end());
}
Hash difficulty_target(const std::string &decimal) {
  // Exact rational conversion of decimal difficulty. Bitcoin difficulty-one target.
  if (decimal.empty() || decimal.size() > 96)
    throw std::runtime_error("invalid difficulty");
  std::string mant = decimal;
  int exponent = 0;
  auto ep = mant.find_first_of("eE");
  if (ep != std::string::npos) {
    auto e = mant.substr(ep + 1);
    size_t n = 0;
    exponent = std::stoi(e, &n);
    if (n != e.size() || exponent > 300 || exponent < -300)
      throw std::runtime_error("difficulty exponent");
    mant.resize(ep);
  }
  auto dot = mant.find('.');
  if (dot != std::string::npos) {
    exponent -= int(mant.size() - dot - 1);
    mant.erase(dot, 1);
  }
  if (mant.empty() || mant.find_first_not_of("0123456789") != std::string::npos)
    throw std::runtime_error("invalid difficulty");
  using BN = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
  using CTX = std::unique_ptr<BN_CTX, decltype(&BN_CTX_free)>;
  BIGNUM *raw = nullptr;
  BN_dec2bn(&raw, mant.c_str());
  BN den(raw, BN_free), num(BN_new(), BN_free), out(BN_new(), BN_free);
  CTX ctx(BN_CTX_new(), BN_CTX_free);
  if (!den || !num || !out || !ctx || BN_is_zero(den.get()))
    throw std::runtime_error("invalid difficulty");
  BN_set_word(num.get(), 65535);
  BN_lshift(num.get(), num.get(), 208);
  for (int i = 0; i < abs(exponent); ++i)
    if (!BN_mul_word(exponent < 0 ? num.get() : den.get(), 10))
      throw std::runtime_error("BN multiply failed");
  if (!BN_div(out.get(), nullptr, num.get(), den.get(), ctx.get()))
    throw std::runtime_error("BN divide failed");
  Hash target{};
  if (BN_num_bits(out.get()) > 256)
    target.fill(255);
  else
    BN_bn2binpad(out.get(), target.data(), 32);
  return target;
}
} // namespace yolo
