#include "services/wifi_setup.h"
#include "services/weak_signal.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include <LittleFS.h>
#include <Preferences.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <nvs_flash.h>

#ifdef WM_MDNS
#include <ESPmDNS.h>
#endif

#include "config.h"
#include "services/display_settings.h"
#include "services/portal_pages.h"
#include "services/portal_style.h"
#include "services/local_airports.h"
#include "services/ota_update.h"
#include "services/radar_location.h"
#include "services/security.h"
#include "services/weather_time.h"
#include "ui/radar_projection.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

portMUX_TYPE s_boot_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_boot_tap_pending = false;
volatile bool s_boot_is_down = false;
volatile unsigned long s_boot_down_ms = 0;
bool s_long_press_handled = false;
bool s_boot_interrupt_attached = false;

void IRAM_ATTR onBootButtonIsr() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_boot_mux);
  if (down) {
    s_boot_is_down = true;
    s_boot_down_ms = now;
  } else if (s_boot_is_down) {
    const unsigned long held = now - s_boot_down_ms;
    if (held >= config::kBootTapMinMs && held < config::kBootResetHoldMs) {
      s_boot_tap_pending = true;
    }
    s_boot_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_boot_mux);
}

void initBootButton() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_boot_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)),
                  onBootButtonIsr, CHANGE);
  s_boot_interrupt_attached = true;
}

