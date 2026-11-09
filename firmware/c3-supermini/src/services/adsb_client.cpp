#include "services/adsb_client.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <optional>

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstring>

#include "config.h"
#include "services/adsb_stream.h"
#include "services/display_settings.h"
#include "services/net_dns.h"
#include "services/tls_trust.h"

namespace services::adsb {

namespace {

constexpr char kApiBase[] = "https://opendata.adsb.fi/api/v3/lat/";
constexpr float kKmPerNm = 1.852f;
/** TCP connect limit; the TLS handshake after it has its own timeout. */
// A failing request holds up the web pages for its whole timeout (they are
// served between requests), so these stay short. adsb.fi normally answers
// in 0.7-2 s.
constexpr unsigned long kConnectTimeoutMs = 4000;
constexpr unsigned long kAdsbRequestTimeoutMs = 6000;
/** Skip receiver positions older than this (readsb keeps them ~60 s). */
constexpr float kMaxPositionAgeS = 30.0f;
/**
 * At least one entry per tracked aircraft. A smaller cache thrashes when
 * more than that many aircraft are visible: entries evict each other and
 * ADSBDB is re-queried every kFlightLookupMinIntervalMs forever.
 * ~52 bytes per entry (static RAM).
 */
constexpr size_t kEnrichmentCacheSize = kMaxAircraft;
static_assert(kEnrichmentCacheSize >= kMaxAircraft,
              "enrichment cache must cover every tracked aircraft");

Aircraft s_aircraft[kMaxAircraft];
size_t s_aircraft_count = 0;
PollFn s_poll_fn = nullptr;
unsigned long s_last_enrichment_lookup_ms = 0;
unsigned long s_last_enrichment_failure_ms = 0;
double s_last_center_lat = 0.0;
double s_last_center_lon = 0.0;
unsigned long s_first_attempt_ms = 0;
unsigned long s_last_success_ms = 0;

// --- Local receiver (Setup > Receiver) ---
/** Failed receiver fetches in a row before using adsb.fi for a while. */
constexpr uint8_t kReceiverFailuresBeforeFallback = 3;
constexpr unsigned long kReceiverFallbackMs = 5UL * 60UL * 1000UL;
uint8_t s_receiver_failures = 0;
bool s_receiver_fallback_active = false;
unsigned long s_receiver_fallback_start_ms = 0;
unsigned long s_receiver_last_ok_ms = 0;
uint16_t s_receiver_last_count = 0;
int s_receiver_last_code = 0;
bool s_last_fetch_from_receiver = false;

struct EnrichmentCacheEntry {
  char hex[7] = {};
  char callsign[9] = {};
  char route[10] = {};
  char type[18] = {};
  char airline[16] = {};
  unsigned long refreshed_ms = 0;
  bool has_data = false;
};

EnrichmentCacheEntry s_enrichment_cache[kEnrichmentCacheSize];

void pollNetwork() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

/**
 * One GET attempt; a failure is retried by the next fetch or lookup.
 * Retrying here used to loop on "connection refused" every 5 ms until the
 * deadline, but mbedTLS also reports "no heap for TLS buffers" that way, so
 * a low-memory moment turned into ~170 failed TLS setups per fetch.
 */
int performGet(HTTPClient& http, unsigned long timeout_ms) {
  http.setConnectTimeout(static_cast<int32_t>(
      timeout_ms < kConnectTimeoutMs ? timeout_ms : kConnectTimeoutMs));
  pollNetwork();
  const int code = http.GET();
  pollNetwork();
  return code;
}

bool readResponseBodyWithPoll(HTTPClient& http, String& payload,
                              unsigned long timeout_ms) {
  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    return false;
  }

  const int content_length = http.getSize();
  if (content_length > 0) {
    payload.reserve(static_cast<unsigned>(content_length + 1));
  }

  uint8_t buffer[512];
  const unsigned long started_ms = millis();
  while (millis() - started_ms < timeout_ms) {
    pollNetwork();
    const int available = stream->available();
    if (available > 0) {
      const int to_read =
          available > static_cast<int>(sizeof(buffer)) ? static_cast<int>(sizeof(buffer))
                                                       : available;
      const int read_bytes = stream->readBytes(buffer, to_read);
      if (read_bytes > 0) {
        payload.concat(reinterpret_cast<const char*>(buffer),
                       static_cast<unsigned>(read_bytes));
      }
    }
    if (content_length > 0 &&
        static_cast<int>(payload.length()) >= content_length) {
      break;
    }
    if (!http.connected() && stream->available() <= 0) {
      break;
    }
    delay(1);
  }

