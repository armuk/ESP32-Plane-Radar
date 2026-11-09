#pragma once

#include <cstddef>
#include <cstdint>

class WebServer;

/**
 * Security events, login throttling and random secrets.
 *
 * Events go to a small ring buffer in RTC memory: it survives software
 * restarts (OTA, factory reset, crashes, self-recovery) but not power loss,
 * and costs no flash wear. Each event is also printed on serial.
 */
namespace services::security {

constexpr size_t kEventTextLen = 72;
constexpr size_t kMaxEvents = 24;

struct Event {
  uint32_t uptime_s;  // seconds since that boot
  uint32_t epoch;     // UTC seconds, 0 when the clock was not set yet
  uint16_t boot;      // boot number (counts software restarts too)
  char text[kEventTextLen];
};

/** Call once at boot, before logging anything. */
void begin();

/** printf-style; truncated to kEventTextLen - 1 characters. */
void logEvent(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * Key events (updates, rollbacks, password changes, lockouts, blocked
 * cross-site requests, Wi-Fi erased, factory reset, settings restored) are
 * also kept in flash (NVS namespace "seclog"), so they survive power cuts.
 * The newest kMaxKeptEvents are kept. At most kMaxKeptWritesPerBoot are
 * written per boot, so a flood (e.g. a page hammering the device with
 * cross-site requests) cannot wear the flash; the RTC log still gets them.
 */
constexpr size_t kMaxKeptEvents = 16;
constexpr unsigned kMaxKeptWritesPerBoot = 20;
void logKeyEvent(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/**
 * As logKeyEvent, but kept only when no kept event has the same text: for
 * conditions reported on every boot (a past rollback).
 */
void logKeyEventOnce(const char* fmt, ...)
    __attribute__((format(printf, 1, 2)));

/**
 * Copies the kept events into out (oldest first, at most max) and returns
 * how many. Kept events carry the boot number of the power-on they were
 * logged in, which restarts at 1 after a power cut.
 */
size_t loadKeptEvents(Event* out, size_t max);

size_t eventCount();
/** index 0 = oldest kept event. */
const Event* eventAt(size_t index);
uint16_t bootNumber();

/**
 * Checks the admin login for the current request (Digest authentication,
 * user config::kOtaUsername, the settings password). While locked out after
 * repeated failures it answers 429 without checking. On failure it sends the
 * Digest challenge. Returns true when the request may go ahead.
 */
bool requireAdmin(WebServer& web);

/**
 * The same check without sending a response (for the start of a firmware
 * upload, where the reply comes after the body). Counts failures and
 * honours the lockout.
 */
bool checkCredentials(WebServer& web);

/**
 * Cross-site request check for requests that change something (see
 * request_origin.h). True when the request came from the device's own
 * pages; otherwise logs it and, when respond is true, answers 403.
 */
bool requireSameOrigin(WebServer& web, bool respond = true);

/** Makes the server keep the Origin and Referer headers (call per server). */
void collectRequestHeaders(WebServer& web);

/** Seconds until another login attempt is accepted (0 = not locked). */
uint32_t lockoutRemainingS();
uint32_t failedLoginsTotal();

/**
 * Fills out with len random characters from an alphabet without look-alike
 * characters (no 0/O, 1/I/l), then a NUL. Uses the hardware RNG with its
 * entropy source enabled, so it is safe to call before Wi-Fi starts.
 */
void randomPassword(char* out, size_t len);

}  // namespace services::security
