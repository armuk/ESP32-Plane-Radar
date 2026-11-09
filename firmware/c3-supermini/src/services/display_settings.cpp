#include "services/display_settings.h"

#include <Preferences.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::settings {
namespace {

constexpr char kPrefsNamespace[] = "display";
constexpr char kKeyFooter[] = "footer";
constexpr char kKeyWeather[] = "weather";
constexpr char kKeyFahrenheit[] = "tempF";
constexpr char kKeyClock24[] = "time24";
constexpr char kKeyTextScale[] = "fontPct";
constexpr char kKeyOtaPassword[] = "otaPass";
/** True while a generated password has not been shown/used yet. */
constexpr char kKeyOtaPasswordNew[] = "otaPassNew";
constexpr char kKeyNtpServer[] = "ntpHost";
constexpr char kKeyTimeZone[] = "tz";
constexpr char kKeyGround[] = "ground";
constexpr char kKeyAltMin[] = "altMin";
constexpr char kKeyAltMax[] = "altMax";
constexpr char kKeyLookups[] = "lookups";
constexpr char kKeyDeviceName[] = "devName";
constexpr char kKeyUpBearing[] = "upDeg";
constexpr char kKeyAirline[] = "airline";
constexpr char kKeyClassColors[] = "acColors";
constexpr char kKeyNightOff[] = "nightOff";
constexpr char kKeyNightFrom[] = "nightFrom";
constexpr char kKeyNightTo[] = "nightTo";
constexpr char kKeyMediumAirports[] = "aptMed";
constexpr char kKeyTagAvoid[] = "tagAvoid";
constexpr char kKeyReceiverOn[] = "rxOn";
constexpr char kKeyReceiverUrl[] = "rxUrl";
constexpr char kKeyReceiverFallback[] = "rxFallbk";
constexpr int kDefaultNightFromHour = 23;
constexpr int kDefaultNightToHour = 7;

char s_ota_password[kOtaPasswordMaxLen + 1] = {};
bool s_ota_password_new = false;
bool s_ota_password_generated = false;
PasswordGenerator s_password_generator = nullptr;
char s_ntp_server[kNtpServerMaxLen + 1] = {};
char s_time_zone[kTimeZoneMaxLen + 1] = {};
char s_device_name[kDeviceNameMaxLen + 1] = {};
bool s_show_ground = config::kAdsbShowGroundAircraft;
int s_min_altitude_ft = 0;
int s_max_altitude_ft = 0;
bool s_route_lookups = true;
bool s_footer_enabled = true;
bool s_weather_enabled = true;
bool s_temperature_fahrenheit = false;
bool s_use_24_hour_clock = true;
int s_text_scale_percent = kTextScaleDefaultPercent;
int s_up_bearing_deg = 0;
bool s_show_airline = true;
bool s_class_colors = true;
bool s_night_off = false;
int s_night_from_hour = kDefaultNightFromHour;
int s_night_to_hour = kDefaultNightToHour;
bool s_show_medium_airports = true;
bool s_avoid_tag_overlap = true;
bool s_receiver_enabled = false;
char s_receiver_url[kReceiverUrlMaxLen + 1] = {};
bool s_receiver_fallback = true;

int clampHour(int value, int fallback) {
  return value >= 0 && value <= 23 ? value : fallback;
}

bool checkboxChecked(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return strcmp(value, "on") == 0 || strcmp(value, "T") == 0 ||
         strcmp(value, "t") == 0 || strcmp(value, "F") == 0 ||
         strcmp(value, "f") == 0;
}

void copyCleanText(const char* value, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (value == nullptr) {
    return;
  }

  size_t written = 0;
  bool previous_space = true;
  for (size_t i = 0; value[i] != '\0' && written + 1 < out_len; ++i) {
    const unsigned char ch = static_cast<unsigned char>(value[i]);
    if (std::isspace(ch)) {
      if (!previous_space) {
        out[written++] = ' ';
        previous_space = true;
      }
      continue;
    }
    if (ch >= 32 && ch <= 126) {
      out[written++] = static_cast<char>(ch);
      previous_space = false;
    }
  }
  while (written > 0 && out[written - 1] == ' ') {
    --written;
  }
  out[written] = '\0';
}

int clampTextScalePercent(int value) {
  if (value < kTextScaleMinPercent) {
    return kTextScaleMinPercent;
  }
  if (value > kTextScaleMaxPercent) {
    return kTextScaleMaxPercent;
  }
  return value;
}

bool parseTextScalePercent(const char* value, int* result) {
  if (value == nullptr || value[0] == '\0' || result == nullptr) {
    return false;
  }

  char* end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (end == value) {
    return false;
  }
  while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  if (*end != '\0') {
    return false;
  }

  *result = clampTextScalePercent(static_cast<int>(parsed));
  return true;
}

void setNtpServer(const char* value) {
  strncpy(s_ntp_server, value, sizeof(s_ntp_server) - 1);
  s_ntp_server[sizeof(s_ntp_server) - 1] = '\0';
}

void setDeviceName(const char* value) {
  strncpy(s_device_name, value, sizeof(s_device_name) - 1);
  s_device_name[sizeof(s_device_name) - 1] = '\0';
}

int clampAltitudeLimit(int value) {
  return value < 0 ? 0 : (value > kAltitudeLimitMaxFt ? 0 : value);
}

void setTimeZone(const char* value) {
  strncpy(s_time_zone, value, sizeof(s_time_zone) - 1);
  s_time_zone[sizeof(s_time_zone) - 1] = '\0';
}

// --- POSIX TZ syntax: std offset [dst [offset] [,rule,rule]] ---

/** Zone name: 3+ letters, or <...> with letters, digits, '+', '-'. */
bool parseTzName(const char*& p) {
  if (*p == '<') {
    ++p;
    int n = 0;
    while (std::isalnum(static_cast<unsigned char>(*p)) || *p == '+' ||
           *p == '-') {
      ++p;
      ++n;
    }
    if (*p != '>' || n < 3) {
      return false;
    }
    ++p;
    return true;
  }
  int n = 0;
  while (std::isalpha(static_cast<unsigned char>(*p))) {
    ++p;
    ++n;
  }
  return n >= 3;
}

/** Up to max_digits digits, value <= max_value. */
bool parseTzNumber(const char*& p, int max_digits, int max_value) {
  int n = 0;
  int value = 0;
  while (std::isdigit(static_cast<unsigned char>(*p)) && n < max_digits) {
    value = value * 10 + (*p - '0');
    ++p;
    ++n;
  }
  return n > 0 && value <= max_value &&
         !std::isdigit(static_cast<unsigned char>(*p));
}

/** [+-]hh[:mm[:ss]]; max_hours is 24 for offsets, 167 for rule times. */
bool parseTzTime(const char*& p, int max_hours) {
  if (*p == '+' || *p == '-') {
    ++p;
  }
  if (!parseTzNumber(p, max_hours > 99 ? 3 : 2, max_hours)) {
    return false;
  }
  for (int part = 0; part < 2 && *p == ':'; ++part) {
    ++p;
    if (!parseTzNumber(p, 2, 59)) {
      return false;
    }
  }
  return true;
}

/** Mm.w.d | Jn | n, optionally followed by /time. */
bool parseTzRule(const char*& p) {
  if (*p == 'M') {
    ++p;
    if (!parseTzNumber(p, 2, 12) || *p++ != '.' ||
        !parseTzNumber(p, 1, 5) || *p++ != '.' || !parseTzNumber(p, 1, 6)) {
      return false;
    }
  } else if (*p == 'J') {
    ++p;
    if (!parseTzNumber(p, 3, 365)) {
      return false;
    }
  } else if (!parseTzNumber(p, 3, 365)) {
    return false;
  }
  if (*p == '/') {
    ++p;
    return parseTzTime(p, 167);
  }
  return true;
}

void loadDefaults() {
  // No shared default: init() generates a per-device password.
  s_ota_password[0] = '\0';
  setNtpServer(config::kDefaultNtpServer);
  setTimeZone("");
  setDeviceName(config::kPortalHostname);
  s_show_ground = config::kAdsbShowGroundAircraft;
  s_min_altitude_ft = 0;
  s_max_altitude_ft = 0;
  s_route_lookups = true;
  s_footer_enabled = true;
  s_weather_enabled = true;
  s_temperature_fahrenheit = false;
  s_use_24_hour_clock = true;
  s_text_scale_percent = kTextScaleDefaultPercent;
  s_up_bearing_deg = 0;
  s_show_airline = true;
  s_class_colors = true;
  s_night_off = false;
  s_night_from_hour = kDefaultNightFromHour;
  s_night_to_hour = kDefaultNightToHour;
  s_show_medium_airports = true;
  s_avoid_tag_overlap = true;
  s_receiver_enabled = false;
  s_receiver_url[0] = '\0';
  s_receiver_fallback = true;
}

void persist() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kKeyFooter, s_footer_enabled);
  prefs.putBool(kKeyWeather, s_weather_enabled);
  prefs.putBool(kKeyFahrenheit, s_temperature_fahrenheit);
  prefs.putBool(kKeyClock24, s_use_24_hour_clock);
  prefs.putInt(kKeyTextScale, s_text_scale_percent);
  prefs.putString(kKeyOtaPassword, s_ota_password);
  prefs.putBool(kKeyOtaPasswordNew, s_ota_password_new);
  prefs.putString(kKeyNtpServer, s_ntp_server);
  prefs.putString(kKeyTimeZone, s_time_zone);
  prefs.putBool(kKeyGround, s_show_ground);
  prefs.putInt(kKeyAltMin, s_min_altitude_ft);
  prefs.putInt(kKeyAltMax, s_max_altitude_ft);
  prefs.putBool(kKeyLookups, s_route_lookups);
  prefs.putString(kKeyDeviceName, s_device_name);
  prefs.putInt(kKeyUpBearing, s_up_bearing_deg);
  prefs.putBool(kKeyAirline, s_show_airline);
  prefs.putBool(kKeyClassColors, s_class_colors);
  prefs.putBool(kKeyNightOff, s_night_off);
  prefs.putUChar(kKeyNightFrom, static_cast<uint8_t>(s_night_from_hour));
  prefs.putUChar(kKeyNightTo, static_cast<uint8_t>(s_night_to_hour));
  prefs.putBool(kKeyMediumAirports, s_show_medium_airports);
  prefs.putBool(kKeyTagAvoid, s_avoid_tag_overlap);
  prefs.putBool(kKeyReceiverOn, s_receiver_enabled);
  prefs.putString(kKeyReceiverUrl, s_receiver_url);
  prefs.putBool(kKeyReceiverFallback, s_receiver_fallback);
  prefs.end();
}

}  // namespace