namespace {

/** Separate from planeradar prefs (rangeInit) to avoid NVS handle conflicts. */
constexpr char kWifiPrefsNamespace[] = "wifi";
constexpr char kPrefsForcePortalKey[] = "portal";

bool s_force_config_portal = false;
WiFiManager s_wm;

/**
 * WPA2 password of the setup hotspot: random per boot and shown on the
 * screen, so only someone who can see the device can join it.
 */
char s_ap_password[kSetupApPasswordLen + 1] = {};

const char* apPassword() {
  if (s_ap_password[0] == '\0') {
    services::security::randomPassword(s_ap_password, kSetupApPasswordLen);
  }
  return s_ap_password;
}
bool s_wm_configured = false;

void ensureWifiManager();
void startLanWebPortal();
void stopLanWebPortal();
bool wifiLinkUp();
void attachSettingsRoutes();
void eraseWifiAndRestart();
void handleRestart();

constexpr int kCoordParamLen = 20;
// step="any": the browser accepts values copied from a map with 15
// decimals; the page script and the firmware round them to 6 (~0.1 m).
constexpr char kLatitudeInputAttrs[] =
    "type=\"number\" step=\"any\" min=\"-90\" max=\"90\"";
constexpr char kLongitudeInputAttrs[] =
    "type=\"number\" step=\"any\" min=\"-180\" max=\"180\"";
constexpr int kOtaPasswordParamLen =
    static_cast<int>(services::settings::kOtaPasswordMaxLen);
constexpr int kTextScaleParamLen = 4;

WiFiManagerParameter s_param_lat("radar_lat", "Latitude (deg)", "0",
                                kCoordParamLen, kLatitudeInputAttrs);
WiFiManagerParameter s_param_lon("radar_lon", "Longitude (deg)", "0",
                                kCoordParamLen, kLongitudeInputAttrs);
WiFiManagerParameter s_param_coord_help(
    "<div class=\"hint\">Paste from Google Maps as is: extra decimals are "
    "trimmed to 6, and a pasted \"lat, lon\" pair fills both boxes.</div>");
char s_miles_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_miles("use_miles", "Display distances in miles", "T", 2,
                                   s_miles_checkbox_attrs, WFM_LABEL_AFTER);

WiFiManagerParameter s_param_after_miles_break("<br/>");

char s_runways_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_runways("show_runways", "Show airport runways", "T", 2,
                                     s_runways_checkbox_attrs, WFM_LABEL_AFTER);
WiFiManagerParameter s_param_after_runways_break("<br/>");

// Medium airports downloaded for the area (status rebuilt on render).
char s_medium_airports_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_medium_airports(
    "medium_airports", "Show medium airports", "T", 2,
    s_medium_airports_checkbox_attrs, WFM_LABEL_AFTER);
char s_airports_status_html[256] = "";
WiFiManagerParameter s_param_airports_status(s_airports_status_html);

char s_footer_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_footer("show_footer", "Show weather and clock", "T",
                                    2, s_footer_checkbox_attrs,
                                    WFM_LABEL_AFTER);

char s_weather_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_weather(
    "show_weather", "Show current weather", "T", 2,
    s_weather_checkbox_attrs, WFM_LABEL_AFTER);

char s_fahrenheit_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_fahrenheit(
    "temp_f", "Temperature in Fahrenheit", "T", 2,
    s_fahrenheit_checkbox_attrs, WFM_LABEL_AFTER);

char s_clock24_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_clock24("clock_24", "Use 24-hour clock", "T", 2,
                                     s_clock24_checkbox_attrs,
                                     WFM_LABEL_AFTER);

WiFiManagerParameter s_param_after_clock_break("<br/>");

constexpr int kNtpServerParamLen =
    static_cast<int>(services::settings::kNtpServerMaxLen);
// Browser-side hint only; the firmware validates again before saving.
constexpr char kNtpServerAttrs[] =
    "placeholder=\"pool.ntp.org\" pattern=\"[A-Za-z0-9.\\-]+\" "
    "title=\"Host name or IPv4 address; leave blank for pool.ntp.org\" "
    "autocapitalize=\"off\" spellcheck=\"false\"";
WiFiManagerParameter s_param_ntp_server(
    "ntp_server", "Time server (NTP)", "pool.ntp.org", kNtpServerParamLen,
    kNtpServerAttrs);
WiFiManagerParameter s_param_after_ntp_break("<br/>");

constexpr int kTimeZoneParamLen =
    static_cast<int>(services::settings::kTimeZoneMaxLen);
constexpr char kTimeZoneAttrs[] =
    "placeholder=\"Automatic (from location)\" "
    "pattern=\"[A-Za-z0-9<>+\\-:,.\\/]+\" "
    "title=\"POSIX TZ rule, e.g. GMT0BST,M3.5.0/1,M10.5.0 for the UK; "
    "leave blank for automatic\" "
    "autocapitalize=\"off\" spellcheck=\"false\"";
WiFiManagerParameter s_param_time_zone("time_zone", "Time zone (POSIX TZ)",
                                       "", kTimeZoneParamLen, kTimeZoneAttrs);
WiFiManagerParameter s_param_time_zone_help(
    "<div class=\"hint\">Blank = automatic. "
    "Examples: UK <code>GMT0BST,M3.5.0/1,M10.5.0</code>, "
    "Central Europe <code>CET-1CEST,M3.5.0,M10.5.0/3</code>, "
    "US Eastern <code>EST5EDT,M3.2.0,M11.1.0</code></div>");

constexpr char kTextScaleAttrs[] =
    "type=\"range\" min=\"80\" max=\"130\" step=\"5\" "
    "oninput=\"document.getElementById('text_scale_value').value="
    "this.value+'%'\"";
WiFiManagerParameter s_param_text_scale(
    "text_scale", "Radar text size", "110", kTextScaleParamLen,
    kTextScaleAttrs);
WiFiManagerParameter s_param_text_scale_output(
    "<div style=\"text-align:center;margin-top:-5px\">"
    "<output id=\"text_scale_value\" for=\"text_scale\"></output></div>"
    "<script>(function(){var s=document.getElementById('text_scale'),"
    "o=document.getElementById('text_scale_value');"
    "if(s&&o)o.value=s.value+'%';})();</script>");

constexpr char kOtaPasswordAttrs[] =
    "type=\"password\" autocomplete=\"new-password\" minlength=\"8\" "
    "placeholder=\"leave blank to keep current\"";
static_assert(services::settings::kOtaPasswordMinLen == 8,
              "update minlength in kOtaPasswordAttrs");
WiFiManagerParameter s_param_ota_password(
    "ota_password", "Settings password (user: admin, 8+ characters)", "", kOtaPasswordParamLen,
    kOtaPasswordAttrs);

// --- Range (dropdown; plain HTML read from the request by name) ---
// Rebuilt by refreshPortalParamDefaults() so the current preset is selected
// and labels follow the km/mi setting. WiFiManager keeps the pointer.
char s_range_select_html[640] = "";
WiFiManagerParameter s_param_range(s_range_select_html);

// --- Aircraft filter ---
char s_ground_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_ground("show_ground",
                                    "Show aircraft on the ground", "T", 2,
                                    s_ground_checkbox_attrs, WFM_LABEL_AFTER);
WiFiManagerParameter s_param_after_ground_break("<br/>");

constexpr int kAltitudeParamLen = 7;
constexpr char kAltitudeAttrs[] =
    "type=\"number\" min=\"0\" max=\"60000\" step=\"100\" "
    "placeholder=\"no limit\"";
WiFiManagerParameter s_param_min_alt("min_alt", "Lowest altitude shown (ft)",
                                     "", kAltitudeParamLen, kAltitudeAttrs);
WiFiManagerParameter s_param_max_alt("max_alt", "Highest altitude shown (ft)",
                                     "", kAltitudeParamLen, kAltitudeAttrs);
WiFiManagerParameter s_param_altitude_help(
    "<div class=\"hint\">Blank = no limit. "
    "E.g. highest 10000 to hide high cruising traffic.</div>");

char s_lookups_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_lookups(
    "route_lookups", "Look up routes and aircraft types (ADSBDB)", "T", 2,
    s_lookups_checkbox_attrs, WFM_LABEL_AFTER);
WiFiManagerParameter s_param_after_lookups_break("<br/>");

// --- Radar view extras ---
constexpr int kSmallNumberParamLen = 4;
constexpr char kUpBearingAttrs[] =
    "type=\"number\" min=\"0\" max=\"359\" step=\"1\" placeholder=\"0\"";
WiFiManagerParameter s_param_up_bearing(
    "up_bearing", "Compass bearing at the top of the screen (0 = north)", "0",
    kSmallNumberParamLen, kUpBearingAttrs);
WiFiManagerParameter s_param_up_bearing_help(
    "<div class=\"hint\">E.g. 90 if the radar "
    "faces a window looking east.</div>");

char s_airline_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_airline("show_airline",
                                     "Show airline names on tags", "T", 2,
                                     s_airline_checkbox_attrs, WFM_LABEL_AFTER);
WiFiManagerParameter s_param_after_airline_break("<br/>");
char s_class_colors_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_class_colors(
    "class_colors", "Light aircraft small/yellow, military green", "T", 2,
    s_class_colors_checkbox_attrs, WFM_LABEL_AFTER);
WiFiManagerParameter s_param_after_class_colors_break("<br/>");

char s_night_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_night("night_off", "Turn the screen off at night",
                                   "T", 2, s_night_checkbox_attrs,
                                   WFM_LABEL_AFTER);
WiFiManagerParameter s_param_after_night_break("<br/>");
constexpr char kHourAttrs[] =
    "type=\"number\" min=\"0\" max=\"23\" step=\"1\"";
WiFiManagerParameter s_param_night_from("night_from", "Off from (hour, 0-23)",
                                        "23", kSmallNumberParamLen, kHourAttrs);
WiFiManagerParameter s_param_night_to("night_to", "Back on at (hour, 0-23)",
                                      "7", kSmallNumberParamLen, kHourAttrs);
WiFiManagerParameter s_param_night_help(
    "<div class=\"hint\">Local time. A tap on "
    "BOOT wakes the screen for a minute.</div>");

// --- Device name ---
constexpr int kDeviceNameParamLen =
    static_cast<int>(services::settings::kDeviceNameMaxLen);
constexpr char kDeviceNameAttrs[] =
    "placeholder=\"plane-radar\" pattern=\"[A-Za-z0-9\\-]+\" "
    "title=\"Letters, digits and hyphens; leave blank for plane-radar\" "
    "autocapitalize=\"off\" spellcheck=\"false\"";
WiFiManagerParameter s_param_device_name("device_name",
                                         "Device name (name.local)",
                                         "plane-radar", kDeviceNameParamLen,
                                         kDeviceNameAttrs);
WiFiManagerParameter s_param_device_name_help(
    "<div class=\"hint\">Changing it restarts "
    "the device. Give each radar on the network its own name.</div>");

// --- Aircraft tags and source ---
char s_overlap_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_overlap(
    "avoid_overlap", "Move or shorten tags so they don't overlap", "T", 2,
    s_overlap_checkbox_attrs, WFM_LABEL_AFTER);
WiFiManagerParameter s_param_after_overlap_break("<br/>");
char s_source_html[200] = "";
WiFiManagerParameter s_param_source(s_source_html);

// --- Setup sections: shown as tabs by the page script (portal_style.cpp),
// or one after another with headings when scripts are off ---
#define SETUP_SECTION(var, title)                                         \
  WiFiManagerParameter var("<section class=\"tab\" data-tab=\"" title \
                           "\"><h3>" title "</h3>")
SETUP_SECTION(s_section_location, "Location");
SETUP_SECTION(s_section_aircraft, "Aircraft");
SETUP_SECTION(s_section_map, "Map");
SETUP_SECTION(s_section_screen, "Screen");
SETUP_SECTION(s_section_clock, "Clock & weather");
SETUP_SECTION(s_section_device, "Device");
#undef SETUP_SECTION
WiFiManagerParameter s_section_end_location("</section>");
WiFiManagerParameter s_section_end_aircraft("</section>");
WiFiManagerParameter s_section_end_map("</section>");
WiFiManagerParameter s_section_end_screen("</section>");
WiFiManagerParameter s_section_end_clock("</section>");
WiFiManagerParameter s_section_end_device("</section>");

/** Device restart requested by a settings save (e.g. new device name). */
unsigned long s_restart_requested_ms = 0;

void buildRangeSelectHtml() {
  size_t used = static_cast<size_t>(snprintf(
      s_range_select_html, sizeof(s_range_select_html),
      "<label for=\"range\">Range (ring label)</label>"
      "<select id=\"range\" name=\"range\" style=\"width:100%%;"
      "margin:5px 0 11px;font-size:1em\">"));
  for (size_t i = 0; i < ui::radar::kRangePresetCount &&
                     used < sizeof(s_range_select_html);
       ++i) {
    char label[12];
    ui::radar::formatRing3Label(label, sizeof(label),
                                ui::radar::kRangePresets[i].ring3_km,
                                ui::radar::useMiles());
    used += static_cast<size_t>(snprintf(
        s_range_select_html + used, sizeof(s_range_select_html) - used,
        "<option value=\"%u\"%s>%s</option>", static_cast<unsigned>(i),
        i == ui::radar::rangeIndex() ? " selected" : "", label));
  }
  if (used < sizeof(s_range_select_html)) {
    snprintf(s_range_select_html + used, sizeof(s_range_select_html) - used,
             "</select>");
  }
}

void refreshCheckboxAttrs(char* attrs, size_t attrs_len, bool checked) {
  snprintf(attrs, attrs_len, "type=\"checkbox\"%s",
           checked ? " checked" : "");
}

void refreshPortalParamDefaults() {
  char lat_buf[kCoordParamLen + 1];
  char lon_buf[kCoordParamLen + 1];
  snprintf(lat_buf, sizeof(lat_buf), "%.6f", services::location::lat());
  snprintf(lon_buf, sizeof(lon_buf), "%.6f", services::location::lon());
  s_param_lat.setValue(lat_buf, kCoordParamLen);
  s_param_lon.setValue(lon_buf, kCoordParamLen);
  refreshCheckboxAttrs(s_miles_checkbox_attrs,
                       sizeof(s_miles_checkbox_attrs),
                       ui::radar::useMiles());
  s_param_miles.setValue("T", 2);
  refreshCheckboxAttrs(s_runways_checkbox_attrs,
                       sizeof(s_runways_checkbox_attrs),
                       ui::radar::showRunways());
  s_param_runways.setValue("T", 2);
  refreshCheckboxAttrs(s_footer_checkbox_attrs,
                       sizeof(s_footer_checkbox_attrs),
                       services::settings::footerEnabled());
  s_param_footer.setValue("T", 2);
  refreshCheckboxAttrs(s_weather_checkbox_attrs,
                       sizeof(s_weather_checkbox_attrs),
                       services::settings::weatherEnabled());
  s_param_weather.setValue("T", 2);
  refreshCheckboxAttrs(s_fahrenheit_checkbox_attrs,
                       sizeof(s_fahrenheit_checkbox_attrs),
                       services::settings::temperatureFahrenheit());
  s_param_fahrenheit.setValue("T", 2);
  refreshCheckboxAttrs(s_clock24_checkbox_attrs,
                       sizeof(s_clock24_checkbox_attrs),
                       services::settings::use24HourClock());
  s_param_clock24.setValue("T", 2);
  char text_scale_buf[kTextScaleParamLen + 1];
  snprintf(text_scale_buf, sizeof(text_scale_buf), "%d",
           services::settings::textScalePercent());
  s_param_text_scale.setValue(text_scale_buf, kTextScaleParamLen);
  s_param_ntp_server.setValue(services::settings::ntpServer(),
                              kNtpServerParamLen);
  s_param_time_zone.setValue(services::settings::timeZone(),
                             kTimeZoneParamLen);
  s_param_ota_password.setValue("", kOtaPasswordParamLen);

  buildRangeSelectHtml();
  refreshCheckboxAttrs(s_medium_airports_checkbox_attrs,
                       sizeof(s_medium_airports_checkbox_attrs),
                       services::settings::showMediumAirports());
  s_param_medium_airports.setValue("T", 2);
  {
    char status[200];
    services::airports::formatStatus(status, sizeof(status));
    snprintf(s_airports_status_html, sizeof(s_airports_status_html),
             "<div class=\"hint\">%s</div>",
             status);
  }
  refreshCheckboxAttrs(s_ground_checkbox_attrs,
                       sizeof(s_ground_checkbox_attrs),
                       services::settings::showGroundAircraft());
  s_param_ground.setValue("T", 2);
  char alt_buf[kAltitudeParamLen + 1] = "";
  if (services::settings::minAltitudeFt() != 0) {
    snprintf(alt_buf, sizeof(alt_buf), "%d",
             services::settings::minAltitudeFt());
  }
  s_param_min_alt.setValue(alt_buf, kAltitudeParamLen);
  alt_buf[0] = '\0';
  if (services::settings::maxAltitudeFt() != 0) {
    snprintf(alt_buf, sizeof(alt_buf), "%d",
             services::settings::maxAltitudeFt());
  }
  s_param_max_alt.setValue(alt_buf, kAltitudeParamLen);
  refreshCheckboxAttrs(s_lookups_checkbox_attrs,
                       sizeof(s_lookups_checkbox_attrs),
                       services::settings::routeLookupsEnabled());
  s_param_lookups.setValue("T", 2);
  s_param_device_name.setValue(services::settings::deviceName(),
                               kDeviceNameParamLen);

  char number_buf[kSmallNumberParamLen + 1];
  snprintf(number_buf, sizeof(number_buf), "%d",
           services::settings::upBearingDeg());
  s_param_up_bearing.setValue(number_buf, kSmallNumberParamLen);
  refreshCheckboxAttrs(s_airline_checkbox_attrs,
                       sizeof(s_airline_checkbox_attrs),
                       services::settings::showAirlineNames());
  s_param_airline.setValue("T", 2);
  refreshCheckboxAttrs(s_class_colors_checkbox_attrs,
                       sizeof(s_class_colors_checkbox_attrs),
                       services::settings::aircraftClassColors());
  s_param_class_colors.setValue("T", 2);
  refreshCheckboxAttrs(s_overlap_checkbox_attrs,
                       sizeof(s_overlap_checkbox_attrs),
                       services::settings::avoidTagOverlap());
  s_param_overlap.setValue("T", 2);
  snprintf(s_source_html, sizeof(s_source_html),
           "<div class=\"msg P\"><strong>Aircraft source:</strong> %s<br>"
           "<a href=\"/receiver\">Receiver settings &rsaquo;</a></div>",
           services::settings::receiverEnabled() ? "your own receiver"
                                                 : "adsb.fi (internet)");
  refreshCheckboxAttrs(s_night_checkbox_attrs, sizeof(s_night_checkbox_attrs),
                       services::settings::nightOffEnabled());
  s_param_night.setValue("T", 2);
  snprintf(number_buf, sizeof(number_buf), "%d",
           services::settings::nightOffFromHour());
  s_param_night_from.setValue(number_buf, kSmallNumberParamLen);
  snprintf(number_buf, sizeof(number_buf), "%d",
           services::settings::nightOffToHour());
  s_param_night_to.setValue(number_buf, kSmallNumberParamLen);
}

/**
 * Applies a settings form. Both save paths read the request by field name:
 * WiFiManager itself fills its parameters from the same request arguments.
 * Returns true when the device must restart to apply a change.
 */
bool savePortalParamsFromRequest(WebServer& web) {
  if (!services::location::saveFromStrings(web.arg("radar_lat").c_str(),
                                           web.arg("radar_lon").c_str())) {
    Serial.println("Invalid lat/lon in portal — keeping previous location");
  }
  ui::radar::saveMilesFromPortal(web.arg("use_miles").c_str());
  ui::radar::saveRunwaysFromPortal(web.arg("show_runways").c_str());
  services::settings::saveMediumAirportsFromPortal(
      web.arg("medium_airports").c_str());
  if (web.hasArg("range")) {
    ui::radar::saveRangeFromPortal(web.arg("range").c_str());
  }
  const services::settings::PasswordChange password_change =
      services::settings::saveFromPortal(
          web.arg("show_footer").c_str(), web.arg("show_weather").c_str(),
          web.arg("temp_f").c_str(), web.arg("clock_24").c_str(),
          web.arg("text_scale").c_str(), web.arg("ota_password").c_str());
  if (password_change == services::settings::PasswordChange::kChanged) {
    services::security::logKeyEvent("settings password changed from %s",
                                 web.client().remoteIP().toString().c_str());
  } else if (password_change ==
             services::settings::PasswordChange::kTooShort) {
    services::security::logEvent("settings password too short, not changed");
  }
  services::settings::saveAircraftFilterFromPortal(
      web.arg("show_ground").c_str(), web.arg("min_alt").c_str(),
      web.arg("max_alt").c_str(), web.arg("route_lookups").c_str());
  if (services::settings::saveNtpServerFromPortal(
          web.arg("ntp_server").c_str())) {
    services::weather::restartTimeSync();
  }
  if (services::settings::saveTimeZoneFromPortal(
          web.arg("time_zone").c_str())) {
    services::weather::applyTimeZone();
  }
  services::settings::saveViewFromPortal(
      web.arg("up_bearing").c_str(), web.arg("show_airline").c_str(),
      web.arg("class_colors").c_str(), web.arg("night_off").c_str(),
      web.arg("night_from").c_str(), web.arg("night_to").c_str());
  ui::radar::setUpBearingDeg(services::settings::upBearingDeg());
  services::settings::saveTagOverlapFromPortal(
      web.arg("avoid_overlap").c_str());
  const bool restart = services::settings::saveDeviceNameFromPortal(
      web.arg("device_name").c_str());
  refreshPortalParamDefaults();
  services::security::logEvent("settings saved from %s",
                               web.client().remoteIP().toString().c_str());
  return restart;
}

/** WiFiManager's own save path (e.g. Wi-Fi page in the setup hotspot). */
void onPortalParamsSaved() {
  if (s_wm.server) {
    // A new device name applies at the next boot here: restarting in the
    // middle of WiFiManager's connect flow would interrupt it.
    savePortalParamsFromRequest(*s_wm.server);
  }
}

void handleSettingsSaved() {
  if (!s_wm.server) {
    return;
  }

  WebServer& web = *s_wm.server;
  if (savePortalParamsFromRequest(web)) {
    s_restart_requested_ms = millis();
    const String host = String(services::settings::deviceName()) + ".local";
    const String refresh =
        "<meta http-equiv='refresh' content='12;url=http://" + host +
        "/param'>";
    web.send(200, "text/html",
             services::portal::page(
                 "Restarting", refresh.c_str(),
                 "<h1>Saved</h1><div class='msg S'><strong>Restarting"
                 "</strong><br><small>The device is now <b>" + host +
                     "</b>. Reconnecting in about 12 seconds...</small>"
                     "</div><p><a href='http://" + host + "/param'>Open http://" +
                     host + "</a></p>"));
    return;
  }
  const bool airports_pending =
      services::settings::showMediumAirports() &&
      services::airports::refreshDue(services::location::lat(),
                                     services::location::lon());
  const int return_s = airports_pending ? 10 : 3;
  String body = "<h1>Saved</h1><div class='msg S'><small>Returning to "
                "Setup in " +
                String(return_s) + " seconds...</small></div>";
  if (airports_pending) {
    body += F("<div class='msg P'><small>New location: the radar will "
              "download nearby airports once Setup has been left alone for "
              "a minute. That takes 2&ndash;3 minutes, with UPDATING "
              "AIRPORTS on screen, and the portal doesn't respond until it "
              "finishes.</small></div>");
  }
  body += F("<p><a href='/param'>Return now</a></p>");
  web.send(200, "text/html",
           services::portal::page(
               "Setup saved",
               ("<meta http-equiv='refresh' content='" + String(return_s) +
                ";url=/param'>")
                   .c_str(),
               body));
}

void attachSettingsRoutes() {
  if (!s_wm.server) {
    return;
  }
  // Register before WiFiManager's built-in /paramsave handler so the custom
  // confirmation can redirect back to Setup.
  s_wm.server->on("/paramsave", HTTP_POST, handleSettingsSaved);
  s_wm.server->on("/restart", handleRestart);
  // Receiver, About, and a confirmation in front of /erase.
  services::portal::attachPages(*s_wm.server, eraseWifiAndRestart);
}

void attachPortalParams(WiFiManager& wm) {
  refreshPortalParamDefaults();
  // cppcheck-suppress constVariable ; addParameter() takes non-const pointers
  WiFiManagerParameter* const params[] = {
      &s_section_location,
      &s_param_lat, &s_param_lon, &s_param_coord_help, &s_param_range, &s_param_miles,
      &s_param_after_miles_break, &s_param_up_bearing, &s_param_up_bearing_help,
      &s_section_end_location,

      &s_section_aircraft,
      &s_param_source,
      &s_param_ground, &s_param_after_ground_break,
      &s_param_min_alt, &s_param_max_alt, &s_param_altitude_help,
      &s_param_class_colors, &s_param_after_class_colors_break,
      &s_param_overlap, &s_param_after_overlap_break,
      &s_param_lookups, &s_param_after_lookups_break,
      &s_param_airline, &s_param_after_airline_break,
      &s_section_end_aircraft,

      &s_section_map,
      &s_param_runways, &s_param_after_runways_break,
      &s_param_medium_airports, &s_param_airports_status,
      &s_section_end_map,

      &s_section_screen,
      &s_param_text_scale, &s_param_text_scale_output,
      &s_param_night, &s_param_after_night_break,
      &s_param_night_from, &s_param_night_to, &s_param_night_help,
      &s_section_end_screen,

      &s_section_clock,
      &s_param_footer, &s_param_weather, &s_param_fahrenheit,
      &s_param_clock24, &s_param_after_clock_break,
      &s_param_ntp_server, &s_param_after_ntp_break,
      &s_param_time_zone, &s_param_time_zone_help,
      &s_section_end_clock,

      &s_section_device,
      &s_param_device_name, &s_param_device_name_help,
      &s_param_ota_password,
      &s_section_end_device,
  };
  for (WiFiManagerParameter* param : params) {
    wm.addParameter(param);
  }
  wm.setSaveParamsCallback(onPortalParamsSaved);
}

/**
 * The "open the portal at next boot" flag. Its namespace only exists after a
 * factory reset, and Preferences logs an error for every failed read-only
 * open, so read it through NVS directly: missing simply means "no".
 */
bool forcePortalFlagSet() {
  nvs_handle_t handle;
  if (nvs_open(kWifiPrefsNamespace, NVS_READONLY, &handle) != ESP_OK) {
    return false;
  }
  uint8_t value = 0;
  const bool set =
      nvs_get_u8(handle, kPrefsForcePortalKey, &value) == ESP_OK && value != 0;
  nvs_close(handle);
  return set;
}

void markForceConfigPortal() {
  s_force_config_portal = true;
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kPrefsForcePortalKey, true);
  prefs.end();
}

