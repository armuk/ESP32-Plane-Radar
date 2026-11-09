#pragma once

/**
 * Minimal host-side stand-in for the Arduino core, just enough for the code
 * the native unit tests compile. Not a general Arduino emulation.
 */

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

class String {
 public:
  String(const char* s = "") : s_(s != nullptr ? s : "") {}
  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }

 private:
  std::string s_;
};

struct HostSerial {
  __attribute__((format(printf, 2, 3))) void printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  }
  void println(const char* s = "") { ::printf("%s\n", s); }
};
inline HostSerial Serial;

inline unsigned long millis() {
  static const auto start = std::chrono::steady_clock::now();
  return static_cast<unsigned long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

inline void delay(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
