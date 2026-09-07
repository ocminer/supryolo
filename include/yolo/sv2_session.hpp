// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#pragma once
#include "yolo/stratum.hpp"
#include "yolo/sv2.hpp"
#include <deque>
#include <functional>
#include <map>
namespace yolo::sv2 {
// Internal UI/result events use the same JSON shape as SV1 replies. This is not
// an SV1 wire bridge: channel jobs and validity remain typed SV2 session state.
class Session {
  Bytes handshake;
  Noise noise;
  std::function<void(const Bytes &)> write;
  Bytes extra;
  std::string host, user;
  uint16_t port;
  float rate;
  unsigned phase = 0;
  uint32_t channel = 0, group = 0;
  uint64_t generation = 0, epoch = 0;
  uint32_t last_ack = 0, last_sent = 0;
  Hash target{}, previous{};
  bool have_previous = false;
  struct Stored {
    NewJob wire;
    Job job;
    bool future;
  };
  std::map<uint32_t, Stored> jobs;
  std::map<uint32_t, bool> pending;
  struct Batch {
    uint32_t last, count;
  };
  std::deque<Batch> batches;
  void send(Frame f);
  void activate(Stored &, uint32_t);
  void settle(std::vector<nlohmann::json> &);

public:
  std::optional<Job> current;
  Session(const Hash &, const std::string &, uint16_t, const std::string &, float,
          std::function<void(const Bytes &)>);
  std::vector<nlohmann::json> receive(std::span<const uint8_t>);
  bool valid(const Job &) const;
  void submit(const Job &, uint64_t, uint32_t);
};
} // namespace yolo::sv2