bool consumeForceConfigPortal() {
  if (s_force_config_portal) {
    s_force_config_portal = false;
    Preferences prefs;
    if (prefs.begin(kWifiPrefsNamespace, false)) {
      prefs.remove(kPrefsForcePortalKey);
      prefs.end();
    }
    return true;
  }

  if (!forcePortalFlagSet()) {
    return false;
  }

  Preferences prefs;
  if (prefs.begin(kWifiPrefsNamespace, false)) {
    prefs.remove(kPrefsForcePortalKey);
    prefs.end();
  }
  return true;
}

/**
 * Reads the saved network from the Wi-Fi driver. Either output may be null.
 * ESP-IDF keeps the SSID in a 32-byte field and the password in a 64-byte
 * one, with no terminating NUL when they are full length. WiFiManager's
 * getWiFiSSID() reads such an SSID straight on into the password, so a
 * 32-character network name never connected; copy and terminate here.
 */
bool readStoredWifiCredentials(String* ssid, String* pass) {
  wifi_mode_t mode = WIFI_MODE_NULL;
  if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
    delay(50);
  }

  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK ||
      conf.sta.ssid[0] == '\0') {
    return false;
  }
  if (ssid != nullptr) {
    char buf[sizeof(conf.sta.ssid) + 1] = {};
    memcpy(buf, conf.sta.ssid, sizeof(conf.sta.ssid));
    ssid->clear();
    ssid->concat(buf);  // copies the bytes
  }
  if (pass != nullptr) {
    char buf[sizeof(conf.sta.password) + 1] = {};
    memcpy(buf, conf.sta.password, sizeof(conf.sta.password));
    pass->clear();
    pass->concat(buf);  // copies the bytes
  }
  return true;
}

