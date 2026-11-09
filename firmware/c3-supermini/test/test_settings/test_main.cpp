/** Setup-page validation and saving: NTP server and POSIX time zone. */

#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

// Unit under test, compiled straight in (test_build_src = no).
#include "../../src/services/display_settings.cpp"

using namespace services::settings;

void setUp() {
  Preferences::store().clear();
  init();
}
void tearDown() {}

// --- NTP server ---

void test_ntp_default() {
  TEST_ASSERT_EQUAL_STRING(config::kDefaultNtpServer, ntpServer());
}

void test_ntp_valid_names() {
  for (const char* s : {"pool.ntp.org", "192.168.1.1", "uk.pool.ntp.org",
                        "time-a.nist.gov", "router"}) {
    TEST_ASSERT_TRUE_MESSAGE(isValidNtpServer(s), s);
  }
  const std::string max_len(kNtpServerMaxLen, 'a');
  TEST_ASSERT_TRUE(isValidNtpServer(max_len.c_str()));
}

void test_ntp_invalid_names() {
  for (const char* s : {"", "pool..ntp.org", ".pool.ntp.org", "pool.ntp.org.",
                        "-x.org", "bad host", "evil\"><script>",
                        "http://pool.ntp.org"}) {
    TEST_ASSERT_FALSE_MESSAGE(isValidNtpServer(s), s);
  }
  const std::string too_long(kNtpServerMaxLen + 1, 'a');
  TEST_ASSERT_FALSE(isValidNtpServer(too_long.c_str()));
}

void test_ntp_save_reports_change_and_persists() {
  TEST_ASSERT_TRUE(saveNtpServerFromPortal("router.lan"));
  TEST_ASSERT_EQUAL_STRING("router.lan", ntpServer());
  TEST_ASSERT_FALSE(saveNtpServerFromPortal("router.lan"));  // unchanged
  TEST_ASSERT_FALSE(saveNtpServerFromPortal("bad host!"));   // rejected
  TEST_ASSERT_EQUAL_STRING("router.lan", ntpServer());
  init();  // reload from "flash"
  TEST_ASSERT_EQUAL_STRING("router.lan", ntpServer());
}

// --- Time zone ---

void test_tz_default_is_automatic() { TEST_ASSERT_EQUAL_STRING("", timeZone()); }

void test_tz_valid() {
  for (const char* s :
       {"UTC0", "GMT0BST,M3.5.0/1,M10.5.0", "CET-1CEST,M3.5.0,M10.5.0/3",
        "EST5EDT,M3.2.0,M11.1.0", "<+0530>-5:30", "IST-5:30",
        "AEST-10AEDT,M10.1.0,M4.1.0/3", "NZST-12NZDT,M9.5.0,M4.1.0/3",
        "EST5EDT", "<-03>3", "XXX3YYY,J60/2,J300", "AAA1BBB2,10,200",
        "EST5EDT,M3.2.0/-1,M11.1.0/26:30"}) {
    TEST_ASSERT_TRUE_MESSAGE(isValidTimeZone(s), s);
  }
}

void test_tz_invalid() {
  for (const char* s :
       {"", "Europe/London", "GMT", "GM0", "UTC+", "UTC25",
        "GMT0BST,M13.5.0,M10.5.0", "GMT0BST,M3.6.0,M10.5.0",
        "GMT0BST,M3.5.7,M10.5.0", "GMT0BST,M3.5.0", "GMT0BST,M3.5.0,M10.5.0,",
        "GMT0BST,M3.5", "<AB>1", "UTC0 x", "UTC0\"><b>", "UTC0:60"}) {
    TEST_ASSERT_FALSE_MESSAGE(isValidTimeZone(s), s);
  }
}

void test_tz_save_strips_spaces_and_blank_clears() {
  TEST_ASSERT_TRUE(saveTimeZoneFromPortal(" GMT0BST, M3.5.0/1, M10.5.0 "));
  TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", timeZone());
  TEST_ASSERT_FALSE(saveTimeZoneFromPortal("Europe/London"));
  init();
  TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", timeZone());
  TEST_ASSERT_TRUE(saveTimeZoneFromPortal(""));
  TEST_ASSERT_EQUAL_STRING("", timeZone());
}

