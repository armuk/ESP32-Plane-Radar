#include "services/ota_update.h"

#include <HTTPClient.h>
#include <Update.h>
#include <Updater_Signing.h>
#include <WiFiClientSecure.h>

#include <cstring>
#include <WiFiManager.h>
#include <esp_ota_ops.h>

#include <algorithm>
#include <iterator>

#include <cstdio>
#include <memory>
#include <new>

#include "config.h"
#include "version.h"
#include "services/display_settings.h"
#include "services/firmware_image.h"
#include "services/net_dns.h"
#include "services/portal_style.h"
#include "services/security.h"
#include "services/tls_trust.h"
#include "services/update_check.h"
#include "services/wifi_setup.h"
#include "ui/status_screens.h"

// Read back from uploaded images to refuse downgrades (firmware_image.h).
// `used` alone does not survive the linker's --gc-sections, so
// checkBootImage() also reads it; scripts/sign_firmware.py fails the build
// if the marker is missing from firmware.bin.
extern "C" __attribute__((used)) const char kPlaneRadarVersionMarker[] =
    "PLANE-RADAR-FW-VERSION=" PLANE_RADAR_VERSION;

// Public half of the update-signing key (keys/ota-signing-public.pem,
// embedded NUL-terminated with board_build.embed_txtfiles).
extern const uint8_t kSigningKeyPem[] asm(
    "_binary_keys_ota_signing_public_pem_start");
extern const uint8_t kSigningKeyPemEnd[] asm(
    "_binary_keys_ota_signing_public_pem_end");

namespace services::ota {
namespace {

constexpr char kFirmwarePage[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Plane Radar firmware</title>
%STYLE%</head><body><div class="wrap">
<h1>Firmware update</h1>
<p>Installed: <strong>%VERSION%</strong></p>
<p>Upload a <strong>signed</strong> OTA image: the release file ending in
<strong>-ota.bin</strong>, or PlatformIO's <strong>firmware-signed.bin</strong>.
Unsigned images and versions older than the installed one are refused.</p>
<form method="post" action="/firmware-upload" enctype="multipart/form-data"
onsubmit="var f=this.firmware.files[0];if(f){this.action='/firmware-upload?size='+f.size;}">
<input type="file" name="firmware" accept=".bin,application/octet-stream" required>
<button type="submit">Install and restart</button></form>
<p><small>Do not upload the merged/full flash image here. Keep power connected
until the device restarts.</small></p><p><a href="/">Back to settings</a></p>
</div></body></html>
)HTML";

WiFiManager* s_manager = nullptr;
unsigned long s_last_request_ms = 0;
AdditionalRoutesFn s_additional_routes = nullptr;
bool s_in_progress = false;
bool s_upload_authenticated = false;
String s_upload_error;
/** Last time upload data arrived; used to give up on a stalled upload. */
unsigned long s_last_upload_activity_ms = 0;
/** Drop a stalled upload after this long without data so the radar resumes. */
constexpr unsigned long kUploadStallTimeoutMs = 30000UL;

/**
 * WiFiManager settings and maintenance pages that must require the OTA
 * password while the device is on the home network. Without this, anyone on
 * the LAN (or any web page the user visits) could change the OTA password via
 * /paramsave and then upload firmware, or erase the saved Wi-Fi.
 */
constexpr const char* kProtectedPaths[] = {
    "/wifi",    "/0wifi",   "/wifisave", "/param", "/paramsave",
    "/info",    "/restart", "/exit",     "/close", "/erase",
    "/receiver", "/receiversave", "/status", "/status.json", "/security",
    "/backup",   "/backup.json",
    // Not /restore: it is an upload, which this gate cannot pass on; it
    // checks the login and the request's origin itself.
};

WebServer* server() {
  if (s_manager == nullptr || !s_manager->server) {
    return nullptr;
  }
  return s_manager->server.get();
}

// Paths that change something. Checked by path, not method: WiFiManager
// runs /restart, /exit and /close on a GET, and /wifisave accepts one.
constexpr const char* kStateChangingPaths[] = {
    "/wifisave", "/paramsave", "/receiversave", "/erase",
    "/restart",  "/exit",      "/close",
};

bool isStateChangingPath(const String& uri) {
  return std::any_of(std::begin(kStateChangingPaths),
                     std::end(kStateChangingPaths),
                     [&uri](const char* path) { return uri == path; });
}

bool requestAuthenticated() {
  WebServer* web = server();
  return web != nullptr && security::requireAdmin(*web);
}

/** Verifies the image signature at Update.end(); lives for the whole boot. */
UpdaterECDSAVerifier& signatureVerifier() {
  // Linker start/end symbols of one embedded file; the length includes the
  // NUL that embed_txtfiles appends, as the PEM parser requires.
  // cppcheck-suppress comparePointers
  static const size_t kPemLen = static_cast<size_t>(kSigningKeyPemEnd - kSigningKeyPem);
  static UpdaterECDSAVerifier verifier(kSigningKeyPem, kPemLen, HASH_SHA256);
  return verifier;
}

firmware_image::VersionScanner s_version_scanner;

void showFirmwarePage() {
  WebServer* web = server();
  if (web == nullptr || !requestAuthenticated()) {
    return;
  }
  String page(kFirmwarePage);
  page.replace("%STYLE%", portal::kStyleTag);
  page.replace("%VERSION%", config::kFirmwareVersion);
  web->send(200, "text/html", page);
}

bool isProtectedPath(const String& uri) {
  return std::any_of(std::begin(kProtectedPaths), std::end(kProtectedPaths),
                     [&uri](const char* path) { return uri == path; });
}

/**
 * Registered ahead of WiFiManager's routes. The ESP32 WebServer picks a
 * handler before request headers are parsed, so this claims every protected
 * path and checks the password in handle(); once authenticated it forwards
 * the request to the next matching handler (ours or WiFiManager's).
 *
 * On the setup hotspot the settings pages need no login: joining it takes
 * the WPA2 password shown on the device's own screen, and the user may not
 * know the settings password yet. Firmware uploads always need the login.
 */
class SettingsAuthGate : public RequestHandler {
 public:
  // Core 3.x WebServer calls the server-aware canHandle(); the plain overload
  // is kept so callers using the old API still get the same answer.
  bool canHandle(WebServer& web, HTTPMethod method,
                 const String& uri) override {
    (void)web;
    return canHandle(method, uri);
  }