bool storedWifiCredentials() {
  return readStoredWifiCredentials(nullptr, nullptr);
}

void eraseWifiCredentials() {
  stopLanWebPortal();
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  ensureWifiManager();
  WiFi.persistent(true);
  s_wm.resetSettings();
  s_wm.erase();
  WiFi.disconnect(true, true);
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(100);
}

/** Menu "Restart", in place of WiFiManager's (which restarts abruptly). */
void handleRestart() {
  if (!s_wm.server) {
    return;
  }
  WebServer& web = *s_wm.server;
  web.send(200, "text/html",
           services::portal::page(
               "Restarting", "<meta http-equiv='refresh' content='15;url=/'>",
               "<h1>Restarting</h1><div class='msg S'><small>Back in about "
               "15 seconds...</small></div><p><a href='/'>Home</a></p>"));
  web.client().flush();
  delay(300);
  services::security::logEvent("restart from the portal (%s)",
                               web.client().remoteIP().toString().c_str());
  wifiLeaveAndRestart();
}

/** "Forget Wi-Fi" in the portal: only the network; settings are kept. */
void eraseWifiAndRestart() {
  services::security::logKeyEvent("saved Wi-Fi erased from the portal");
  markForceConfigPortal();
  eraseWifiCredentials();
  statusScreenWifiReset();
  delay(800);
  esp_restart();
}