namespace {

void loadStored() {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return;
  }

  s_footer_enabled = prefs.getBool(kKeyFooter, true);
  s_weather_enabled = prefs.getBool(kKeyWeather, true);
  s_temperature_fahrenheit = prefs.getBool(kKeyFahrenheit, false);
  s_use_24_hour_clock = prefs.getBool(kKeyClock24, true);
  s_text_scale_percent = clampTextScalePercent(
      prefs.getInt(kKeyTextScale, kTextScaleDefaultPercent));

  String value = prefs.getString(kKeyOtaPassword, "");
  copyCleanText(value.c_str(), s_ota_password, sizeof(s_ota_password));
  s_ota_password_new = prefs.getBool(kKeyOtaPasswordNew, false);
  s_ota_password_generated = s_ota_password_new;

  const String ntp = prefs.getString(kKeyNtpServer, config::kDefaultNtpServer);
  setNtpServer(isValidNtpServer(ntp.c_str()) ? ntp.c_str()
                                             : config::kDefaultNtpServer);

  const String tz = prefs.getString(kKeyTimeZone, "");
  setTimeZone(isValidTimeZone(tz.c_str()) ? tz.c_str() : "");

  s_show_ground = prefs.getBool(kKeyGround, config::kAdsbShowGroundAircraft);
  s_min_altitude_ft = clampAltitudeLimit(prefs.getInt(kKeyAltMin, 0));
  s_max_altitude_ft = clampAltitudeLimit(prefs.getInt(kKeyAltMax, 0));
  if (s_max_altitude_ft != 0 && s_min_altitude_ft > s_max_altitude_ft) {
    s_min_altitude_ft = 0;
    s_max_altitude_ft = 0;
  }
  s_route_lookups = prefs.getBool(kKeyLookups, true);

