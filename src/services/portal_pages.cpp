#include "services/portal_pages.h"

#include <ArduinoJson.h>
#include <Esp.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include <esp_core_dump.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>

#include <ctime>
#include <memory>
#include <new>

#include "config.h"
#include "services/adsb_client.h"
#include "services/display_settings.h"
#include "services/ota_update.h"
#include "services/portal_style.h"
#include "services/settings_backup.h"
#include "services/security.h"
#include "services/tls_trust.h"
#include "services/update_check.h"
#include "services/wifi_setup.h"
#include "version.h"

extern const uint8_t kStatusSigningKeyPem[] asm(
    "_binary_keys_ota_signing_public_pem_start");
extern const uint8_t kStatusSigningKeyPemEnd[] asm(
    "_binary_keys_ota_signing_public_pem_end");

namespace services::portal {
namespace {

WebServer* s_web = nullptr;
EraseWifiFn s_erase_wifi = nullptr;
unsigned long s_startup_wifi_ms = 0;
unsigned long s_startup_data_ms = 0;

/** Seconds with one decimal ("6.4"), or null when not known yet. */
void setSeconds(JsonObject obj, const char* key, unsigned long ms) {
  if (ms == 0) {
    obj[key] = nullptr;
    return;
  }
  obj[key] = serialized(String(ms / 1000UL) + "." + String((ms % 1000UL) / 100UL));
}

const char* checkedAttr(bool on) { return on ? " checked" : ""; }

// --- Receiver (sub-page of Setup) ---

void showReceiverPage() {
  char status[200];
  services::adsb::formatReceiverStatus(status, sizeof(status));
  String body;
  body.reserve(2048);
  body += F("<h1>Receiver</h1>"
            "<p><small>Read aircraft from your own ADS-B receiver (readsb, "
            "tar1090, dump1090-fa or ultrafeeder) on your network instead of "
            "adsb.fi. A plain <code>http://</code> address needs no secure "
            "connection, so the radar has about 30 KB more memory free while "
            "it fetches. Route lookups and weather still use the internet."
            "</small></p>"
            "<form method='POST' action='/receiversave'>"
            "<input type='checkbox' id='rx_on' name='rx_on' value='T'");
  body += checkedAttr(services::settings::receiverEnabled());
  body += F("><label for='rx_on'>Use my receiver</label><br/><br/>"
            "<label for='rx_url'>Receiver address</label>"
            "<input id='rx_url' name='rx_url' maxlength='120' "
            "placeholder='192.168.1.50:8080' autocapitalize='off' "
            "spellcheck='false' value='");
  // Stored URLs never contain quotes or angle brackets (validated on save).
  body += services::settings::receiverUrl();
  body += F("'><div class='hint'>Address or name, with the port if it isn't "
            "80. Without a path, <code>/data/aircraft.json</code> is added. "
            "For tar1090 use e.g. "
            "<code>http://pi.local/tar1090/data/aircraft.json</code>."
            "</div>"
            "<input type='checkbox' id='rx_fb' name='rx_fb' value='T'");
  body += checkedAttr(services::settings::receiverFallback());
  body += F("><label for='rx_fb'>Use adsb.fi when the receiver can't be "
            "reached</label><div class='hint'>After 3 failed reads in a row, "
            "for 5 minutes at a time.</div>"
            "<div class='msg P'><strong>Status:</strong> ");
  body += status;
  body += F("</div><button type='submit'>Save</button></form>"
            "<p><a href='/param'>&lsaquo; Back to Setup</a></p>");
  s_web->send(200, "text/html", page("Receiver", "", body));
}

void handleReceiverSave() {
  const bool ok = services::settings::saveReceiverFromPortal(
      s_web->arg("rx_on").c_str(), s_web->arg("rx_url").c_str(),
      s_web->arg("rx_fb").c_str());
  String body = F("<h1>Saved</h1>");
  if (ok) {
    body += F("<div class='msg S'><small>Receiver settings saved. They apply "
              "from the next aircraft fetch.</small></div>");
  } else {
    body += F("<div class='msg D'><small>That address isn't valid (use "
              "http:// or https://, no spaces), so the previous one was "
              "kept. The checkboxes were saved.</small></div>");
  }
  body += F("<p><a href='/receiver'>Back to Receiver</a></p>");
  s_web->send(200, "text/html",
              page("Receiver saved",
                   ok ? "<meta http-equiv='refresh' content='3;url=/receiver'>"
                      : "",
                   body));
}

// --- About ---

void showAboutPage() {
  String body;
  body.reserve(2600);
  body += F("<h1>About</h1><dl><dt>Plane Radar firmware</dt><dd>");
  body += config::kFirmwareVersion;
  body += F("</dd>");
  if (PLANE_RADAR_RELEASE_REPO[0] != '\0') {
    // Fixed at build time to owner/name characters (firmware_version.py).
    body += F("<dt>Source</dt><dd><a href='https://github.com/");
    body += PLANE_RADAR_RELEASE_REPO;
    body += F("'>");
    body += PLANE_RADAR_RELEASE_REPO;
    body += F("</a></dd>");
  }
  body += F("<dt>Based on</dt><dd><a href='https://github.com/ironicbadger/"
            "ESP32-Plane-Radar'>ironicbadger/ESP32-Plane-Radar</a>, a fork "
            "of <a href='https://github.com/MatixYo/ESP32-Plane-Radar'>"
            "MatixYo/ESP32-Plane-Radar</a></dd>"
            "<dt>Arduino-ESP32 core</dt><dd>");
  body += String(ESP_ARDUINO_VERSION_MAJOR) + "." +
          String(ESP_ARDUINO_VERSION_MINOR) + "." +
          String(ESP_ARDUINO_VERSION_PATCH);
  body += F("</dd><dt>ESP-IDF</dt><dd>");
  body += ESP.getSdkVersion();
  body += F("</dd><dt>Device health</dt><dd><a href='/status'>Status</a> "
            "&middot; <a href='/security'>Security log</a> (login needed)"
            "</dd>");
  if (services::ota::rollbackNote().length() != 0) {
    body += F("<dt>Last update</dt><dd>");
    body += services::ota::rollbackNote();
    body += F("</dd>");
  }
  body += F("</dl>"
            "<h3>Libraries</h3><dl>"
            "<dt><a href='https://github.com/tzapu/WiFiManager'>WiFiManager"
            "</a> 2.0.17</dt><dd>Wi-Fi setup and this portal. MIT.</dd>"
            "<dt><a href='https://github.com/lovyan03/LovyanGFX'>LovyanGFX"
            "</a> 1.2.36</dt><dd>Display driver and drawing. FreeBSD.</dd>"
            "<dt><a href='https://arduinojson.org'>ArduinoJson</a> 7.4.3</dt>"
            "<dd>Reading the aircraft, route and weather data. MIT.</dd>"
            "<dt><a href='https://fonts.google.com/noto/specimen/Noto+Sans'>"
            "Noto Sans</a></dt><dd>Screen font. SIL Open Font License.</dd>"
            "</dl>"
            "<h3>Data</h3><dl>"
            "<dt><a href='https://adsb.fi'>adsb.fi</a></dt>"
            "<dd>Aircraft positions (open data API).</dd>"
            "<dt><a href='https://www.adsbdb.com'>ADSBDB</a></dt>"
            "<dd>Routes, airlines and aircraft types.</dd>"
            "<dt><a href='https://open-meteo.com'>Open-Meteo</a></dt>"
            "<dd>Weather and automatic time zone. CC BY 4.0.</dd>"
            "<dt><a href='https://ourairports.com/data/'>OurAirports</a></dt>"
            "<dd>Airports and runways. Public domain.</dd>"
            "<dt><a href='https://www.ntppool.org'>NTP Pool</a></dt>"
            "<dd>Clock (or the time server set in Setup).</dd></dl>");
  s_web->send(200, "text/html", page("About", "", body));
}


// --- Device health (NCSC device principle 5) ---

const char* resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "power on";
    case ESP_RST_EXT: return "reset pin";
    case ESP_RST_SW: return "software restart";
    case ESP_RST_PANIC: return "crash (panic)";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    case ESP_RST_EFUSE: return "eFuse error";
    case ESP_RST_PWR_GLITCH: return "power glitch";
    case ESP_RST_CPU_LOCKUP: return "CPU lockup";
    default: return "unknown";
  }
}

