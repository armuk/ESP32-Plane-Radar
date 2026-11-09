#include "services/tls_trust.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#include "services/security.h"


namespace services::tls {
namespace {

constexpr unsigned long kHandshakeTimeoutS = 5;

#if PLANE_RADAR_TLS_VERIFY == 2
/** Alternation state per host tag, so each host gets both modes equally. */
struct TagTurn {
  const char* tag;
  bool next_verify;
};
TagTurn s_turns[8] = {};

bool takeTurn(const char* tag) {
  for (TagTurn& t : s_turns) {
    if (t.tag == nullptr || strcmp(t.tag, tag) == 0) {
      t.tag = tag;
      const bool verify = t.next_verify;
      t.next_verify = !verify;
      return verify;
    }
  }
  return false;
}
#endif

}  // namespace

Session::Session(WiFiClientSecure& client, const char* tag)
    : client_(client), tag_(tag) {
#if PLANE_RADAR_TLS_VERIFY == 1
  verify_ = true;
#elif PLANE_RADAR_TLS_VERIFY == 2
  verify_ = takeTurn(tag);
#endif

  // The core waits up to 120 s for a TLS handshake by default; a normal one
  // takes under 2 s, and the web pages wait while it runs.
  client_.setHandshakeTimeout(kHandshakeTimeoutS);

  // ESP-IDF's built-in root bundle (Mozilla's CA list, ~150 roots, updated
  // with each core release). Linking any bundle check pulls all of it into
  // flash (~69 KB), so a smaller custom bundle would save nothing.
#if PLANE_RADAR_TLS_VERIFY == 1
  client_.useBuiltinCACertBundle();
#elif PLANE_RADAR_TLS_VERIFY == 2
  if (verify_) {
    client_.useBuiltinCACertBundle();
  } else {
    client_.setInsecure();
  }
#else
  // Compiled out, not just skipped: if the compiler cannot prove verify_ is
  // false, the 69 KB bundle is linked and the image outgrows its slot.
  client_.setInsecure();
#endif

#if PLANE_RADAR_TLS_VERIFY == 2
  free_before_ = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
  largest_before_ = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  started_ms_ = millis();
  measuring_ = heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
#endif
}

Session::~Session() {
#if PLANE_RADAR_TLS_VERIFY == 1
  // A refused certificate can mean someone is intercepting the connection:
  // put it in the security log, once per host per boot (no flash writes).
  if (code_ < 0) {  // verify_ is always true in this build
    char err[80] = "";
    client_.lastError(err, sizeof(err));
    if (strstr(err, "X509") != nullptr || strstr(err, "erif") != nullptr) {
      static const char* s_logged[8] = {};
      for (const char*& t : s_logged) {
        if (t == tag_) {
          break;
        }
        if (t == nullptr) {
          t = tag_;
          services::security::logEvent("certificate refused for %s: %.40s",
                                       tag_, err);
          break;
        }
      }
    }
  }
#endif
#if PLANE_RADAR_TLS_VERIFY == 2
  if (!measuring_) {
    return;
  }
  const uint32_t local_min =
      heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
  heap_caps_monitor_local_minimum_free_size_stop();
  char err[80] = "";
  if (code_ < 0) {
    client_.lastError(err, sizeof(err));
  }
  // Fixed-size line on the stack: this runs near the heap low point.
  char line[200];
  const int len = snprintf(
      line, sizeof(line),
      "tls-ab: %s verify=%d code=%d ms=%lu free_before=%u largest_before=%u "
      "local_min=%u drop=%u%s%s\n",
      tag_, verify_ ? 1 : 0, code_, millis() - started_ms_,
      static_cast<unsigned>(free_before_),
      static_cast<unsigned>(largest_before_), static_cast<unsigned>(local_min),
      static_cast<unsigned>(free_before_ - local_min), err[0] ? " err=" : "",
      err);
  if (len > 0) {
    Serial.write(reinterpret_cast<const uint8_t*>(line),
                 std::min(static_cast<size_t>(len), sizeof(line) - 1));
  }
#endif
}

}  // namespace services::tls
