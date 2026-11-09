#pragma once

/**
 * Pieces of the local-airport download that don't touch the network or the
 * file system, kept header-only so the native unit tests can compile them:
 * a streaming CSV reader, the area filter and the runway geometry.
 *
 * Data: OurAirports airports.csv / runways.csv (public domain).
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace services::airports {

// --- Stored records (also the on-flash file layout) ---

/** Only medium airports are downloaded (large ones are built in). */
enum class AirportType : uint8_t { kMedium = 1 };

struct LocalAirport {
  /** ICAO code if it has one, otherwise the OurAirports ident. */
  char label[9];
  uint8_t type;  // AirportType
  uint8_t has_runway;
  uint8_t reserved;
  int32_t lat_e7;
  int32_t lon_e7;
};

struct LocalRunway {
  uint16_t airport_idx;
  uint16_t reserved;
  int32_t le_lat_e7;
  int32_t le_lon_e7;
  int32_t he_lat_e7;
  int32_t he_lon_e7;
};

constexpr uint32_t kFileMagic = 0x4C415054;  // "LAPT"
constexpr uint16_t kFileVersion = 1;

struct FileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t radius_km;
  int32_t center_lat_e7;
  int32_t center_lon_e7;
  uint16_t airport_count;
  uint16_t runway_count;
};

/** Keep at most this many, nearest first (London has 25 within 140 km). */
constexpr size_t kMaxLocalAirports = 64;
constexpr size_t kMaxLocalRunways = 192;
/** 100 km ring = 133 km to the outer ring; a little margin. */
constexpr float kAreaRadiusKm = 140.0f;

inline int32_t degToE7(double deg) {
  return static_cast<int32_t>(llround(deg * 1e7));
}

inline double e7ToDeg(int32_t e7) { return static_cast<double>(e7) * 1e-7; }

/** Flat-earth distance in km (fine within a few hundred km). */
inline float distanceKm(double lat0, double lon0, double lat1, double lon1) {
  constexpr double kKmPerDeg = 111.0;
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  double dlon = lon1 - lon0;
  if (dlon > 180.0) {
    dlon -= 360.0;
  } else if (dlon < -180.0) {
    dlon += 360.0;
  }
  const double dx = dlon * kKmPerDeg * cos(lat0 * kDegToRad);
  const double dy = (lat1 - lat0) * kKmPerDeg;
  return static_cast<float>(sqrt(dx * dx + dy * dy));
}

/** Parses a decimal number; false for blank or junk. */
inline bool parseDouble(const char* text, double* out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  const double value = strtod(text, &end);
  if (end == text || *end != '\0' || !std::isfinite(value)) {
    return false;
  }
  *out = value;
  return true;
}

/**
 * Streaming CSV reader for a fixed set of columns, found by name in the
 * header row. Feed it bytes in any chunk sizes; onRow() gets the wanted
 * fields of each data row (blank when missing). Quoted fields with commas,
 * doubled quotes and CRLF line ends are handled. Values longer than
 * kFieldMax - 1 are cut.
 */
template <size_t kColumns, size_t kFieldMax = 24>
class CsvColumns {
 public:
  explicit CsvColumns(const char* const (&names)[kColumns]) : names_(names) {
    for (size_t i = 0; i < kColumns; ++i) {
      index_[i] = -1;
    }
    clearRow();
  }

  /** Calls on_row(const char* const* fields) for each complete data row. */
  template <typename OnRow>
  void feed(const char* data, size_t len, OnRow on_row) {
    for (size_t i = 0; i < len; ++i) {
      feedChar(data[i], on_row);
    }
  }

  /** Call at end of input to flush a last row without a newline. */
  template <typename OnRow>
  void finish(OnRow on_row) {
    if (column_ > 0 || cell_len_ > 0) {
      feedChar('\n', on_row);
    }
  }

  /** True when every wanted column was found in the header. */
  bool headerComplete() const {
    if (!header_done_) {
      return false;
    }
    for (size_t i = 0; i < kColumns; ++i) {
      if (index_[i] < 0) {
        return false;
      }
    }
    return true;
  }

  size_t rows() const { return rows_; }

 private:
  template <typename OnRow>
  void feedChar(char ch, OnRow on_row) {
    if (in_quotes_) {
      if (ch == '"') {
        in_quotes_ = false;
        after_quote_ = true;
      } else {
        appendChar(ch);
      }
      return;
    }
    if (ch == '"') {
      if (after_quote_) {
        appendChar('"');  // doubled quote inside a quoted field
      }
      in_quotes_ = true;
      after_quote_ = false;
      return;
    }
    after_quote_ = false;
    if (ch == ',') {
      endCell();
      return;
    }
    if (ch == '\r') {
      return;
    }
    if (ch == '\n') {
      endCell();
      endRow(on_row);
      return;
    }
    appendChar(ch);
  }