  const String name =
      prefs.getString(kKeyDeviceName, config::kPortalHostname);
  setDeviceName(isValidDeviceName(name.c_str()) ? name.c_str()
                                                : config::kPortalHostname);

  const int up = prefs.getInt(kKeyUpBearing, 0);
  s_up_bearing_deg = up >= 0 && up <= 359 ? up : 0;
  s_show_airline = prefs.getBool(kKeyAirline, true);
  s_class_colors = prefs.getBool(kKeyClassColors, true);
  s_night_off = prefs.getBool(kKeyNightOff, false);
  s_night_from_hour = clampHour(
      prefs.getUChar(kKeyNightFrom, kDefaultNightFromHour),
      kDefaultNightFromHour);
  s_night_to_hour = clampHour(prefs.getUChar(kKeyNightTo, kDefaultNightToHour),
                              kDefaultNightToHour);
  s_show_medium_airports = prefs.getBool(kKeyMediumAirports, true);
  s_avoid_tag_overlap = prefs.getBool(kKeyTagAvoid, true);
  s_receiver_enabled = prefs.getBool(kKeyReceiverOn, false);
  s_receiver_fallback = prefs.getBool(kKeyReceiverFallback, true);
  {
    const String url = prefs.getString(kKeyReceiverUrl, "");
    char cleaned[kReceiverUrlMaxLen + 1] = {};
    if (normaliseReceiverUrl(url.c_str(), cleaned, sizeof(cleaned))) {
      memcpy(s_receiver_url, cleaned, sizeof(s_receiver_url));
    }
  }
  prefs.end();
}