/**
 * Factory reset: erases the whole NVS partition (Wi-Fi network, location,
 * settings, settings password), the downloaded airports near the old
 * location and any crash dump (which can hold memory contents). Clearing
 * keys one by one would only mark them deleted, so old values could still
 * be read from flash over USB (NCSC device principles 3.6 and 11).
 */
void resetWifiCredentials() {
  services::security::logEvent("factory reset (BOOT held)");
  eraseWifiCredentials();  // also stops the radio, which uses NVS

  LittleFS.end();
  const esp_partition_t* storage = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
  if (storage != nullptr) {
    esp_partition_erase_range(storage, 0, storage->size);
  }
  esp_core_dump_image_erase();

  nvs_flash_deinit();
  const esp_err_t err = nvs_flash_erase();
  nvs_flash_init();
  Serial.printf("Factory reset: storage, crash dump and NVS erased (%s)\n",
                esp_err_to_name(err));
  // The force-portal flag lives in NVS, so set it after the erase. The kept
  // security log was erased too; start it again with this event.
  markForceConfigPortal();
  services::security::logKeyEvent("factory reset: settings and logs erased");
}

void onConfigPortalApStarted(WiFiManager*) {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  statusScreenPortal(apPassword());
#ifdef WM_MDNS
  if (MDNS.begin(services::settings::deviceName())) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Setup portal: http://%s.local (or http://%s)\n",
                  services::settings::deviceName(), config::kPortalIp);
  } else {
    Serial.printf("Setup portal: http://%s (mDNS unavailable)\n", config::kPortalIp);
  }