  bool canHandle(HTTPMethod method, const String& uri) override {
    (void)method;
    // First handler, so it sees every request: note portal activity.
    s_last_request_ms = millis();
    return isProtectedPath(uri);
  }

  bool handle(WebServer& web, HTTPMethod method, const String& uri) override {
    const bool setup_ap =
        s_manager != nullptr && s_manager->getConfigPortalActive();
    if (!setup_ap) {
      if (!security::requireAdmin(web)) {
        return true;
      }
      // Logged-in browsers send the login with any request, including ones
      // another site makes them send; only our own pages may change things.
      // (/erase GET only shows the confirmation, but checking it is harmless.)
      if (isStateChangingPath(uri) && !security::requireSameOrigin(web)) {
        return true;
      }
    }
    for (RequestHandler* handler = next(); handler != nullptr;
         handler = handler->next()) {
      if (handler->canHandle(web, method, uri)) {
        // Slow handlers show up on serial (saves and pages normally take
        // 20-250 ms; waiting behind an HTTPS fetch is not counted here).
        const unsigned long started = millis();
        const bool handled = handler->handle(web, method, uri);
        const unsigned long took_ms = millis() - started;
        if (took_ms >= 500) {
          Serial.printf("web: %s took %lu ms\n", uri.c_str(), took_ms);
        }
        return handled;
      }
    }
    return false;
  }
};

void recordUpdateError() {
  if (Update.getError() == UPDATE_ERROR_SIGN) {
    s_upload_error = "Refused: the image is not signed with this device's "
                     "update key.";
  } else {
    s_upload_error = Update.errorString();
  }
  if (s_upload_error.length() == 0) {
    s_upload_error = "Unknown flash error";
  }
  Update.abort();
}

// --- Writing an image: shared by uploads and the Status page's Install ---

/**
 * A partition table cannot change over the air. An image built for larger
 * app slots (e.g. v2.1.1's layout on a v2.0.x device) does not fit; say how
 * to move to the new layout instead of "Not enough space". Empty if it fits.
 */
String slotSizeError(size_t size) {
  const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr || size <= target->size) {
    return String();
  }
  return "Refused: this image (" + String(static_cast<unsigned long>(size)) +
         " bytes) is larger than this device's update slot (" +
         String(static_cast<unsigned long>(target->size)) +
         " bytes). It needs the newer partition layout: flash the -full.bin "
         "over USB once (see the README, \"Partition layout\").";
}

/** Starts writing a size-byte signed image. False sets s_upload_error. */
bool beginImage(size_t size) {
  s_version_scanner.reset();
  if (!Update.installSignature(&signatureVerifier()) ||
      !Update.begin(size, U_FLASH)) {
    recordUpdateError();
    return false;
  }
  return true;
}

bool writeImage(const uint8_t* data, size_t len) {
  s_version_scanner.feed(data, len);
  if (Update.write(const_cast<uint8_t*>(data), len) != len) {
    recordUpdateError();
    return false;
  }
  return true;
}

/**
 * Refuses a downgrade (and, when expected_version is set, an image that
 * reports another version), then checks the signature over the whole image
 * and makes it the next boot image. False sets s_upload_error.
 */
bool finishImage(const char* expected_version) {
  const char* incoming =
      s_version_scanner.found() ? s_version_scanner.version() : "";
  // Before Update.end() can make it the boot image.
  if (firmware_image::isDowngrade(config::kFirmwareVersion, incoming)) {
    Update.abort();
    s_upload_error = String("Refused: ") + incoming +
                     " is older than the installed " + config::kFirmwareVersion +
                     ". Downgrade over USB if you really need it.";
    return false;
  }
  if (expected_version != nullptr && strcmp(incoming, expected_version) != 0) {
    Update.abort();
    s_upload_error = String("Refused: the downloaded image reports version ") +
                     (incoming[0] != '\0' ? incoming : "(none)") + ", not " +
                     expected_version + ".";
    return false;
  }
  // end() checks the signature over the whole image before activating it.
  if (!Update.end(true)) {
    recordUpdateError();
    return false;
  }
  security::logKeyEvent("update %s verified and installed",
                        incoming[0] != '\0' ? incoming : "(no version)");
  return true;
}

/** Key event for a refused or failed update, without the "Refused: ". */
void logRefusal(const char* reason) {
  if (strncmp(reason, "Refused: ", 9) == 0) {
    reason += 9;
  }
  security::logKeyEvent("update refused: %.56s", reason);
}

void handleUploadChunk() {
  WebServer* web = server();
  if (web == nullptr) {
    return;
  }
  HTTPUpload& upload = web->upload();

  if (upload.status == UPLOAD_FILE_START) {
    s_upload_authenticated = security::checkCredentials(*web);
    s_upload_error.clear();
    if (!s_upload_authenticated) {
      return;
    }
    if (!security::requireSameOrigin(*web, false)) {
      s_upload_error = "Blocked: the upload did not come from this device's "
                       "Firmware update page.";
      return;
    }
    s_in_progress = true;
    s_last_upload_activity_ms = millis();
    if (!upload.filename.endsWith(".bin")) {
      s_upload_error = "Please select an OTA .bin application image";
      return;
    }
    // The signature check needs the exact size up front: the page's script
    // adds it to the upload URL.
    const long size = web->arg("size").toInt();
    if (size <= 0) {
      s_upload_error =
          "The image size was not sent. Use the Firmware update page with "
          "JavaScript enabled.";
      return;
    }
    s_upload_error = slotSizeError(static_cast<size_t>(size));
    if (s_upload_error.length() != 0) {
      return;
    }

    statusScreenFirmwareUpdate();
    security::logEvent("update started from %s (%ld bytes)",
                       web->client().remoteIP().toString().c_str(), size);
    beginImage(static_cast<size_t>(size));
    return;
  }

  // The WebServer never calls handleUploadDone() after an abort (client gone,
  // read timeout), so clear the in-progress flag here or loop() stays paused.
  if (upload.status == UPLOAD_FILE_ABORTED) {
    if (s_in_progress) {
      Update.abort();
      Serial.println("OTA: upload aborted");
    }
    s_upload_error = "Upload aborted";
    s_in_progress = false;
    return;
  }

  if (!s_upload_authenticated || s_upload_error.length() != 0) {
    return;
  }

  s_last_upload_activity_ms = millis();
  if (upload.status == UPLOAD_FILE_WRITE) {
    writeImage(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    finishImage(nullptr);
  }
}

void handleUploadDone() {
  WebServer* web = server();
  if (web == nullptr) {
    return;
  }
  if (!s_upload_authenticated) {
    s_in_progress = false;
    security::requireAdmin(*web);  // 429 while locked out, else the challenge
    return;
  }

  if (s_upload_error.length() != 0 || Update.hasError()) {
    s_in_progress = false;
    const String message = portal::page(
        "Update failed", "",
        String("<h1>Update failed</h1><div class='msg D'>") +
            (s_upload_error.length() != 0 ? s_upload_error
                                          : String(Update.errorString())) +
            "</div><p><a href='/firmware'>Try again</a></p>");
    web->send(500, "text/html", message);
    logRefusal(s_upload_error.length() != 0 ? s_upload_error.c_str()
                                            : Update.errorString());
    return;
  }

  web->client().setNoDelay(true);
  web->send(200, "text/html",
            portal::page("Update installed", "",
                         "<h1>Update installed</h1><div class='msg S'>"
                         "Plane Radar is restarting...</div>"));
  delay(250);
  web->client().stop();
  wifiLeaveAndRestart();
}

// --- Install from the Status page ---
// The button only records which release to install; loop() then runs the
// download (runPendingInstall), because it takes about a minute and the web
// server is not served meanwhile.

/** Stop a download after this long without data. */
constexpr unsigned long kDownloadStallMs = 20000UL;
constexpr size_t kDownloadChunk = 2048;

char s_install_tag[32] = "";  // requested, not started yet
String s_install_error;       // last failed install, for the Status page

constexpr char kInstallingPage[] PROGMEM = R"HTML(<h1>Installing %TAG%</h1>
<div class='msg S' id='m'>Downloading and checking the update. This takes about
a minute, and the radar restarts when it is done. Keep power connected.</div>
<script>
function chk(){var c=new AbortController();setTimeout(function(){c.abort()},8000);
fetch('/status.json',{signal:c.signal,cache:'no-store'}).then(function(r){return r.json()})
.then(function(j){if(j.firmware.version=='%TAG%'||j.updates.install_error){location='/status';}
else{setTimeout(chk,5000);}}).catch(function(){setTimeout(chk,5000);});}
setTimeout(chk,10000);
setTimeout(function(){document.getElementById('m').innerHTML+=
' <a href="/status">Open Status</a>';},180000);
</script>)HTML";

void handleInstallRequest() {
  WebServer* web = server();
  // Needs the login even on the setup hotspot, like a firmware upload.
  if (web == nullptr || !requestAuthenticated() ||
      !security::requireSameOrigin(*web)) {
    return;
  }
  const String tag = web->arg("tag");
  if (!updates::updateAvailable() || tag != updates::latestTag()) {
    web->send(409, "text/html",
              portal::page("Update not offered", "",
                           "<h1>Update not offered</h1><div class='msg D'>That "
                           "release is no longer the one on offer.</div><p><a "
                           "href='/status'>Back to Status</a></p>"));
    return;
  }
  // latestTag() only holds [A-Za-z0-9.-_+], so it is safe in the page.
  snprintf(s_install_tag, sizeof(s_install_tag), "%s", updates::latestTag());
  s_install_error.clear();
  security::logEvent("install of %s requested from %s", s_install_tag,
                     web->client().remoteIP().toString().c_str());
  String body(kInstallingPage);
  body.replace("%TAG%", s_install_tag);
  web->send(200, "text/html", portal::page("Installing update", "", body));
}

/**
 * Downloads the release's signed -ota.bin straight into the update slot.
 * The signature, not the HTTPS connection, is what makes it safe: an image
 * not signed with this device's key is refused at the end, as are older
 * versions and images that report a version other than tag.
 */
bool downloadAndInstall(const char* tag) {
  char url[200];
  if (!updates::releaseAssetUrl(PLANE_RADAR_RELEASE_REPO, tag, url, sizeof(url))) {
    s_upload_error = "This build has no release download address.";
    return false;
  }
  if (!net::resolveQuickly(url, net::kDnsTimeoutMs, wifiPollDuringIo)) {
    s_upload_error = "Could not look up github.com.";
    return false;
  }
  WiFiClientSecure client;
  tls::Session tls(client, "release download");
  HTTPClient http;
  if (!http.begin(client, url)) {
    s_upload_error = "Could not start the download.";
    return false;
  }
  http.setConnectTimeout(kDownloadStallMs);
  http.setTimeout(kDownloadStallMs);
  http.useHTTP10(true);  // unchunked, with a Content-Length
  http.setReuse(false);
  // GitHub answers with a redirect to its download host.
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent(String("PlaneRadar/") + config::kFirmwareVersion);
  wifiPollDuringIo();
  const int code = http.GET();
  tls.setResult(code);
  if (code != HTTP_CODE_OK) {
    s_upload_error = code < 0 ? String("Download failed: ") +
                                    HTTPClient::errorToString(code)
                              : "Download failed: HTTP " + String(code);
    http.end();
    return false;
  }
  const int size = http.getSize();
  if (size <= 0) {
    s_upload_error = "Download failed: no image size.";
    http.end();
    return false;
  }
  s_upload_error = slotSizeError(static_cast<size_t>(size));
  if (s_upload_error.length() != 0) {
    http.end();
    return false;
  }
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kDownloadChunk]);
  if (!buf) {
    s_upload_error = "Not enough memory to download.";
    http.end();
    return false;
  }
  security::logEvent("download of %s started (%d bytes)", tag, size);
  if (!beginImage(static_cast<size_t>(size))) {
    http.end();
    return false;
  }
  WiFiClient* stream = http.getStreamPtr();
  const size_t total = static_cast<size_t>(size);
  size_t done = 0;
  int shown = -1;
  unsigned long last_data_ms = millis();
  const unsigned long started_ms = last_data_ms;
  while (done < total && stream != nullptr) {
    const size_t want = std::min(kDownloadChunk, total - done);
    const int n = stream->read(buf.get(), want);
    if (n > 0) {
      if (!writeImage(buf.get(), static_cast<size_t>(n))) {
        break;
      }
      done += static_cast<size_t>(n);
      last_data_ms = millis();
      const int percent = static_cast<int>(done * 100 / total);
      if (percent / 5 != shown) {
        shown = percent / 5;
        statusScreenFirmwareDownload(percent);
      }
      continue;
    }
    if (!stream->connected() && stream->available() <= 0) {
      break;
    }
    if (millis() - last_data_ms >= kDownloadStallMs) {
      break;
    }
    wifiPollDuringIo();
    delay(2);
  }
  http.end();
  Serial.printf("OTA: downloaded %u of %u bytes in %lu ms\n",
                static_cast<unsigned>(done), static_cast<unsigned>(total),
                millis() - started_ms);
  if (s_upload_error.length() != 0) {
    return false;  // writeImage() failed and aborted
  }
  if (done != total) {
    Update.abort();
    s_upload_error = "Download stopped after " + String(static_cast<unsigned>(done)) +
                     " of " + String(static_cast<unsigned>(total)) + " bytes.";
    return false;
  }
  return finishImage(tag);
}