/**
 * Devices without a password (new, or after a factory reset) and devices
 * still on the old shared default get a random per-device password, shown
 * on the screen until someone uses it (NCSC device principle 2.3).
 */
void ensureOtaPassword() {
  const bool legacy_default =
      strcmp(s_ota_password, config::kLegacyDefaultOtaPassword) == 0;
  if (s_ota_password[0] != '\0' && !legacy_default) {
    return;
  }
  if (s_password_generator == nullptr) {
    return;
  }
  s_password_generator(s_ota_password, kGeneratedPasswordLen);
  s_ota_password_new = true;
  s_ota_password_generated = true;
  persist();
  Serial.printf("Settings password %s: a new one is shown on the screen\n",
                legacy_default ? "was the old default" : "not set");
}

}  // namespace

void init() {
  loadDefaults();
  loadStored();
  ensureOtaPassword();
}

bool footerEnabled() { return s_footer_enabled; }

bool weatherEnabled() { return s_weather_enabled; }

bool temperatureFahrenheit() { return s_temperature_fahrenheit; }

bool use24HourClock() { return s_use_24_hour_clock; }

int textScalePercent() { return s_text_scale_percent; }

const char* otaPassword() { return s_ota_password; }

const char* ntpServer() { return s_ntp_server; }

bool isValidNtpServer(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  const size_t len = strlen(value);
  if (len > kNtpServerMaxLen || value[0] == '.' || value[0] == '-' ||
      value[len - 1] == '.' || value[len - 1] == '-') {
    return false;
  }
  char previous = '\0';
  for (size_t i = 0; i < len; ++i) {
    const unsigned char ch = static_cast<unsigned char>(value[i]);
    if (!std::isalnum(ch) && ch != '.' && ch != '-') {
      return false;
    }
    if (ch == '.' && previous == '.') {
      return false;  // empty label, e.g. "pool..ntp.org"
    }
    previous = static_cast<char>(ch);
  }
  return true;
}

