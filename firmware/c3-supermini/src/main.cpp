/**
 * Plane Radar — WiFi setup, then radar UI on the round GC9A01 display.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>

#include "config.h"
#include "version.h"
#include "hardware/display.h"
#include "services/adsb_client.h"
#include "services/display_settings.h"
#include "services/local_airports.h"
#include "services/ota_update.h"
#include "services/portal_pages.h"
#include "services/radar_location.h"
#include "services/net_dns.h"
#include "services/security.h"
#include "services/settings_backup.h"
#include "services/update_check.h"
#include "services/weather_time.h"
#include "services/wifi_setup.h"
#include "ui/radar_display.h"
#include "ui/radar_projection.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

bool g_radar_visible = false;
unsigned long g_wifi_down_since = 0;
unsigned long g_last_reconnect_ms = 0;
unsigned long g_last_adsb_fetch_ms = 0;
/** When Wi-Fi last came up (baseline for the no-data restart). */
unsigned long g_wifi_up_since = 0;
unsigned long g_boot_wifi_ms = 0;  // when Wi-Fi first came up this boot
/** When a BOOT tap woke the screen during the night-off hours (0 = not). */
unsigned long g_night_wake_ms = 0;
constexpr unsigned long kNightWakeMs = 60UL * 1000UL;

// --- Self-recovery restart reason, kept across the software restart ---
// RTC_NOINIT memory survives ESP.restart() but not power-off; the magic
// value tells real data from power-on garbage.
enum class RestartReason : uint32_t { kNone = 0, kNoData = 1, kLowHeap = 2 };
constexpr uint32_t kRestartMagic = 0x52414452;  // "RADR"
RTC_NOINIT_ATTR uint32_t g_restart_magic;
RTC_NOINIT_ATTR uint32_t g_restart_reason;
/**
 * Consecutive no-data restarts without a successful fetch in between. Each
 * one doubles the wait (10, 20, 40, 80 min) so a long adsb.fi outage doesn't
 * reboot the device every 10 minutes.
 */
RTC_NOINIT_ATTR uint32_t g_nodata_restarts;
constexpr uint32_t kMaxNoDataBackoffShift = 3;

const char* restartReasonText(uint32_t reason) {
  switch (static_cast<RestartReason>(reason)) {
    case RestartReason::kNoData:
      return "no aircraft data while Wi-Fi was up";
    case RestartReason::kLowHeap:
      return "largest free heap block too small";
    default:
      return "unknown";
  }
}

void logBootReason() {
  Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
  if (g_restart_magic == kRestartMagic) {
    services::security::logEvent("boot %u after self-recovery restart: %s",
                                 services::security::bootNumber(),
                                 restartReasonText(g_restart_reason));
  } else {
    services::security::logEvent(
        "boot %u, firmware %s, reset reason %d",
        services::security::bootNumber(), config::kFirmwareVersion,
        static_cast<int>(esp_reset_reason()));
    g_nodata_restarts = 0;  // power-on or other reset: RTC memory is garbage
  }
  g_restart_magic = 0;
  g_restart_reason = 0;
}

[[noreturn]] void selfRecoveryRestart(RestartReason reason) {
  g_restart_magic = kRestartMagic;
  g_restart_reason = static_cast<uint32_t>(reason);
  if (reason == RestartReason::kNoData) {
    ++g_nodata_restarts;
  }
  services::security::logEvent(
      "self-recovery restart: %s",
      restartReasonText(static_cast<uint32_t>(reason)));
  wifiLeaveAndRestart();
}

/**
 * Once per boot: how long until the first aircraft data, so a slow start
 * (e.g. joined a distant access point, DNS failing) shows in the security
 * log without a USB cable.
 */
void logFirstData() {
  static bool logged = false;
  if (logged) {
    return;
  }
  logged = true;
  const unsigned long first_ms = services::adsb::lastSuccessMs();
  const unsigned long wifi_ms = g_boot_wifi_ms != 0 ? g_boot_wifi_ms : first_ms;
  services::portal::noteStartup(wifi_ms, first_ms);
  services::security::logEvent(
      "first data %lu.%lu s after boot (Wi-Fi %lu.%lu s, %d dBm, %lu DNS skips)",
      first_ms / 1000, (first_ms % 1000) / 100, wifi_ms / 1000,
      (wifi_ms % 1000) / 100, static_cast<int>(WiFi.RSSI()),
      services::net::skippedLookups());
}

