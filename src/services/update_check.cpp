#include "services/update_check.h"

#include <cstring>

#include "services/firmware_image.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "services/net_dns.h"
#include "services/security.h"
#include "services/tls_trust.h"
#include "version.h"
#endif

namespace services::updates {

bool newerRelease(const char* running, const char* latest) {
  firmware_image::Version a;
  firmware_image::Version b;
  if (!firmware_image::parseVersion(running, &a) ||
      !firmware_image::parseVersion(latest, &b)) {
    return false;
  }
  // "running is a downgrade from latest" == latest is newer.
  return firmware_image::isDowngrade(latest, running);
}

#ifdef ARDUINO
namespace {

constexpr unsigned long kFirstCheckMs = 2UL * 60UL * 1000UL;
constexpr unsigned long kCheckIntervalMs = 24UL * 60UL * 60UL * 1000UL;
constexpr unsigned long kRetryMs = 60UL * 60UL * 1000UL;
constexpr unsigned long kTimeoutMs = 6000UL;
constexpr char kReleasePagePrefix[] = "https://github.com/";

PollFn s_poll = nullptr;
unsigned long s_last_attempt_ms = 0;
unsigned long s_last_ok_ms = 0;
bool s_attempted = false;
bool s_last_ok = false;
char s_latest[32] = "";
char s_page[160] = "";
char s_logged[32] = "";

void poll() {
  if (s_poll != nullptr) {
    s_poll();
  }
}

/** Tags end up on web pages: keep them to safe characters. */
bool cleanTag(const char* tag, char* out, size_t out_len) {
  const size_t len = tag != nullptr ? strlen(tag) : 0;
  if (len == 0 || len >= out_len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const char c = tag[i];
    if (!isalnum(static_cast<unsigned char>(c)) && strchr(".-_+", c) == nullptr) {
      return false;
    }
  }
  memcpy(out, tag, len + 1);
  return true;
}

bool fetchLatest() {
  char url[160];
  snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest",
           PLANE_RADAR_RELEASE_REPO);
  if (!net::resolveQuickly(url, net::kDnsTimeoutMs, poll)) {
    return false;
  }
  WiFiClientSecure client;
  tls::Session tls(client, "github");
  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  http.setConnectTimeout(kTimeoutMs);
  http.setTimeout(kTimeoutMs);
  http.useHTTP10(true);  // unchunked, so the JSON can be read as a stream
  http.setReuse(false);
  http.setUserAgent(String("PlaneRadar/") + config::kFirmwareVersion);
  http.addHeader("Accept", "application/vnd.github+json");

  poll();
  const int code = http.GET();
  tls.setResult(code);
  poll();
  if (code != HTTP_CODE_OK) {
    // 404: no published release yet (or the repository is private).
    Serial.printf("update check: HTTP %d\n", code);
    http.end();
    return false;
  }
  // The reply also carries release notes and the asset list (tens of KB);
  // the filter keeps only these two fields while streaming.
  JsonDocument filter;
  filter["tag_name"] = true;
  filter["html_url"] = true;
  JsonDocument doc;
  const DeserializationError error = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  poll();
  if (error) {
    Serial.printf("update check: JSON error %s\n", error.c_str());
    return false;
  }
  char tag[sizeof(s_latest)];
  if (!cleanTag(doc["tag_name"] | "", tag, sizeof(tag))) {
    Serial.println("update check: no usable tag");
    return false;
  }
  memcpy(s_latest, tag, sizeof(s_latest));
  const char* page = doc["html_url"] | "";
  const bool page_ok =
      strncmp(page, kReleasePagePrefix, sizeof(kReleasePagePrefix) - 1) == 0 &&
      strlen(page) < sizeof(s_page) && strpbrk(page, "\"'<> ") == nullptr;
  if (page_ok) {
    strcpy(s_page, page);
  } else {
    s_page[0] = '\0';
  }
  return true;
}

}  // namespace

void setPollFn(PollFn fn) { s_poll = fn; }

bool enabled() { return PLANE_RADAR_RELEASE_REPO[0] != '\0'; }

bool due() {
  if (!enabled() || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  const unsigned long now = millis();
  if (!s_attempted) {
    return now >= kFirstCheckMs;
  }
  return now - s_last_attempt_ms >= (s_last_ok ? kCheckIntervalMs : kRetryMs);
}

void check() {
  s_attempted = true;
  s_last_attempt_ms = millis();
  s_last_ok = fetchLatest();
  if (!s_last_ok) {
    return;
  }
  s_last_ok_ms = millis();
  const bool newer = newerRelease(config::kFirmwareVersion, s_latest);
  Serial.printf("update check: latest %s, running %s%s\n", s_latest,
                config::kFirmwareVersion, newer ? " - update available" : "");
  if (newer && strcmp(s_logged, s_latest) != 0) {
    strcpy(s_logged, s_latest);
    security::logEvent("update available: %s (running %s)", s_latest,
                       config::kFirmwareVersion);
  }
}

const char* latestTag() { return s_latest; }
const char* releasePage() { return s_page; }

bool updateAvailable() {
  return s_latest[0] != '\0' &&
         newerRelease(config::kFirmwareVersion, s_latest);
}

long lastCheckAgeS() {
  return s_last_ok_ms == 0 ? -1 : static_cast<long>((millis() - s_last_ok_ms) / 1000UL);
}
#endif

}  // namespace services::updates