  void appendChar(char ch) {
    if (cell_len_ + 1 < sizeof(cell_)) {
      cell_[cell_len_++] = ch;
    }
  }

  void endCell() {
    cell_[cell_len_] = '\0';
    if (!header_done_) {
      for (size_t i = 0; i < kColumns; ++i) {
        if (strcmp(cell_, names_[i]) == 0) {
          index_[i] = static_cast<int>(column_);
        }
      }
    } else {
      for (size_t i = 0; i < kColumns; ++i) {
        if (index_[i] == static_cast<int>(column_)) {
          memcpy(fields_[i], cell_, cell_len_ + 1 <= kFieldMax
                                        ? cell_len_ + 1
                                        : kFieldMax);
          fields_[i][kFieldMax - 1] = '\0';
        }
      }
    }
    cell_len_ = 0;
    ++column_;
  }

  template <typename OnRow>
  void endRow(OnRow on_row) {
    if (!header_done_) {
      header_done_ = true;
    } else if (column_ > 1) {
      ++rows_;
      const char* row[kColumns];
      for (size_t i = 0; i < kColumns; ++i) {
        row[i] = fields_[i];
      }
      on_row(row);
    }
    clearRow();
  }

  void clearRow() {
    for (size_t i = 0; i < kColumns; ++i) {
      fields_[i][0] = '\0';
    }
    column_ = 0;
    cell_len_ = 0;
    in_quotes_ = false;
    after_quote_ = false;
  }

  const char* const (&names_)[kColumns];
  int index_[kColumns];
  char fields_[kColumns][kFieldMax] = {};
  // Wide enough for every wanted value; longer unwanted cells are cut.
  char cell_[kFieldMax] = {};
  size_t cell_len_ = 0;
  size_t column_ = 0;
  size_t rows_ = 0;
  bool header_done_ = false;
  bool in_quotes_ = false;
  bool after_quote_ = false;
};

/** Medium from the OurAirports "type" column; false for all others. */
inline bool parseAirportType(const char* type, AirportType* out) {
  if (strcmp(type, "medium_airport") == 0) {
    *out = AirportType::kMedium;
    return true;
  }
  // Large (built in), small airfields (too many: 400+ near London, most
  // without runway data), heliports, closed, seaplane bases, balloonports.
  return false;
}

/** Copies up to 8 characters of the best label: ICAO, then ident. */
inline void chooseLabel(const char* icao, const char* ident, char* out) {
  const char* src = (icao != nullptr && icao[0] != '\0') ? icao : ident;
  size_t n = 0;
  if (src != nullptr) {
    for (; n < 8 && src[n] != '\0'; ++n) {
      out[n] = src[n];
    }
  }
  out[n] = '\0';
}

/**
 * Runway ends. Uses the surveyed end coordinates when both are present;
 * otherwise centres the strip on the airport using its length and the
 * true heading, or the heading from the runway number ("09" = 90 deg).
 * False when there's nothing to draw from.
 */
inline bool runwayEnds(const char* le_lat, const char* le_lon,
                       const char* he_lat, const char* he_lon,
                       const char* heading_true, const char* le_ident,
                       const char* length_ft, double airport_lat,
                       double airport_lon, double* out /* le lat, lon, he lat, lon */) {
  double a = 0.0;
  double b = 0.0;
  double c = 0.0;
  double d = 0.0;
  if (parseDouble(le_lat, &a) && parseDouble(le_lon, &b) &&
      parseDouble(he_lat, &c) && parseDouble(he_lon, &d)) {
    out[0] = a;
    out[1] = b;
    out[2] = c;
    out[3] = d;
    return true;
  }

  double length = 0.0;
  if (!parseDouble(length_ft, &length) || length <= 0.0 || length > 20000.0) {
    return false;
  }
  double heading = 0.0;
  if (!parseDouble(heading_true, &heading)) {
    if (le_ident == nullptr || le_ident[0] < '0' || le_ident[0] > '3' ||
        le_ident[1] < '0' || le_ident[1] > '9') {
      return false;
    }
    const int number = (le_ident[0] - '0') * 10 + (le_ident[1] - '0');
    if (number < 1 || number > 36) {
      return false;
    }
    heading = number * 10.0;
  }

  constexpr double kKmPerDeg = 111.0;
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  const double half_km = length * 0.0003048 / 2.0;
  const double east = sin(heading * kDegToRad) * half_km;
  const double north = cos(heading * kDegToRad) * half_km;
  const double km_per_deg_lon = kKmPerDeg * cos(airport_lat * kDegToRad);
  if (km_per_deg_lon < 1.0) {
    return false;  // too near a pole to matter
  }
  out[0] = airport_lat - north / kKmPerDeg;
  out[1] = airport_lon - east / km_per_deg_lon;
  out[2] = airport_lat + north / kKmPerDeg;
  out[3] = airport_lon + east / km_per_deg_lon;
  return true;
}

}  // namespace services::airports