  return payload.length() > 0;
}

float kmToNauticalMiles(float km) { return km / kKmPerNm; }

bool readJsonFloat(const JsonObject& obj, const char* key, float* out) {
  if (obj[key].is<float>() || obj[key].is<double>() || obj[key].is<int>()) {
    *out = obj[key].as<float>();
    return true;
  }
  return false;
}

float pickNoseHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickTrackHeading(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "track", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "true_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "mag_heading", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "dir", &v)) {
    return v;
  }
  return 0.0f;
}

float pickGroundSpeed(const JsonObject& plane) {
  float v = 0.0f;
  if (readJsonFloat(plane, "gs", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "tas", &v)) {
    return v;
  }
  if (readJsonFloat(plane, "ias", &v)) {
    return v;
  }
  return 0.0f;
}

bool isOnGround(const JsonObject& plane) {
  if (!plane["alt_baro"].is<const char*>()) {
    return false;
  }
  return strcmp(plane["alt_baro"].as<const char*>(), "ground") == 0;
}

void copyJsonStringTrimmed(const JsonObject& obj, const char* key, char* out,
                           size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  if (!obj[key].is<const char*>()) {
    return;
  }
  const char* s = obj[key].as<const char*>();
  size_t n = strnlen(s, out_len - 1);
  while (n > 0 && s[n - 1] == ' ') {
    --n;
  }
  memcpy(out, s, n);
  out[n] = '\0';
}

void formatAltitudeTag(const JsonObject& plane, char* out, size_t out_len) {
  out[0] = '\0';
  if (out_len == 0) {
    return;
  }

  if (plane["alt_baro"].is<const char*>()) {
    const char* s = plane["alt_baro"].as<const char*>();
    if (strcmp(s, "ground") == 0) {
      strncpy(out, "GND", out_len - 1);
      out[out_len - 1] = '\0';
      return;
    }
  }

  float alt = 0.0f;
  if (readJsonFloat(plane, "alt_baro", &alt) ||
      readJsonFloat(plane, "alt_geom", &alt)) {
    snprintf(out, out_len, "%d ft", static_cast<int>(lroundf(alt)));
  }
}

void fillTagFields(Aircraft* ac, const JsonObject& plane) {
  copyJsonStringTrimmed(plane, "hex", ac->hex, sizeof(ac->hex));
  copyJsonStringTrimmed(plane, "flight", ac->callsign, sizeof(ac->callsign));
  if (ac->callsign[0] == '\0') {
    copyJsonStringTrimmed(plane, "hex", ac->callsign, sizeof(ac->callsign));
  }

  ac->route[0] = '\0';
  ac->airline[0] = '\0';
  copyJsonStringTrimmed(plane, "t", ac->type, sizeof(ac->type));
  ac->kind = static_cast<uint8_t>(classifyAircraft(
      plane["category"] | nullptr, plane["dbFlags"] | 0, ac->type));
  formatAltitudeTag(plane, ac->alt, sizeof(ac->alt));
}

bool sameKey(const EnrichmentCacheEntry& entry, const Aircraft& plane) {
  return entry.refreshed_ms != 0 && strcmp(entry.hex, plane.hex) == 0 &&
         strcmp(entry.callsign, plane.callsign) == 0;
}

bool cacheEntryFresh(const EnrichmentCacheEntry& entry,
                     unsigned long now) {
  if (entry.refreshed_ms == 0) {
    return false;
  }
  const unsigned long ttl = entry.has_data ? config::kFlightCacheSuccessMs
                                           : config::kFlightCacheMissMs;
  return now - entry.refreshed_ms < ttl;
}

EnrichmentCacheEntry* findCacheEntry(const Aircraft& plane,
                                     unsigned long now) {
  for (auto& entry : s_enrichment_cache) {
    if (sameKey(entry, plane) && cacheEntryFresh(entry, now)) {
      return &entry;
    }
  }
  return nullptr;
}

