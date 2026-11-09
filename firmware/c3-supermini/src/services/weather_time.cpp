#include "services/weather_time.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>

#include "lwip/apps/sntp.h"
#include "lwip/priv/tcpip_priv.h"
#include "lwip/tcpip.h"

#include "config.h"
#include "hardware/display_font.h"
#include "services/display_settings.h"
#include "services/http_date.h"
#include "services/net_dns.h"
#include "services/tls_trust.h"

namespace services::weather {
namespace {

constexpr time_t kMinimumValidEpoch = 1609459200;  // 2021-01-01 UTC

bool s_started = false;
bool s_valid = false;
float s_temperature_c = 0.0f;
int s_humidity_percent = 0;
int s_weather_code = -1;
int32_t s_utc_offset_seconds = 0;
unsigned long s_last_attempt_ms = 0;
bool s_last_fetch_ok = false;
double s_last_latitude = 999.0;
double s_last_longitude = 999.0;
PollFn s_poll_fn = nullptr;

bool clockValid() { return time(nullptr) >= kMinimumValidEpoch; }

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

const char* conditionLabel(int code) {
  if (code == 0) return "CLEAR";
  if (code == 1) return "MOSTLY CLEAR";
  if (code == 2) return "PARTLY CLOUDY";
  if (code == 3) return "OVERCAST";
  if (code == 45 || code == 48) return "FOG";
  if (code >= 51 && code <= 57) return "DRIZZLE";
  if (code >= 61 && code <= 67) return "RAIN";
  if (code >= 71 && code <= 77) return "SNOW";
  if (code >= 80 && code <= 82) return "SHOWERS";
  if (code == 85 || code == 86) return "SNOW";
  if (code >= 95 && code <= 99) return "STORM";
  return "WEATHER";
}

/**
 * Backup clock from the HTTP Date header, used only until NTP has synced.
 * Accurate to about a second (Open-Meteo's current.time is rounded down to
 * 15 minutes, so that is only the last resort).
 */
bool seedClockFromHttpDate(const String& date_header) {
  if (clockValid()) {
    return true;
  }
  time_t utc = 0;
  if (!http_date::parse(date_header.c_str(), &utc) ||
      utc < kMinimumValidEpoch) {
    return false;
  }
  timeval value = {};
  value.tv_sec = utc;
  settimeofday(&value, nullptr);
  Serial.println("weather: clock set from HTTP Date (NTP not synced yet)");
  return true;
}

void seedClockFromApiTime(const char* local_iso_time, int32_t utc_offset) {
  if (clockValid() || local_iso_time == nullptr) {
    return;
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  if (sscanf(local_iso_time, "%d-%d-%dT%d:%d", &year, &month, &day, &hour,
             &minute) != 5) {
    return;
  }

  const time_t local_epoch = static_cast<time_t>(
      http_date::daysFromCivil(year, static_cast<unsigned>(month),
                    static_cast<unsigned>(day)) *
          86400 +
      hour * 3600 + minute * 60);
  if (local_epoch < kMinimumValidEpoch) {
    return;
  }

  timeval value = {};
  value.tv_sec = local_epoch - utc_offset;
  settimeofday(&value, nullptr);
}

bool fetch(double latitude, double longitude) {
  String url = config::kWeatherApiBase;
  url += "?latitude=";
  url += String(latitude, 6);
  url += "&longitude=";
  url += String(longitude, 6);
  url +=
      "&current=temperature_2m,relative_humidity_2m,weather_code,is_day"
      "&temperature_unit=celsius&timezone=auto&forecast_days=1";

  if (!services::net::resolveQuickly(url.c_str(), services::net::kDnsTimeoutMs,
                                     pollNetwork)) {
    return false;
  }
  WiFiClientSecure client;
  services::tls::Session tls(client, "weather");

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("weather: http.begin failed");
    return false;
  }
  http.setConnectTimeout(config::kWeatherRequestTimeoutMs);
  http.setTimeout(config::kWeatherRequestTimeoutMs);
  // HTTP/1.0 + no keep-alive: free the TLS session in http.end(), before
  // JSON parsing, and keep responses unchunked like the other requests.
  http.useHTTP10(true);
  http.setReuse(false);
  static const char* kCollectHeaders[] = {"Date"};
  http.collectHeaders(kCollectHeaders, 1);

  pollNetwork();
  const int code = http.GET();
  tls.setResult(code);
  pollNetwork();
  if (code != HTTP_CODE_OK) {
    Serial.printf("weather: HTTP %d\n", code);
    http.end();
    return false;
  }

  const bool clock_from_date = seedClockFromHttpDate(http.header("Date"));
  const String payload = http.getString();
  http.end();
  pollNetwork();

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.printf("weather: JSON parse error: %s\n", error.c_str());
    return false;
  }

  JsonObject current = doc["current"].as<JsonObject>();
  if (current.isNull() || !current["temperature_2m"].is<float>() ||
      !current["relative_humidity_2m"].is<int>() ||
      !current["weather_code"].is<int>()) {
    Serial.println("weather: incomplete response");
    return false;
  }

  s_temperature_c = current["temperature_2m"].as<float>();
  s_humidity_percent = current["relative_humidity_2m"].as<int>();
  s_weather_code = current["weather_code"].as<int>();
  s_utc_offset_seconds = doc["utc_offset_seconds"] | 0;
  if (!clock_from_date) {
    // Last resort: 15-minute resolution.
    seedClockFromApiTime(current["time"] | nullptr, s_utc_offset_seconds);
  }
  s_valid = true;
  Serial.printf("weather: %.1f C, %d%%, code %d, UTC%+ld\n", s_temperature_c,
                s_humidity_percent, s_weather_code,
                static_cast<long>(s_utc_offset_seconds));
  return true;
}

}  // namespace