/** First 16 hex digits of the SHA-256 of the embedded public key (PEM). */
String signingKeyId() {
  uint8_t digest[32];
  // cppcheck-suppress comparePointers
  const size_t len = static_cast<size_t>(kStatusSigningKeyPemEnd -
                                         kStatusSigningKeyPem);
  // The embedded text ends with a NUL that is not part of the PEM file.
  mbedtls_sha256(kStatusSigningKeyPem, len > 0 ? len - 1 : 0, digest, 0);
  char hex[17];
  for (int i = 0; i < 8; ++i) {
    snprintf(hex + i * 2, 3, "%02x", digest[i]);
  }
  return String(hex);
}

const char* tlsPolicyText() {
#if PLANE_RADAR_TLS_VERIFY == 1
  return "checked (built-in root CA bundle)";
#elif PLANE_RADAR_TLS_VERIFY == 2
  return "measurement build (alternating)";
#else
  return "not checked";
#endif
}

void fillStatus(JsonDocument& doc) {
  JsonObject fw = doc["firmware"].to<JsonObject>();
  fw["version"] = config::kFirmwareVersion;
  fw["built"] = __DATE__ " " __TIME__;
  fw["arduino_core"] = String(ESP_ARDUINO_VERSION_MAJOR) + "." +
                       String(ESP_ARDUINO_VERSION_MINOR) + "." +
                       String(ESP_ARDUINO_VERSION_PATCH);
  fw["esp_idf"] = ESP.getSdkVersion();

  JsonObject run = doc["running"].to<JsonObject>();
  run["uptime_s"] = millis() / 1000UL;
  run["boot"] = security::bootNumber();
  run["reset_reason"] = resetReasonText(esp_reset_reason());
  run["crash_dump_stored"] = esp_core_dump_image_check() == ESP_OK;

  JsonObject startup = doc["startup"].to<JsonObject>();
  setSeconds(startup, "wifi_joined_s", s_startup_wifi_ms);
  setSeconds(startup, "first_data_s", s_startup_data_ms);

  JsonObject upd = doc["updates"].to<JsonObject>();
  upd["signed_only"] = true;
  upd["signing_key_id"] = signingKeyId();
  upd["downgrades_refused"] = true;
  upd["awaiting_confirmation"] = services::ota::awaitingConfirmation();
  upd["last_rollback"] = services::ota::rollbackNote();
  if (services::ota::installError().length() != 0) {
    upd["install_error"] = services::ota::installError();
  }
  if (services::updates::enabled()) {
    const char* latest = services::updates::latestTag();
    upd["latest_release"] = latest[0] != '\0' ? latest : nullptr;
    upd["update_available"] = services::updates::updateAvailable();
    const long age_s = services::updates::lastCheckAgeS();
    if (age_s >= 0) {
      upd["checked_s_ago"] = age_s;
    } else {
      upd["checked_s_ago"] = nullptr;
    }
  } else {
    upd["release_check"] = "off in this build";
  }

  JsonObject sec = doc["security"].to<JsonObject>();
  sec["password"] = services::settings::otaPasswordGenerated()
                        ? "generated on this device"
                        : "set by the user";
  sec["login"] = "HTTP Digest";
  sec["failed_logins_since_boot"] = security::failedLoginsTotal();
  sec["lockout_remaining_s"] = security::lockoutRemainingS();
  sec["https_certificates"] = tlsPolicyText();
  sec["secure_boot"] = false;
  sec["flash_encryption"] = false;

  JsonObject heap = doc["heap"].to<JsonObject>();
  heap["free"] = ESP.getFreeHeap();
  heap["min_free"] = ESP.getMinFreeHeap();
  heap["largest_block"] = ESP.getMaxAllocHeap();

  JsonObject wifi = doc["wifi"].to<JsonObject>();
  wifi["ssid"] = WiFi.SSID();
  wifi["rssi_dbm"] = WiFi.RSSI();
  wifi["ip"] = WiFi.localIP().toString();

  JsonObject data = doc["aircraft_data"].to<JsonObject>();
  const unsigned long last_ok = services::adsb::lastSuccessMs();
  if (last_ok != 0) {
    data["last_success_s_ago"] = (millis() - last_ok) / 1000UL;
  } else {
    data["last_success_s_ago"] = nullptr;
  }
  data["stale"] = services::adsb::dataStale();
}