bool copyIfDifferent(char* destination, size_t destination_len,
                     const char* source) {
  if (source == nullptr || source[0] == '\0' ||
      strcmp(destination, source) == 0) {
    return false;
  }
  strncpy(destination, source, destination_len - 1);
  destination[destination_len - 1] = '\0';
  return true;
}

bool applyCacheEntry(Aircraft* plane, const EnrichmentCacheEntry& entry) {
  bool changed = false;
  changed |= copyIfDifferent(plane->route, sizeof(plane->route), entry.route);
  changed |= copyIfDifferent(plane->type, sizeof(plane->type), entry.type);
  changed |=
      copyIfDifferent(plane->airline, sizeof(plane->airline), entry.airline);
  return changed;
}

EnrichmentCacheEntry* cacheSlotFor(const Aircraft& plane,
                                   unsigned long now) {
  EnrichmentCacheEntry* oldest = &s_enrichment_cache[0];
  unsigned long oldest_age = 0;
  for (auto& entry : s_enrichment_cache) {
    if (sameKey(entry, plane) || entry.refreshed_ms == 0) {
      return &entry;
    }
    const unsigned long age = now - entry.refreshed_ms;
    if (age >= oldest_age) {
      oldest = &entry;
      oldest_age = age;
    }
  }
  return oldest;
}

void copySafeIdentifier(const char* source, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  size_t written = 0;
  if (source != nullptr) {
    for (size_t i = 0; source[i] != '\0' && written + 1 < out_len; ++i) {
      const unsigned char ch = static_cast<unsigned char>(source[i]);
      if (std::isalnum(ch)) {
        out[written++] =
            static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      }
    }
  }
  out[written] = '\0';
}

void copyAirportCode(const JsonObject& airport, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  copyJsonStringTrimmed(airport, "iata_code", out, out_len);
  if (out[0] == '\0') {
    copyJsonStringTrimmed(airport, "icao_code", out, out_len);
  }
}

void parseRoute(const JsonObject& response, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  JsonObject route = response["flightroute"].as<JsonObject>();
  if (route.isNull()) {
    return;
  }

  char origin[5] = {};
  char destination[5] = {};
  copyAirportCode(route["origin"].as<JsonObject>(), origin, sizeof(origin));
  copyAirportCode(route["destination"].as<JsonObject>(), destination,
                  sizeof(destination));
  if (origin[0] != '\0' && destination[0] != '\0') {
    snprintf(out, out_len, "%s-%s", origin, destination);
  }
}

/** Airline name from flightroute.airline.name, e.g. "British Airways". */
void parseAirline(const JsonObject& response, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  JsonObject airline =
      response["flightroute"]["airline"].as<JsonObject>();
  if (!airline.isNull()) {
    copyJsonStringTrimmed(airline, "name", out, out_len);
  }
}

bool startsWith(const char* value, const char* prefix) {
  return value != nullptr && prefix != nullptr &&
         strncmp(value, prefix, strlen(prefix)) == 0;
}

void compactAircraftType(const char* detailed, const char* icao, char* out,
                         size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';

  const char* value = detailed;
  char prefixed[40] = {};
  if (startsWith(value, "Boeing ")) {
    snprintf(prefixed, sizeof(prefixed), "B%s", value + 7);
    value = prefixed;
  } else if (startsWith(value, "Airbus ")) {
    value += 7;
  } else if (startsWith(value, "Bombardier ")) {
    value += 11;
  } else if (startsWith(value, "Embraer ")) {
    value += 8;
  } else if (startsWith(value, "De Havilland Canada ")) {
    value += 20;
  }

  if (value == nullptr || value[0] == '\0') {
    value = icao;
  }
  if (value == nullptr) {
    return;
  }

  size_t written = 0;
  bool previous_space = false;
  for (size_t i = 0; value[i] != '\0' && written + 1 < out_len; ++i) {
    const char ch = value[i];
    if (ch == ' ') {
      if (!previous_space) {
        out[written++] = ' ';
        previous_space = true;
      }
      continue;
    }
    out[written++] = ch;
    previous_space = false;
  }
  while (written > 0 && out[written - 1] == ' ') {
    --written;
  }
  out[written] = '\0';
}

