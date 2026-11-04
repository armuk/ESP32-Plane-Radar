#include "services/radar_location.h"

#include <Preferences.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "config.h"

namespace services::location {

namespace {

constexpr char kPrefsNamespace[] = "radar";
constexpr char kKeyLat[] = "lat";
constexpr char kKeyLon[] = "lon";

double s_lat = config::kDefaultRadarLat;
double s_lon = config::kDefaultRadarLon;

/**
 * Whole decimal number, surrounding spaces allowed, rounded to 6 decimals
 * (~0.1 m) so a value copied from a map with 15 decimals is accepted.
 */
bool parseCoord(const char* text, double* out) {
  if (text == nullptr) {
    return false;
  }
  while (*text == ' ' || *text == '\t') {
    ++text;
  }
  if (*text == '\0') {
    return false;
  }
  char* end = nullptr;
  const double v = strtod(text, &end);
  if (end == text) {
    return false;
  }
  while (*end == ' ' || *end == '\t') {
    ++end;
  }
  if (*end != '\0' || !std::isfinite(v)) {
    return false;
  }
  *out = std::round(v * 1e6) / 1e6;
  return true;
}

bool validLatLon(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

void persist(double lat, double lon) {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.putDouble(kKeyLat, lat);
  prefs.putDouble(kKeyLon, lon);
  prefs.end();
  s_lat = lat;
  s_lon = lon;
}

}  // namespace

void init() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, true);
  if (prefs.isKey(kKeyLat) && prefs.isKey(kKeyLon)) {
    const double stored_lat =
        prefs.getDouble(kKeyLat, config::kDefaultRadarLat);
    const double stored_lon =
        prefs.getDouble(kKeyLon, config::kDefaultRadarLon);
    if (validLatLon(stored_lat, stored_lon)) {
      s_lat = stored_lat;
      s_lon = stored_lon;
    }
  }
  prefs.end();
}

double lat() { return s_lat; }

double lon() { return s_lon; }

bool saveFromStrings(const char* lat_str, const char* lon_str) {
  double new_lat = 0.0;
  double new_lon = 0.0;
  if (!parseCoord(lat_str, &new_lat) || !parseCoord(lon_str, &new_lon)) {
    return false;
  }
  if (!validLatLon(new_lat, new_lon)) {
    return false;
  }
  persist(new_lat, new_lon);
  Serial.printf("Radar location saved: %.6f, %.6f\n", new_lat, new_lon);
  return true;
}

void clear() {
  Preferences prefs;
  prefs.begin(kPrefsNamespace, false);
  prefs.remove(kKeyLat);
  prefs.remove(kKeyLon);
  prefs.end();
  s_lat = config::kDefaultRadarLat;
  s_lon = config::kDefaultRadarLon;
}

}  // namespace services::location
