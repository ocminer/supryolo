// SPDX-License-Identifier: MIT
#include "yolo/stratum.hpp"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
namespace yolo {
using json = nlohmann::json;
namespace {
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
class Socket {
  int fd = -1;
  SSL_CTX *ctx = nullptr;
  SSL *ssl = nullptr;
  std::string buffer;
  void close() {
    if (ssl)
      SSL_free(ssl);
    if (ctx)
      SSL_CTX_free(ctx);
    if (fd >= 0)
      ::close(fd);
    ssl = nullptr;
    ctx = nullptr;
    fd = -1;
  }
  void wait(short events) {
    pollfd p{fd, events, 0};
    int r = ::poll(&p, 1, 10000);
    if (r <= 0 || p.revents & (POLLERR | POLLHUP | POLLNVAL))
      throw std::runtime_error("pool I/O timeout or disconnect");
  }

public:
  explicit Socket(const std::string &url) {
    try {
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
        fd = ::socket(a->ai_family, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, a->ai_protocol);
        if (fd < 0)
          continue;
        int c = ::connect(fd, a->ai_addr, a->ai_addrlen);
        if (c == 0)
          break;
        if (errno == EINPROGRESS) {
          pollfd p{fd, POLLOUT, 0};
          if (::poll(&p, 1, 5000) > 0) {
            int error = 0;
            socklen_t n = sizeof(error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &n) == 0 && error == 0)
              break;
          }
        }
        ::close(fd);
        fd = -1;
      }
      if (fd < 0)
        throw std::runtime_error("pool connection failed");
      if (tls) {
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx || SSL_CTX_set_default_verify_paths(ctx) != 1)
          throw std::runtime_error("TLS trust initialization failed");
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        ssl = SSL_new(ctx);
        if (!ssl || SSL_set_tlsext_host_name(ssl, host.c_str()) != 1 ||
            SSL_set1_host(ssl, host.c_str()) != 1 || SSL_set_fd(ssl, fd) != 1)
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
    size_t off = 0;
    while (off < s.size()) {
      int n = ssl ? SSL_write(ssl, s.data() + off, int(s.size() - off))
                  : ::send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
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
      } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
        wait(POLLOUT);
        continue;
      } else if (errno == EINTR)
        continue;
      throw std::runtime_error("pool send failed");
    }
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
      } else if (errno == EAGAIN || errno == EWOULDBLOCK)
        break;
      else if (errno == EINTR)
        continue;
      throw std::runtime_error("pool receive failed");
    }
    return out;
  }
};
struct Found {
  Job job;
  uint64_t nonce;
};
} // namespace
int mine(const MineOptions &o) {
  if (o.user.empty())
    throw std::runtime_error("payout worker required with --user");
  if (o.devices.empty() || o.devices.size() > 64)
    throw std::runtime_error("invalid device list");
  interrupted = 0;
  auto oldint = std::signal(SIGINT, interrupt), oldterm = std::signal(SIGTERM, interrupt);
  std::signal(SIGPIPE, SIG_IGN);
  const auto begin = Clock::now();
  uint64_t total = 0, accepted = 0, rejected = 0, stale = 0;
  int backoff = 1;
  bool had_work = false;
  auto expired = [&]() {
    return interrupted ||
           (o.seconds > 0 &&
            std::chrono::duration<double>(Clock::now() - begin).count() >= o.seconds);
  };
  while (!expired()) {
    try {
      Socket socket(o.url);
      StratumState state;
      socket.send({{"id", 1},
                   {"method", "mining.subscribe"},
                   {"params", json::array({"supryolo/0.1.0-dev"})}});
      socket.send(
          {{"id", 2}, {"method", "mining.extranonce.subscribe"}, {"params", json::array()}});
      socket.send({{"id", 3},
                   {"method", "mining.authorize"},
                   {"params", json::array({o.user, o.password})}});
      std::mutex mutex;
      std::optional<Job> work;
      std::deque<Found> found;
      std::string failure;
      std::atomic<uint64_t> done{0};
      std::vector<std::jthread> workers;
      for (size_t index = 0; index < o.devices.size(); ++index)
        workers.emplace_back([&, index](std::stop_token stop) {
          try {
            auto backend =
                o.cpu ? cpu_backend() : cuda_backend(o.devices[index], o.block, o.variant);
            uint64_t generation = 0, cursor = 0;
            while (!stop.stop_requested()) {
              std::optional<Job> j;
              {
                std::lock_guard lock(mutex);
                j = work;
              }
              if (!j) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
              }
              if (j->generation != generation) {
                generation = j->generation;
                cursor = 0;
              }
              constexpr uint64_t space = uint64_t{1} << 56;
              if (cursor > space - o.batch)
                throw std::runtime_error("device nonce partition exhausted");
              uint64_t start = (uint64_t(index) << 56) | cursor;
              auto r = backend->scan(j->work, start, o.batch);
              cursor += r.hashes;
              done += r.hashes;
              {
                std::lock_guard lock(mutex);
                if (found.size() + r.nonces.size() > 8192)
                  throw std::runtime_error("share queue overflow");
                for (auto n : r.nonces)
                  found.push_back({*j, n});
              }
            }
          } catch (const std::exception &e) {
            std::lock_guard lock(mutex);
            failure = e.what();
          }
        });
      struct Stop {
        std::vector<std::jthread> &workers;
        std::atomic<uint64_t> &done;
        uint64_t &total;
        ~Stop() {
          for (auto &w : workers)
            w.request_stop();
          for (auto &w : workers)
            if (w.joinable())
              w.join();
          total += done.exchange(0);
        }
      } stop{workers, done, total};
      std::unordered_map<int64_t, Clock::time_point> pending;
      int64_t nextid = 10;
      auto last = Clock::now(), received = last;
      bool activated = false;
      std::cout << "Connected to " << o.url << "\n";
      while (!expired()) {
        for (auto &msg : socket.read()) {
          received = Clock::now();
          auto j = state.receive(msg);
          if (msg.contains("id") && msg["id"].is_number_integer()) {
            auto id = msg["id"].get<int64_t>();
            if (id == 2)
              std::cout << "Extranonce subscription: "
                        << (msg.value("result", json()) == true ? "supported"
                                                                : "not supported by endpoint")
                        << "\n";
            if (pending.erase(id)) {
              if (msg.value("result", json()) == true) {
                ++accepted;
                std::cout << "Share accepted (" << accepted << ")\n";
              } else {
                int code = 0;
                if (msg.contains("error") && msg["error"].is_array() && !msg["error"].empty() &&
                    msg["error"][0].is_number_integer())
                  code = msg["error"][0];
                if (code == 21)
                  ++stale;
                else
                  ++rejected;
                std::cout << "Share rejected code=" << code << "\n";
              }
            }
          }
          if (state.authorized && (j || !activated)) {
            std::lock_guard lock(mutex);
            work = state.current;
            activated = bool(work);
            if (activated) {
              backoff = 1;
              had_work = true;
            }
          }
        }
        std::deque<Found> queue;
        {
          std::lock_guard lock(mutex);
          if (!failure.empty())
            throw std::runtime_error(failure);
          queue.swap(found);
        }
        for (const auto &f : queue) {
          if (!state.valid(f.job))
            continue;
          if (pending.size() >= 4096)
            throw std::runtime_error("too many outstanding shares");
          std::array<uint8_t, 8> nonce;
          store_le(nonce.data(), f.nonce);
          socket.send(
              {{"id", nextid},
               {"method", "mining.submit"},
               {"params", json::array({o.user, f.job.id, f.job.en2, f.job.ntime, hex(nonce)})}});
          pending.emplace(nextid++, Clock::now());
        }
        auto now = Clock::now();
        if (now - received > std::chrono::seconds(120))
          throw std::runtime_error("pool receive timeout");
        for (auto [id, t] : pending)
          if (now - t > std::chrono::seconds(60))
            throw std::runtime_error("share response timeout");
        if (now - last >= std::chrono::seconds(10)) {
          total += done.exchange(0);
          auto elapsed = std::chrono::duration<double>(now - begin).count();
          std::cout << "Total " << total / elapsed / 1e6 << " MH/s accepted=" << accepted
                    << " rejected=" << rejected << " stale=" << stale << "\n";
          last = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      for (auto &w : workers)
        w.request_stop();
      for (auto &w : workers)
        w.join();
      total += done.exchange(0);
    } catch (const std::exception &e) {
      std::cerr << "Session stopped: " << e.what() << "; reconnect in " << backoff << "s\n";
      for (int i = 0; i < backoff * 10 && !expired(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      backoff = std::min(backoff * 2, 30);
    }
  }
  std::signal(SIGINT, oldint);
  std::signal(SIGTERM, oldterm);
  auto elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
  std::cout << "Final hashes=" << total << " seconds=" << elapsed
            << " MH/s=" << total / elapsed / 1e6 << " accepted=" << accepted
            << " rejected=" << rejected << " stale=" << stale << "\n";
  return rejected ? 2 : (had_work ? 0 : 1);
}
} // namespace yolo
