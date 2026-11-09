#include "services/local_airports.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include "services/net_dns.h"
#include "services/tls_trust.h"

namespace services::airports {
namespace {

constexpr char kAirportsUrl[] =
    "https://davidmegginson.github.io/ourairports-data/airports.csv";
constexpr char kRunwaysUrl[] =
    "https://davidmegginson.github.io/ourairports-data/runways.csv";
constexpr char kFilePath[] = "/airports.bin";
constexpr char kTempPath[] = "/airports.tmp";

/** Re-download when the radar moves further than this from the saved area. */
constexpr float kRefreshDistanceKm = 5.0f;
constexpr unsigned long kRetryMs = 15UL * 60UL * 1000UL;
/** Whole-file limit (airports.csv is ~13 MB) and no-data limit. */
constexpr unsigned long kFileTimeoutMs = 6UL * 60UL * 1000UL;
constexpr unsigned long kStallTimeoutMs = 20UL * 1000UL;
constexpr int kConnectTimeoutMs = 8000;

static_assert(sizeof(LocalAirport) == 20, "file layout");
static_assert(sizeof(LocalRunway) == 20, "file layout");
static_assert(sizeof(FileHeader) == 20, "file layout");

bool s_fs_ready = false;
bool s_fs_failed = false;
bool s_have = false;
FileHeader s_header = {};
unsigned long s_last_attempt_ms = 0;
double s_attempt_lat = 999.0;
double s_attempt_lon = 999.0;
char s_last_error[40] = "";
PollFn s_poll_fn = nullptr;
File s_read_file;

/** Owns a heap array (nullptr when the allocation failed). */
template <typename T>
struct HeapArray {
  explicit HeapArray(size_t count) : data(new (std::nothrow) T[count]) {}
  ~HeapArray() { delete[] data; }
  HeapArray(const HeapArray&) = delete;
  HeapArray& operator=(const HeapArray&) = delete;
  T* data;
};

struct Candidate {
  LocalAirport airport;
  int32_t id;
  float dist_km;
};

void poll() {
  if (s_poll_fn != nullptr) {
    s_poll_fn();
  }
}

void setError(const char* text) {
  snprintf(s_last_error, sizeof(s_last_error), "%s", text);
  Serial.printf("airports: %s\n", text);
}

bool mountFs() {
  if (s_fs_ready || s_fs_failed) {
    return s_fs_ready;
  }
  // Formats the partition on first use (a few seconds, once).
  s_fs_ready = LittleFS.begin(true, "/littlefs", 2, "spiffs");
  if (!s_fs_ready) {
    s_fs_failed = true;
    setError("file system mount failed");
  }
  return s_fs_ready;
}

void loadHeader() {
  s_have = false;
  if (!LittleFS.exists(kFilePath)) {
    return;
  }
  File f = LittleFS.open(kFilePath, "r");
  if (!f) {
    return;
  }
  FileHeader h = {};
  // The runways follow the airports to the end of the file, so their
  // count comes from the file size (the header is written before them).
  if (f.read(reinterpret_cast<uint8_t*>(&h), sizeof(h)) != sizeof(h) ||
      h.magic != kFileMagic || h.version != kFileVersion ||
      h.airport_count > kMaxLocalAirports) {
    Serial.printf("airports: saved file invalid (size %u, magic %08lx, "
                  "version %u, airports %u), ignoring it\n",
                  static_cast<unsigned>(f.size()),
                  static_cast<unsigned long>(h.magic),
                  static_cast<unsigned>(h.version),
                  static_cast<unsigned>(h.airport_count));
    f.close();
    return;
  }
  const size_t size = f.size();
  const size_t data_start =
      sizeof(h) + static_cast<size_t>(h.airport_count) * sizeof(LocalAirport);
  if (size < data_start || (size - data_start) % sizeof(LocalRunway) != 0 ||
      (size - data_start) / sizeof(LocalRunway) > kMaxLocalRunways) {
    f.close();
    Serial.println("airports: saved file has the wrong size, ignoring it");
    return;
  }
  h.runway_count =
      static_cast<uint16_t>((size - data_start) / sizeof(LocalRunway));
  f.close();
  s_header = h;
  s_have = true;
}

/**
 * Streams an HTTPS body to on_bytes(const char*, size_t). True only when
 * the whole body arrived.
 */
template <typename OnBytes>
bool streamUrl(const char* url, const char* name, OnBytes on_bytes) {
  if (!services::net::resolveQuickly(url, services::net::kDnsTimeoutMs,
                                     s_poll_fn)) {
    setError("DNS lookup failed");
    return false;
  }
  WiFiClientSecure client;
  services::tls::Session tls(client, "airports");
  HTTPClient http;
  if (!http.begin(client, url)) {
    setError("http.begin failed");
    return false;
  }
  http.setConnectTimeout(kConnectTimeoutMs);
  http.setTimeout(kStallTimeoutMs);
  // HTTP/1.0: a plain (unchunked) body that ends when the server closes.
  http.useHTTP10(true);
  http.setReuse(false);
  poll();
  const int code = http.GET();
  tls.setResult(code);
  if (code != HTTP_CODE_OK) {
    char text[40];
    snprintf(text, sizeof(text), "%s: HTTP %d", name, code);
    setError(text);
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  int remaining = http.getSize();  // -1 = until the server closes
  static char buffer[512];
  const unsigned long started = millis();
  unsigned long last_data = started;
  unsigned long last_yield = started;
  size_t total = 0;
  bool ok = true;
  while (stream != nullptr && remaining != 0) {
    const unsigned long now = millis();
    if (now - started >= kFileTimeoutMs || now - last_data >= kStallTimeoutMs) {
      ok = false;
      break;
    }
    const int available = stream->available();
    if (available > 0) {
      size_t want = std::min(static_cast<size_t>(available), sizeof(buffer));
      if (remaining > 0) {
        want = std::min(want, static_cast<size_t>(remaining));
      }
      const int got = stream->read(reinterpret_cast<uint8_t*>(buffer), want);
      if (got > 0) {
        on_bytes(buffer, static_cast<size_t>(got));
        total += static_cast<size_t>(got);
        if (remaining > 0) {
          remaining -= got;
        }
        last_data = now;
      }
    } else if (!stream->connected()) {
      break;
    } else {
      delay(1);
    }
    // Let the idle task run (task watchdog) and poll the BOOT button.
    if (now - last_yield >= 50) {
      last_yield = now;
      delay(1);
      poll();
    }
  }
  http.end();
  if (remaining > 0) {
    ok = false;
  }
  Serial.printf("airports: %s %u bytes in %lu s%s\n", name,
                static_cast<unsigned>(total), (millis() - started) / 1000UL,
                ok ? "" : " (incomplete)");
  if (!ok) {
    char text[40];
    snprintf(text, sizeof(text), "%s download incomplete", name);
    setError(text);
  }
  return ok;
}

/** Keeps the cap nearest candidates in slots[0..count). */
void keepNearest(Candidate* slots, size_t cap, size_t* count,
                 const Candidate& candidate) {
  if (*count < cap) {
    slots[(*count)++] = candidate;
    return;
  }
  size_t farthest = 0;
  for (size_t i = 1; i < cap; ++i) {
    if (slots[i].dist_km > slots[farthest].dist_km) {
      farthest = i;
    }
  }
  if (candidate.dist_km < slots[farthest].dist_km) {
    slots[farthest] = candidate;
  }
}

bool download(double lat, double lon) {
  HeapArray<Candidate> slot_storage(kMaxLocalAirports);
  HeapArray<uint16_t> by_id_storage(kMaxLocalAirports);
  Candidate* const slots = slot_storage.data;
  uint16_t* const by_id = by_id_storage.data;
  if (slots == nullptr || by_id == nullptr) {
    setError("not enough memory");
    return false;
  }
  size_t n = 0;

  // --- Pass 1: airports within the area ---
  {
    static constexpr const char* kCols[] = {
        "id", "type", "latitude_deg", "longitude_deg", "icao_code", "ident"};
    std::unique_ptr<CsvColumns<6>> csv(new (std::nothrow) CsvColumns<6>(kCols));
    if (!csv) {
      setError("not enough memory");
      return false;
    }
    auto on_row = [&](const char* const* f) {
      AirportType type;
      double a_lat = 0.0;
      double a_lon = 0.0;
      if (!parseAirportType(f[1], &type) || !parseDouble(f[2], &a_lat) ||
          !parseDouble(f[3], &a_lon)) {
        return;
      }
      const float dist = distanceKm(lat, lon, a_lat, a_lon);
      if (dist > kAreaRadiusKm) {
        return;
      }
      Candidate c = {};
      chooseLabel(f[4], f[5], c.airport.label);
      c.airport.type = static_cast<uint8_t>(type);
      c.airport.lat_e7 = degToE7(a_lat);
      c.airport.lon_e7 = degToE7(a_lon);
      c.id = static_cast<int32_t>(strtol(f[0], nullptr, 10));
      c.dist_km = dist;
      keepNearest(slots, kMaxLocalAirports, &n, c);
    };
    const bool ok = streamUrl(kAirportsUrl, "airports.csv",
                              [&](const char* data, size_t len) {
                                csv->feed(data, len, on_row);
                              });
    csv->finish(on_row);
    if (!ok) {
      return false;
    }
    if (!csv->headerComplete()) {
      setError("airports.csv columns changed");
      return false;
    }
  }

  for (size_t i = 0; i < n; ++i) {
    by_id[i] = static_cast<uint16_t>(i);
  }
  std::sort(by_id, by_id + n, [&](uint16_t a, uint16_t b) {
    return slots[a].id < slots[b].id;
  });

  // --- Pass 2: open runways of those airports (kept in RAM; the file is
  // written once, after both downloads, never while TLS is open) ---
  HeapArray<LocalRunway> runway_storage(kMaxLocalRunways);
  LocalRunway* const runways = runway_storage.data;
  if (runways == nullptr) {
    setError("not enough memory");
    return false;
  }
  size_t runway_count = 0;
  if (n > 0) {
    static constexpr const char* kCols[] = {
        "airport_ref",     "length_ft",        "closed",
        "le_ident",        "le_latitude_deg",  "le_longitude_deg",
        "le_heading_degT", "he_latitude_deg",  "he_longitude_deg"};
    std::unique_ptr<CsvColumns<9>> csv(new (std::nothrow) CsvColumns<9>(kCols));
    if (!csv) {
      setError("not enough memory");
      return false;
    }
    auto on_row = [&](const char* const* r) {
      if (runway_count >= kMaxLocalRunways || strcmp(r[2], "1") == 0) {
        return;
      }
      const int32_t ref = static_cast<int32_t>(strtol(r[0], nullptr, 10));
      const uint16_t* hit = std::lower_bound(
          by_id, by_id + n, ref,
          [&](uint16_t idx, int32_t id) { return slots[idx].id < id; });
      if (hit == by_id + n || slots[*hit].id != ref) {
        return;
      }
      const LocalAirport& ap = slots[*hit].airport;
      double ends[4];
      if (!runwayEnds(r[4], r[5], r[7], r[8], r[6], r[3], r[1],
                      e7ToDeg(ap.lat_e7), e7ToDeg(ap.lon_e7), ends)) {
        return;
      }
      LocalRunway& rw = runways[runway_count++];
      rw = {};
      rw.airport_idx = *hit;
      rw.le_lat_e7 = degToE7(ends[0]);
      rw.le_lon_e7 = degToE7(ends[1]);
      rw.he_lat_e7 = degToE7(ends[2]);
      rw.he_lon_e7 = degToE7(ends[3]);
    };
    const bool ok = streamUrl(kRunwaysUrl, "runways.csv",
                              [&](const char* data, size_t len) {
                                csv->feed(data, len, on_row);
                              });
    csv->finish(on_row);
    if (!ok) {
      return false;
    }
    if (!csv->headerComplete()) {
      setError("runways.csv columns changed");
      return false;
    }
  }

  // --- Write the file in one go, then check it reads back ---
  FileHeader header = {};
  header.magic = kFileMagic;
  header.version = kFileVersion;
  header.radius_km = static_cast<uint16_t>(kAreaRadiusKm);
  header.center_lat_e7 = degToE7(lat);
  header.center_lon_e7 = degToE7(lon);
  header.airport_count = static_cast<uint16_t>(n);
  header.runway_count = static_cast<uint16_t>(runway_count);
  const size_t expected_size = sizeof(header) + n * sizeof(LocalAirport) +
                               runway_count * sizeof(LocalRunway);

  if (s_read_file) {
    s_read_file.close();
  }
  if (LittleFS.exists(kTempPath)) {
    LittleFS.remove(kTempPath);
  }
  File f = LittleFS.open(kTempPath, FILE_WRITE);
  if (!f) {
    setError("cannot create file");
    return false;
  }
  bool write_ok = f.write(reinterpret_cast<const uint8_t*>(&header),
                          sizeof(header)) == sizeof(header);
  for (size_t i = 0; i < n && write_ok; ++i) {
    write_ok = f.write(reinterpret_cast<const uint8_t*>(&slots[i].airport),
                       sizeof(LocalAirport)) == sizeof(LocalAirport);
  }
  if (write_ok && runway_count > 0) {
    const size_t bytes = runway_count * sizeof(LocalRunway);
    write_ok = f.write(reinterpret_cast<const uint8_t*>(runways), bytes) ==
               bytes;
  }
  f.flush();
  f.close();
  File check = LittleFS.open(kTempPath, "r");
  const size_t written = check ? check.size() : 0;
  check.close();
  if (!write_ok || written != expected_size) {
    char text[40];
    snprintf(text, sizeof(text), "file write failed (%u of %u)",
             static_cast<unsigned>(written),
             static_cast<unsigned>(expected_size));
    setError(text);
    LittleFS.remove(kTempPath);
    return false;
  }
  if (LittleFS.exists(kFilePath)) {
    LittleFS.remove(kFilePath);
  }
  if (!LittleFS.rename(kTempPath, kFilePath)) {
    setError("file rename failed");
    return false;
  }
  loadHeader();
  Serial.printf("airports: saved %u medium airports, %u runways\n",
                static_cast<unsigned>(n), static_cast<unsigned>(runway_count));
  return s_have;
}

}  // namespace

void init() {
  if (mountFs()) {
    loadHeader();
    if (s_have) {
      Serial.printf("airports: %u medium airports, %u runways saved\n",
                    s_header.airport_count, s_header.runway_count);
    }
  }
}

void setPollFn(PollFn fn) { s_poll_fn = fn; }

bool hasDataFor(double lat, double lon) {
  return s_have &&
         distanceKm(lat, lon, e7ToDeg(s_header.center_lat_e7),
                    e7ToDeg(s_header.center_lon_e7)) <= kRefreshDistanceKm;
}

bool refreshDue(double lat, double lon) {
  if (!mountFs() || hasDataFor(lat, lon)) {
    return false;
  }
  const bool moved_since_attempt =
      distanceKm(lat, lon, s_attempt_lat, s_attempt_lon) > kRefreshDistanceKm;
  return s_last_attempt_ms == 0 || moved_since_attempt ||
         millis() - s_last_attempt_ms >= kRetryMs;
}

bool refresh(double lat, double lon) {
  if (!mountFs()) {
    return false;
  }
  s_last_attempt_ms = millis();
  s_attempt_lat = lat;
  s_attempt_lon = lon;
  s_last_error[0] = '\0';
  Serial.printf("airports: downloading for %.4f, %.4f\n", lat, lon);
  const unsigned long started = millis();
  const bool ok = download(lat, lon);
  Serial.printf("airports: %s after %lu s\n", ok ? "done" : "failed",
                (millis() - started) / 1000UL);
  return ok;
}

uint16_t airportCount() { return s_have ? s_header.airport_count : 0; }

uint16_t runwayCount() { return s_have ? s_header.runway_count : 0; }


void formatStatus(char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  if (s_fs_failed) {
    snprintf(out, out_len, "Local airports unavailable (file system).");
  } else if (s_last_error[0] != '\0') {
    snprintf(out, out_len,
             "Last airport download failed (%s); it retries every 15 "
             "minutes.",
             s_last_error);
  } else if (s_have) {
    snprintf(out, out_len,
             "%u medium airports saved for the area around the radar "
             "(from OurAirports).",
             s_header.airport_count);
  } else {
    snprintf(out, out_len,
             "Medium airports download from OurAirports shortly after "
             "start-up and after the location changes.");
  }
}

bool beginRead() {
  if (!s_have) {
    return false;
  }
  if (!s_read_file) {
    s_read_file = LittleFS.open(kFilePath, "r");
  }
  return static_cast<bool>(s_read_file);
}

bool readAirport(uint16_t index, LocalAirport* out) {
  if (!s_read_file || index >= s_header.airport_count) {
    return false;
  }
  const size_t offset = sizeof(FileHeader) + index * sizeof(LocalAirport);
  return s_read_file.seek(offset) &&
         s_read_file.read(reinterpret_cast<uint8_t*>(out), sizeof(*out)) ==
             sizeof(*out);
}

bool readRunway(uint16_t index, LocalRunway* out) {
  if (!s_read_file || index >= s_header.runway_count) {
    return false;
  }
  const size_t offset = sizeof(FileHeader) +
                        s_header.airport_count * sizeof(LocalAirport) +
                        index * sizeof(LocalRunway);
  return s_read_file.seek(offset) &&
         s_read_file.read(reinterpret_cast<uint8_t*>(out), sizeof(*out)) ==
             sizeof(*out);
}

void endRead() {
  if (s_read_file) {
    s_read_file.close();
  }
}

}  // namespace services::airports