void attachRoutes() {
  WebServer* web = server();
  if (web == nullptr) {
    return;
  }

  // Must be the first handler so it sees protected paths before anything
  // else. WebServer owns and deletes its handlers, and WiFiManager recreates
  // the server on each portal start, so allocate a fresh gate every time.
  web->addHandler(new SettingsAuthGate());
  security::collectRequestHeaders(*web);

  web->on("/firmware", HTTP_GET, showFirmwarePage);
  web->on("/firmware-upload", HTTP_POST, handleUploadDone,
          handleUploadChunk);
  web->on("/update-install", HTTP_POST, handleInstallRequest);

  // Claim WiFiManager's legacy unauthenticated paths before it registers them.
  web->on("/update", HTTP_GET, showFirmwarePage);
  web->on("/u", HTTP_POST, handleUploadDone, handleUploadChunk);

  if (s_additional_routes != nullptr) {
    s_additional_routes();
  }
}

}  // namespace

void configure(WiFiManager& manager, AdditionalRoutesFn additional_routes) {
  s_manager = &manager;
  s_additional_routes = additional_routes;
  manager.setShowInfoUpdate(false);
  // Erasing Wi-Fi lives on the Wi-Fi page now, behind a confirmation.
  manager.setShowInfoErase(false);
  // WiFiManager keeps these pointers, so the buffers are static.
  static char menu_html[160];
  snprintf(menu_html, sizeof(menu_html),
           "<form action='/firmware' method='get'><button>Firmware update"
           "</button></form><p><small>Firmware %s</small></p>\n",
           config::kFirmwareVersion);
  manager.setCustomMenuHTML(menu_html);
  // Dark theme on every page, plus the version row on /info. A pointer to
  // flash: WiFiManager only appends it to each page.
  manager.setCustomHeadElement(portal::kHeadElement);
  // "wifinoscan" opens straight away; the page's own button scans on
  // request (see portal_style.cpp).
  const char* menu[] = {"wifinoscan", "param", "info", "custom",
                        "sep",  "restart", "exit"};
  manager.setMenu(menu, sizeof(menu) / sizeof(menu[0]));
  manager.setWebServerCallback(attachRoutes);
}