bool saveNtpServerFromPortal(const char* value) {
  char cleaned[kNtpServerMaxLen + 2] = {};
  copyCleanText(value, cleaned, sizeof(cleaned));

  const char* next = cleaned;
  if (cleaned[0] == '\0') {
    next = config::kDefaultNtpServer;
  } else if (!isValidNtpServer(cleaned)) {
    Serial.printf("Invalid NTP server \"%s\" — keeping %s\n", cleaned,
                  s_ntp_server);
    return false;
  }

  if (strcmp(next, s_ntp_server) == 0) {
    return false;
  }
  setNtpServer(next);
  persist();
  Serial.printf("NTP server: %s\n", s_ntp_server);
  return true;
}

const char* timeZone() { return s_time_zone; }

bool isValidTimeZone(const char* value) {
  if (value == nullptr || value[0] == '\0' ||
      strlen(value) > kTimeZoneMaxLen) {
    return false;
  }
  const char* p = value;
  if (!parseTzName(p) || !parseTzTime(p, 24)) {
    return false;
  }
  if (*p == '\0') {
    return true;  // no daylight saving
  }
  if (!parseTzName(p)) {
    return false;
  }
  // parseTzName advanced p, so it may now be at the end (cppcheck doesn't
  // follow the reference parameter).
  // cppcheck-suppress knownConditionTrueFalse
  if (*p != '\0' && *p != ',' && !parseTzTime(p, 24)) {
    return false;
  }
  if (*p == '\0') {
    return true;  // daylight saving with the C library's default rules
  }
  if (*p++ != ',' || !parseTzRule(p) || *p++ != ',' || !parseTzRule(p)) {
    return false;
  }
  return *p == '\0';
}

bool saveTimeZoneFromPortal(const char* value) {
  // TZ strings never contain spaces; strip them so pasted values still work.
  char cleaned[kTimeZoneMaxLen + 2] = {};
  size_t written = 0;
  if (value != nullptr) {
    for (size_t i = 0; value[i] != '\0' && written + 1 < sizeof(cleaned); ++i) {
      if (!std::isspace(static_cast<unsigned char>(value[i]))) {
        cleaned[written++] = value[i];
      }
    }
  }
  cleaned[written] = '\0';

  if (cleaned[0] != '\0' && !isValidTimeZone(cleaned)) {
    Serial.printf("Invalid time zone \"%s\" — keeping %s\n", cleaned,
                  s_time_zone[0] != '\0' ? s_time_zone : "automatic");
    return false;
  }
  if (strcmp(cleaned, s_time_zone) == 0) {
    return false;
  }
  setTimeZone(cleaned);
  persist();
  Serial.printf("Time zone: %s\n",
                s_time_zone[0] != '\0' ? s_time_zone : "automatic");
  return true;
}

void setPasswordGenerator(PasswordGenerator generator) {
  s_password_generator = generator;
}

bool otaPasswordNeedsShowing() { return s_ota_password_new; }

bool otaPasswordGenerated() { return s_ota_password_generated; }

void markOtaPasswordShown() {
  if (!s_ota_password_new) {
    return;
  }
  s_ota_password_new = false;
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putBool(kKeyOtaPasswordNew, false);
    prefs.end();
  }
}