void parseDetailedType(const JsonObject& response, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  out[0] = '\0';
  JsonObject aircraft = response["aircraft"].as<JsonObject>();
  if (aircraft.isNull()) {
    return;
  }
  compactAircraftType(aircraft["type"] | nullptr,
                      aircraft["icao_type"] | nullptr, out, out_len);
}

String enrichmentUrl(const Aircraft& plane) {
  char hex[sizeof(plane.hex)] = {};
  char callsign[sizeof(plane.callsign)] = {};
  copySafeIdentifier(plane.hex, hex, sizeof(hex));
  copySafeIdentifier(plane.callsign, callsign, sizeof(callsign));

  String url = config::kFlightDataApiBase;
  if (hex[0] != '\0') {
    url += "aircraft/";
    url += hex;
    if (callsign[0] != '\0' && strcmp(hex, callsign) != 0) {
      url += "?callsign=";
      url += callsign;
    }
  } else {
    url += "callsign/";
    url += callsign;
  }
  return url;
}

String callsignRouteUrl(const Aircraft& plane) {
  char callsign[sizeof(plane.callsign)] = {};
  copySafeIdentifier(plane.callsign, callsign, sizeof(callsign));
  if (callsign[0] == '\0') {
    return {};
  }

  String url = config::kFlightDataApiBase;
  url += "callsign/";
  url += callsign;
  return url;
}

bool fetchFlightDataJson(const String& url, const char* callsign,
                         JsonDocument* doc, bool* not_found) {
  *not_found = false;
  if (!services::net::resolveQuickly(url.c_str(), services::net::kDnsTimeoutMs,
                                     pollNetwork)) {
    return false;
  }
  WiFiClientSecure client;
  services::tls::Session tls(client, "adsbdb");

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("flight data: http.begin failed");
    return false;
  }
  http.setTimeout(config::kFlightLookupTimeoutMs);
  // HTTP/1.0: no chunked encoding in the raw body read, and no keep-alive,
  // so http.end() frees the ~40 KB TLS session before the JSON is parsed.
  http.useHTTP10(true);
  http.setReuse(false);
  const int code = performGet(http, config::kFlightLookupTimeoutMs);
  tls.setResult(code);

  String payload;
  if (code == HTTP_CODE_OK) {
    readResponseBodyWithPoll(http, payload, config::kFlightLookupTimeoutMs);
  }
  http.end();

  if (code == HTTP_CODE_NOT_FOUND) {
    *not_found = true;
    return true;
  }
  if (code != HTTP_CODE_OK || payload.length() == 0) {
    Serial.printf("flight data: HTTP %d for %s\n", code, callsign);
    return false;
  }

  const DeserializationError error = deserializeJson(*doc, payload);
  if (error) {
    Serial.printf("flight data: JSON parse error: %s\n", error.c_str());
    return false;
  }
  return true;
}

bool fetchEnrichment(const Aircraft& plane, EnrichmentCacheEntry* entry) {
  entry->route[0] = '\0';
  entry->type[0] = '\0';
  entry->airline[0] = '\0';

  {
    // Scoped: the parsed reply (a few KB) is freed before a second request
    // below, which would otherwise hold it through a whole TLS session.
    JsonDocument doc;
    bool not_found = false;
    if (!fetchFlightDataJson(enrichmentUrl(plane), plane.callsign, &doc,
                             &not_found)) {
      return false;
    }

    if (!not_found) {
      JsonObject response = doc["response"].as<JsonObject>();
      if (!response.isNull()) {
        parseRoute(response, entry->route, sizeof(entry->route));
        parseDetailedType(response, entry->type, sizeof(entry->type));
        parseAirline(response, entry->airline, sizeof(entry->airline));
      }
    }
  }

  // ADSBDB may return HTTP 200 "unknown aircraft" from the combined endpoint
  // even though its callsign database has a valid route. Retry only the route
  // through the callsign endpoint in that case.
  if (entry->route[0] == '\0') {
    const String route_url = callsignRouteUrl(plane);
    if (route_url.length() > 0 && route_url != enrichmentUrl(plane)) {
      JsonDocument route_doc;
      bool route_not_found = false;
      if (!fetchFlightDataJson(route_url, plane.callsign, &route_doc,
                               &route_not_found)) {
        return false;
      }
      if (!route_not_found) {
        JsonObject response = route_doc["response"].as<JsonObject>();
        if (!response.isNull()) {
          parseRoute(response, entry->route, sizeof(entry->route));
          if (entry->airline[0] == '\0') {
            parseAirline(response, entry->airline, sizeof(entry->airline));
          }
        }
      }
    }
  }

  if (entry->route[0] == '\0' && entry->type[0] == '\0') {
    Serial.printf("flight data: no match for %s\n", plane.callsign);
  }
  return true;
}