// The firmware hands the saved string to newlib via setenv/tzset; check that
// strings the validator accepts give the expected local time on the host libc.
static void assertLocal(const char* tz, time_t utc, const char* want) {
  setenv("TZ", tz, 1);
  tzset();
  tm local{};
  localtime_r(&utc, &local);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(want, buf, tz);
}

void test_tz_uk_conversions() {
  const char* uk = "GMT0BST,M3.5.0/1,M10.5.0";
  assertLocal(uk, 1784635200, "2026-07-21 13:00");  // summer: BST
  assertLocal(uk, 1798459200, "2026-12-28 12:00");  // winter: GMT
  assertLocal(uk, 1774745940, "2026-03-29 00:59");  // 1 min before switch
  assertLocal(uk, 1774746000, "2026-03-29 02:00");  // switch at 01:00Z
  assertLocal("<+0530>-5:30", 1784635200, "2026-07-21 17:30");
}

// --- Aircraft filter ---

void test_filter_defaults() {
  TEST_ASSERT_EQUAL(config::kAdsbShowGroundAircraft, showGroundAircraft());
  TEST_ASSERT_EQUAL_INT(0, minAltitudeFt());
  TEST_ASSERT_EQUAL_INT(0, maxAltitudeFt());
  TEST_ASSERT_TRUE(routeLookupsEnabled());
  TEST_ASSERT_TRUE(altitudeAllowed(0.0f));
  TEST_ASSERT_TRUE(altitudeAllowed(45000.0f));
}

void test_parse_altitude_limit() {
  int ft = -1;
  TEST_ASSERT_TRUE(parseAltitudeLimit("", &ft));
  TEST_ASSERT_EQUAL_INT(0, ft);
  TEST_ASSERT_TRUE(parseAltitudeLimit(nullptr, &ft));
  TEST_ASSERT_EQUAL_INT(0, ft);
  TEST_ASSERT_TRUE(parseAltitudeLimit("10000", &ft));
  TEST_ASSERT_EQUAL_INT(10000, ft);
  TEST_ASSERT_TRUE(parseAltitudeLimit(" 10,000 ", &ft));
  TEST_ASSERT_EQUAL_INT(10000, ft);
  TEST_ASSERT_TRUE(parseAltitudeLimit("60000", &ft));
  for (const char* s : {"60001", "-100", "10k", "1e4", "12.5", "9999999"}) {
    TEST_ASSERT_FALSE_MESSAGE(parseAltitudeLimit(s, &ft), s);
  }
}

void test_save_filter_and_limits() {
  saveAircraftFilterFromPortal("T", "1000", "10000", "");
  TEST_ASSERT_TRUE(showGroundAircraft());
  TEST_ASSERT_FALSE(routeLookupsEnabled());
  TEST_ASSERT_EQUAL_INT(1000, minAltitudeFt());
  TEST_ASSERT_EQUAL_INT(10000, maxAltitudeFt());
  TEST_ASSERT_FALSE(altitudeAllowed(500.0f));
  TEST_ASSERT_TRUE(altitudeAllowed(1000.0f));
  TEST_ASSERT_TRUE(altitudeAllowed(10000.0f));
  TEST_ASSERT_FALSE(altitudeAllowed(10100.0f));

  init();  // persisted
  TEST_ASSERT_EQUAL_INT(1000, minAltitudeFt());
  TEST_ASSERT_EQUAL_INT(10000, maxAltitudeFt());
  TEST_ASSERT_FALSE(routeLookupsEnabled());

  // Minimum above maximum, or junk: limits unchanged, checkboxes still saved.
  saveAircraftFilterFromPortal("", "20000", "5000", "T");
  TEST_ASSERT_EQUAL_INT(1000, minAltitudeFt());
  TEST_ASSERT_EQUAL_INT(10000, maxAltitudeFt());
  TEST_ASSERT_FALSE(showGroundAircraft());
  TEST_ASSERT_TRUE(routeLookupsEnabled());
  saveAircraftFilterFromPortal("", "abc", "", "T");
  TEST_ASSERT_EQUAL_INT(1000, minAltitudeFt());

  // Only a minimum: no upper limit.
  saveAircraftFilterFromPortal("", "20000", "", "T");
  TEST_ASSERT_EQUAL_INT(20000, minAltitudeFt());
  TEST_ASSERT_EQUAL_INT(0, maxAltitudeFt());
  TEST_ASSERT_TRUE(altitudeAllowed(41000.0f));

  clear();
  TEST_ASSERT_EQUAL_INT(0, minAltitudeFt());
  TEST_ASSERT_TRUE(routeLookupsEnabled());
}

