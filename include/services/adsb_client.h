#pragma once

#include <cstddef>
#include <cstdint>

namespace services::adsb {

/** How an aircraft is drawn. */
enum class AircraftKind : uint8_t { kStandard = 0, kLight = 1, kMilitary = 2 };

struct Aircraft {
  float lat;
  float lon;
  float nose_deg;
  float track_deg;
  float gs_knots;
  char hex[7];
  char callsign[9];
  /** IATA/ICAO origin-destination pair, for example "BOS-IND". */
  char route[10];
  /** Compact detailed model, for example "B737-800". */
  char type[18];
  char alt[12];
  /** Airline name from ADSBDB, e.g. "British Airways" (may be empty). */
  char airline[16];
  /** services::adsb::AircraftKind: standard, light or military. */
  uint8_t kind;
};

constexpr size_t kMaxAircraft = 64;

size_t aircraftCount();
const Aircraft* aircraftList();

/** Hook invoked during long HTTP I/O (e.g. wifiLoop). Optional. */
using PollFn = void (*)();
void setPollFn(PollFn fn);

/**
 * Fetch aircraft within fetch_radius_km of center_lat/lon from adsb.fi.
 * Keeps the kMaxAircraft nearest. On failure, clears the list once the data
 * is stale (see dataStale()) so old positions aren't shown as live.
 */
bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km);

/** millis() of the last successful fetch, or 0 if none yet. */
unsigned long lastSuccessMs();

/** One line for the Receiver page: last read, errors, fallback. */
void formatReceiverStatus(char* out, size_t out_len);

/**
 * True when no fetch has succeeded for config::kAircraftStaleMs (counting
 * from the first attempt if none has ever succeeded).
 */
bool dataStale();

/**
 * Look up one uncached aircraft through ADSBDB. Results are rate-limited and
 * cached. Returns true when a visible route or type changed.
 */
bool enrichOnePending();

}  // namespace services::adsb
