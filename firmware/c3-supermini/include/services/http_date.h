#pragma once

/**
 * Parse an HTTP "Date" response header (RFC 7231 IMF-fixdate), e.g.
 * "Tue, 03 Nov 2026 12:34:56 GMT". Header-only so the native tests can use it.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace services::http_date {

/** Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant). */
inline int64_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
  const unsigned day_of_year =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return static_cast<int64_t>(era) * 146097 +
         static_cast<int64_t>(day_of_era) - 719468;
}

/** UTC epoch seconds from an IMF-fixdate string; false if malformed. */
inline bool parse(const char* value, time_t* out_utc) {
  if (value == nullptr || out_utc == nullptr) {
    return false;
  }
  char weekday[4] = {};
  char month_name[4] = {};
  char zone[4] = {};
  int day = 0;
  int year = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (sscanf(value, "%3[A-Za-z], %d %3s %d %d:%d:%d %3s", weekday, &day, month_name,
             &year, &hour, &minute, &second, zone) != 8) {
    return false;
  }
  if (strcmp(zone, "GMT") != 0) {
    return false;
  }
  static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* found = strstr(kMonths, month_name);
  if (strlen(month_name) != 3 || found == nullptr ||
      (found - kMonths) % 3 != 0) {
    return false;
  }
  const unsigned month = static_cast<unsigned>((found - kMonths) / 3 + 1);
  if (year < 1970 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
      second > 60 || hour < 0 || minute < 0 || second < 0) {
    return false;
  }
  *out_utc = static_cast<time_t>(
      daysFromCivil(year, month, static_cast<unsigned>(day)) * 86400 +
      hour * 3600 + minute * 60 + second);
  return true;
}

}  // namespace services::http_date
