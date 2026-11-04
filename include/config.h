#pragma once

#include <cstdint>

#include <driver/gpio.h>

namespace config {

// --- Wi-Fi portal ---
constexpr char kPortalApName[] = "PlaneRadar-Setup";
constexpr char kPortalIp[] = "192.168.4.1";
/**
 * Default mDNS / DHCP host name (no ".local"); browser: http://plane-radar.local
 * Changeable on the Setup page ("Device name").
 */
constexpr char kPortalHostname[] = "plane-radar";

/** Per-attempt STA connect wait (ms); retried kWifiConnectAttempts times. */
constexpr unsigned long kWifiConnectAttemptMs = 15000;
constexpr uint8_t kWifiConnectAttempts = 3;
constexpr unsigned long kWifiPortalTimeoutSec = 0;  // 0 = no timeout while configuring
constexpr unsigned long kWifiConnectingFrameMs = 50;
/** Wait after disconnect before reconnecting (avoids portal on brief drops). */
constexpr unsigned long kWifiDownGraceMs = 4000;
/** Minimum interval between background reconnect tries. */
constexpr unsigned long kWifiReconnectIntervalMs = 15000;
/**
 * Turn the radio fully off and on only on every Nth failed reconnect round
 * (on its last attempt); other retries just disconnect and reconnect.
 */
constexpr uint32_t kWifiFullResetEveryRounds = 5;

// --- BOOT button (ESP32-C3 Super Mini, active LOW) ---
constexpr gpio_num_t kBootPin = GPIO_NUM_9;
constexpr unsigned long kBootResetHoldMs = 3000UL;
/** Ignore BOOT taps shorter than this (debounce). */
constexpr unsigned long kBootTapMinMs = 40UL;

// --- Display: GC9A01 1.28" round 240×240 (SPI) ---
constexpr gpio_num_t kDisplayPinRst = GPIO_NUM_0;
constexpr gpio_num_t kDisplayPinCs = GPIO_NUM_1;
constexpr gpio_num_t kDisplayPinDc = GPIO_NUM_10;
constexpr gpio_num_t kDisplayPinMosi = GPIO_NUM_3;  // display SDA
constexpr gpio_num_t kDisplayPinSclk = GPIO_NUM_4;  // display SCL

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 240;

constexpr uint32_t kDisplaySpiWriteHz = 40000000;
// GC9A01 modules often need invert + BGR for correct black/green output
constexpr bool kDisplayInvert = true;
constexpr bool kDisplayRgbOrder = true;

// --- Radar center defaults (overridden via WiFi setup portal) ---
constexpr double kDefaultRadarLat = 51.558418;
constexpr double kDefaultRadarLon = -1.782040;

/** Poll adsb.fi (API public limit: 1 req/s). */
constexpr unsigned long kAdsbFetchIntervalMs = 3000;
/** Default for Setup's "Show aircraft on the ground" (alt_baro "ground"). */
constexpr bool kAdsbShowGroundAircraft = false;
/** After this long without a successful fetch, clear aircraft + show NO DATA. */
constexpr unsigned long kAircraftStaleMs = 30UL * 1000UL;

// --- Self-recovery (restart when the device can't recover by itself) ---
/** Restart after this long with Wi-Fi up but no successful aircraft fetch. */
constexpr unsigned long kNoDataRestartMs = 10UL * 60UL * 1000UL;
/**
 * Restart when the largest free heap block stays below this: a TLS
 * connection needs one ~17 KB buffer, so below this fetches start failing.
 */
constexpr uint32_t kMinLargestFreeBlockBytes = 20UL * 1024UL;
/** Consecutive low-heap checks (one per kHeapLogIntervalMs) before restart. */
constexpr uint8_t kLowHeapChecksBeforeRestart = 3;

// --- Flight enrichment (origin/destination and detailed aircraft type) ---
constexpr char kFlightDataApiBase[] = "https://api.adsbdb.com/v0/";
/** One lookup at a time; successful results remain cached for six hours. */
constexpr unsigned long kFlightLookupMinIntervalMs = 750UL;
constexpr unsigned long kFlightLookupTimeoutMs = 5000UL;
constexpr unsigned long kFlightLookupFailureBackoffMs = 30000UL;
constexpr unsigned long kFlightCacheSuccessMs = 6UL * 60UL * 60UL * 1000UL;
constexpr unsigned long kFlightCacheMissMs = 10UL * 60UL * 1000UL;

// --- Weather and local time ---
constexpr char kWeatherApiBase[] = "https://api.open-meteo.com/v1/forecast";
constexpr unsigned long kWeatherFetchIntervalMs = 15UL * 60UL * 1000UL;
constexpr unsigned long kWeatherRequestTimeoutMs = 6000UL;
/** After a failed weather fetch, try again this soon instead of 15 min. */
constexpr unsigned long kWeatherRetryMs = 60UL * 1000UL;

// --- Time sync (NTP) ---
/** Default primary server; can be changed on the Setup page. */
constexpr char kDefaultNtpServer[] = "pool.ntp.org";
/** Always used as the secondary server, in case the primary is unreachable. */
constexpr char kFallbackNtpServer[] = "time.nist.gov";

// --- Diagnostics ---
/**
 * Serial heap/stack report interval (0 = off). Watch "largest" over hours:
 * falling largest with steady free = fragmentation; falling free = a leak.
 */
constexpr unsigned long kHeapLogIntervalMs = 60UL * 1000UL;

// --- User-facing defaults ---
constexpr char kOtaUsername[] = "admin";
/**
 * The shared default of earlier firmware. Devices still using it get a
 * random per-device password at the next boot.
 */
constexpr char kLegacyDefaultOtaPassword[] = "plane-radar";

// --- UI colors (RGB565) — status screens ---
constexpr uint16_t kColorBlack = 0x0000;
constexpr uint16_t kColorYellow = 0xFFE0;
constexpr uint16_t kTextOnYellow = kColorBlack;
constexpr uint16_t kTextOnBlack = 0xFFFF;

}  // namespace config
