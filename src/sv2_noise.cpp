// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/sv2.hpp"
#include <algorithm>
#include <stdexcept>
extern "C" {
void *yolo_noise_new(const uint8_t *, uint8_t *);
int yolo_noise_finish(void *, const uint8_t *, size_t);
int yolo_noise_transform(void *, bool, const uint8_t *, size_t, uint8_t *, size_t);
void yolo_noise_free(void *);
}
namespace yolo::sv2 {
namespace {
constexpr size_t max_payload = 1024 * 1024, chunk = 65519;
}
Noise::Noise(const Hash &key, Bytes &first) {
  first.resize(64);
  state = yolo_noise_new(key.data(), first.data());
  if (!state)
    throw std::runtime_error("invalid SV2 authority key or Noise initialization failed");
}
Noise::~Noise() { yolo_noise_free(state); }
void Noise::finish(std::span<const uint8_t> b) {
  if (ready || failed || yolo_noise_finish(state, b.data(), b.size()) != 0) {
    failed = true;
    throw std::runtime_error("SV2 Noise certificate authentication failed");
  }
  ready = true;
}
Bytes Noise::transform(bool decrypt, std::span<const uint8_t> b) {
  if (!ready || failed)
    throw std::runtime_error("SV2 Noise session unavailable");
  Bytes out(b.size() + 16);
  int n = yolo_noise_transform(state, decrypt, b.data(), b.size(), out.data(), out.size());
  if (n < 0) {
    failed = true;
    throw std::runtime_error("SV2 Noise authentication/encryption failed");
  }
  out.resize(n);
  return out;
}
Bytes Noise::encode(const Frame &f) {
  if (f.payload.size() > max_payload)
    throw std::runtime_error("SV2 frame exceeds local size limit");
  Bytes h{
      uint8_t(f.extension),      uint8_t(f.extension >> 8),      f.type,
      uint8_t(f.payload.size()), uint8_t(f.payload.size() >> 8), uint8_t(f.payload.size() >> 16)};
  auto out = transform(false, h);
  for (size_t i = 0; i < f.payload.size(); i += chunk) {
    auto b =
        transform(false, std::span(f.payload).subspan(i, std::min(chunk, f.payload.size() - i)));
    out.insert(out.end(), b.begin(), b.end());
  }
  return out;
}
std::vector<Frame> Noise::receive(std::span<const uint8_t> b) {
  try {
    if (!ready || failed)
      throw std::runtime_error("SV2 Noise session unavailable");
    if (input.size() + b.size() > max_payload + 4096)
      throw std::runtime_error("SV2 receive buffer limit");
    input.insert(input.end(), b.begin(), b.end());
    std::vector<Frame> out;
    size_t consumed = 0;
    for (;;) {
      if (!incoming) {
        if (input.size() - consumed < 22)
          break;
        auto h = transform(true, std::span(input).subspan(consumed, 22));
        consumed += 22;
        remaining = size_t(h[3]) + (size_t(h[4]) << 8) + (size_t(h[5]) << 16);
        if (remaining > max_payload)
          throw std::runtime_error("SV2 frame exceeds local size limit");
        incoming = Frame{uint16_t(h[0] | (uint16_t(h[1]) << 8)), h[2], {}};
      }
      if (remaining) {
        auto n = std::min(remaining, chunk);
        if (input.size() - consumed < n + 16)
          break;
        auto p = transform(true, std::span(input).subspan(consumed, n + 16));
        consumed += n + 16;
        incoming->payload.insert(incoming->payload.end(), p.begin(), p.end());
        remaining -= n;
        if (remaining)
          continue;
      }
      out.push_back(std::move(*incoming));
      incoming.reset();
      if (out.size() > 1024)
        throw std::runtime_error("SV2 message flood");
    }
    input.erase(input.begin(), input.begin() + consumed);
    return out;
  } catch (...) {
    failed = true;
    throw;
  }
}
} // namespace yolo::sv2