void htmlEscapeInto(String& out, const String& text) {
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '&') out += F("&amp;");
    else if (c == '\'') out += F("&#39;");
    else if (c == '"') out += F("&quot;");
    else out += c;
  }
}

void showStatusJson() {
  JsonDocument doc;
  fillStatus(doc);
  String out;
  serializeJsonPretty(doc, out);
  s_web->send(200, "application/json", out);
}

void showStatusPage() {
  JsonDocument doc;
  fillStatus(doc);
  String body;
  body.reserve(2600);
  body += F("<h1>Status</h1>");
  if (services::updates::updateAvailable()) {
    body += F("<div class='msg S'><strong>Update available: ");
    htmlEscapeInto(body, services::updates::latestTag());
    body += F("</strong> (running ");
    htmlEscapeInto(body, config::kFirmwareVersion);
    body += F(").");
    if (services::updates::releasePage()[0] != '\0') {
      body += F(" <a href='");
      body += services::updates::releasePage();  // checked: no quotes or <>
      body += F("' target='_blank' rel='noopener'>Release notes and "
                "download</a>.");
    }
    // The tag only holds [A-Za-z0-9.-_+] (update_check.cpp).
    const char* tag = services::updates::latestTag();
    body += F("<form method='post' action='/update-install' "
              "onsubmit=\"return confirm('Download and install ");
    body += tag;
    body += F(", then restart?')\"><input type='hidden' name='tag' value='");
    body += tag;
    body += F("'><button type='submit'>Install ");
    body += tag;
    body += F("</button></form><small>The radar downloads the signed "
              "<em>-ota.bin</em> from the release and checks it like an "
              "upload on the <a href='/firmware'>Firmware update</a> page.</small>"
              "</div>");
  }
  if (services::ota::installError().length() != 0) {
    body += F("<div class='msg D'><strong>Install failed:</strong> ");
    htmlEscapeInto(body, services::ota::installError());
    body += F("</div>");
  }
  for (JsonPair section : doc.as<JsonObject>()) {
    body += F("<h3>");
    body += section.key().c_str();
    body += F("</h3><dl>");
    for (JsonPair item : section.value().as<JsonObject>()) {
      body += F("<dt>");
      body += item.key().c_str();
      body += F("</dt><dd>");
      String value;
      if (item.value().isNull()) {
        value = "-";
      } else if (item.value().is<bool>()) {
        value = item.value().as<bool>() ? "yes" : "no";
      } else {
        serializeJson(item.value(), value);
        value.replace("\"", "");
      }
      htmlEscapeInto(body, value);
      body += F("</dd>");
    }
    body += F("</dl>");
  }
  body += F("<p><a href='/status.json'>As JSON</a> &middot; "
            "<a href='/security'>Security log</a> &middot; "
            "<a href='/backup'>Backup &amp; restore</a> &middot; "
            "<a href='/'>Back</a></p>");
  s_web->send(200, "text/html", page("Status", "", body));
}