/** Restart if Wi-Fi has been up but no aircraft fetch has worked for ages. */
void checkNoDataRestart() {
  if (config::kNoDataRestartMs == 0 || g_wifi_up_since == 0) {
    return;
  }
  const unsigned long last_ok = services::adsb::lastSuccessMs();
  if (last_ok != 0) {
    g_nodata_restarts = 0;  // data flowed since the last restart
  }
  const uint32_t shift = std::min(g_nodata_restarts, kMaxNoDataBackoffShift);
  const unsigned long limit_ms = config::kNoDataRestartMs << shift;
  // Measure from whichever is later: Wi-Fi coming up or the last success.
  const unsigned long since_up = millis() - g_wifi_up_since;
  const unsigned long since_ok =
      last_ok != 0 ? millis() - last_ok : since_up;
  if (since_up >= limit_ms && since_ok >= limit_ms) {
    selfRecoveryRestart(RestartReason::kNoData);
  }
}

/**
 * Called once per heap report. Restarts when the largest free block has
 * been too small for a TLS connection for several reports in a row.
 */
void checkLowHeapRestart(uint32_t largest_free_block) {
  static uint8_t low_checks = 0;
  if (largest_free_block >= config::kMinLargestFreeBlockBytes) {
    low_checks = 0;
    return;
  }
  ++low_checks;
  Serial.printf("heap: largest block below %u (%u/%u)\n",
                static_cast<unsigned>(config::kMinLargestFreeBlockBytes),
                static_cast<unsigned>(low_checks),
                static_cast<unsigned>(config::kLowHeapChecksBeforeRestart));
  if (low_checks >= config::kLowHeapChecksBeforeRestart) {
    selfRecoveryRestart(RestartReason::kLowHeap);
  }
}

/**
 * A newly generated settings password stays on the screen until BOOT is
 * tapped or someone logs in with it (3 minutes at most; it comes back at
 * the next boot until then). The portal keeps working meanwhile.
 */
void showSettingsPasswordIfNeeded() {
  if (!services::settings::otaPasswordNeedsShowing()) {
    return;
  }
  constexpr unsigned long kShowMs = 3UL * 60UL * 1000UL;
  statusScreenSettingsPassword(services::settings::otaPassword());
  bootButtonConsumeTap();  // ignore a tap from before the screen appeared
  const unsigned long started = millis();
  while (millis() - started < kShowMs &&
         services::settings::otaPasswordNeedsShowing()) {
    wifiLoop();
    if (bootButtonConsumeTap()) {
      services::settings::markOtaPasswordShown();
      services::security::logEvent("new settings password acknowledged");
    }
    delay(20);
  }
}

void showRadarIfConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    g_radar_visible = false;
    return;
  }
  services::weather::begin();
  ui::radarDisplayDraw();
  g_radar_visible = true;
}

void onRangeTap() {
  ui::radar::rangeNext();
  char range_label[12];
  ui::radar::formatCurrentRing3Label(range_label, sizeof(range_label));
  Serial.printf("Range: %s (outer ~%.0f km)\n", range_label,
                ui::radar::rangeCurrent().outer_km);

  if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

/** Night screen-off from Setup; checked about once a second. */
void updateNightScreen(bool force_on) {
  static unsigned long last_check_ms = 0;
  const unsigned long now = millis();
  if (!force_on && last_check_ms != 0 && now - last_check_ms < 1000UL) {
    return;
  }
  last_check_ms = now;

  if (g_night_wake_ms != 0 && now - g_night_wake_ms >= kNightWakeMs) {
    g_night_wake_ms = 0;
  }
  int hour = 0;
  const bool off = !force_on && g_night_wake_ms == 0 &&
                   services::settings::nightOffEnabled() &&
                   services::weather::localHour(&hour) &&
                   services::settings::hourInNightOff(hour);
  if (off == !ui::radarDisplayScreenOn()) {
    return;
  }
  ui::radarDisplaySetScreenOn(!off);
  if (!off && g_radar_visible && WiFi.status() == WL_CONNECTED) {
    ui::radarDisplayDraw();
  }
}

void handleBootButton() {
  bootButtonPollLongPress();
  if (bootButtonConsumeTap()) {
    if (!ui::radarDisplayScreenOn()) {
      // First tap at night only wakes the screen for a minute.
      g_night_wake_ms = millis();
      updateNightScreen(true);
      return;
    }
    onRangeTap();
  }
}

/**
 * Periodic memory report on serial, e.g.
 *   heap: free 98304 min 61440 largest 57344 (lowest 49152) stack-free 2100 up 3d 04:12
 * free     = total free heap now
 * min      = lowest free heap since boot (worst peak so far)
 * largest  = biggest single block that can be allocated now; TLS needs a
 *            large one, so watch this for fragmentation
 * lowest   = smallest "largest" value seen at any report
 * stack-free = unused bytes of the loop task stack at its deepest so far
 */
void logHeapIfDue() {
  if (config::kHeapLogIntervalMs == 0) {
    return;
  }
  static unsigned long last_ms = 0;
  static uint32_t lowest_largest = UINT32_MAX;
  const unsigned long now = millis();
  if (last_ms != 0 && now - last_ms < config::kHeapLogIntervalMs) {
    return;
  }
  last_ms = now;

  const uint32_t largest = ESP.getMaxAllocHeap();
  if (largest < lowest_largest) {
    lowest_largest = largest;
  }
  const unsigned long up_min = now / 60000UL;
  // Format on the stack and write: Serial.printf would heap-allocate for a
  // line this long, and this line matters most when the heap is nearly full.
  char line[160];
  const int len = snprintf(
      line, sizeof(line),
      "heap: free %u min %u largest %u (lowest %u) stack-free %u "
      "up %lud %02lu:%02lu\n",
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMinFreeHeap()),
      static_cast<unsigned>(largest), static_cast<unsigned>(lowest_largest),
      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
      up_min / 1440UL, (up_min / 60UL) % 24UL, up_min % 60UL);
  if (len > 0) {
    Serial.write(reinterpret_cast<const uint8_t*>(line),
                 std::min(static_cast<size_t>(len), sizeof(line) - 1));
  }

  checkLowHeapRestart(largest);
}

