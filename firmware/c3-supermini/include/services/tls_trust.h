#pragma once

#include <WiFiClientSecure.h>

/**
 * HTTPS certificate policy, chosen at build time with PLANE_RADAR_TLS_VERIFY:
 *   0            no certificate check (setInsecure)
 *   1 (default)  check against ESP-IDF's built-in root CA bundle (needs the
 *                v2.1.1 partition layout: the bundle adds ~69 KB of flash)
 *   2            measurement: alternate checked / unchecked per host and log
 *                each request's heap low point ("tls-ab:" lines)
 * Certificate dates are not checked (MBEDTLS_HAVE_TIME_DATE is off in the
 * core), so checking does not depend on the clock being set at boot.
 */
#ifndef PLANE_RADAR_TLS_VERIFY
#define PLANE_RADAR_TLS_VERIFY 1
#endif

namespace services::tls {

/**
 * Applies the policy to one request's client and, in measurement builds,
 * records the heap low point from construction to destruction. Create it
 * before http.begin() and keep it alive until after http.end() and any
 * parsing, so the measured span is the whole request.
 */
class Session {
 public:
  Session(WiFiClientSecure& client, const char* tag);
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  /** HTTP status (or negative HTTPClient error) for the measurement log. */
  void setResult(int code) { code_ = code; }

 private:
  WiFiClientSecure& client_;
  const char* tag_;
  bool verify_ = false;
  bool measuring_ = false;
  int code_ = 0;
  uint32_t free_before_ = 0;
  uint32_t largest_before_ = 0;
  unsigned long started_ms_ = 0;
};

}  // namespace services::tls