#else
  Serial.printf("Setup portal: http://%s\n", config::kPortalIp);
#endif
}

bool wifiLinkUp() {
  return WiFi.status() == WL_CONNECTED &&
         WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void ensureWifiManager() {
  if (s_wm_configured) {
    return;
  }
  s_wm.setConfigPortalTimeout(config::kWifiPortalTimeoutSec);
  s_wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                           IPAddress(255, 255, 255, 0));
  s_wm.setHostname(services::settings::deviceName());
  s_wm.setTitle("Plane Radar");
  s_wm.setAPCallback(onConfigPortalApStarted);
  attachPortalParams(s_wm);
  services::ota::configure(s_wm, attachSettingsRoutes);
  s_wm_configured = true;
}

void startLanWebPortal() {
  if (!wifiLinkUp() || s_wm.getWebPortalActive() ||
      s_wm.getConfigPortalActive()) {
    return;
  }
  refreshPortalParamDefaults();
  WiFi.mode(WIFI_STA);
  s_wm.setConfigPortalBlocking(false);
#ifdef WM_MDNS
  MDNS.end();
  if (MDNS.begin(services::settings::deviceName())) {
    MDNS.addService("http", "tcp", 80);
  }
#endif
  s_wm.startWebPortal();
  Serial.printf("LAN config: http://%s.local or http://%s\n",
                services::settings::deviceName(),
                WiFi.localIP().toString().c_str());
}

void stopLanWebPortal() {
  if (!s_wm.getWebPortalActive()) {
    return;
  }
  s_wm.stopWebPortal();
#ifdef WM_MDNS
  MDNS.end();
#endif
}

/**
 * Join the strongest access point for the network, not the first one heard.
 * The core's default is a fast scan, which stops at the first match: with
 * two access points on the same channel the device sometimes joined the far
 * one at -85 dBm, got an address, then lost every DNS lookup until the link
 * dropped 1-3 minutes later. A full scan adds about a second.
 *
 * WiFi.begin() without arguments (the saved network, and the core's own
 * reconnects) reuses the configuration stored by the Wi-Fi driver, so that
 * is updated too.
 */