// --- Device name ---

void test_device_name_validation() {
  TEST_ASSERT_EQUAL_STRING(config::kPortalHostname, deviceName());
  for (const char* s : {"plane-radar", "radar2", "a", "kitchen-radar-01"}) {
    TEST_ASSERT_TRUE_MESSAGE(isValidDeviceName(s), s);
  }
  for (const char* s : {"", "-radar", "radar-", "Plane", "plane radar",
                        "plane_radar", "plane.radar", "<b>"}) {
    TEST_ASSERT_FALSE_MESSAGE(isValidDeviceName(s), s);
  }
  const std::string max_len(kDeviceNameMaxLen, 'a');
  TEST_ASSERT_TRUE(isValidDeviceName(max_len.c_str()));
  const std::string too_long(kDeviceNameMaxLen + 1, 'a');
  TEST_ASSERT_FALSE(isValidDeviceName(too_long.c_str()));
}

void test_device_name_save() {
  TEST_ASSERT_FALSE(saveDeviceNameFromPortal("plane-radar"));  // unchanged
  TEST_ASSERT_TRUE(saveDeviceNameFromPortal(" Kitchen-Radar.local "));
  TEST_ASSERT_EQUAL_STRING("kitchen-radar", deviceName());
  TEST_ASSERT_FALSE(saveDeviceNameFromPortal("bad name!"));
  TEST_ASSERT_EQUAL_STRING("kitchen-radar", deviceName());
  init();
  TEST_ASSERT_EQUAL_STRING("kitchen-radar", deviceName());
  TEST_ASSERT_TRUE(saveDeviceNameFromPortal(""));  // blank = default
  TEST_ASSERT_EQUAL_STRING(config::kPortalHostname, deviceName());
}

void test_view_defaults() {
  TEST_ASSERT_EQUAL_INT(0, upBearingDeg());
  TEST_ASSERT_TRUE(showAirlineNames());
  TEST_ASSERT_TRUE(aircraftClassColors());
  TEST_ASSERT_FALSE(nightOffEnabled());
  TEST_ASSERT_EQUAL_INT(23, nightOffFromHour());
  TEST_ASSERT_EQUAL_INT(7, nightOffToHour());
}

void test_parse_int_setting() {
  int v = -1;
  TEST_ASSERT_TRUE(parseIntSetting(" 45 ", 0, 359, 0, &v));
  TEST_ASSERT_EQUAL_INT(45, v);
  TEST_ASSERT_TRUE(parseIntSetting("", 0, 359, 7, &v));
  TEST_ASSERT_EQUAL_INT(7, v);
  TEST_ASSERT_TRUE(parseIntSetting(nullptr, 0, 359, 3, &v));
  TEST_ASSERT_EQUAL_INT(3, v);
  for (const char* bad : {"360", "-1", "4 5", "abc", "1.5", "9999999"}) {
    TEST_ASSERT_FALSE_MESSAGE(parseIntSetting(bad, 0, 359, 0, &v), bad);
  }
}

void test_save_view_and_reload() {
  saveViewFromPortal("90", "", "T", "T", "22", "6");
  TEST_ASSERT_EQUAL_INT(90, upBearingDeg());
  TEST_ASSERT_FALSE(showAirlineNames());
  TEST_ASSERT_TRUE(aircraftClassColors());
  init();  // reload from "flash"
  TEST_ASSERT_EQUAL_INT(90, upBearingDeg());
  TEST_ASSERT_TRUE(nightOffEnabled());
  TEST_ASSERT_EQUAL_INT(22, nightOffFromHour());
  TEST_ASSERT_EQUAL_INT(6, nightOffToHour());
  // 360 means north; invalid numbers keep the previous values.
  saveViewFromPortal("360", "T", "T", "T", "25", "6");
  TEST_ASSERT_EQUAL_INT(0, upBearingDeg());
  TEST_ASSERT_EQUAL_INT(22, nightOffFromHour());
  saveViewFromPortal("abc", "T", "T", "T", "1", "2");
  TEST_ASSERT_EQUAL_INT(0, upBearingDeg());
  TEST_ASSERT_EQUAL_INT(1, nightOffFromHour());
}

