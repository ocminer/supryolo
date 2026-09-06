// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
#include "yolo/ui.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <conio.h>
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif
namespace yolo {
std::string console_safe(const std::string &s) {
  std::string out;
  for (unsigned char c : s)
    if (c >= 32 && c < 127)
      out += char(c);
    else if (c >= 128)
      out += '?';
  return out;
}
namespace {
std::string decimal(double n, int precision = 1) {
  std::ostringstream s;
  s << std::fixed << std::setprecision(precision) << n;
  return s.str();
}
std::string rate(double n) {
  const char *units[] = {"H/s", "KH/s", "MH/s", "GH/s", "TH/s"};
  int i = 0;
  while (n >= 1000 && i < 4) {
    n /= 1000;
    ++i;
  }
  return decimal(n, 2) + " " + units[i];
}
std::string duration(double seconds) {
  auto n = uint64_t(std::max(0.0, seconds));
  std::ostringstream s;
  s << std::setfill('0') << std::setw(2) << n / 3600 << ':' << std::setw(2) << (n / 60) % 60 << ':'
    << std::setw(2) << n % 60;
  return s.str();
}
std::string value(std::optional<unsigned> n, const std::string &unit = "") {
  return n ? std::to_string(*n) + unit : "--";
}
std::string stamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  char out[16];
  std::strftime(out, sizeof(out), "%H:%M:%S", &tm);
  return out;
}
enum Color { dim, green, bright, cyan, yellow, red, alarm };
struct Cell {
  char c = ' ';
  Color color = dim;
};
const char *colors[] = {"\033[0;32;40m",   "\033[0;32;40m",   "\033[0;1;92;40m", "\033[0;1;96;40m",
                        "\033[0;1;93;40m", "\033[0;1;91;40m", "\033[1;97;41m"};
} // namespace
std::string render_dashboard(const DashboardView &v, int width, int height, double tick,
                             size_t offset) {
  width = std::clamp(width, 20, 500);
  height = std::clamp(height, 5, 200);
  std::vector<Cell> grid(size_t(width) * height);
  auto put = [&](int x, int y, const std::string &text, Color color = green, int maximum = 1000) {
    if (y < 0 || y >= height)
      return;
    int count = 0;
    for (char c : console_safe(text)) {
      if (count++ >= maximum || x >= width)
        break;
      if (x >= 0)
        grid[size_t(y) * width + x] = {c, color};
      ++x;
    }
  };
  auto box = [&](int x, int y, int w, int h, const std::string &title) {
    if (w < 2 || h < 2)
      return;
    put(x, y, "+" + std::string(w - 2, '-') + "+", dim);
    put(x, y + h - 1, "+" + std::string(w - 2, '-') + "+", dim);
    for (int j = 1; j < h - 1; ++j) {
      put(x, y + j, "|", dim);
      put(x + w - 1, y + j, "|", dim);
    }
    put(x + 2, y, " " + title + " ", bright, w - 4);
  };
  if (width < 76 || height < 23) {
    put(1, 0, "SUPRYOLO // " + v.coin, bright);
    put(1, 1, v.state + "  " + duration(v.elapsed), cyan);
    put(1, 2,
        "A " + std::to_string(v.accepted) + " R " + std::to_string(v.rejected) + " S " +
            std::to_string(v.stale));
    int y = 3;
    for (const auto &d : v.devices) {
      if (y >= height - 1)
        break;
      bool hot = d.sensors.temperature && *d.sensors.temperature >= v.alarm_temperature;
      put(1, y++,
          d.label + " " + rate(d.hashes_per_second) + " " + value(d.sensors.temperature, "C") +
              (hot && (uint64_t(tick * 4) & 1) == 0 ? " ALARM" : ""),
          hot ? red : green);
    }
    put(0, height - 1, "Resize >=76x23 | q quit", dim, width);
  } else {
    int top = 9, log_h = std::max(7, height / 3), log_y = height - log_h - 1,
        devices_h = log_y - top, left = std::max(38, width * 45 / 100);
    box(0, 0, left, top, "LINK // " + v.coin);
    box(left, 0, width - left, top, "SUPRYOLO // 0.1.0");
    put(2, 1, v.state + "  UP " + duration(v.elapsed), v.state == "MINING" ? bright : yellow,
        left - 4);
    put(2, 2, v.pool, cyan, left - 4);
    put(2, 3, "Worker " + v.worker, dim, left - 4);
    put(2, 4, "Accepted " + std::to_string(v.accepted) + "  Rejected " + std::to_string(v.rejected),
        bright, left - 4);
    put(2, 5, "Stale " + std::to_string(v.stale) + "  Pending " + std::to_string(v.pending), green,
        left - 4);
    put(2, 6, "Difficulty " + v.difficulty, cyan, left - 4);
    put(2, 7, "Block candidates " + std::to_string(v.block_candidates), green, left - 4);
    static const std::vector<std::string> logo = {
        "  ____  _   _ ____  ____  ",    " / ___|| | | |  _ \\|  _ \\ ",
        " \\___ \\| | | | |_) | |_) |",  "  ___) | |_| |  __/|  _ < ",
        " |____/ \\___/|_|   |_| \\_\\", "       Y O L O  //  01"};
    int lx = left + std::max(2, (width - left - 29) / 2);
    for (size_t i = 0; i < logo.size(); ++i)
      put(lx, 1 + int(i), logo[i], i == 5 ? cyan : bright, width - left - 3);
    std::string rain;
    for (int i = 0; i < width - left - 4; ++i)
      rain += (((i * 17 + int(tick * 3)) % 13) < 6 ? '0' : '1');
    put(left + 2, 7, rain, dim);
    double total = 0;
    for (const auto &d : v.devices)
      total += d.hashes_per_second;
    box(0, top, width, devices_h, "DEVICES // " + rate(total));
    put(2, top + 1, "DEVICE  RATE          TEMP  FAN   CORE   MEM    POWER     A/R", cyan);
    size_t visible = size_t(std::max(0, devices_h - 3));
    if (offset >= v.devices.size())
      offset = 0;
    for (size_t i = 0; i < visible && i + offset < v.devices.size(); ++i) {
      const auto &d = v.devices[i + offset];
      int y = top + 2 + int(i);
      bool hot = d.sensors.temperature && *d.sensors.temperature >= v.alarm_temperature;
      bool warm = d.sensors.temperature && *d.sensors.temperature >= v.warn_temperature;
      Color c = hot ? red : (warm ? yellow : bright);
      put(2, y, d.label, c, 7);
      put(10, y, rate(d.hashes_per_second), bright, 13);
      put(24, y, value(d.sensors.temperature, "C"), c, 5);
      put(30, y, value(d.sensors.fan_percent, "%"), green, 5);
      put(36, y, value(d.sensors.core_mhz), green, 6);
      put(43, y, value(d.sensors.memory_mhz), green, 6);
      put(50, y, d.sensors.watts ? decimal(*d.sensors.watts, 0) + "W" : "--", green, 8);
      put(59, y, std::to_string(d.accepted) + "/" + std::to_string(d.rejected), green, 9);
      if (hot) {
        if ((uint64_t(tick * 4) & 1) == 0)
          put(width - 8, y, " ALARM ", alarm, 7);
      } else if (width >= 100)
        put(70, y, d.name, dim, width - 72);
      else if (warm)
        put(width - 7, y, "WARM", yellow, 5);
    }
    if (v.devices.size() > visible)
      put(width - 29, top + devices_h - 1,
          " j/k scroll " + std::to_string(offset + 1) + "/" + std::to_string(v.devices.size()) +
              " ",
          cyan, 27);
    box(0, log_y, width, log_h, "EVENT STREAM");
    int rows = log_h - 2;
    size_t first = v.events.size() > size_t(rows) ? v.events.size() - rows : 0;
    for (size_t i = first; i < v.events.size(); ++i) {
      const auto &e = v.events[i];
      Color c = e.severity == Severity::error     ? red
                : e.severity == Severity::warning ? yellow
                : e.severity == Severity::success ? bright
                                                  : green;
      put(2, log_y + 1 + int(i - first), e.time + " " + e.message, c, width - 4);
    }
    put(1, height - 1,
        "q quit | j/k devices | clocks MHz | power W | block candidates are unconfirmed", dim,
        width - 2);
  }
  std::string out = "\033[H";
  Color last = alarm;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto c = grid[size_t(y) * width + x];
      if (c.color != last) {
        out += colors[c.color];
        last = c.color;
      }
      out += c.c;
    }
    if (y + 1 < height)
      out += "\r\n";
  }
  out += "\033[0m";
  return out;
}
struct Dashboard::Impl {
  std::mutex mutex;
  DashboardView view;
  std::deque<Event> queued;
  std::atomic<bool> quit{false};
  bool active = false;
  std::ofstream log;
  std::jthread thread;
#ifdef _WIN32
  HANDLE output=GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD old_output=0;
  bool output_mode_changed=false;
#else
  termios old_term{};
  int old_flags = -1;
#endif
  bool raw = false;
  size_t offset = 0;
  explicit Impl(const std::string &mode, const std::string &log_file) {
    if (!log_file.empty()) {
      log.open(log_file, std::ios::app);
      if (!log) throw std::runtime_error("Cannot open miner log file");
    }
    if (mode != "auto" && mode != "on" && mode != "off")
      throw std::runtime_error("TUI mode must be auto, on or off");
#ifdef _WIN32
    active=mode!="off" && GetConsoleMode(output,&old_output) &&
           SetConsoleMode(output,old_output|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    output_mode_changed=active;
    raw=active;
#else
    auto term = std::getenv("TERM");
    active = mode != "off" && isatty(STDOUT_FILENO) &&
             (mode == "on" || (term && std::string(term) != "dumb"));
#endif
    if (active) {
      std::cout << "\033[?1049h\033[?25l\033[2J" << std::flush;
#ifndef _WIN32
      if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old_term) == 0) {
        auto t = old_term;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0) {
          raw = true;
          old_flags = fcntl(STDIN_FILENO, F_GETFL);
          if (old_flags >= 0)
            fcntl(STDIN_FILENO, F_SETFL, old_flags | O_NONBLOCK);
        }
      }
#endif
    }
    try {
      thread = std::jthread([this](std::stop_token stop) {
        auto start = std::chrono::steady_clock::now(), last_summary = start;
        do {
          DashboardView copy;
          std::deque<Event> lines;
          {
            std::lock_guard lock(mutex);
            copy = view;
            lines.swap(queued);
          }
          auto now = std::chrono::steady_clock::now();
          if (log.is_open()) {
            for (const auto &e : lines) log << e.time << " " << console_safe(e.message) << '\n';
            log.flush();
          }
          if (active) {
            if (raw) {
              char c;
#ifdef _WIN32
              while (_kbhit()) { c=static_cast<char>(_getch());
#else
              while (read(STDIN_FILENO, &c, 1) == 1) {
#endif
                if (c == 'q' || c == 'Q')
                  quit = true;
                if (c == 'j')
                  ++offset;
                if (c == 'k' && offset)
                  --offset;
              }
            }
#ifdef _WIN32
            CONSOLE_SCREEN_BUFFER_INFO info{};
            GetConsoleScreenBufferInfo(output,&info);
            int width=info.srWindow.Right-info.srWindow.Left+1;
            int height=info.srWindow.Bottom-info.srWindow.Top+1;
#else
            winsize size{};
            ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
            int width=size.ws_col ? size.ws_col:100, height=size.ws_row ? size.ws_row:30;
#endif
            std::cout << render_dashboard(
                             copy, width, height,
                             std::chrono::duration<double>(now - start).count(), offset)
                      << std::flush;
          } else {
            for (const auto &e : lines)
              std::cout << e.time << " " << console_safe(e.message) << '\n';
            if (now - last_summary >= std::chrono::seconds(10)) {
              std::cout << "Total " << rate(copy.elapsed > 0 ? copy.hashes / copy.elapsed : 0)
                        << " accepted=" << copy.accepted << " rejected=" << copy.rejected
                        << " stale=" << copy.stale << '\n';
              for (const auto &d : copy.devices)
                std::cout << "  " << d.label << " " << rate(d.hashes_per_second)
                          << " temp=" << value(d.sensors.temperature, "C")
                          << " fan=" << value(d.sensors.fan_percent, "%")
                          << " core=" << value(d.sensors.core_mhz)
                          << " mem=" << value(d.sensors.memory_mhz)
                          << " power=" << (d.sensors.watts ? decimal(*d.sensors.watts) + "W" : "--")
                          << '\n';
              last_summary = now;
            }
            std::cout.flush();
          }
          if (stop.stop_requested())
            break;
          for (int i = 0; i < 5 && !stop.stop_requested(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } while (true);
      });
    } catch (...) {
      restore();
      throw;
    }
  }
  void restore() {
#ifndef _WIN32
    if (raw) {
      tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
      if (old_flags >= 0)
        fcntl(STDIN_FILENO, F_SETFL, old_flags);
      raw = false;
    }
#endif
    if (active)
      std::cout << "\033[0m\033[?25h\033[?1049l" << std::flush;
  }
  ~Impl() {
    thread.request_stop();
    if (thread.joinable())
      thread.join();
    restore();
#ifdef _WIN32
    if(output_mode_changed) SetConsoleMode(output,old_output);
#endif
  }
};
Dashboard::Dashboard(const std::string &mode, const std::string &log_file) : impl(std::make_unique<Impl>(mode, log_file)) {}
Dashboard::~Dashboard() = default;
void Dashboard::update(const DashboardView &view) {
  std::lock_guard lock(impl->mutex);
  auto events = std::move(impl->view.events);
  impl->view = view;
  impl->view.events = std::move(events);
}
void Dashboard::event(std::string message, Severity severity) {
  std::lock_guard lock(impl->mutex);
  Event e{stamp(), console_safe(message), severity};
  impl->view.events.push_back(e);
  impl->queued.push_back(e);
  if (impl->view.events.size() > 200)
    impl->view.events.pop_front();
  if (impl->queued.size() > 1000)
    impl->queued.pop_front();
}
bool Dashboard::quit_requested() const { return impl->quit.load(); }
bool Dashboard::interactive() const { return impl->active; }
} // namespace yolo