void preferStrongestAp() {
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  wifi_config_t conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK ||
      conf.sta.ssid[0] == 0) {
    return;
  }
  if (conf.sta.scan_method == WIFI_ALL_CHANNEL_SCAN &&
      conf.sta.sort_method == WIFI_CONNECT_AP_BY_SIGNAL &&
      !conf.sta.bssid_set && conf.sta.rm_enabled && conf.sta.btm_enabled) {
    return;
  }
  conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  conf.sta.bssid_set = 0;
  // 802.11k (radio measurements) and 802.11v (BSS transition): lets a
  // network that supports them (e.g. UniFi with "BSS Transition" on) steer
  // the device to a better access point while connected. Needs no BSSID or
  // channel pinned, hence bssid_set = 0 above.
  conf.sta.rm_enabled = 1;
  conf.sta.btm_enabled = 1;
  esp_wifi_set_config(WIFI_IF_STA, &conf);
}

// Weak-signal rejoin: a backstop for networks that don't steer devices.
// The device keeps whichever access point it joined; if that one stays weak
// (e.g. the near one rebooted), leave and rejoin, which picks the strongest.
#ifndef PLANE_RADAR_WEAK_RSSI_DBM
#define PLANE_RADAR_WEAK_RSSI_DBM -78  // test builds raise it to force a rejoin
#endif
constexpr int kWeakRssiDbm = PLANE_RADAR_WEAK_RSSI_DBM;
constexpr unsigned long kSignalSampleMs = 30UL * 1000UL;
constexpr unsigned long kRejoinMinIntervalMs = 15UL * 60UL * 1000UL;
constexpr unsigned long kRejoinMaxIntervalMs = 4UL * 60UL * 60UL * 1000UL;

struct SignalWatch {
  unsigned long last_sample_ms = 0;
  services::wifi_signal::WeakSignalWindow window{kWeakRssiDbm};
  unsigned long last_rejoin_ms = 0;
  unsigned long rejoin_interval_ms = kRejoinMinIntervalMs;
  bool awaiting_result = false;
  unsigned long rejoin_started_ms = 0;  // 0 = no rejoin under way
  int rssi_before = 0;
  uint8_t bssid_before[6] = {};
};
SignalWatch s_signal;

/** Which access point was joined, and why the link went down. */
void logWifiEvents() {
  static bool registered = false;
  if (registered) {
    return;
  }
  registered = true;
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
      const uint8_t* b = info.wifi_sta_connected.bssid;
      Serial.printf("WiFi: joined %02x:%02x:%02x:%02x:%02x:%02x ch %u, %d dBm\n",
                    b[0], b[1], b[2], b[3], b[4], b[5],
                    info.wifi_sta_connected.channel, WiFi.RSSI());
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("WiFi: link down, reason %u (%s), %d dBm\n",
                    info.wifi_sta_disconnected.reason,
                    WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(
                        info.wifi_sta_disconnected.reason)),
                    info.wifi_sta_disconnected.rssi);
    }
  });
}

void prepareSta() {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);
  logWifiEvents();
  preferStrongestAp();
}

void startStaConnect(const String& ssid, const String& pass) {
  prepareSta();
  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    WiFi.begin();
  }
}

bool waitForLinkWithUi(const char* ssid_for_ui, unsigned long attempt_ms) {
  // Elapsed-time form stays correct when millis() wraps (~49.7 days).
  const unsigned long start = millis();
  while (millis() - start < attempt_ms) {
    if (wifiLinkUp()) {
      return true;
    }
    bootButtonPollLongPress();
    statusScreenConnectingTick();
    delay(config::kWifiConnectingFrameMs);
  }
  return wifiLinkUp();
}

bool tryConnectWithUi(const String& ssid, const String& pass, bool show_ui) {
  if (wifiLinkUp()) {
    return true;
  }

  const char* ui_ssid = ssid.length() > 0 ? ssid.c_str() : "network";
  if (show_ui) {
    statusScreenConnectingBegin(ui_ssid);
  }

  // Failed rounds since the last successful connection. Retries normally
  // just disconnect and reconnect; a full radio off/on (which can leak a
  // little memory in some IDF versions) is kept as an occasional recovery
  // step for a radio stuck mid-connect, not done on every retry.
  static uint32_t s_failed_rounds = 0;

  for (uint8_t attempt = 1; attempt <= config::kWifiConnectAttempts; ++attempt) {
    if (attempt > 1) {
      const bool full_reset =
          attempt == config::kWifiConnectAttempts &&
          (s_failed_rounds % config::kWifiFullResetEveryRounds) ==
              config::kWifiFullResetEveryRounds - 1;
      Serial.printf("WiFi connect retry %u/%u%s\n", attempt,
                    config::kWifiConnectAttempts,
                    full_reset ? " (radio reset)" : "");
      if (full_reset) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
      } else {
        WiFi.disconnect();
      }
      delay(400);
    }

    startStaConnect(ssid, pass);

    if (waitForLinkWithUi(ui_ssid, config::kWifiConnectAttemptMs)) {
      s_failed_rounds = 0;
      return true;
    }
  }

  ++s_failed_rounds;
  return false;
}

bool connectSavedNetwork(bool show_ui) {
  String ssid;
  String pass;
  if (!readStoredWifiCredentials(&ssid, &pass)) {
    return false;
  }

  ensureWifiManager();
  return tryConnectWithUi(ssid, pass, show_ui);
}

bool openConfigPortal() {
  stopLanWebPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  statusScreenPortal(apPassword());
  s_wm.setConfigPortalBlocking(false);
  s_wm.startConfigPortal(config::kPortalApName, apPassword());
  while (s_wm.getConfigPortalActive()) {
    bootButtonPollLongPress();
    if (s_wm.process()) {
      return true;
    }
    delay(10);
  }
  return wifiLinkUp();
}

}  // namespace

bool wifiShowsSetupScreenOnBoot() {
  if (s_force_config_portal) {
    return true;
  }
  return forcePortalFlagSet();
}

const char* wifiSetupApPassword() { return apPassword(); }

bool wifiBootButtonPressed() {
  return digitalRead(config::kBootPin) == LOW;
}

