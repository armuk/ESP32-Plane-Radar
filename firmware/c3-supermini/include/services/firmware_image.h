#pragma once

#include <cstddef>
#include <cstdint>

/**
 * Reads the Plane Radar version out of a firmware image as it streams in,
 * so an update can be refused before it is activated when it is older than
 * the running firmware (NCSC device principle 11.1: recovery and updates
 * must not bring back fixed flaws). The signature check guarantees the
 * marker was put there by the release build.
 *
 * Every image carries "PLANE-RADAR-FW-VERSION=<version>\0" (see
 * ota_update.cpp). The scanner keeps its own copy of the prefix scrambled,
 * so it never matches itself inside the image.
 */
namespace services::firmware_image {

constexpr size_t kMaxVersionLen = 31;

class VersionScanner {
 public:
  VersionScanner();
  void reset();
  void feed(const uint8_t* data, size_t len);
  /** True once a complete marker has been seen; version() is then valid. */
  bool found() const { return found_; }
  const char* version() const { return version_; }

 private:
  size_t matched_ = 0;     // prefix characters matched so far
  bool capturing_ = false;
  size_t captured_ = 0;
  bool found_ = false;
  char version_[kMaxVersionLen + 1] = {};
};

struct Version {
  int major = 0;
  int minor = 0;
  int patch = 0;
};

/**
 * Parses "1.2.3", "v1.2.3" or "v1.2.3-4-gabcdef" (git describe). Returns
 * false for anything else, e.g. "dev" or a bare commit hash.
 */
bool parseVersion(const char* text, Version* out);

/**
 * True when an update from running to candidate is a downgrade. Only release
 * versions are compared: if either does not parse, it is not refused.
 */
bool isDowngrade(const char* running, const char* candidate);

}  // namespace services::firmware_image
