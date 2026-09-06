// SPDX-License-Identifier: MIT
// Four independent 80-byte BLAKE2b-256 messages, following RFC 7693.
// Written for supryolo; the scalar implementation remains the independent oracle.
#include "yolo/core.hpp"
#include <chrono>
#include <stdexcept>
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define AVX2 __attribute__((target("avx2")))
#define INLINE_AVX2 AVX2 __attribute__((always_inline)) inline
namespace yolo {
namespace {
using V = __m256i;
constexpr uint64_t iv[8] = {0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
                            0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
                            0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL};
constexpr int sigma[10][16] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
                               {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
                               {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
                               {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
                               {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
                               {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
                               {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
                               {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
                               {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
                               {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0}};
INLINE_AVX2 V splat(uint64_t x) { return _mm256_set1_epi64x(x); }
INLINE_AVX2 void g(V &a, V &b, V &c, V &d, V x, V y) {
  const V r24 = _mm256_setr_epi8(3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10, 3, 4, 5, 6,
                                 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10);
  const V r16 = _mm256_setr_epi8(2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9, 2, 3, 4, 5,
                                 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9);
  a = _mm256_add_epi64(_mm256_add_epi64(a, b), x);
  d = _mm256_shuffle_epi32(_mm256_xor_si256(d, a), _MM_SHUFFLE(2, 3, 0, 1));
  c = _mm256_add_epi64(c, d);
  b = _mm256_shuffle_epi8(_mm256_xor_si256(b, c), r24);
  a = _mm256_add_epi64(_mm256_add_epi64(a, b), y);
  d = _mm256_shuffle_epi8(_mm256_xor_si256(d, a), r16);
  c = _mm256_add_epi64(c, d);
  b = _mm256_xor_si256(b, c);
  b = _mm256_or_si256(_mm256_slli_epi64(b, 1), _mm256_srli_epi64(b, 63));
}
template <int R> INLINE_AVX2 void round(V *v, const V *m) {
  constexpr auto s = sigma[R % 10];
  g(v[0], v[4], v[8], v[12], m[s[0]], m[s[1]]);
  g(v[1], v[5], v[9], v[13], m[s[2]], m[s[3]]);
  g(v[2], v[6], v[10], v[14], m[s[4]], m[s[5]]);
  g(v[3], v[7], v[11], v[15], m[s[6]], m[s[7]]);
  g(v[0], v[5], v[10], v[15], m[s[8]], m[s[9]]);
  g(v[1], v[6], v[11], v[12], m[s[10]], m[s[11]]);
  g(v[2], v[7], v[8], v[13], m[s[12]], m[s[13]]);
  g(v[3], v[4], v[9], v[14], m[s[14]], m[s[15]]);
}
INLINE_AVX2 void compress(const V *base, uint64_t nonce, V *out) {
  V m[16], v[16];
  for (int i = 0; i < 16; ++i)
    m[i] = base[i];
  m[4] = _mm256_set_epi64x(nonce + 3, nonce + 2, nonce + 1, nonce);
  for (int i = 0; i < 8; ++i)
    v[i] = v[i + 8] = splat(iv[i]);
  v[0] = splat(iv[0] ^ 0x01010020);
  v[12] = splat(iv[4] ^ 80);
  v[14] = splat(~iv[6]);
  round<0>(v, m);
  round<1>(v, m);
  round<2>(v, m);
  round<3>(v, m);
  round<4>(v, m);
  round<5>(v, m);
  round<6>(v, m);
  round<7>(v, m);
  round<8>(v, m);
  round<9>(v, m);
  round<10>(v, m);
  round<11>(v, m);
  for (int i = 0; i < 4; ++i)
    out[i] = _mm256_xor_si256(splat(iv[i] ^ (i == 0 ? 0x01010020 : 0)),
                              _mm256_xor_si256(v[i], v[i + 8]));
}
INLINE_AVX2 void prepare(const Header &h, V *m) {
  for (int i = 0; i < 16; ++i)
    m[i] = splat(i < 10 ? load_le(h.data() + 8 * i) : 0);
}
AVX2 std::vector<Hash> hashes(const Header &h, uint64_t start, uint32_t count) {
  V m[16], out[4];
  prepare(h, m);
  std::vector<Hash> result(count);
  for (uint64_t i = 0; i < count; i += 4) {
    compress(m, start + i, out);
    for (int word = 0; word < 4; ++word) {
      alignas(32) uint64_t lanes[4];
      _mm256_store_si256(reinterpret_cast<V *>(lanes), out[word]);
      for (unsigned lane = 0; lane < 4 && i + lane < count; ++lane)
        store_le(result[i + lane].data() + 8 * word, lanes[lane]);
    }
  }
  return result;
}
class CpuAvx2 final : public Backend {
public:
  std::string name() const override { return "CPU AVX2 4-way"; }
  AVX2 Scan scan(const Work &w, uint64_t start, uint32_t count) override {
    if (!count || start > UINT64_MAX - (count - 1))
      throw std::runtime_error("nonce range overflow");
    auto begin = std::chrono::steady_clock::now();
    Scan r;
    V m[16], out[4];
    prepare(w.header, m);
    uint64_t limit = __builtin_bswap64(load_le(w.target.data()));
    for (uint64_t i = 0; i < count; i += 4) {
      compress(m, start + i, out);
      alignas(32) uint64_t lanes[4];
      _mm256_store_si256(reinterpret_cast<V *>(lanes), out[0]);
      for (unsigned lane = 0; lane < 4 && i + lane < count; ++lane) {
        if (__builtin_bswap64(lanes[lane]) > limit)
          continue;
        Header h = w.header;
        store_le(h.data() + 32, start + i + lane);
        auto hash = blake2b256(h);
        if (load_le(hash.data()) != lanes[lane])
          throw std::runtime_error("AVX2 candidate failed scalar validation");
        if (meets(hash, w.target))
          r.nonces.push_back(start + i + lane);
      }
    }
    r.hashes = count;
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    return r;
  }
};
} // namespace
bool cpu_avx2_available() { return __builtin_cpu_supports("avx2"); }
std::unique_ptr<Backend> cpu_avx2_backend() {
  if (!cpu_avx2_available())
    throw std::runtime_error("AVX2 unavailable on this CPU");
  return std::make_unique<CpuAvx2>();
}
std::vector<Hash> cpu_avx2_hashes(const Header &h, uint64_t start, uint32_t count) {
  if (!cpu_avx2_available())
    throw std::runtime_error("AVX2 unavailable on this CPU");
  if (!count || start > UINT64_MAX - (count - 1))
    throw std::runtime_error("nonce range overflow");
  return hashes(h, start, count);
}
} // namespace yolo
#else
namespace yolo {
bool cpu_avx2_available() { return false; }
std::unique_ptr<Backend> cpu_avx2_backend() {
  throw std::runtime_error("AVX2 unavailable in this build");
}
std::vector<Hash> cpu_avx2_hashes(const Header &, uint64_t, uint32_t) {
  throw std::runtime_error("AVX2 unavailable in this build");
}
} // namespace yolo
#endif