unsigned long lastPortalRequestMs() { return s_last_request_ms; }

bool inProgress() {
  if (s_in_progress &&
      millis() - s_last_upload_activity_ms >= kUploadStallTimeoutMs) {
    // Safety net if no abort callback ever arrives (e.g. the connection hangs).
    Serial.println("OTA: upload stalled — resuming radar");
    Update.abort();
    s_upload_error = "Upload timed out";
    s_in_progress = false;
  }
  return s_in_progress;
}

bool installPending() { return s_install_tag[0] != '\0'; }

void runPendingInstall() {
  if (!installPending()) {
    return;
  }
  char tag[sizeof(s_install_tag)];
  memcpy(tag, s_install_tag, sizeof(tag));
  s_install_tag[0] = '\0';
  s_upload_error.clear();
  Serial.printf("OTA: installing %s from the release\n", tag);
  statusScreenFirmwareDownload(0);
  if (downloadAndInstall(tag)) {
    Serial.println("OTA: installed, restarting");
    delay(250);
    wifiLeaveAndRestart();
  }
  if (s_upload_error.length() == 0) {
    s_upload_error = Update.errorString();
  }
  s_install_error = String(tag) + ": " + s_upload_error;
  Serial.printf("OTA: install failed: %s\n", s_install_error.c_str());
  logRefusal(s_upload_error.c_str());
}