void test_night_hours() {
  saveViewFromPortal("0", "T", "T", "T", "23", "7");  // wraps midnight
  TEST_ASSERT_TRUE(hourInNightOff(23));
  TEST_ASSERT_TRUE(hourInNightOff(0));
  TEST_ASSERT_TRUE(hourInNightOff(6));
  TEST_ASSERT_FALSE(hourInNightOff(7));
  TEST_ASSERT_FALSE(hourInNightOff(22));
  saveViewFromPortal("0", "T", "T", "T", "1", "5");  // same day
  TEST_ASSERT_TRUE(hourInNightOff(1));
  TEST_ASSERT_FALSE(hourInNightOff(5));
  TEST_ASSERT_FALSE(hourInNightOff(0));
  saveViewFromPortal("0", "T", "T", "T", "4", "4");  // never
  TEST_ASSERT_FALSE(hourInNightOff(4));
}

// --- Local receiver ---

void test_receiver_url_normalise() {
  char out[kReceiverUrlMaxLen + 1];
  struct Case {
    const char* in;
    const char* want;
  };
  const Case ok[] = {
      {"192.168.1.50", "http://192.168.1.50/data/aircraft.json"},
      {" adsb.local:8080 ", "http://adsb.local:8080/data/aircraft.json"},
      {"http://pi/tar1090/data/aircraft.json",
       "http://pi/tar1090/data/aircraft.json"},
      {"https://feeder.example/data/aircraft.json",
       "https://feeder.example/data/aircraft.json"},
      {"http://pi", "http://pi/data/aircraft.json"},
      {"", ""},
  };
  for (const Case& c : ok) {
    TEST_ASSERT_TRUE_MESSAGE(normaliseReceiverUrl(c.in, out, sizeof(out)),
                             c.in);
    TEST_ASSERT_EQUAL_STRING(c.want, out);
  }
  for (const char* bad :
       {"ftp://pi/x", "http://", "pi/a b", "pi\"x", "http://<pi>"}) {
    TEST_ASSERT_FALSE_MESSAGE(normaliseReceiverUrl(bad, out, sizeof(out)),
                              bad);
  }
  std::string longest(kReceiverUrlMaxLen, 'a');
  TEST_ASSERT_FALSE(normaliseReceiverUrl(longest.c_str(), out, sizeof(out)));
}

void test_receiver_save_and_reload() {
  TEST_ASSERT_FALSE(receiverEnabled());
  TEST_ASSERT_TRUE(receiverFallback());
  TEST_ASSERT_TRUE(saveReceiverFromPortal("T", "10.0.0.5:8080", ""));
  init();
  TEST_ASSERT_TRUE(receiverEnabled());
  TEST_ASSERT_FALSE(receiverFallback());
  TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:8080/data/aircraft.json",
                           receiverUrl());
  // Bad URL keeps the old one; blank URL turns the receiver off.
  TEST_ASSERT_FALSE(saveReceiverFromPortal("T", "ftp://x/y", "T"));
  TEST_ASSERT_EQUAL_STRING("http://10.0.0.5:8080/data/aircraft.json",
                           receiverUrl());
  TEST_ASSERT_TRUE(saveReceiverFromPortal("T", "", "T"));
  TEST_ASSERT_FALSE(receiverEnabled());
}

void test_tag_overlap_setting() {
  TEST_ASSERT_TRUE(avoidTagOverlap());
  saveTagOverlapFromPortal("");
  init();
  TEST_ASSERT_FALSE(avoidTagOverlap());
}

// --- Settings password (NCSC 2.3: no shared default) ---

int g_generated = 0;
void fakeGenerator(char* out, size_t len) {
  ++g_generated;
  for (size_t i = 0; i < len; ++i) {
    out[i] = static_cast<char>('A' + (i + g_generated) % 26);
  }
  out[len] = '\0';
}

void test_password_generated_when_missing() {
  g_generated = 0;
  setPasswordGenerator(fakeGenerator);
  Preferences::store().clear();
  init();
  TEST_ASSERT_EQUAL_INT(1, g_generated);
  TEST_ASSERT_EQUAL_size_t(kGeneratedPasswordLen, strlen(otaPassword()));
  TEST_ASSERT_TRUE(otaPasswordNeedsShowing());
  TEST_ASSERT_TRUE(otaPasswordGenerated());
  // Kept across restarts, and still waiting to be shown.
  const std::string first = otaPassword();
  init();
  TEST_ASSERT_EQUAL_INT(1, g_generated);
  TEST_ASSERT_EQUAL_STRING(first.c_str(), otaPassword());
  TEST_ASSERT_TRUE(otaPasswordNeedsShowing());
  markOtaPasswordShown();
  init();
  TEST_ASSERT_FALSE(otaPasswordNeedsShowing());
  TEST_ASSERT_EQUAL_STRING(first.c_str(), otaPassword());
}