void applyCachedEnrichment(Aircraft* plane) {
  EnrichmentCacheEntry* entry = findCacheEntry(*plane, millis());
  if (entry != nullptr) {
    applyCacheEntry(plane, *entry);
  }
}

}  // namespace

void setPollFn(PollFn fn) { s_poll_fn = fn; }

size_t aircraftCount() { return s_aircraft_count; }

const Aircraft* aircraftList() { return s_aircraft; }

namespace {

/**
 * One aircraft fetch, from adsb.fi (area query over HTTPS) or the local
 * receiver (its whole aircraft.json, usually plain HTTP: no TLS session, so
 * ~33 KB more free heap during the fetch). *http_code gets the HTTP status
 * or the client's negative error.
 */
bool fetchAircraft(double center_lat, double center_lon,
                   float fetch_radius_km, bool from_receiver,
                   int* http_code) {
  s_last_center_lat = center_lat;
  s_last_center_lon = center_lon;
  *http_code = 0;

  String url;
  if (from_receiver) {
    url = services::settings::receiverUrl();
  } else {
    url = kApiBase;
    url += String(center_lat, 6);
    url += "/lon/";
    url += String(center_lon, 6);
    url += "/dist/";
    url += String(kmToNauticalMiles(fetch_radius_km), 1);
  }
  const char* tag = from_receiver ? "receiver" : "adsb";

  if (!services::net::resolveQuickly(url.c_str(), services::net::kDnsTimeoutMs,
                                     pollNetwork)) {
    return false;
  }

  // Only the client that's used opens a connection; the TLS buffers are
  // allocated on connect, not here.
  WiFiClient plain_client;
  WiFiClientSecure secure_client;
  WiFiClient* client = &plain_client;
  std::optional<services::tls::Session> tls;
  if (url.startsWith("https://")) {
    tls.emplace(secure_client, tag);
    client = &secure_client;
  }

  HTTPClient http;
  if (!http.begin(*client, url)) {
    Serial.printf("%s: http.begin failed\n", tag);
    return false;
  }

  http.setTimeout(kAdsbRequestTimeoutMs);
  // HTTP/1.0: no chunked transfer encoding (the stream is read raw) and the
  // server closes the connection at the end of the body. Also disables
  // keep-alive so http.end() frees the TLS session.
  http.useHTTP10(true);
  http.setReuse(false);
  const int code = performGet(http, kAdsbRequestTimeoutMs);
  *http_code = code;
  if (tls) {
    tls->setResult(code);
  }
  if (code != HTTP_CODE_OK) {
    Serial.printf("%s: HTTP %d\n", tag, code);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (stream == nullptr) {
    Serial.printf("%s: no response stream\n", tag);
    http.end();
    return false;
  }

  // Parse straight from the network one aircraft at a time, keeping only
  // the filtered fields: RAM use doesn't grow with the number of aircraft.
  // The API's order isn't by distance: when more than kMaxAircraft are
  // returned, keep the nearest ones rather than whichever came first.
  NearestSet<kMaxAircraft> nearest;
  size_t candidates = 0;
  // A receiver sends everything it hears: apply the area query ourselves.
  const float max_dist_sq_km = fetch_radius_km * fetch_radius_km;
  JsonDocument plane_doc;
  PollingStreamReader<WiFiClient> reader(*stream, http.getSize(),
                                         kAdsbRequestTimeoutMs, pollNetwork);
  const DeserializationError err = forEachAircraft(
      reader, plane_doc, [&](JsonObject plane) {
        if (!plane["lat"].is<float>() || !plane["lon"].is<float>()) {
          return;
        }
        // Receivers keep a position for a while after the last report.
        if (plane["seen_pos"].is<float>() &&
            plane["seen_pos"].as<float>() > kMaxPositionAgeS) {
          return;
        }
        if (isOnGround(plane)) {
          if (!services::settings::showGroundAircraft()) {
            return;
          }
        } else {
          // Altitude limits from Setup; aircraft without an altitude are
          // kept.
          float alt_ft = 0.0f;
          if ((readJsonFloat(plane, "alt_baro", &alt_ft) ||
               readJsonFloat(plane, "alt_geom", &alt_ft)) &&
              !services::settings::altitudeAllowed(alt_ft)) {
            return;
          }
        }
        ++candidates;

        const float lat = plane["lat"].as<float>();
        const float lon = plane["lon"].as<float>();
        const float dist_sq = approxDistSqKm(center_lat, center_lon, lat, lon);
        if (from_receiver && dist_sq > max_dist_sq_km) {
          return;
        }
        const int slot = nearest.slotFor(dist_sq);
        if (slot < 0) {
          return;
        }

        Aircraft& target = s_aircraft[slot];
        target.lat = lat;
        target.lon = lon;
        target.nose_deg = pickNoseHeading(plane);
        target.track_deg = pickTrackHeading(plane);
        target.gs_knots = pickGroundSpeed(plane);
        fillTagFields(&target, plane);
        applyCachedEnrichment(&target);
      });
  http.end();

  const size_t n = nearest.count();
  if (err) {
    Serial.printf("%s: JSON parse error: %s%s after %u aircraft\n", tag,
                  err.c_str(), reader.timedOut() ? " (timed out)" : "",
                  static_cast<unsigned>(candidates));
    // Slots 0..n-1 were all rewritten by this fetch (NearestSet fills them
    // in order), so show what arrived rather than a mix with old data.
    if (n > 0) {
      s_aircraft_count = n;
    }
    return false;
  }

  if (candidates > n) {
    Serial.printf("%s: kept nearest %u of %u aircraft\n", tag,
                  static_cast<unsigned>(n), static_cast<unsigned>(candidates));
  }
  s_aircraft_count = n;
  Serial.printf("%s: %u aircraft\n", tag, static_cast<unsigned>(n));
  return true;
}

}  // namespace

bool fetchUpdate(double center_lat, double center_lon, float fetch_radius_km) {
  const unsigned long now = millis();
  if (s_first_attempt_ms == 0) {
    s_first_attempt_ms = now;
  }
  bool ok = false;
  int code = 0;
  const bool use_receiver = services::settings::receiverEnabled() &&
                            services::settings::receiverUrl()[0] != '\0';
  if (!use_receiver) {
    s_receiver_failures = 0;
    s_receiver_fallback_active = false;
    ok = fetchAircraft(center_lat, center_lon, fetch_radius_km, false, &code);
    s_last_fetch_from_receiver = false;
  } else {
    if (s_receiver_fallback_active &&
        now - s_receiver_fallback_start_ms >= kReceiverFallbackMs) {
      s_receiver_fallback_active = false;  // try the receiver again
    }
    if (!s_receiver_fallback_active) {
      ok = fetchAircraft(center_lat, center_lon, fetch_radius_km, true, &code);
      s_receiver_last_code = code;
      s_last_fetch_from_receiver = ok;
      if (ok) {
        s_receiver_failures = 0;
        s_receiver_last_ok_ms = millis();
        s_receiver_last_count = static_cast<uint16_t>(s_aircraft_count);
      } else if (++s_receiver_failures >= kReceiverFailuresBeforeFallback &&
                 services::settings::receiverFallback()) {
        s_receiver_failures = 0;
        s_receiver_fallback_active = true;
        s_receiver_fallback_start_ms = millis();
        Serial.println("receiver: unreachable, using adsb.fi for 5 minutes");
      }
    }
    if (!ok && s_receiver_fallback_active) {
      ok = fetchAircraft(center_lat, center_lon, fetch_radius_km, false,
                         &code);
      s_last_fetch_from_receiver = false;
    }
  }
  if (ok) {
    s_last_success_ms = millis();
    return true;
  }
  // Don't keep showing old positions as if they were live.
  if (dataStale() && s_aircraft_count != 0) {
    Serial.println("adsb: no data for a while — clearing aircraft");
    s_aircraft_count = 0;
  }
  return false;
}

unsigned long lastSuccessMs() { return s_last_success_ms; }

void formatReceiverStatus(char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  if (!services::settings::receiverEnabled()) {
    snprintf(out, out_len, "Not in use: aircraft come from adsb.fi.");
    return;
  }
  const unsigned long now = millis();
  int len = 0;
  if (s_receiver_last_ok_ms != 0) {
    len = snprintf(out, out_len, "Last read %lu s ago: %u aircraft in range.",
                   (now - s_receiver_last_ok_ms) / 1000UL,
                   static_cast<unsigned>(s_receiver_last_count));
  } else {
    len = snprintf(out, out_len, "No successful read since start-up.");
  }
  if (len < 0 || static_cast<size_t>(len) >= out_len) {
    return;
  }
  if (s_receiver_last_code != HTTP_CODE_OK && s_receiver_last_code != 0) {
    const String error =
        s_receiver_last_code < 0
            ? HTTPClient::errorToString(s_receiver_last_code)
            : "HTTP " + String(s_receiver_last_code);
    const int more = snprintf(out + len, out_len - len, " Last error: %s.",
                              error.c_str());
    if (more > 0) {
      len += more;
    }
  }
  if (s_receiver_fallback_active && static_cast<size_t>(len) < out_len) {
    snprintf(out + len, out_len - len,
             " Using adsb.fi until the receiver answers again.");
  }
}

bool dataStale() {
  const unsigned long now = millis();
  if (s_last_success_ms != 0) {
    return now - s_last_success_ms >= config::kAircraftStaleMs;
  }
  // Never succeeded: only stale once we've been trying for a while.
  return s_first_attempt_ms != 0 &&
         now - s_first_attempt_ms >= config::kAircraftStaleMs;
}

bool enrichOnePending() {
  const unsigned long now = millis();
  if (!services::settings::routeLookupsEnabled() || s_aircraft_count == 0 ||
      (s_last_enrichment_failure_ms != 0 &&
       now - s_last_enrichment_failure_ms <
           config::kFlightLookupFailureBackoffMs) ||
      (s_last_enrichment_lookup_ms != 0 &&
       now - s_last_enrichment_lookup_ms <
           config::kFlightLookupMinIntervalMs)) {
    return false;
  }

  size_t candidate_index = kMaxAircraft;
  float candidate_dist_sq = FLT_MAX;
  for (size_t index = 0; index < s_aircraft_count; ++index) {
    Aircraft& plane = s_aircraft[index];
    if (plane.hex[0] == '\0' && plane.callsign[0] == '\0') {
      continue;
    }

    EnrichmentCacheEntry* cached = findCacheEntry(plane, now);
    if (cached != nullptr) {
      applyCacheEntry(&plane, *cached);
      continue;
    }

    const float dlat = plane.lat - static_cast<float>(s_last_center_lat);
    const float dlon = plane.lon - static_cast<float>(s_last_center_lon);
    const float dist_sq = dlat * dlat + dlon * dlon;
    if (dist_sq < candidate_dist_sq) {
      candidate_index = index;
      candidate_dist_sq = dist_sq;
    }
  }

  if (candidate_index == kMaxAircraft) {
    return false;
  }

  Aircraft& plane = s_aircraft[candidate_index];
  s_last_enrichment_lookup_ms = now;
  EnrichmentCacheEntry* entry = cacheSlotFor(plane, now);
  *entry = {};
  strncpy(entry->hex, plane.hex, sizeof(entry->hex) - 1);
  strncpy(entry->callsign, plane.callsign, sizeof(entry->callsign) - 1);

  if (!fetchEnrichment(plane, entry)) {
    // Network failures are retried after a short global backoff.
    *entry = {};
    s_last_enrichment_failure_ms = millis();
    return false;
  }

  s_last_enrichment_failure_ms = 0;
  entry->refreshed_ms = millis();
  entry->has_data = entry->route[0] != '\0' || entry->type[0] != '\0';
  const bool changed = applyCacheEntry(&plane, *entry);
  Serial.printf("flight data: %s %s %s\n", plane.callsign,
                entry->route[0] != '\0' ? entry->route : "(route unknown)",
                entry->type[0] != '\0' ? entry->type : "(type unchanged)");
  return changed;
}

}  // namespace services::adsb