void appendEvent(String& body, const security::Event& e) {
  char when[40];
  if (e.epoch != 0) {
    const time_t t = static_cast<time_t>(e.epoch);
    struct tm utc = {};
    gmtime_r(&t, &utc);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S UTC", &utc);
  } else {
    // Before the clock was set: only the boot and the seconds since it.
    snprintf(when, sizeof(when), "boot %u, +%lus",
             static_cast<unsigned>(e.boot),
             static_cast<unsigned long>(e.uptime_s));
  }
  body += F("<dt>");
  body += when;
  body += F("</dt><dd>");
  htmlEscapeInto(body, String(e.text));
  body += F("</dd>");
}

void showSecurityLog() {
  String body;
  body.reserve(4200);
  body += F("<h1>Security log</h1><p><small>Newest first.</small></p>"
            "<h3>Since the last power-on</h3><p><small>Logins, settings "
            "changes, updates, resets and restarts. Kept across restarts, "
            "cleared by a power cut.</small></p><dl>");
  const size_t n = security::eventCount();
  if (n == 0) {
    body += F("<dd>No events yet.</dd>");
  }
  for (size_t i = n; i-- > 0;) {
    const security::Event* e = security::eventAt(i);
    if (e != nullptr) {
      appendEvent(body, *e);
    }
  }
  body += F("</dl><h3>Kept through power cuts</h3><p><small>Updates, "
            "rollbacks, password changes, lockouts, blocked cross-site "
            "requests, Wi-Fi erased, factory reset and settings restored: "
            "the newest ");
  body += static_cast<unsigned>(security::kMaxKeptEvents);
  body += F(". Boot numbers restart after a power cut.</small></p><dl>");
  // On the heap: about 1.4 KB, more than a web handler should put on the
  // loop task's stack.
  std::unique_ptr<security::Event[]> kept(
      new (std::nothrow) security::Event[security::kMaxKeptEvents]);
  const size_t kept_n =
      kept ? security::loadKeptEvents(kept.get(), security::kMaxKeptEvents) : 0;
  if (kept_n == 0) {
    body += F("<dd>No events yet.</dd>");
  }
  for (size_t i = kept_n; i-- > 0;) {
    appendEvent(body, kept[i]);
  }
  body += F("</dl><p><a href='/status'>Status</a> &middot; "
            "<a href='/'>Back</a></p>");
  s_web->send(200, "text/html", page("Security log", "", body));
}

