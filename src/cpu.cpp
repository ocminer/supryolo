// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/core.hpp"
#include <chrono>
#include <stdexcept>
namespace yolo {
class Cpu final : public Backend {
public:
  std::string name() const override { return "CPU scalar reference"; }
  Scan scan(const Work &w, uint64_t start, uint32_t count) override {
    if (!count || start > UINT64_MAX - (count - 1))
      throw std::runtime_error("nonce range overflow");
    auto t = std::chrono::steady_clock::now();
    Scan r;
    Header h = w.header;
    for (uint64_t i = 0; i < count; ++i) {
      store_le(h.data() + 32, start + i);
      if (meets(blake2b256(h), w.target))
        r.nonces.push_back(start + i);
    }
    r.hashes = count;
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    return r;
  }
};
std::unique_ptr<Backend> cpu_backend(const std::string &variant) {
  if (variant == "avx2" || (variant == "auto" && cpu_avx2_available()))
    return cpu_avx2_backend();
  if (variant == "scalar" || variant == "auto")
    return std::make_unique<Cpu>();
  throw std::runtime_error("CPU variant must be auto, scalar or avx2");
}
} // namespace yolo
