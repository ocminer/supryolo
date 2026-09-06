// SPDX-License-Identifier: MIT
#include "yolo/stratum.hpp"
#include <stdexcept>
namespace yolo {
using json = nlohmann::json;
std::optional<Job> StratumState::receive(const json &msg) {
  if (!msg.is_object())
    throw std::runtime_error("invalid Stratum object");
  if (msg.contains("method")) {
    auto method = msg.at("method").get<std::string>();
    const auto &p = msg.at("params");
    if (!p.is_array())
      throw std::runtime_error("invalid Stratum parameters");
    if (method == "mining.set_difficulty") {
      if (p.size() != 1 || !p[0].is_number())
        throw std::runtime_error("invalid difficulty message");
      next_target = difficulty_target(p[0].dump());
      have_target = true;
    } else if (method == "mining.set_extranonce") {
      if (p.size() != 2)
        throw std::runtime_error("invalid extranonce message");
      auto e = p[0].get<std::string>();
      auto n = p[1].get<int>();
      if (unhex(e).size() != 4 || n != 8)
        throw std::runtime_error("unsupported extranonce sizes");
      en1 = e;
      en2_size = n;
    } else if (method == "mining.notify") {
      if (p.size() != 9 || !have_target || en2_size != 8 || !p[4].is_array() || !p[4].empty())
        throw std::runtime_error("unsupported or premature job");
      Job j;
      j.id = p[0].get<std::string>();
      if (j.id.empty() || j.id.size() > 256)
        throw std::runtime_error("invalid job ID");
      if (unhex(p[5].get<std::string>()).size() != 4 || unhex(p[6].get<std::string>()).size() != 4)
        throw std::runtime_error("invalid version or nbits length");
      bool clean = p[8].get<bool>();
      if (clean) {
        ++epoch;
        valid_ids.clear();
      }
      if (valid_ids.size() >= 1024)
        throw std::runtime_error("too many active jobs");
      j.generation = ++generation;
      valid_ids[j.id] = j.generation;
      j.epoch = epoch;
      std::array<uint8_t, 8> extra{};
      store_le(extra.data(), generation);
      j.en2 = hex(extra);
      j.ntime = p[7].get<std::string>();
      j.work.header = sia_work(p[1], p[2], p[3], en1, j.en2, j.ntime);
      j.work.target = next_target;
      current = j;
      return j;
    }
  } else if (msg.contains("id") && msg["id"].is_number_integer()) {
    auto id = msg["id"].get<int64_t>();
    if (id == 1) {
      const auto &r = msg.at("result");
      if (!r.is_array() || r.size() != 3)
        throw std::runtime_error("subscription refused");
      auto e = r[1].get<std::string>();
      auto n = r[2].get<int>();
      if (unhex(e).size() != 4 || n != 8)
        throw std::runtime_error("unsupported subscription extranonce");
      en1 = e;
      en2_size = n;
    }
    if (id == 3) {
      if (!msg.contains("result") || msg["result"] != true)
        throw std::runtime_error("authorization refused");
      authorized = true;
    }
  }
  return {};
}
bool StratumState::valid(const Job &j) const {
  return authorized && j.epoch == epoch && valid_ids.contains(j.id) &&
         valid_ids.at(j.id) == j.generation;
}
} // namespace yolo