PasswordChange saveFromPortal(const char* footer_checkbox,
                              const char* weather_checkbox,
                    const char* fahrenheit_checkbox,
                    const char* clock24_checkbox,
                    const char* text_scale_percent_value,
                    const char* ota_password_value) {
  s_footer_enabled = checkboxChecked(footer_checkbox);
  s_weather_enabled = checkboxChecked(weather_checkbox);
  s_temperature_fahrenheit = checkboxChecked(fahrenheit_checkbox);
  s_use_24_hour_clock = checkboxChecked(clock24_checkbox);
  int text_scale_percent = s_text_scale_percent;
  if (parseTextScalePercent(text_scale_percent_value, &text_scale_percent)) {
    s_text_scale_percent = text_scale_percent;
  }

  char password[kOtaPasswordMaxLen + 1] = {};
  copyCleanText(ota_password_value, password, sizeof(password));
  PasswordChange change = PasswordChange::kUnchanged;
  if (strlen(password) >= kOtaPasswordMinLen) {
    strncpy(s_ota_password, password, sizeof(s_ota_password) - 1);
    s_ota_password[sizeof(s_ota_password) - 1] = '\0';
    s_ota_password_new = false;
    s_ota_password_generated = false;
    change = PasswordChange::kChanged;
  } else if (password[0] != '\0') {
    change = PasswordChange::kTooShort;
  }

  persist();
  Serial.printf("Display footer: %s, weather: %s, text: %d%%\n",
                s_footer_enabled ? "on" : "off",
                s_weather_enabled ? "on" : "off", s_text_scale_percent);
  return change;
}

bool showGroundAircraft() { return s_show_ground; }

int minAltitudeFt() { return s_min_altitude_ft; }

int maxAltitudeFt() { return s_max_altitude_ft; }

bool altitudeAllowed(float alt_ft) {
  if (s_min_altitude_ft != 0 && alt_ft < static_cast<float>(s_min_altitude_ft)) {
    return false;
  }
  return s_max_altitude_ft == 0 ||
         alt_ft <= static_cast<float>(s_max_altitude_ft);
}

bool routeLookupsEnabled() { return s_route_lookups; }

bool parseAltitudeLimit(const char* value, int* out_ft) {
  if (out_ft == nullptr) {
    return false;
  }
  long total = 0;
  if (value != nullptr) {
    int digits = 0;
    for (const char* p = value; *p != '\0'; ++p) {
      const unsigned char ch = static_cast<unsigned char>(*p);
      if (std::isdigit(ch)) {
        total = total * 10 + (ch - '0');
        if (++digits > 6) {
          return false;
        }
      } else if (ch != ',' && !std::isspace(ch)) {
        return false;
      }
    }
  }
  if (total > kAltitudeLimitMaxFt) {
    return false;
  }
  *out_ft = static_cast<int>(total);  // blank -> 0 (no limit)
  return true;
}

void saveAircraftFilterFromPortal(const char* ground_checkbox,
                                  const char* min_alt_value,
                                  const char* max_alt_value,
                                  const char* lookups_checkbox) {
  s_show_ground = checkboxChecked(ground_checkbox);
  s_route_lookups = checkboxChecked(lookups_checkbox);

  int min_ft = 0;
  int max_ft = 0;
  if (!parseAltitudeLimit(min_alt_value, &min_ft) ||
      !parseAltitudeLimit(max_alt_value, &max_ft) ||
      (max_ft != 0 && min_ft > max_ft)) {
    Serial.printf("Invalid altitude limits \"%s\"-\"%s\" — keeping %d-%d ft\n",
                  min_alt_value != nullptr ? min_alt_value : "",
                  max_alt_value != nullptr ? max_alt_value : "",
                  s_min_altitude_ft, s_max_altitude_ft);
  } else {
    s_min_altitude_ft = min_ft;
    s_max_altitude_ft = max_ft;
  }

  persist();
  char max_text[8] = "any";
  if (s_max_altitude_ft != 0) {
    snprintf(max_text, sizeof(max_text), "%d", s_max_altitude_ft);
  }
  Serial.printf("Aircraft: ground %s, altitude %d-%s ft, route lookups %s\n",
                s_show_ground ? "shown" : "hidden", s_min_altitude_ft,
                max_text, s_route_lookups ? "on" : "off");
}