void test_legacy_default_password_replaced() {
  g_generated = 0;
  setPasswordGenerator(fakeGenerator);
  Preferences::store().clear();
  Preferences prefs;
  prefs.begin("display", false);
  prefs.putString("otaPass", config::kLegacyDefaultOtaPassword);
  prefs.end();
  init();
  TEST_ASSERT_EQUAL_INT(1, g_generated);
  TEST_ASSERT_NOT_EQUAL(0, strcmp(config::kLegacyDefaultOtaPassword, otaPassword()));
  TEST_ASSERT_TRUE(otaPasswordNeedsShowing());
}

void test_user_password_kept_and_clears_notice() {
  g_generated = 0;
  setPasswordGenerator(fakeGenerator);
  Preferences::store().clear();
  init();
  saveFromPortal("T", "T", "", "T", "110", "my own secret");
  TEST_ASSERT_EQUAL_STRING("my own secret", otaPassword());
  TEST_ASSERT_FALSE(otaPasswordNeedsShowing());
  TEST_ASSERT_FALSE(otaPasswordGenerated());
  init();
  TEST_ASSERT_EQUAL_STRING("my own secret", otaPassword());
  TEST_ASSERT_EQUAL_INT(1, g_generated);
  // A blank field keeps the current password.
  TEST_ASSERT_TRUE(saveFromPortal("T", "T", "", "T", "110", "") ==
                   PasswordChange::kUnchanged);
  TEST_ASSERT_EQUAL_STRING("my own secret", otaPassword());
  setPasswordGenerator(nullptr);
}

void test_short_password_rejected() {
  g_generated = 0;
  setPasswordGenerator(fakeGenerator);
  Preferences::store().clear();
  init();
  const std::string generated = otaPassword();
  // 7 characters: refused, the rest of the form is still saved.
  TEST_ASSERT_TRUE(saveFromPortal("T", "T", "", "T", "120", "1234567") ==
                   PasswordChange::kTooShort);
  TEST_ASSERT_EQUAL_STRING(generated.c_str(), otaPassword());
  TEST_ASSERT_TRUE(otaPasswordNeedsShowing());
  TEST_ASSERT_EQUAL_INT(120, textScalePercent());
  TEST_ASSERT_TRUE(saveFromPortal("T", "T", "", "T", "110", "12345678") ==
                   PasswordChange::kChanged);
  TEST_ASSERT_EQUAL_STRING("12345678", otaPassword());
  setPasswordGenerator(nullptr);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ntp_default);
  RUN_TEST(test_ntp_valid_names);
  RUN_TEST(test_ntp_invalid_names);
  RUN_TEST(test_ntp_save_reports_change_and_persists);
  RUN_TEST(test_tz_default_is_automatic);
  RUN_TEST(test_tz_valid);
  RUN_TEST(test_tz_invalid);
  RUN_TEST(test_tz_save_strips_spaces_and_blank_clears);
  RUN_TEST(test_tz_uk_conversions);
  RUN_TEST(test_filter_defaults);
  RUN_TEST(test_parse_altitude_limit);
  RUN_TEST(test_save_filter_and_limits);
  RUN_TEST(test_device_name_validation);
  RUN_TEST(test_device_name_save);
  RUN_TEST(test_view_defaults);
  RUN_TEST(test_parse_int_setting);
  RUN_TEST(test_save_view_and_reload);
  RUN_TEST(test_night_hours);
  RUN_TEST(test_receiver_url_normalise);
  RUN_TEST(test_receiver_save_and_reload);
  RUN_TEST(test_tag_overlap_setting);
  RUN_TEST(test_password_generated_when_missing);
  RUN_TEST(test_legacy_default_password_replaced);
  RUN_TEST(test_user_password_kept_and_clears_notice);
  RUN_TEST(test_short_password_rejected);
  return UNITY_END();
}