// --- Settings backup and restore ---

void showBackupPage() {
  String body = F(
      "<h1>Backup &amp; restore</h1>"
      "<p>The backup holds this radar's settings: location, display and "
      "data choices, device name and receiver. It does <strong>not</strong> "
      "hold the settings password or the Wi-Fi network. It does hold your "
      "location, so keep the file private.</p>"
      "<form action='/backup.json' method='get'><button>Download backup"
      "</button></form>"
      "<h3>Restore</h3><p>Choose a backup file. The radar writes its "
      "settings and restarts; the settings password stays as it is.</p>"
      "<form method='POST' action='/restore' enctype='multipart/form-data'>"
      "<input type='file' name='backup' accept='.json,application/json'>"
      "<button class='D' type='submit'>Restore and restart</button></form>"
      "<p><a href='/status'>Status</a> &middot; <a href='/'>Back</a></p>");
  s_web->send(200, "text/html", page("Backup", "", body));
}

void sendBackup() {
  JsonDocument doc;
  services::backup::exportSettings(doc);
  String out;
  serializeJsonPretty(doc, out);
  String name = "attachment; filename=\"";
  name += services::settings::deviceName();
  name += "-settings.json\"";
  s_web->sendHeader("Content-Disposition", name);
  s_web->sendHeader("Cache-Control", "no-store");
  s_web->send(200, "application/json", out);
}

bool s_restore_authenticated = false;
String s_restore_error;
String s_restore_body;