int upBearingDeg() { return s_up_bearing_deg; }

bool showAirlineNames() { return s_show_airline; }

bool aircraftClassColors() { return s_class_colors; }

bool nightOffEnabled() { return s_night_off; }

int nightOffFromHour() { return s_night_from_hour; }

int nightOffToHour() { return s_night_to_hour; }

bool hourInNightOff(int local_hour) {
  if (s_night_from_hour == s_night_to_hour) {
    return false;
  }
  if (s_night_from_hour < s_night_to_hour) {
    return local_hour >= s_night_from_hour && local_hour < s_night_to_hour;
  }
  return local_hour >= s_night_from_hour || local_hour < s_night_to_hour;
}

bool parseIntSetting(const char* value, int min_value, int max_value,
                     int blank_value, int* out) {
  if (out == nullptr) {
    return false;
  }
  const char* p = value != nullptr ? value : "";
  while (std::isspace(static_cast<unsigned char>(*p))) {
    ++p;
  }
  if (*p == '\0') {
    *out = blank_value;
    return true;
  }
  long total = 0;
  int digits = 0;
  for (; std::isdigit(static_cast<unsigned char>(*p)); ++p) {
    total = total * 10 + (*p - '0');
    if (++digits > 6) {
      return false;
    }
  }
  while (std::isspace(static_cast<unsigned char>(*p))) {
    ++p;
  }
  if (digits == 0 || *p != '\0' || total < min_value || total > max_value) {
    return false;
  }
  *out = static_cast<int>(total);
  return true;
}

void saveViewFromPortal(const char* up_bearing_value,
                        const char* airline_checkbox,
                        const char* class_colors_checkbox,
                        const char* night_checkbox, const char* night_from_value,
                        const char* night_to_value) {
  int up = s_up_bearing_deg;
  if (parseIntSetting(up_bearing_value, 0, 360, 0, &up)) {
    s_up_bearing_deg = up % 360;  // 360 is north too
  }
  s_show_airline = checkboxChecked(airline_checkbox);
  s_class_colors = checkboxChecked(class_colors_checkbox);
  s_night_off = checkboxChecked(night_checkbox);
  int from = s_night_from_hour;
  int to = s_night_to_hour;
  if (parseIntSetting(night_from_value, 0, 23, kDefaultNightFromHour, &from) &&
      parseIntSetting(night_to_value, 0, 23, kDefaultNightToHour, &to)) {
    s_night_from_hour = from;
    s_night_to_hour = to;
  }
  persist();
  Serial.printf("View: up %d deg, airline %s, colours %s, night off %s %02d-%02d\n",
                s_up_bearing_deg, s_show_airline ? "on" : "off",
                s_class_colors ? "on" : "off", s_night_off ? "on" : "off",
                s_night_from_hour, s_night_to_hour);
}

bool showMediumAirports() { return s_show_medium_airports; }

void saveMediumAirportsFromPortal(const char* checkbox) {
  s_show_medium_airports = checkboxChecked(checkbox);
  persist();
  Serial.printf("Medium airports: %s\n",
                s_show_medium_airports ? "on" : "off");
}

bool avoidTagOverlap() { return s_avoid_tag_overlap; }

void saveTagOverlapFromPortal(const char* checkbox) {
  s_avoid_tag_overlap = checkboxChecked(checkbox);
  persist();
}

bool receiverEnabled() { return s_receiver_enabled; }
const char* receiverUrl() { return s_receiver_url; }
bool receiverFallback() { return s_receiver_fallback; }