/**
 * Logs when the lowest-ever free heap drops, naming the step that did it,
 * e.g. "heap low: 15132 after adsb fetch". Only prints on a new low.
 */
void noteHeapLow(const char* step) {
  static uint32_t reported_min = UINT32_MAX;
  const uint32_t min_free = ESP.getMinFreeHeap();
  if (min_free >= reported_min) {
    return;
  }
  reported_min = min_free;
  char line[64];
  const int len = snprintf(line, sizeof(line), "heap low: %u after %s\n",
                           static_cast<unsigned>(min_free), step);
  if (len > 0) {
    Serial.write(reinterpret_cast<const uint8_t*>(line),
                 std::min(static_cast<size_t>(len), sizeof(line) - 1));
  }
}

/** Downloaded medium airports: due once Wi-Fi has settled. */
bool localAirportsDue() {
  constexpr unsigned long kSettleMs = 20UL * 1000UL;
  // The download blocks the loop (and so the portal) for a few minutes:
  // wait until nobody has used the portal for a minute, so saving a new
  // location returns to Setup instead of hanging.
  constexpr unsigned long kPortalQuietMs = 60UL * 1000UL;
  const unsigned long last_request = services::ota::lastPortalRequestMs();
  if (last_request != 0 && millis() - last_request < kPortalQuietMs) {
    return false;
  }
  return services::settings::showMediumAirports() &&
         g_wifi_up_since != 0 && millis() - g_wifi_up_since >= kSettleMs &&
         services::airports::refreshDue(services::location::lat(),
                                        services::location::lon());
}

/** Blocks for the download (about a minute); the radar shows a banner. */
void refreshLocalAirports() {
  ui::radarDisplaySetBanner("UPDATING AIRPORTS");
  ui::radarDisplayRefreshAircraft();
  services::airports::refresh(services::location::lat(),
                              services::location::lon());
  ui::radarDisplaySetBanner(nullptr);
  noteHeapLow("airport download");
  // The fetch clock restarted while we were busy: fetch aircraft next.
  g_last_adsb_fetch_ms = millis() - config::kAdsbFetchIntervalMs;
  ui::radarDisplayDraw();
}

