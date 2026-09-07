// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#pragma once
#include "yolo/core.hpp"
#include <optional>
#include <variant>
namespace yolo::sv2 {
// Wire integers are little endian. Opaque hashes retain their wire byte order;
// only numeric targets are converted to the miner's big-endian representation.
struct Frame {
  uint16_t extension{};
  uint8_t type{};
  Bytes payload;
};
struct SetupSuccess {
  uint16_t version;
  uint32_t flags;
};
struct OpenSuccess {
  uint32_t request, channel;
  Hash target;
  Bytes prefix;
  uint32_t group;
};
struct NewJob {
  uint32_t channel, job;
  std::optional<uint32_t> ntime;
  uint32_t version;
  Hash root;
};
struct PrevHash {
  uint32_t channel, job;
  Hash hash;
  uint32_t ntime, nbits;
};
struct Target {
  uint32_t channel;
  Hash target;
};
struct Accepted {
  uint32_t channel, last_sequence, count;
  uint64_t difficulty_sum;
};
struct Rejected {
  uint32_t channel, sequence;
  std::string reason;
};
struct Error {
  uint8_t type;
  uint32_t reference;
  std::string reason;
};
using Message =
    std::variant<SetupSuccess, OpenSuccess, NewJob, PrevHash, Target, Accepted, Rejected, Error>;
Message decode(const Frame &);
Frame setup(const std::string &host, uint16_t port, const std::string &agent);
Frame open_standard(uint32_t request, const std::string &user, float hashes_per_second,
                    const Hash &max_target);
Frame submit_standard(uint32_t channel, uint32_t sequence, uint32_t job, uint32_t nonce,
                      uint32_t ntime, uint32_t version);
// BTCB2 profile: upper nonce/time halves are zero. No Bitcoin time rolling rules.
Header work_header(const Hash &hidden_prevhash, const Hash &work_root, uint32_t ntime);
Header standard_work(const Hash &hidden_prevhash, const Hash &commitment,
                     std::span<const uint8_t> extranonce, uint32_t ntime);
struct Partition {
  uint32_t nonce, ntime, count;
};
Partition partition(uint32_t base_ntime, unsigned slot, uint64_t cursor, uint32_t batch);

// One connection owns both AEAD counters. Malformed or unauthenticated input
// permanently poisons it. No copy, plaintext fallback, or optional authority pin.
class Noise {
  void *state = nullptr;
  bool ready = false, failed = false;
  Bytes input;
  std::optional<Frame> incoming;
  size_t remaining = 0;
  Bytes transform(bool decrypt, std::span<const uint8_t>);

public:
  explicit Noise(const Hash &authority, Bytes &first_message);
  ~Noise();
  Noise(const Noise &) = delete;
  Noise &operator=(const Noise &) = delete;
  void finish(std::span<const uint8_t> response);
  Bytes encode(const Frame &);
  std::vector<Frame> receive(std::span<const uint8_t>);
};
} // namespace yolo::sv2
