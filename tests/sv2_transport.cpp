// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/sv2.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace yolo;
void check(bool b) {
  if (!b)
    throw std::runtime_error("SV2 transport test failed");
}
int main() {
  auto key = unhex("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
  Hash authority;
  std::copy(key.begin(), key.end(), authority.begin());
  Bytes first;
  sv2::Noise noise(authority, first);
  std::cout.write(reinterpret_cast<const char *>(first.data()), first.size());
  std::cout.flush();
  Bytes response(234);
  std::cin.read(reinterpret_cast<char *>(response.data()), response.size());
  check(bool(std::cin));
  noise.finish(response);
  for (size_t n : {0, 1, 65519, 65520, 131039}) {
    sv2::Frame original{0x8000, 0x15, Bytes(n)};
    for (size_t i = 0; i < n; ++i)
      original.payload[i] = uint8_t(i);
    auto wire = noise.encode(original);
    std::cout.write(reinterpret_cast<const char *>(wire.data()), wire.size());
    std::cout.flush();
    std::vector<sv2::Frame> frames;
    for (size_t i = 0; i < wire.size();) {
      size_t count = std::min<size_t>((i % 31) + 1, wire.size() - i);
      Bytes b(count);
      std::cin.read(reinterpret_cast<char *>(b.data()), count);
      check(bool(std::cin));
      auto received = noise.receive(b);
      frames.insert(frames.end(), received.begin(), received.end());
      i += count;
    }
    check(frames.size() == 1 && frames[0].payload == original.payload &&
          frames[0].extension == original.extension && frames[0].type == original.type);
  }
  Bytes tampered(22);
  std::cin.read(reinterpret_cast<char *>(tampered.data()), tampered.size());
  check(bool(std::cin));
  bool rejected = false;
  try {
    noise.receive(tampered);
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected);
  rejected = false;
  try {
    noise.encode({0, 1, {}});
  } catch (const std::exception &) {
    rejected = true;
  }
  check(rejected);
  std::cerr << "SV2 SRI interop, fragmentation, chunk boundaries and tamper tests passed\n";
}
