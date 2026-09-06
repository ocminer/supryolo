// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace yolo {
using Bytes = std::vector<uint8_t>;
using Hash = std::array<uint8_t, 32>;
using Header = std::array<uint8_t, 80>;
Bytes unhex(const std::string &s);
std::string hex(std::span<const uint8_t> b);
uint64_t load_le(const uint8_t *p);
void store_le(uint8_t *p, uint64_t x);
Hash blake2b256(std::span<const uint8_t> b);
Hash tagged_sha256(const std::string &tag, std::span<const uint8_t> b);
Hash difficulty_target(const std::string &decimal);
Hash compact_target(const std::string &nbits);
bool meets(const Hash &hash, const Hash &target);
struct Work {
  Header header{};
  Hash target{};
};
struct Scan {
  uint64_t hashes{};
  std::vector<uint64_t> nonces;
  double seconds{};
};
class Backend {
public:
  virtual ~Backend() = default;
  virtual Scan scan(const Work &, uint64_t start, uint32_t count) = 0;
  virtual std::string name() const = 0;
};
std::unique_ptr<Backend> cuda_backend(int device, int block = 128, int variant = 0);
std::unique_ptr<Backend> cpu_backend(const std::string &variant = "auto");
bool cpu_avx2_available();
std::unique_ptr<Backend> cpu_avx2_backend();
std::vector<Hash> cpu_avx2_hashes(const Header &, uint64_t start, uint32_t count);
std::vector<Hash> cuda_hashes(const Header &, uint64_t start, uint32_t count, int device,
                              int variant = 0);
std::vector<Hash> opencl_hashes(const Header &, uint64_t start, uint32_t count, int device,
                                int variant = 0);
Header sia_work(const std::string &prev, const std::string &coinb1, const std::string &coinb2,
                const std::string &en1, const std::string &en2, const std::string &ntime);
} // namespace yolo
