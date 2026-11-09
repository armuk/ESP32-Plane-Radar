#include "services/firmware_image.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace services::firmware_image {
namespace {

// "PLANE-RADAR-FW-VERSION=" XOR 0x5A: the plain prefix must not appear in
// the scanner itself, or it would find its own copy in the image.
constexpr uint8_t kKey = 0x5A;
constexpr uint8_t kScrambledPrefix[] = {
    'P' ^ kKey, 'L' ^ kKey, 'A' ^ kKey, 'N' ^ kKey, 'E' ^ kKey, '-' ^ kKey,
    'R' ^ kKey, 'A' ^ kKey, 'D' ^ kKey, 'A' ^ kKey, 'R' ^ kKey, '-' ^ kKey,
    'F' ^ kKey, 'W' ^ kKey, '-' ^ kKey, 'V' ^ kKey, 'E' ^ kKey, 'R' ^ kKey,
    'S' ^ kKey, 'I' ^ kKey, 'O' ^ kKey, 'N' ^ kKey, '=' ^ kKey,
};
constexpr size_t kPrefixLen = sizeof(kScrambledPrefix);

inline uint8_t prefixAt(size_t i) { return kScrambledPrefix[i] ^ kKey; }

bool parseNumber(const char*& p, int* out) {
  if (!isdigit(static_cast<unsigned char>(*p))) {
    return false;
  }
  long value = 0;
  while (isdigit(static_cast<unsigned char>(*p))) {
    value = value * 10 + (*p - '0');
    if (value > 1000000) {
      return false;
    }
    ++p;
  }
  *out = static_cast<int>(value);
  return true;
}

}  // namespace

VersionScanner::VersionScanner() { reset(); }

void VersionScanner::reset() {
  matched_ = 0;
  capturing_ = false;
  captured_ = 0;
  found_ = false;
  version_[0] = '\0';
}

void VersionScanner::feed(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len && !found_; ++i) {
    const uint8_t c = data[i];
    if (capturing_) {
      if (c == '\0') {
        version_[captured_] = '\0';
        found_ = captured_ > 0;
        capturing_ = false;
        matched_ = 0;
      } else if (captured_ < kMaxVersionLen && isprint(c)) {
        version_[captured_++] = static_cast<char>(c);
      } else {
        // Too long or not text: not our marker.
        capturing_ = false;
        captured_ = 0;
        matched_ = 0;
      }
      continue;
    }
    if (c == prefixAt(matched_)) {
      if (++matched_ == kPrefixLen) {
        capturing_ = true;
        captured_ = 0;
      }
    } else {
      // The prefix has no repeated start, so restarting here is exact.
      matched_ = c == prefixAt(0) ? 1 : 0;
    }
  }
}

bool parseVersion(const char* text, Version* out) {
  if (text == nullptr) {
    return false;
  }
  const char* p = text;
  if (*p == 'v' || *p == 'V') {
    ++p;
  }
  Version v;
  if (!parseNumber(p, &v.major) || *p++ != '.' || !parseNumber(p, &v.minor) ||
      *p++ != '.' || !parseNumber(p, &v.patch)) {
    return false;
  }
  // Allow git describe / pre-release suffixes after a dash or plus.
  if (*p != '\0' && *p != '-' && *p != '+') {
    return false;
  }
  *out = v;
  return true;
}

bool isDowngrade(const char* running, const char* candidate) {
  Version a;
  Version b;
  if (!parseVersion(running, &a) || !parseVersion(candidate, &b)) {
    return false;
  }
  if (b.major != a.major) {
    return b.major < a.major;
  }
  if (b.minor != a.minor) {
    return b.minor < a.minor;
  }
  return b.patch < a.patch;
}

}  // namespace services::firmware_image
