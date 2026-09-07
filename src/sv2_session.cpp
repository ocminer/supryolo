// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/sv2_session.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace yolo::sv2 {
using json = nlohmann::json;
namespace {
void require(bool b, const char *why) {
  if (!b)
    throw std::runtime_error(why);
}
} // namespace
Session::Session(const Hash &key, const std::string &h, uint16_t p, const std::string &u, float r,
                 std::function<void(const Bytes &)> sender)
    : noise(key, handshake), write(std::move(sender)), host(h), user(u), port(p), rate(r) {
  write(handshake);
  handshake.clear();
}
void Session::send(Frame f) { write(noise.encode(f)); }
void Session::activate(Stored &s, uint32_t time) {
  require(have_previous, "SV2 active job before previous hash");
  auto &j = s.job;
  j.id = std::to_string(s.wire.job);
  j.generation = ++generation;
  j.epoch = epoch;
  j.sv2 = true;
  j.channel = channel;
  j.wire_job = s.wire.job;
  j.version = s.wire.version;
  j.time32 = time;
  j.work.header = standard_work(previous, s.wire.root, extra, time);
  j.work.target = target;
  j.network_target = network_target;
  // BTCB2 minimum proof: top 32 digest bits must be zero even at an easy target.
  Hash maximum;
  maximum.fill(255);
  std::fill_n(maximum.begin(), 4, 0);
  j.work.target = std::min(j.work.target, maximum);
  current = j;
  s.future = false;
}
void Session::settle(std::vector<json> &out) {
  while (!batches.empty()) {
    auto b = batches.front();
    size_t n = 0;
    for (auto &[s, _] : pending)
      if (s <= b.last)
        ++n;
    require(n >= b.count, "SV2 inconsistent accepted count");
    if (n != b.count)
      break; // Delayed error replies may identify the remaining submits.
    for (auto i = pending.begin(); i != pending.end() && i->first <= b.last;) {
      out.push_back({{"id", i->first}, {"result", true}});
      i = pending.erase(i);
    }
    batches.pop_front();
  }
}
std::vector<json> Session::receive(std::span<const uint8_t> bytes) {
  std::vector<json> out;
  if (phase == 0) {
    handshake.insert(handshake.end(), bytes.begin(), bytes.end());
    require(handshake.size() <= 65536, "SV2 handshake buffer limit");
    if (handshake.size() < 234)
      return out;
    noise.finish(std::span(handshake).first(234));
    Bytes remainder(handshake.begin() + 234, handshake.end());
    handshake.clear();
    phase = 1;
    send(setup(host, port, "supryolo/0.2.1"));
    return receive(remainder);
  }
  for (auto &frame : noise.receive(bytes)) {
    auto message = decode(frame);
    if (auto m = std::get_if<SetupSuccess>(&message)) {
      require(phase == 1 && m->version == 2 && (m->flags & ~1u) == 0,
              "unsupported SV2 setup response");
      Hash maximum;
      maximum.fill(255);
      send(open_standard(1, user, rate, maximum));
      phase = 2;
    } else if (auto m = std::get_if<OpenSuccess>(&message)) {
      require(phase == 2 && m->request == 1, "unexpected SV2 channel response");
      require(m->prefix.size() == 4 || m->prefix.size() == 12,
              "unsupported BTCB2 standard extranonce");
      channel = m->channel;
      group = m->group;
      target = m->target;
      extra = m->prefix;
      extra.resize(12, 0);
      phase = 3;
    } else if (auto m = std::get_if<NewJob>(&message)) {
      require(phase == 3 && m->channel == channel, "SV2 job channel mismatch");
      require(jobs.size() < 1024 && !jobs.contains(m->job),
              "SV2 job limit or reused active job ID");
      auto [it, _] = jobs.emplace(m->job, Stored{*m, {}, !m->ntime});
      if (m->ntime)
        activate(it->second, *m->ntime);
    } else if (auto m = std::get_if<PrevHash>(&message)) {
      require(phase == 3 && (m->channel == channel || m->channel == group),
              "SV2 previous hash channel mismatch");
      auto i = jobs.find(m->job);
      require(i != jobs.end(), "SV2 activation of unknown job");
      require(std::all_of(m->hash.begin(), m->hash.begin() + 6, [](auto b) { return b == 0; }),
              "pool sent non-hidden BTCB2 prevhash");
      // nBits is the consensus target, independent of the channel share target.
      const std::array<uint8_t, 4> compact = {uint8_t(m->nbits >> 24), uint8_t(m->nbits >> 16),
                                             uint8_t(m->nbits >> 8), uint8_t(m->nbits)};
      const auto next_network_target = compact_target(hex(compact));
      auto selected = i->second;
      jobs.clear();
      ++epoch;
      previous = m->hash;
      network_target = next_network_target;
      have_previous = true;
      auto [at, _] = jobs.emplace(m->job, std::move(selected));
      activate(at->second, m->ntime);
    } else if (auto m = std::get_if<Target>(&message)) {
      require(phase == 3 && (m->channel == channel || m->channel == group),
              "SV2 target channel mismatch");
      target = m->target;
      long double numeric = 0;
      for (auto b : target)
        numeric = numeric * 256 + b;
      out.push_back({{"method", "mining.set_difficulty"},
                     {"params", json::array({double(std::ldexp(65535.L, 208) / numeric)})}});
    } else if (auto m = std::get_if<Accepted>(&message)) {
      require(phase == 3 && m->channel == channel && m->count > 0 && m->count <= 4096 &&
                  m->last_sequence > last_ack && m->last_sequence <= last_sent &&
                  pending.contains(m->last_sequence),
              "invalid SV2 batch acknowledgement");
      last_ack = m->last_sequence;
      batches.push_back({m->last_sequence, m->count});
      settle(out);
    } else if (auto m = std::get_if<Rejected>(&message)) {
      require(phase == 3 && m->channel == channel && pending.erase(m->sequence) == 1,
              "unknown SV2 rejected sequence");
      const bool stale =
          m->reason == "stale-share" || m->reason == "invalid-job-id" || m->reason == "stale-job";
      out.push_back({{"id", m->sequence},
                     {"result", false},
                     {"error", json::array({stale ? 21 : 20, m->reason})}});
      settle(out);
    } else if (auto m = std::get_if<Error>(&message))
      throw std::runtime_error("SV2 pool error: " + m->reason);
    out.push_back(json::object()); // Prompt the shared scheduler to inspect current.
  }
  return out;
}
bool Session::valid(const Job &j) const {
  auto i = jobs.find(j.wire_job);
  return phase == 3 && j.channel == channel && j.epoch == epoch && i != jobs.end() &&
         !i->second.future && i->second.job.generation == j.generation;
}
void Session::submit(const Job &j, uint64_t nonce, uint32_t sequence) {
  require(valid(j) && nonce <= UINT32_MAX && sequence > last_sent && pending.size() < 4096,
          "invalid SV2 submit or sequence exhausted");
  pending.emplace(sequence, true);
  last_sent = sequence;
  send(submit_standard(channel, sequence, j.wire_job, uint32_t(nonce), j.time32, j.version));
}
} // namespace yolo::sv2