void handleRestoreChunk() {
  HTTPUpload& upload = s_web->upload();
  if (upload.status == UPLOAD_FILE_START) {
    s_restore_body = String();
    s_restore_error = String();
    // Counted once here; the reply in handleRestoreDone does not count again.
    s_restore_authenticated = security::checkCredentials(*s_web);
    if (s_restore_authenticated && !security::requireSameOrigin(*s_web, false)) {
      s_restore_error = F("Blocked: the upload did not come from this "
                          "device's Backup page.");
    }
    return;
  }
  if (!s_restore_authenticated || s_restore_error.length() != 0) {
    return;
  }
  if (upload.status == UPLOAD_FILE_WRITE) {
    if (s_restore_body.length() + upload.currentSize >
        services::backup::kMaxRestoreBytes) {
      s_restore_error = F("The file is too large for a settings backup.");
      s_restore_body = String();
      return;
    }
    s_restore_body.concat(reinterpret_cast<const char*>(upload.buf),
                          upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    s_restore_error = F("Upload aborted.");
    s_restore_body = String();
  }
}

void restoreFailed(const String& reason) {
  String body = F("<h1>Restore failed</h1><div class='msg D'>");
  htmlEscapeInto(body, reason);
  body += F("</div><p>Nothing was changed.</p>"
            "<p><a href='/backup'>Try again</a></p>");
  s_web->send(400, "text/html", page("Restore failed", "", body));
}

void handleRestoreDone() {
  if (!s_restore_authenticated) {
    s_restore_body = String();
    security::requireAdmin(*s_web);  // 429 while locked out, else the challenge
    return;
  }
  if (s_restore_error.length() != 0) {
    restoreFailed(s_restore_error);
    return;
  }
  if (s_restore_body.length() == 0) {
    restoreFailed(F("No file was chosen."));
    return;
  }
  JsonDocument doc;
  const DeserializationError parse_error = deserializeJson(doc, s_restore_body);
  s_restore_body = String();
  if (parse_error) {
    restoreFailed(String(F("The file is not valid JSON (")) +
                  parse_error.c_str() + ")");
    return;
  }
  const char* error = nullptr;
  const int written = services::backup::importSettings(doc, error);
  if (written < 0) {
    restoreFailed(String(F("The file was refused: ")) + error);
    return;
  }
  security::logKeyEvent("settings restored from %s (%d values)",
                        s_web->client().remoteIP().toString().c_str(), written);
  s_web->send(200, "text/html",
              page("Restored",
                   "<meta http-equiv='refresh' content='15;url=/'>",
                   "<h1>Settings restored</h1><div class='msg S'>Restarting "
                   "with the restored settings. Back in about 15 seconds..."
                   "</div>"));
  s_web->client().flush();
  delay(300);
  wifiLeaveAndRestart();
}

// --- Forget Wi-Fi (replaces WiFiManager's one-click /erase) ---

void handleErase() {
  if (s_web->method() != HTTP_POST || s_web->arg("confirm") != "yes") {
    String body = F("<h1>Forget Wi-Fi</h1><div class='msg D'>This erases "
                    "the saved Wi-Fi network and restarts the radar into its "
                    "setup hotspot, <strong>");
    body += config::kPortalApName;
    body += F("</strong>. Join that hotspot to choose a network again. "
              "Location and other settings are kept.</div>"
              "<form method='POST' action='/erase'>"
              "<input type='hidden' name='confirm' value='yes'>"
              "<button class='D' type='submit'>Erase Wi-Fi and restart"
              "</button></form><p><a href='/0wifi'>Cancel</a></p>");
    s_web->send(200, "text/html", page("Forget Wi-Fi", "", body));
    return;
  }
  String body = F("<h1>Wi-Fi erased</h1><div class='msg S'>Restarting into "
                  "the setup hotspot <strong>");
  body += config::kPortalApName;
  body += F("</strong>...</div>");
  s_web->send(200, "text/html", page("Wi-Fi erased", "", body));
  s_web->client().flush();
  delay(300);
  if (s_erase_wifi != nullptr) {
    s_erase_wifi();
  }
}

}  // namespace

void noteStartup(unsigned long wifi_ms, unsigned long first_data_ms) {
  s_startup_wifi_ms = wifi_ms;
  s_startup_data_ms = first_data_ms;
}

void attachPages(WebServer& web, EraseWifiFn erase_wifi) {
  s_web = &web;
  s_erase_wifi = erase_wifi;
  web.on("/receiver", HTTP_GET, showReceiverPage);
  web.on("/receiversave", HTTP_POST, handleReceiverSave);
  web.on("/about", HTTP_GET, showAboutPage);
  web.on("/status", HTTP_GET, showStatusPage);
  web.on("/status.json", HTTP_GET, showStatusJson);
  web.on("/security", HTTP_GET, showSecurityLog);
  web.on("/erase", handleErase);
  web.on("/backup", HTTP_GET, showBackupPage);
  web.on("/backup.json", HTTP_GET, sendBackup);
  web.on("/restore", HTTP_POST, handleRestoreDone, handleRestoreChunk);
}

}  // namespace services::portal