const String& installError() { return s_install_error; }

// --- Rollback after a bad update ---
// The bootloader is built with rollback enabled. A new image boots in
// PENDING_VERIFY; if the device resets before it is marked valid, the
// bootloader goes back to the previous image.

namespace {
bool s_boot_pending_verify = false;
bool s_boot_confirmed = false;
String s_rollback_note;
}  // namespace

void checkBootImage() {
  // A real read, so the marker stays in the image (see its definition).
  Serial.printf("Firmware marker: %s\n",
                static_cast<const char*>(kPlaneRadarVersionMarker));
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (running != nullptr &&
      esp_ota_get_state_partition(running, &state) == ESP_OK &&
      state == ESP_OTA_IMG_PENDING_VERIFY) {
    s_boot_pending_verify = true;
    Serial.printf("OTA: new firmware on %s, confirmed after the first "
                  "aircraft fetch\n",
                  running->label);
  }
  const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
  if (invalid != nullptr) {
    esp_app_desc_t desc = {};
    String version = "unknown version";
    if (esp_ota_get_partition_description(invalid, &desc) == ESP_OK) {
      version = desc.version;
    }
    s_rollback_note = "An update (" + version + ") on " + invalid->label +
                      " failed its first run and was rolled back.";
    security::logKeyEventOnce("rolled back: %s on %s failed its first run",
                       version.c_str(), invalid->label);
  }
}

void confirmRunningFirmware() {
  if (!s_boot_pending_verify || s_boot_confirmed) {
    return;
  }
  s_boot_confirmed = true;
  const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
  security::logKeyEvent("new firmware %s confirmed (%s)", config::kFirmwareVersion,
                     esp_err_to_name(err));
}

const String& rollbackNote() { return s_rollback_note; }

bool awaitingConfirmation() { return s_boot_pending_verify && !s_boot_confirmed; }

}  // namespace services::ota

/**
 * Overrides the core's weak default (which marks every image valid at boot)
 * so a new image stays in PENDING_VERIFY until confirmRunningFirmware().
 */
extern "C" bool verifyRollbackLater() { return true; }