void fetchAndDrawAircraft() {
  const float fetch_km = ui::radar::fetchRadiusKm();
  if (!services::adsb::fetchUpdate(services::location::lat(),
                                   services::location::lon(), fetch_km)) {
    if (services::adsb::dataStale()) {
      ui::radarDisplayRefreshAircraft();  // clear old aircraft, show NO DATA
    }
    handleBootButton();
    return;
  }
  ui::radarDisplayRefreshAircraft();
  handleBootButton();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.printf("Plane Radar %s (built %s %s)\n", config::kFirmwareVersion,
                __DATE__, __TIME__);
  services::security::begin();
  logBootReason();
  services::ota::checkBootImage();
  if (const int removed = services::backup::removeOddKeys(); removed > 0) {
    services::security::logEvent("removed %d damaged setting(s) left by a restore",
                                 removed);
  }

  bootButtonInit();
  displayInit();
  if (wifiShowsSetupScreenOnBoot()) {
    statusScreenPortal(wifiSetupApPassword());
  }
  services::location::init();
  ui::radar::rangeInit();
  services::settings::setPasswordGenerator(services::security::randomPassword);
  services::settings::init();
  ui::radar::setUpBearingDeg(services::settings::upBearingDeg());
  // During HTTP I/O only poll the BOOT button: running the web server there
  // would process settings saves and firmware uploads in the middle of a
  // TLS request (peak heap + deep stack). Web requests wait until it ends.
  services::adsb::setPollFn(wifiPollDuringIo);
  services::weather::setPollFn(wifiPollDuringIo);
  services::airports::setPollFn(wifiPollDuringIo);
  services::updates::setPollFn(wifiPollDuringIo);
  services::airports::init();

  noteHeapLow("boot");
  if (wifiSetupConnect()) {
    g_boot_wifi_ms = millis();
    noteHeapLow("wifi connect");
    showSettingsPasswordIfNeeded();
    showRadarIfConnected();
    noteHeapLow("first radar draw");
  }
}

void loop() {
  handleBootButton();
  wifiLoop();
  logHeapIfDue();  // also runs while Wi-Fi is down, to spot reconnect leaks

  // Firmware uploads and installs always show their progress screen.
  updateNightScreen(services::ota::inProgress() ||
                    services::ota::installPending());

  static bool ota_was_in_progress = false;
  if (services::ota::installPending()) {
    // Restarts on success; on failure the radar is redrawn below next pass.
    services::ota::runPendingInstall();
    ota_was_in_progress = true;
    return;
  }
  if (services::ota::inProgress()) {
    ota_was_in_progress = true;
    delay(10);
    return;
  }
  if (ota_was_in_progress) {
    // A failed, aborted or stalled upload leaves the "Updating firmware"
    // screen up; redraw the radar right away instead of waiting for data.
    ota_was_in_progress = false;
    if (g_radar_visible && WiFi.status() == WL_CONNECTED) {
      ui::radarDisplayDraw();
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (g_radar_visible) {
      Serial.println("WiFi lost — will reconnect");
      g_radar_visible = false;
    }
    g_wifi_up_since = 0;

    if (g_wifi_down_since == 0) {
      g_wifi_down_since = millis();
    }

    const unsigned long down_ms = millis() - g_wifi_down_since;
    if (down_ms >= config::kWifiDownGraceMs && !wifiRejoinInProgress() &&
        millis() - g_last_reconnect_ms >= config::kWifiReconnectIntervalMs) {
      g_last_reconnect_ms = millis();
      if (wifiReconnect()) {
        g_wifi_down_since = 0;
        showRadarIfConnected();
      }
    }
  } else {
    g_wifi_down_since = 0;
    if (g_wifi_up_since == 0) {
      g_wifi_up_since = millis();
    }
    checkNoDataRestart();
    wifiCheckSignal();
    if (services::adsb::lastSuccessMs() != 0) {
      services::ota::confirmRunningFirmware();
      logFirstData();
    }
    // An HTTPS fetch takes about as long as the fetch interval, so the next
    // one is already due when it returns. Give weather and route lookups one
    // pass after every fetch, or they would never run.
    static bool others_turn = false;
    const bool adsb_due =
        millis() - g_last_adsb_fetch_ms >= config::kAdsbFetchIntervalMs;
    if (!g_radar_visible) {
      showRadarIfConnected();
    } else if (adsb_due && !others_turn) {
      g_last_adsb_fetch_ms = millis();
      fetchAndDrawAircraft();
      noteHeapLow("adsb fetch");
      others_turn = true;
    } else if (localAirportsDue()) {
      others_turn = false;
      refreshLocalAirports();
    } else if (services::updates::due()) {
      others_turn = false;
      services::updates::check();
      noteHeapLow("update check");
    } else {
      others_turn = false;
      if (services::weather::refreshIfDue(services::location::lat(),
                                          services::location::lon())) {
        ui::radarDisplayRefreshAircraft();
      } else if (services::adsb::enrichOnePending()) {
        ui::radarDisplayRefreshAircraft();
      }
      noteHeapLow("weather/route lookup");
    }
  }

  delay(10);
}