namespace {

/** The user's POSIX TZ, or "UTC0" (localtime() == UTC) when automatic. */
const char* posixTimeZone() {
  const char* tz = settings::timeZone();
  return tz[0] != '\0' ? tz : "UTC0";
}

// SNTP keeps the pointer passed to configTzTime(), not a copy, so the server
// name must live in a buffer that is only rewritten while SNTP is stopped.
char s_ntp_server[settings::kNtpServerMaxLen + 1] = {};

// Core 3.x enables lwIP core locking: SNTP calls made outside the tcpip
// thread must hold the core lock or lwIP asserts. configTzTime() does the same.
void stopTimeSync() {
#if CONFIG_LWIP_TCPIP_CORE_LOCKING
  const bool take_lock = !sys_thread_tcpip(LWIP_CORE_LOCK_QUERY_HOLDER);
  if (take_lock) {
    LOCK_TCPIP_CORE();
  }
#endif
  if (sntp_enabled()) {
    sntp_stop();
  }
#if CONFIG_LWIP_TCPIP_CORE_LOCKING
  if (take_lock) {
    UNLOCK_TCPIP_CORE();
  }
#endif
}

void startTimeSync() {
  stopTimeSync();
  strncpy(s_ntp_server, settings::ntpServer(), sizeof(s_ntp_server) - 1);
  s_ntp_server[sizeof(s_ntp_server) - 1] = '\0';
  // Sets TZ and starts SNTP in one call (configTime() would set TZ to UTC).
  configTzTime(posixTimeZone(), s_ntp_server, config::kFallbackNtpServer);
  Serial.printf("NTP: %s (fallback %s)\n", s_ntp_server,
                config::kFallbackNtpServer);
}

}  // namespace

void applyTimeZone() {
  setenv("TZ", posixTimeZone(), 1);
  tzset();
}

void begin() {
  if (!s_started) {
    startTimeSync();
    s_started = true;
  }
}

void restartTimeSync() {
  // Before the first Wi-Fi connection begin() will pick up the new server.
  if (s_started) {
    startTimeSync();
  }
}

void setPollFn(PollFn fn) { s_poll_fn = fn; }

bool refreshIfDue(double latitude, double longitude, bool force) {
  begin();
  const unsigned long now = millis();
  const bool location_changed =
      fabs(latitude - s_last_latitude) > 0.0001 ||
      fabs(longitude - s_last_longitude) > 0.0001;
  const unsigned long interval = s_last_fetch_ok
                                     ? config::kWeatherFetchIntervalMs
                                     : config::kWeatherRetryMs;
  if (!force && !location_changed && s_last_attempt_ms != 0 &&
      now - s_last_attempt_ms < interval) {
    return false;
  }
  s_last_attempt_ms = now;
  s_last_latitude = latitude;
  s_last_longitude = longitude;
  s_last_fetch_ok = fetch(latitude, longitude);
  return s_last_fetch_ok;
}

bool valid() { return s_valid; }

void formatWeatherLine(char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  if (!s_valid) {
    snprintf(out, out_len, "WEATHER --");
    return;
  }

  float temperature = s_temperature_c;
  const char unit =
      settings::temperatureFahrenheit() ? 'F' : 'C';
  if (settings::temperatureFahrenheit()) {
    temperature = temperature * 9.0f / 5.0f + 32.0f;
  }
  // UTF-8 degree sign: the embedded smooth font has it, the ASCII-only
  // bitmap fallback fonts don't.
  const char* degree = displayFontIsSmooth() ? "\xC2\xB0" : "";
  // lroundf returns long: print with %ld (a %f here would read garbage).
  snprintf(out, out_len, "%s %ld%s%c RH%d%%", conditionLabel(s_weather_code),
           lroundf(temperature), degree, unit, s_humidity_percent);
}

namespace {

/** Local broken-down time, as shown on the footer clock. */
bool localNow(tm* local) {
  const time_t utc_now = time(nullptr);
  if (utc_now < kMinimumValidEpoch) {
    return false;
  }
  if (settings::timeZone()[0] != '\0') {
    // User-set POSIX TZ (applied by applyTimeZone): exact DST switchover.
    localtime_r(&utc_now, local);
  } else {
    // Automatic: offset reported by Open-Meteo for the radar location.
    const time_t local_now = utc_now + s_utc_offset_seconds;
    gmtime_r(&local_now, local);
  }
  return true;
}

}  // namespace

bool localHour(int* hour) {
  tm local = {};
  if (hour == nullptr || !localNow(&local)) {
    return false;
  }
  *hour = local.tm_hour;
  return true;
}

void formatDateTimeLine(char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }

  tm local = {};
  if (!localNow(&local)) {
    snprintf(out, out_len, "--:-- -- ---");
    return;
  }
  constexpr const char* kMonths[] = {"JAN", "FEB", "MAR", "APR",
                                     "MAY", "JUN", "JUL", "AUG",
                                     "SEP", "OCT", "NOV", "DEC"};
  const char* month =
      local.tm_mon >= 0 && local.tm_mon < 12 ? kMonths[local.tm_mon] : "---";

  if (settings::use24HourClock()) {
    snprintf(out, out_len, "%02d:%02d %02d %s", local.tm_hour, local.tm_min,
             local.tm_mday, month);
    return;
  }

  int hour = local.tm_hour % 12;
  if (hour == 0) {
    hour = 12;
  }
  snprintf(out, out_len, "%d:%02d%c %02d %s", hour, local.tm_min,
           local.tm_hour >= 12 ? 'P' : 'A', local.tm_mday, month);
}

}  // namespace services::weather