void bootButtonInit() { initBootButton(); }

bool bootButtonConsumeTap() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_boot_tap_pending;
  if (tap) {
    s_boot_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

void bootButtonPollLongPress() {
  if (wifiBootButtonPressed()) {
    portENTER_CRITICAL(&s_boot_mux);
    if (!s_boot_is_down) {
      s_boot_is_down = true;
      s_boot_down_ms = millis();
    }
    const unsigned long down_ms = s_boot_down_ms;
    portEXIT_CRITICAL(&s_boot_mux);

    if (!s_long_press_handled &&
        millis() - down_ms >= config::kBootResetHoldMs) {
      s_long_press_handled = true;
      Serial.println("BOOT held — resetting WiFi");
      wifiResetCredentialsAndReboot();
    }
  } else {
    portENTER_CRITICAL(&s_boot_mux);
    s_boot_is_down = false;
    portEXIT_CRITICAL(&s_boot_mux);
    s_long_press_handled = false;
  }
}

bool wifiRejoinInProgress() {
  if (s_signal.rejoin_started_ms == 0) {
    return false;
  }
  // A full-scan rejoin plus DHCP takes 4-7 s; give up waiting after 15 s
  // and let the normal reconnect take over.
  if (wifiLinkUp() || millis() - s_signal.rejoin_started_ms > 15000UL) {
    s_signal.rejoin_started_ms = 0;
    return false;
  }
  return true;
}

void wifiCheckSignal() {
  if (!wifiLinkUp()) {
    s_signal.window.clear();
    return;
  }
  const unsigned long now = millis();
  if (now - s_signal.last_sample_ms < kSignalSampleMs) {
    return;
  }
  s_signal.last_sample_ms = now;
  const int rssi = WiFi.RSSI();
  const uint8_t* bssid = WiFi.BSSID();

  if (s_signal.awaiting_result && bssid != nullptr) {
    s_signal.awaiting_result = false;
    const bool moved = memcmp(bssid, s_signal.bssid_before, 6) != 0;
    services::security::logEvent(
        "Wi-Fi rejoin: %s, now %d dBm (was %d)",
        moved ? "moved access point" : "same access point", rssi,
        s_signal.rssi_before);
    // Nothing better in range: back off so a weak-everywhere spot doesn't
    // drop the link every 15 minutes.
    s_signal.rejoin_interval_ms =
        moved ? kRejoinMinIntervalMs
              : std::min(s_signal.rejoin_interval_ms * 2, kRejoinMaxIntervalMs);
  }

  s_signal.window.add(rssi);
  if (!s_signal.window.mostlyWeak() ||
      (s_signal.last_rejoin_ms != 0 &&
       now - s_signal.last_rejoin_ms < s_signal.rejoin_interval_ms) ||
      bssid == nullptr) {
    return;
  }
  const int weak = s_signal.window.weakCount();
  const int samples = s_signal.window.samples();
  s_signal.window.clear();
  s_signal.last_rejoin_ms = now;
  s_signal.awaiting_result = true;
  s_signal.rejoin_started_ms = now != 0 ? now : 1;
  s_signal.rssi_before = rssi;
  memcpy(s_signal.bssid_before, bssid, 6);
  services::security::logEvent("Wi-Fi weak (%d dBm, %d of last %d samples): "
                               "rejoining", rssi, weak, samples);
  preferStrongestAp();
  WiFi.reconnect();  // leave + full scan, strongest access point first
}

void wifiLeaveAndRestart() {
  if (WiFi.status() == WL_CONNECTED) {
    // Keep the saved network: disconnect only, no erase, radio stays on.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    const unsigned long started = millis();
    while (WiFi.status() == WL_CONNECTED && millis() - started < 300) {
      delay(10);
    }
    delay(50);  // let the frame leave the radio
    Serial.println("WiFi: left the network before restarting");
  }
  Serial.flush();
  esp_restart();
}

void wifiResetCredentialsAndReboot() {
  resetWifiCredentials();
  statusScreenWifiReset();
  delay(800);
  esp_restart();
}

bool wifiReconnect() {
  initBootButton();
  Serial.println("WiFi reconnecting...");
  return connectSavedNetwork(true);
}

void wifiPollDuringIo() { bootButtonPollLongPress(); }

void wifiLoop() {
  ensureWifiManager();
  // Restart a moment after a save that needs it, so the "restarting" page
  // reaches the browser first.
  if (s_restart_requested_ms != 0 && millis() - s_restart_requested_ms >= 1500) {
    Serial.println("Restarting to apply the new device name");
    wifiLeaveAndRestart();
  }
  if (wifiLinkUp()) {
    if (!s_wm.getWebPortalActive() && !s_wm.getConfigPortalActive()) {
      startLanWebPortal();
    }
    if (s_wm.getWebPortalActive() || s_wm.getConfigPortalActive()) {
      bootButtonPollLongPress();
      s_wm.process();
    }
  } else {
    stopLanWebPortal();
  }
}

bool wifiSetupConnect() {
  initBootButton();
  ensureWifiManager();
  // Also covers WiFiManager's own WiFi.begin(ssid, pass) after a portal save.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  const bool force_portal = consumeForceConfigPortal();
  WiFi.setAutoReconnect(false);

  if (force_portal) {
    eraseWifiCredentials();
    WiFi.mode(WIFI_OFF);
    delay(100);

    Serial.println("Opening WiFi setup portal (after reset)");
    if (openConfigPortal() && wifiLinkUp()) {
      WiFi.setAutoReconnect(true);
      Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("WiFi connection failed");
    statusScreenConnectFailed();
    return false;
  }

  Serial.println("Connecting to WiFi (portal opens if needed)...");

  if (wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials() && connectSavedNetwork(true)) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials()) {
    Serial.println("Saved WiFi could not connect — opening setup portal");
  } else {
    Serial.println("No saved WiFi — opening setup portal");
  }

  if (openConfigPortal() && wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("WiFi connection failed");
  statusScreenConnectFailed();
  return false;
}
