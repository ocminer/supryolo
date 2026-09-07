// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/net.hpp"
#ifdef YOLO_SV2
#include "yolo/sv2_session.hpp"
#endif
#include "yolo/api.hpp"
#include "yolo/hardware.hpp"
#include "yolo/stratum.hpp"
#include "yolo/ui.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <deque>
#include <iostream>
#include <mutex>
#include <openssl/err.h>
#include <openssl/ssl.h>
#ifndef _WIN32
#include <sched.h>
#endif
#include <stdexcept>
#include <thread>
#include <unordered_map>
namespace yolo {
using json = nlohmann::json;
namespace {
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
class Socket {
  net::Socket fd = net::invalid;
  SSL_CTX *ctx = nullptr;
  SSL *ssl = nullptr;
  std::string buffer;
  void close() {
    if (ssl)
      SSL_free(ssl);
    if (ctx)
      SSL_CTX_free(ctx);
    if (fd != net::invalid)
      net::close(fd);
    ssl = nullptr;
    ctx = nullptr;
    fd = net::invalid;
  }
  void wait(short events) {
    net::Poll p{fd, events, 0};
    int r = net::poll(&p, 1, 10000);
    if (r <= 0 || p.revents & (POLLERR | POLLHUP | POLLNVAL))
      throw std::runtime_error("pool I/O timeout or disconnect");
  }

public:
  explicit Socket(const std::string &url) {
    try {
      net::init();
      bool tls = url.starts_with("stratum+tls://") || url.starts_with("stratum+ssl://");
      if (!tls && !url.starts_with("stratum+tcp://"))
        throw std::runtime_error("use stratum+tcp:// or stratum+tls://");
      auto hp = url.substr(url.find("://") + 3);
      auto colon = hp.rfind(':');
      if (colon == std::string::npos)
        throw std::runtime_error("pool port required");
      auto host = hp.substr(0, colon), port = hp.substr(colon + 1);
      if (host.starts_with('[') && host.ends_with(']'))
        host = host.substr(1, host.size() - 2);
      if (host.empty() || port.empty() || hp.find_first_of("/@?#") != std::string::npos)
        throw std::runtime_error("invalid pool URL");
      addrinfo hint{}, *raw = nullptr;
      hint.ai_socktype = SOCK_STREAM;
      hint.ai_family = AF_UNSPEC;
      if (getaddrinfo(host.c_str(), port.c_str(), &hint, &raw))
        throw std::runtime_error("pool DNS failed");
      std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> list(raw, freeaddrinfo);
      for (auto a = raw; a; a = a->ai_next) {
        fd = net::socket(a->ai_family);
        if (fd == net::invalid)
          continue;
        int c = ::connect(fd, a->ai_addr, a->ai_addrlen);
        if (c == 0)
          break;
        if (net::again()) {
          net::Poll p{fd, POLLOUT, 0};
          if (net::poll(&p, 1, 5000) > 0) {
            int error = 0;
#ifdef _WIN32
            int n = sizeof(error);
#else
            socklen_t n = sizeof(error);
#endif
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &n) == 0 &&
                error == 0)
              break;
          }
        }
        net::close(fd);
        fd = net::invalid;
      }
      if (fd == net::invalid)
        throw std::runtime_error("pool connection failed");
      if (tls) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx || SSL_CTX_set_default_verify_paths(ctx) != 1)
          throw std::runtime_error("TLS trust initialization failed");
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        ssl = SSL_new(ctx);
        if (!ssl || SSL_set_tlsext_host_name(ssl, host.c_str()) != 1 ||
            SSL_set1_host(ssl, host.c_str()) != 1 || SSL_set_fd(ssl, static_cast<int>(fd)) != 1)
          throw std::runtime_error("TLS initialization failed");
        for (;;) {
          int n = SSL_connect(ssl);
          if (n == 1)
            break;
          int e = SSL_get_error(ssl, n);
          if (e == SSL_ERROR_WANT_READ)
            wait(POLLIN);
          else if (e == SSL_ERROR_WANT_WRITE)
            wait(POLLOUT);
          else
            throw std::runtime_error("TLS handshake failed");
        }
      }
    } catch (...) {
      close();
      throw;
    }
  }
  ~Socket() { close(); }
  void send(const json &j) {
    auto s = j.dump() + "\n";
    send_bytes(std::span(reinterpret_cast<const uint8_t *>(s.data()), s.size()));
  }
  void send_bytes(std::span<const uint8_t> s) {
    size_t off = 0;
    while (off < s.size()) {
      int n = ssl ? SSL_write(ssl, s.data() + off, int(s.size() - off))
                  : ::send(fd, reinterpret_cast<const char *>(s.data()) + off,
                           static_cast<int>(s.size() - off), net::send_flags);
      if (n > 0) {
        off += n;
        continue;
      }
      if (ssl) {
        int e = SSL_get_error(ssl, n);
        if (e == SSL_ERROR_WANT_READ) {
          wait(POLLIN);
          continue;
        }
        if (e == SSL_ERROR_WANT_WRITE) {
          wait(POLLOUT);
          continue;
        }
      } else if (net::again()) {
        wait(POLLOUT);
        continue;
      } else if (net::interrupted())
        continue;
      throw std::runtime_error("pool send failed");
    }
  }
  Bytes read_bytes() {
    Bytes b(8192);
    int n = ::recv(fd, reinterpret_cast<char *>(b.data()), int(b.size()), 0);
    if (n > 0) {
      b.resize(n);
      return b;
    }
    if (n < 0 && (net::again() || net::interrupted()))
      return {};
    throw std::runtime_error("SV2 pool disconnected");
  }
  std::vector<json> read() {
    std::vector<json> out;
    char buf[8192];
    for (;;) {
      int n = ssl ? SSL_read(ssl, buf, sizeof(buf)) : ::recv(fd, buf, sizeof(buf), 0);
      if (n > 0) {
        buffer.append(buf, n);
        if (buffer.size() > 65536)
          throw std::runtime_error("Stratum line too large");
        size_t e;
        while ((e = buffer.find('\n')) != std::string::npos) {
          auto line = buffer.substr(0, e);
          buffer.erase(0, e + 1);
          if (!line.empty())
            out.push_back(json::parse(line, [](int depth, json::parse_event_t, json &) {
              if (depth > 32)
                throw std::runtime_error("Stratum JSON nesting limit");
              return true;
            }));
        }
        if (out.size() > 1024)
          throw std::runtime_error("Stratum message flood");
        continue;
      }
      if (n == 0)
        throw std::runtime_error("pool closed connection");
      if (ssl) {
        auto e = SSL_get_error(ssl, n);
        if (e == SSL_ERROR_WANT_READ)
          break;
        if (e == SSL_ERROR_WANT_WRITE) {
          wait(POLLOUT);
          continue;
        }
      } else if (net::again())
        break;
      else if (net::interrupted())
        continue;
      throw std::runtime_error("pool receive failed");
    }
    return out;
  }
};
struct Found {
  Job job;
  uint64_t nonce;
  size_t row;
};
struct Pending {
  Clock::time_point sent;
  size_t row;
};
struct Slot {
  size_t row;
  int gpu;
  std::atomic<uint64_t> hashes{0};
};
struct SignalRestore {
  void (*oldint)(int) = std::signal(SIGINT, interrupt);
  void (*oldterm)(int) = std::signal(SIGTERM, interrupt);
#ifndef _WIN32
  void (*oldpipe)(int) = std::signal(SIGPIPE, SIG_IGN);
#endif
  ~SignalRestore() {
    std::signal(SIGINT, oldint);
    std::signal(SIGTERM, oldterm);
#ifndef _WIN32
    std::signal(SIGPIPE, oldpipe);
#endif
  }
};
std::string reject_reason(const json &message) {
  if (message.contains("error") && message["error"].is_array() && message["error"].size() > 1 &&
      message["error"][1].is_string())
    return message["error"][1].get<std::string>();
  return "unspecified pool rejection";
}
} // namespace
int mine(const MineOptions &o) {
  const bool use_sv2 = o.url.starts_with("stratum2+tcp://");
  std::string transport_url = o.url;
#ifdef YOLO_SV2
  Hash authority{};
  if (use_sv2) {
    auto key = unhex(o.sv2_authority);
    if (key.size() != 32)
      throw std::runtime_error("SV2 requires --sv2-authority with 32-byte hex key");
    std::copy(key.begin(), key.end(), authority.begin());
    transport_url.replace(0, transport_url.find("://") + 3, "stratum+tcp://");
  }
#else
  if (use_sv2)
    throw std::runtime_error("SV2 was disabled at build time");
#endif
  if (o.user.empty())
    throw std::runtime_error("payout worker required with --user");
  if (o.no_cpu && o.no_gpu)
    throw std::runtime_error("No mining devices enabled (--no-cpu and --no-gpu)");
  auto available = o.no_gpu ? std::vector<GpuInfo>{} : gpu_devices(o.gpu_mode);
  std::vector<GpuInfo> selected;
  if (!o.no_gpu) {
    if (!o.devices_explicit)
      selected = available;
    else
      for (int id : o.devices) {
        auto it = std::find_if(available.begin(), available.end(),
                               [&](const GpuInfo &d) { return d.index == id; });
        if (it == available.end())
          throw std::runtime_error("GPU device " + std::to_string(id) + " unavailable");
        selected.push_back(*it);
      }
  }
  if (selected.size() > 64)
    throw std::runtime_error("At most 64 GPUs may be selected");
  if (selected.empty() && o.no_cpu)
    throw std::runtime_error("No enabled GPU devices available; CPU disabled");
  unsigned cpu_threads = o.no_cpu ? 0 : o.cpu_threads;
  if (!o.no_cpu && !cpu_threads) {
#ifndef _WIN32
    cpu_set_t mask;
    CPU_ZERO(&mask);
    unsigned logical = sched_getaffinity(0, sizeof(mask), &mask) == 0
                           ? CPU_COUNT(&mask)
                           : std::max(1u, std::thread::hardware_concurrency());
#else
    unsigned logical = std::max(1u, std::thread::hardware_concurrency());
#endif
    cpu_threads = std::max(1, int(logical / 2) - int(selected.size()) - 1);
  }
  if (cpu_threads > 128)
    throw std::runtime_error("At most 128 CPU threads supported");
  GpuManagement hardware(selected);
  auto control_events = hardware.apply(o.controls);
  interrupted = 0;
  SignalRestore signals;
  ApiServer api(o.api_port);
  Dashboard ui(o.tui, o.log_file);
  DashboardView view;
  view.pool = o.url;
  view.worker = o.user;
  auto dot = view.worker.rfind('.');
  if (dot != std::string::npos)
    view.worker = view.worker.substr(dot + 1);
  view.warn_temperature = o.warn_temperature;
  view.alarm_temperature = o.alarm_temperature;
  std::vector<std::unique_ptr<Slot>> slots;
  for (size_t i = 0; i < selected.size(); ++i) {
    DeviceView d;
    d.label = "GPU" + std::to_string(selected[i].index);
    d.name = selected[i].name;
    d.pci_bus = selected[i].pci_bus;
    view.devices.push_back(d);
    auto slot = std::make_unique<Slot>();
    slot->row = i;
    slot->gpu = int(i);
    slots.push_back(std::move(slot));
  }
  size_t cpu_row = view.devices.size();
  if (cpu_threads) {
    DeviceView d;
    d.label = "CPU";
    d.name = cpu_backend(o.cpu_variant)->name() + " x" + std::to_string(cpu_threads);
    view.devices.push_back(d);
    for (unsigned i = 0; i < cpu_threads; ++i) {
      auto s = std::make_unique<Slot>();
      s->row = cpu_row;
      s->gpu = -1;
      slots.push_back(std::move(s));
    }
  }
  ui.update(view);
  for (auto &e : control_events)
    ui.event(e);
  ui.event("Selected " + std::to_string(selected.size()) + " GPU(s), " +
           std::to_string(cpu_threads) + " CPU thread(s)");
  std::mutex sensor_mutex;
  std::vector<GpuReadings> readings(selected.size());
  std::jthread sensor_thread([&](std::stop_token stop) {
    while (!stop.stop_requested()) {
      std::vector<GpuReadings> next;
      for (size_t i = 0; i < selected.size(); ++i)
        next.push_back(hardware.sample(i));
      {
        std::lock_guard lock(sensor_mutex);
        readings = std::move(next);
      }
      for (int i = 0; i < 10 && !stop.stop_requested(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
  const auto begin = Clock::now();
  auto last_update = begin;
  std::vector<uint64_t> last_hashes(view.devices.size());
  std::vector<int> thermal(view.devices.size(), -1);
  bool had_work = false, fatal = false;
  int backoff = 1;
  auto expired = [&]() {
    return interrupted || ui.quit_requested() ||
           (o.seconds > 0 &&
            std::chrono::duration<double>(Clock::now() - begin).count() >= o.seconds);
  };
  auto update = [&](bool force = false) {
    auto now = Clock::now();
    double delta = std::chrono::duration<double>(now - last_update).count();
    if (!force && delta < .25)
      return;
    view.elapsed = std::chrono::duration<double>(now - begin).count();
    for (auto &d : view.devices)
      d.hashes = 0;
    for (auto &s : slots)
      view.devices[s->row].hashes += s->hashes.load();
    view.hashes = 0;
    {
      std::lock_guard lock(sensor_mutex);
      for (size_t i = 0; i < readings.size(); ++i)
        view.devices[i].sensors = readings[i];
    }
    for (size_t i = 0; i < view.devices.size(); ++i) {
      auto &d = view.devices[i];
      view.hashes += d.hashes;
      if (delta > 0) {
        double instant = (d.hashes - last_hashes[i]) / delta;
        d.hashes_per_second =
            d.hashes_per_second == 0 ? instant : (.25 * instant + .75 * d.hashes_per_second);
      }
      last_hashes[i] = d.hashes;
      if (d.sensors.temperature) {
        int level = *d.sensors.temperature >= o.alarm_temperature  ? 2
                    : *d.sensors.temperature >= o.warn_temperature ? 1
                                                                   : 0;
        if (level != thermal[i]) {
          if (level == 2)
            ui.event(d.label + " temperature " + std::to_string(*d.sensors.temperature) + "C ALARM",
                     Severity::error);
          else if (level == 1)
            ui.event(d.label + " temperature warning " + std::to_string(*d.sensors.temperature) +
                         "C",
                     Severity::warning);
          else if (thermal[i] > 0)
            ui.event(d.label + " temperature recovered", Severity::success);
          thermal[i] = level;
        }
      }
    }
    last_update = now;
    ui.update(view);
    api.update(view);
  };
  while (!expired() && !fatal) {
    try {
      view.state = "CONNECTING";
      ui.update(view);
      Socket socket(transport_url);
      StratumState state;
#ifdef YOLO_SV2
      std::unique_ptr<sv2::Session> v2;
      if (use_sv2) {
        auto address = transport_url.substr(transport_url.find("://") + 3);
        auto colon = address.rfind(':');
        auto host = address.substr(0, colon);
        auto port = std::stoul(address.substr(colon + 1));
        if (port == 0 || port > 65535)
          throw std::runtime_error("invalid SV2 port");
        v2 = std::make_unique<sv2::Session>(authority, host, uint16_t(port), o.user,
                                            float(selected.size() * 17e9 + cpu_threads * 25e6),
                                            [&](const Bytes &b) { socket.send_bytes(b); });
      } else
#endif
      {
        socket.send({{"id", 1},
                     {"method", "mining.subscribe"},
                     {"params", json::array({"supryolo/0.2.1"})}});
        socket.send(
            {{"id", 2}, {"method", "mining.extranonce.subscribe"}, {"params", json::array()}});
        socket.send({{"id", 3},
                     {"method", "mining.authorize"},
                     {"params", json::array({o.user, o.password})}});
      }
      std::mutex mutex;
      std::optional<Job> work;
      std::deque<Found> found;
      std::string failure;
      uint64_t shared_cursor = 0, shared_generation = 0;
      std::vector<std::jthread> workers;
      struct Stop {
        std::vector<std::jthread> &workers;
        ~Stop() {
          for (auto &w : workers)
            w.request_stop();
          for (auto &w : workers)
            if (w.joinable())
              w.join();
        }
      } stop{workers};
      for (size_t index = 0; index < slots.size(); ++index)
        workers.emplace_back([&, index](std::stop_token token) {
          try {
            auto &slot = *slots[index];
            auto backend = slot.gpu < 0 ? cpu_backend(o.cpu_variant)
                                        : gpu_backend(selected[slot.gpu], o.block, o.variant,
                                                      o.opencl_variant);
            uint32_t batch = slot.gpu < 0 ? o.cpu_batch : o.batch;
            uint64_t generation = 0, cursor = 0;
            while (!token.stop_requested()) {
              std::optional<Job> j;
              uint64_t reserved_cursor = 0;
              {
                std::lock_guard lock(mutex);
                j = work;
#ifdef YOLO_SV2
                if (j && j->sv2) {
                  if (shared_generation != j->generation) {
                    shared_generation = j->generation;
                    shared_cursor = 0;
                  }
                  reserved_cursor = shared_cursor;
                  auto reserved = sv2::partition(j->time32, 0, shared_cursor, batch);
                  shared_cursor += reserved.count;
                }
#endif
              }
              if (!j) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
              }
              if (j->generation != generation) {
                generation = j->generation;
                cursor = 0;
              }
#ifdef YOLO_SV2
              uint64_t start;
              uint32_t count = batch;
              if (j->sv2) {
                auto p = sv2::partition(j->time32, 0, reserved_cursor, batch);
                start = p.nonce;
                count = p.count;
                j->time32 = p.ntime;
                store_le(j->work.header.data() + 40, p.ntime);
              } else {
#endif
                constexpr uint64_t space = uint64_t{1} << 56;
                if (cursor > space - batch)
                  throw std::runtime_error("nonce partition exhausted");
#ifdef YOLO_SV2
                start = (uint64_t(index) << 56) | cursor;
              }
              auto r = backend->scan(j->work, start, count);
#else
              uint64_t start = (uint64_t(index) << 56) | cursor;
              auto r = backend->scan(j->work, start, batch);
#endif
              cursor += r.hashes;
              slot.hashes += r.hashes;
              {
                std::lock_guard lock(mutex);
                if (found.size() + r.nonces.size() > 8192)
                  throw std::runtime_error("share queue overflow");
                for (auto n : r.nonces)
                  found.push_back({*j, n, slot.row});
              }
            }
          } catch (const std::exception &e) {
            std::lock_guard lock(mutex);
            failure = e.what();
          }
        });
      std::unordered_map<int64_t, Pending> pending;
      int64_t nextid = 10;
      auto received = Clock::now();
      bool activated = false;
      ui.event("Connected to " + o.url);
      bool draining = false;
      auto drain_until = Clock::now();
      while (!expired() || (!draining && !pending.empty()) ||
             (draining && !pending.empty() && Clock::now() < drain_until)) {
        if (expired() && !draining) {
          draining = true;
          drain_until = Clock::now() + std::chrono::seconds(5);
          for (auto &w : workers)
            w.request_stop();
        }

#ifdef YOLO_SV2
        auto messages = v2 ? v2->receive(socket.read_bytes()) : socket.read();
#else
        auto messages = socket.read();
#endif
        for (auto &message : messages) {
          received = Clock::now();
#ifdef YOLO_SV2
          auto job = v2 ? v2->current : state.receive(message);
          bool authorized = v2 ? bool(v2->current) : state.authorized;
#else
          auto job = state.receive(message);
          bool authorized = state.authorized;
#endif
          if (message.contains("method")) {
            auto method = message["method"].get<std::string>();
            if (method == "mining.set_difficulty") {
              view.difficulty = message["params"][0].dump();
              ui.event("Vardiff retarget -> " + view.difficulty + " (next job)", Severity::info);
            } else if (method == "mining.set_extranonce")
              ui.event("Extranonce updated (next job)");
            else if (method == "client.show_message" && message.contains("params") &&
                     message["params"].is_array() && !message["params"].empty() &&
                     message["params"][0].is_string())
              ui.event("Pool: " + message["params"][0].get<std::string>());
          }
          if (message.contains("id") && message["id"].is_number_integer()) {
            auto id = message["id"].get<int64_t>();
            if (id == 2)
              ui.event(message.value("result", json()) == true
                           ? "Extranonce subscription supported"
                           : "Extranonce subscription unavailable on this endpoint");
            auto found_pending = pending.find(id);
            if (found_pending != pending.end()) {
              auto &device = view.devices[found_pending->second.row];
              if (message.value("result", json()) == true) {
                ++view.accepted;
                ++device.accepted;
                ui.event("Share accepted (" + device.label + ") #" + std::to_string(view.accepted),
                         Severity::success);
              } else {
                int code = 0;
                if (message.contains("error") && message["error"].is_array() &&
                    !message["error"].empty() && message["error"][0].is_number_integer())
                  code = message["error"][0];
                if (code == 21) {
                  ++view.stale;
                  ++device.stale;
                } else {
                  ++view.rejected;
                  ++device.rejected;
                }
                ui.event("Share rejected (" + device.label + ", " + reject_reason(message) +
                             ", code " + std::to_string(code) + ")",
                         code == 21 ? Severity::warning : Severity::error);
              }
              pending.erase(found_pending);
            }
          }
          if (!draining && authorized && (job || !activated)) {
            std::lock_guard lock(mutex);
#ifdef YOLO_SV2
            work = v2 ? v2->current : state.current;
#else
            work = state.current;
#endif
            activated = bool(work);
            if (activated) {
              backoff = 1;
              had_work = true;
              view.state = "MINING";
            }
          }
        }
        std::deque<Found> queue;
        {
          std::lock_guard lock(mutex);
          if (!failure.empty()) {
            fatal = true;
            throw std::runtime_error(failure);
          }
          queue.swap(found);
        }
        for (const auto &f : queue) {
          if (draining)
            continue;
#ifdef YOLO_SV2
          if (!(v2 ? v2->valid(f.job) : state.valid(f.job)))
#else
          if (!state.valid(f.job))
#endif
            continue;
          if (pending.size() >= 4096)
            throw std::runtime_error("too many outstanding shares");
          std::array<uint8_t, 8> nonce;
          store_le(nonce.data(), f.nonce);
          Header header = f.job.work.header;
          store_le(header.data() + 32, f.nonce);
          auto hash = blake2b256(header);
          if (!meets(hash, f.job.work.target)) {
            fatal = true;
            throw std::runtime_error("independent submit-time hash check failed");
          }
          if (meets(hash, f.job.network_target)) {
            ++view.block_candidates;
            ui.event("Block candidate (" + view.devices[f.row].label +
                         ") - awaiting pool confirmation",
                     Severity::success);
          }
#ifdef YOLO_SV2
          if (v2) {
            if (nextid > UINT32_MAX)
              throw std::runtime_error("SV2 sequence exhausted; reconnecting");
            v2->submit(f.job, f.nonce, uint32_t(nextid));
          } else
#endif
            socket.send(
                {{"id", nextid},
                 {"method", "mining.submit"},
                 {"params", json::array({o.user, f.job.id, f.job.en2, f.job.ntime, hex(nonce)})}});
          pending.emplace(nextid++, Pending{Clock::now(), f.row});
        }
        auto now = Clock::now();
        if (now - received > std::chrono::seconds(120))
          throw std::runtime_error("pool receive timeout");
        for (auto &[id, p] : pending)
          if (now - p.sent > std::chrono::seconds(60))
            throw std::runtime_error("share response timeout");
        view.pending = pending.size();
        update();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      for (auto &w : workers)
        w.request_stop();
      for (auto &w : workers)
        w.join();
    } catch (const std::exception &e) {
      view.state = fatal ? "DEVICE ERROR" : "RECONNECTING";
      ui.event(std::string("Session stopped: ") + e.what() +
                   (fatal ? "" : "; reconnect in " + std::to_string(backoff) + "s"),
               Severity::error);
      if (!fatal) {
        for (int i = 0; i < backoff * 10 && !expired(); ++i) {
          update();
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        backoff = std::min(backoff * 2, 30);
      }
    }
  }
  sensor_thread.request_stop();
  if (sensor_thread.joinable())
    sensor_thread.join();
  view.state = "STOPPED";
  update(true);
  ui.event(
      "Final hashes=" + std::to_string(view.hashes) + " seconds=" + std::to_string(view.elapsed) +
      " MH/s=" + std::to_string(view.elapsed > 0 ? view.hashes / view.elapsed / 1e6 : 0) +
      " accepted=" + std::to_string(view.accepted) + " rejected=" + std::to_string(view.rejected) +
      " stale=" + std::to_string(view.stale) + " pending=" + std::to_string(view.pending));
  return fatal ? 1 : view.rejected ? 2 : (had_work ? 0 : 1);
}
} // namespace yolo
