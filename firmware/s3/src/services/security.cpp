#include "services/security.h"

#include <Arduino.h>
#include <WebServer.h>
#include <bootloader_random.h>
#include <esp_attr.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <nvs.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "config.h"
#include "services/display_settings.h"
#include "services/request_origin.h"

namespace services::security {
namespace {

constexpr uint32_t kLogMagic = 0x5345434C;  // "SECL"
constexpr time_t kMinValidEpoch = 1609459200;  // 2021-01-01

// RTC_NOINIT keeps these across software restarts; the magic and the bounds
// check reject the random contents after a power-on.
struct RtcLog {
  uint32_t magic;
  uint16_t boot;
  uint16_t head;   // next slot to write
  uint16_t count;  // valid entries
  Event events[kMaxEvents];
};
RTC_NOINIT_ATTR RtcLog s_log;

// Login throttling (RAM: a restart clears it, which an attacker cannot
// trigger without already having access).
constexpr uint32_t kFreeAttempts = 5;
constexpr uint32_t kFirstLockoutS = 30;
constexpr uint32_t kMaxLockoutS = 15 * 60;
uint32_t s_consecutive_failures = 0;
uint32_t s_total_failures = 0;
uint32_t s_lockout_s = 0;
unsigned long s_locked_at_ms = 0;

constexpr char kRealm[] = "Plane Radar";

// Kept events: one blob per slot ("e0".."e15") and a running count ("seq");
// slot = sequence % kMaxKeptEvents. Small separate writes wear the flash
// less than rewriting one big blob.
constexpr char kKeptNamespace[] = "seclog";
constexpr char kKeptSeqKey[] = "seq";
unsigned s_kept_writes = 0;

void keptKey(char* out, size_t len, uint32_t seq) {
  snprintf(out, len, "e%u", static_cast<unsigned>(seq % kMaxKeptEvents));
}

/**
 * True when text matches the newest kept event (any_slot false) or any kept
 * event (any_slot true). Keeps repeats from filling the log or the flash.
 */
bool alreadyKept(nvs_handle_t h, uint32_t seq, const char* text,
                 bool any_slot) {
  const uint32_t available = std::min<uint32_t>(seq, kMaxKeptEvents);
  const uint32_t look = any_slot ? available : std::min<uint32_t>(available, 1);
  for (uint32_t i = 1; i <= look; ++i) {
    char key[8];
    keptKey(key, sizeof(key), seq - i);
    Event old;
    size_t len = sizeof(old);
    if (nvs_get_blob(h, key, &old, &len) == ESP_OK && len == sizeof(old) &&
        strncmp(old.text, text, kEventTextLen) == 0) {
      return true;
    }
  }
  return false;
}

void keepEvent(const Event& e, bool once) {
  if (s_kept_writes >= kMaxKeptWritesPerBoot) {
    if (s_kept_writes == kMaxKeptWritesPerBoot) {
      ++s_kept_writes;
      Serial.println("security: kept-log write limit reached this boot");
    }
    return;
  }
  ++s_kept_writes;
  nvs_handle_t h;
  if (nvs_open(kKeptNamespace, NVS_READWRITE, &h) != ESP_OK) {
    return;
  }
  uint32_t seq = 0;
  nvs_get_u32(h, kKeptSeqKey, &seq);  // stays 0 when not found
  if (alreadyKept(h, seq, e.text, once)) {
    nvs_close(h);
    return;
  }
  char key[8];
  keptKey(key, sizeof(key), seq);
  if (nvs_set_blob(h, key, &e, sizeof(e)) == ESP_OK &&
      nvs_set_u32(h, kKeptSeqKey, seq + 1) == ESP_OK) {
    nvs_commit(h);
  }
  nvs_close(h);
}

bool logValid() {
  return s_log.magic == kLogMagic && s_log.head < kMaxEvents &&
         s_log.count <= kMaxEvents;
}

String clientIp(WebServer& web) { return web.client().remoteIP().toString(); }

/**
 * WebServer keeps only the latest Digest challenge (nonce + opaque) and
 * rejects any answer to an older one before looking at the password. That
 * happens after every restart (the browser re-sends its saved answer) and
 * when two tabs fetch challenges, so such a failure is not a guess.
 * The members are protected; a derived class may name them.
 */
struct ChallengePeek : WebServer {
  static String WebServer::*opaque() { return &ChallengePeek::_sopaque; }
};

/** True when the request's Digest answer is for the current challenge. */
bool answersCurrentChallenge(WebServer& web) {
  const String header = web.header("Authorization");
  if (!header.startsWith("Digest")) {
    return true;  // Basic (or other): an ordinary password attempt
  }
  const String& current = web.*ChallengePeek::opaque();
  if (current.length() == 0) {
    return false;  // no challenge sent since boot
  }
  const int start = header.indexOf("opaque=\"");
  if (start < 0) {
    return false;
  }
  const int value = start + 8;
  const int end = header.indexOf('"', value);
  return end > value && header.substring(value, end) == current;
}

}  // namespace

void begin() {
  if (!logValid()) {
    memset(&s_log, 0, sizeof(s_log));
    s_log.magic = kLogMagic;
  }
  ++s_log.boot;
}

void logEvent(const char* fmt, ...) {
  if (!logValid()) {
    begin();
  }
  Event& e = s_log.events[s_log.head];
  va_list args;
  va_start(args, fmt);
  vsnprintf(e.text, sizeof(e.text), fmt, args);
  va_end(args);
  e.uptime_s = millis() / 1000UL;
  const time_t now = time(nullptr);
  e.epoch = now >= kMinValidEpoch ? static_cast<uint32_t>(now) : 0;
  e.boot = s_log.boot;
  s_log.head = static_cast<uint16_t>((s_log.head + 1) % kMaxEvents);
  if (s_log.count < kMaxEvents) {
    ++s_log.count;
  }
  Serial.printf("security: %s\n", e.text);
}

namespace {
void logKeyEventV(bool once, const char* fmt, va_list args) {
  char text[kEventTextLen];
  vsnprintf(text, sizeof(text), fmt, args);
  logEvent("%s", text);
  keepEvent(s_log.events[(s_log.head + kMaxEvents - 1) % kMaxEvents], once);
}
}  // namespace

void logKeyEvent(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logKeyEventV(false, fmt, args);
  va_end(args);
}

void logKeyEventOnce(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  logKeyEventV(true, fmt, args);
  va_end(args);
}

size_t loadKeptEvents(Event* out, size_t max) {
  nvs_handle_t h;
  // Read-only open of a namespace that was never written fails quietly
  // (ESP_ERR_NVS_NOT_FOUND) without the Preferences error log.
  if (out == nullptr || nvs_open(kKeptNamespace, NVS_READONLY, &h) != ESP_OK) {
    return 0;
  }
  uint32_t seq = 0;
  nvs_get_u32(h, kKeptSeqKey, &seq);
  const uint32_t available = std::min<uint32_t>(seq, kMaxKeptEvents);
  const uint32_t wanted = std::min<uint32_t>(available, max);
  size_t n = 0;
  for (uint32_t s = seq - wanted; s != seq; ++s) {
    char key[8];
    keptKey(key, sizeof(key), s);
    size_t len = sizeof(Event);
    if (nvs_get_blob(h, key, &out[n], &len) == ESP_OK && len == sizeof(Event)) {
      out[n].text[kEventTextLen - 1] = '\0';
      ++n;
    }
  }
  nvs_close(h);
  return n;
}

size_t eventCount() { return logValid() ? s_log.count : 0; }

const Event* eventAt(size_t index) {
  if (!logValid() || index >= s_log.count) {
    return nullptr;
  }
  const size_t oldest = (s_log.head + kMaxEvents - s_log.count) % kMaxEvents;
  return &s_log.events[(oldest + index) % kMaxEvents];
}

uint16_t bootNumber() { return logValid() ? s_log.boot : 0; }

uint32_t lockoutRemainingS() {
  if (s_lockout_s == 0) {
    return 0;
  }
  const uint32_t elapsed_s = (millis() - s_locked_at_ms) / 1000UL;
  if (elapsed_s >= s_lockout_s) {
    return 0;
  }
  return s_lockout_s - elapsed_s;
}

uint32_t failedLoginsTotal() { return s_total_failures; }

/** Checks the credentials and counts failures; sends nothing. */
bool checkCredentials(WebServer& web) {
  if (lockoutRemainingS() != 0) {
    return false;
  }
  if (web.authenticate(config::kOtaUsername, settings::otaPassword())) {
    if (s_consecutive_failures != 0) {
      logEvent("login ok from %s after %u failed", clientIp(web).c_str(),
               static_cast<unsigned>(s_consecutive_failures));
    }
    s_consecutive_failures = 0;
    s_lockout_s = 0;
    // Whoever logs in has the password, so the screen notice can stop.
    settings::markOtaPasswordShown();
    return true;
  }
  // A request without credentials is the browser asking for the challenge,
  // and an answer to an old challenge never reached the password check:
  // neither is a failed attempt.
  if (web.hasHeader("Authorization") && answersCurrentChallenge(web)) {
    ++s_consecutive_failures;
    ++s_total_failures;
    logEvent("login failed from %s (%u in a row)", clientIp(web).c_str(),
             static_cast<unsigned>(s_consecutive_failures));
    if (s_consecutive_failures >= kFreeAttempts) {
      const uint32_t extra = s_consecutive_failures - kFreeAttempts;
      s_lockout_s = std::min<uint32_t>(
          kFirstLockoutS << std::min<uint32_t>(extra, 5), kMaxLockoutS);
      s_locked_at_ms = millis();
      logKeyEvent("logins locked for %u s (last try from %s)",
                  static_cast<unsigned>(s_lockout_s), clientIp(web).c_str());
    }
  }
  return false;
}

void collectRequestHeaders(WebServer& web) {
  // Authorization is always kept; WebServer copies the key names.
  static const char* kHeaders[] = {"Origin", "Referer"};
  web.collectHeaders(kHeaders, sizeof(kHeaders) / sizeof(kHeaders[0]));
}

bool requireSameOrigin(WebServer& web, bool respond) {
  const String origin = web.header("Origin");
  const String referer = web.header("Referer");
  const String host = web.hostHeader();
  if (request_origin::sameOrigin(origin.c_str(), referer.c_str(),
                                 host.c_str())) {
    return true;
  }
  const String& source = origin.length() != 0 ? origin : referer;
  logKeyEvent("blocked cross-site %s from %.24s",
           web.uri().c_str(), source.length() != 0 ? source.c_str() : "(none)");
  if (respond) {
    web.send(403, "text/plain",
             "Blocked: this request did not come from Plane Radar's own "
             "pages. Open the page on the device and try again.");
  }
  return false;
}

bool requireAdmin(WebServer& web) {
  const uint32_t wait_s = lockoutRemainingS();
  if (wait_s != 0) {
    web.sendHeader("Retry-After", String(wait_s));
    web.send(429, "text/plain",
             "Too many failed logins. Try again in " + String(wait_s) +
                 " seconds.");
    return false;
  }
  if (checkCredentials(web)) {
    return true;
  }
  web.requestAuthentication(DIGEST_AUTH, kRealm);
  return false;
}

void randomPassword(char* out, size_t len) {
  // 32 symbols (A-Z without I and O, then 2-9): 5 bits per character.
  static const char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  constexpr size_t kAlphabetLen = sizeof(kAlphabet) - 1;
  // Before the radio starts the RNG needs its entropy source switched on;
  // with Wi-Fi running it already is (and enabling it then is not allowed).
  wifi_mode_t mode = WIFI_MODE_NULL;
  const bool radio_on = esp_wifi_get_mode(&mode) == ESP_OK && mode != WIFI_MODE_NULL;
  if (!radio_on) {
    bootloader_random_enable();
  }
  for (size_t i = 0; i < len; ++i) {
    // Rejection sampling: no bias towards the first characters.
    uint32_t r = 0;
    do {
      r = esp_random() & 0xFF;
    } while (r >= (256 / kAlphabetLen) * kAlphabetLen);
    out[i] = kAlphabet[r % kAlphabetLen];
  }
  out[len] = '\0';
  if (!radio_on) {
    bootloader_random_disable();
  }
}

}  // namespace services::security