bool normaliseReceiverUrl(const char* in, char* out, size_t out_len) {
  if (out == nullptr || out_len == 0) {
    return false;
  }
  out[0] = '\0';
  if (in == nullptr) {
    return true;
  }
  while (*in == ' ' || *in == '\t') {
    ++in;
  }
  size_t len = strlen(in);
  while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '\t')) {
    --len;
  }
  if (len == 0) {
    return true;
  }
  for (size_t i = 0; i < len; ++i) {
    const unsigned char ch = static_cast<unsigned char>(in[i]);
    if (ch <= ' ' || ch >= 0x7F || ch == '"' || ch == '\'' || ch == '<' ||
        ch == '>' || ch == '\\') {
      return false;
    }
  }

  const bool has_scheme = len >= 7 && (strncmp(in, "http://", 7) == 0 ||
                                       (len >= 8 && strncmp(in, "https://", 8) == 0));
  if (!has_scheme && strstr(in, "://") != nullptr) {
    return false;  // ftp:// and the like
  }
  const char* prefix = has_scheme ? "" : "http://";
  const size_t prefix_len = strlen(prefix);
  const size_t scheme_len = has_scheme ? (in[4] == 's' ? 8 : 7) : 0;
  if (len == scheme_len) {
    return false;  // "http://" alone
  }
  const bool has_path =
      memchr(in + scheme_len, '/', len - scheme_len) != nullptr;
  static constexpr char kDefaultPath[] = "/data/aircraft.json";
  const size_t suffix_len = has_path ? 0 : sizeof(kDefaultPath) - 1;
  if (prefix_len + len + suffix_len + 1 > out_len ||
      prefix_len + len + suffix_len > kReceiverUrlMaxLen) {
    return false;
  }
  memcpy(out, prefix, prefix_len);
  memcpy(out + prefix_len, in, len);
  memcpy(out + prefix_len + len, kDefaultPath, suffix_len);
  out[prefix_len + len + suffix_len] = '\0';
  return true;
}

bool saveReceiverFromPortal(const char* enabled_checkbox, const char* url,
                            const char* fallback_checkbox) {
  s_receiver_enabled = checkboxChecked(enabled_checkbox);
  s_receiver_fallback = checkboxChecked(fallback_checkbox);
  char cleaned[kReceiverUrlMaxLen + 1] = {};
  const bool ok = normaliseReceiverUrl(url, cleaned, sizeof(cleaned));
  if (ok) {
    memcpy(s_receiver_url, cleaned, sizeof(s_receiver_url));
  } else {
    Serial.printf("Invalid receiver URL \"%s\" — keeping %s\n",
                  url != nullptr ? url : "", s_receiver_url);
  }
  if (s_receiver_url[0] == '\0') {
    s_receiver_enabled = false;  // nothing to fetch from
  }
  persist();
  Serial.printf("Receiver: %s %s (fallback %s)\n",
                s_receiver_enabled ? "on" : "off", s_receiver_url,
                s_receiver_fallback ? "on" : "off");
  return ok;
}

const char* deviceName() { return s_device_name; }

bool isValidDeviceName(const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  const size_t len = strlen(value);
  if (len > kDeviceNameMaxLen || value[0] == '-' || value[len - 1] == '-') {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    const char ch = value[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-')) {
      return false;
    }
  }
  return true;
}

bool saveDeviceNameFromPortal(const char* value) {
  char cleaned[kDeviceNameMaxLen + 2] = {};
  copyCleanText(value, cleaned, sizeof(cleaned));
  for (char* p = cleaned; *p != '\0'; ++p) {
    *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
  }
  // Allow pasting "name.local".
  const size_t len = strlen(cleaned);
  if (len > 6 && strcmp(cleaned + len - 6, ".local") == 0) {
    cleaned[len - 6] = '\0';
  }

  const char* next = cleaned;
  if (cleaned[0] == '\0') {
    next = config::kPortalHostname;
  } else if (!isValidDeviceName(cleaned)) {
    Serial.printf("Invalid device name \"%s\" — keeping %s\n", cleaned,
                  s_device_name);
    return false;
  }
  if (strcmp(next, s_device_name) == 0) {
    return false;
  }
  setDeviceName(next);
  persist();
  Serial.printf("Device name: %s (applies after restart)\n", s_device_name);
  return true;
}

void clear() {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
  loadDefaults();
}

}  // namespace services::settings
