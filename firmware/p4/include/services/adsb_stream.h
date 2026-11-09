#pragma once

/**
 * Pieces of the ADS-B client that don't touch the network stack directly,
 * kept header-only so the native unit tests (test/) can compile them.
 */

#include <Arduino.h>  // millis(), delay()
#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "services/adsb_client.h"  // AircraftKind

namespace services::adsb {

/**
 * ArduinoJson custom reader over an HTTP body stream. Lets the aircraft JSON
 * be parsed as it arrives, so the whole response never sits in RAM, and
 * enforces a timeout. Reads in small chunks: per-byte reads from a TLS stream
 * are slow.
 *
 * TStream needs available(), read(uint8_t*, size_t) and connected().
 * poll (optional) is called while waiting and every few chunks; keep it
 * light, it runs in the middle of an HTTP request.
 */
template <typename TStream>
class PollingStreamReader {
 public:
  using PollFn = void (*)();

  PollingStreamReader(TStream& stream, int content_length,
                      unsigned long timeout_ms, PollFn poll = nullptr)
      : stream_(stream),
        poll_(poll),
        remaining_(content_length),
        timeout_ms_(timeout_ms),
        started_ms_(millis()) {}

  int read() {
    if (pos_ >= len_ && !refill()) {
      return -1;
    }
    return static_cast<unsigned char>(buffer_[pos_++]);
  }

  size_t readBytes(char* out, size_t length) {
    size_t copied = 0;
    while (copied < length) {
      if (pos_ >= len_ && !refill()) {
        break;
      }
      const size_t n = std::min(length - copied, len_ - pos_);
      memcpy(out + copied, buffer_ + pos_, n);
      pos_ += n;
      copied += n;
    }
    return copied;
  }

  bool timedOut() const { return timed_out_; }

 private:
  void poll() {
    if (poll_ != nullptr) {
      poll_();
    }
  }

  bool refill() {
    pos_ = 0;
    len_ = 0;
    if (remaining_ == 0) {
      return false;  // Content-Length fully consumed
    }
    while (true) {
      if (millis() - started_ms_ >= timeout_ms_) {
        timed_out_ = true;
        return false;
      }
      const int available = stream_.available();
      if (available > 0) {
        size_t want = std::min(static_cast<size_t>(available), sizeof(buffer_));
        if (remaining_ > 0) {
          want = std::min(want, static_cast<size_t>(remaining_));
        }
        const int got =
            stream_.read(reinterpret_cast<uint8_t*>(buffer_), want);
        if (got > 0) {
          len_ = static_cast<size_t>(got);
          if (remaining_ > 0) {
            remaining_ -= got;
          }
          if (++chunks_since_poll_ >= 4) {
            chunks_since_poll_ = 0;
            poll();
          }
          return true;
        }
      }
      if (!stream_.connected() && stream_.available() <= 0) {
        return false;  // server closed the connection: end of body
      }
      poll();
      delay(1);
    }
  }

  TStream& stream_;
  PollFn poll_;
  int remaining_;  // -1 = unknown length (read until close)
  unsigned long timeout_ms_;
  unsigned long started_ms_;
  char buffer_[256];
  size_t pos_ = 0;
  size_t len_ = 0;
  int chunks_since_poll_ = 0;
  bool timed_out_ = false;
};

/**
 * Keep only the fields the display uses from each aircraft object. The
 * adsb.fi v3 response has ~50 fields per aircraft; without a filter
 * ArduinoJson stores them all. Built once so this small, long-lived
 * allocation happens early.
 */
inline const JsonDocument& aircraftFieldFilter() {
  static JsonDocument filter;
  static bool built = false;
  if (!built) {
    for (const char* key :
         {"lat", "lon", "true_heading", "mag_heading", "track", "dir", "gs",
          "tas", "ias", "alt_baro", "alt_geom", "flight", "hex", "t",
          "category", "dbFlags", "seen_pos"}) {
      filter[key] = true;
    }
    built = true;
  }
  return filter;
}

/** Wraps a reader so one character can be pushed back. */
template <typename TReader>
class PushbackReader {
 public:
  explicit PushbackReader(TReader& reader) : reader_(reader) {}

  void unread(int ch) { pending_ = ch; }

  int read() {
    if (pending_ >= 0) {
      const int ch = pending_;
      pending_ = -1;
      return ch;
    }
    return reader_.read();
  }

  size_t readBytes(char* out, size_t length) {
    size_t copied = 0;
    if (length > 0 && pending_ >= 0) {
      out[copied++] = static_cast<char>(pending_);
      pending_ = -1;
    }
    return copied + reader_.readBytes(out + copied, length - copied);
  }

 private:
  TReader& reader_;
  int pending_ = -1;
};

inline bool isJsonSpace(int ch) {
  return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
}

template <typename TReader>
int readNonSpace(TReader& reader) {
  int ch = 0;
  do {
    ch = reader.read();
  } while (isJsonSpace(ch));
  return ch;
}

/**
 * Parses the response's "ac" (or "aircraft") array one aircraft at a time, calling
 * on_plane(JsonObject) for each. Only one aircraft is held in plane_doc at
 * once, so RAM use doesn't grow with the number of aircraft in range (the
 * whole filtered array was ~250 B per aircraft: 60-90 KB for a busy sky at
 * 100 km, while a TLS session is also open). Stops after the closing ']';
 * the rest of the body ("msg", "now", ...) is not read.
 */
template <typename TReader, typename Fn>
DeserializationError forEachAircraft(TReader& source, JsonDocument& plane_doc,
                                     Fn on_plane) {
  PushbackReader<TReader> reader(source);

  // Find the aircraft list key: "ac" (adsb.fi) or "aircraft" (a local
  // readsb / tar1090 / dump1090-fa aircraft.json), followed by ':'. Strings
  // are read whole, so a value that happens to be "ac" (followed by ',' or
  // '}') or a longer key containing it doesn't match.
  bool any_input = false;
  int ch = 0;
  while (true) {
    ch = reader.read();
    if (ch < 0) {
      return any_input ? DeserializationError::IncompleteInput
                       : DeserializationError::EmptyInput;
    }
    any_input = true;
    if (ch != '"') {
      continue;
    }
    char name[10];
    size_t len = 0;
    bool fits = true;
    while (true) {
      ch = reader.read();
      if (ch < 0) {
        return DeserializationError::IncompleteInput;
      }
      if (ch == '"') {
        break;
      }
      if (ch == '\\') {
        ch = reader.read();  // escaped character: never part of our keys
        fits = false;
        if (ch < 0) {
          return DeserializationError::IncompleteInput;
        }
        continue;
      }
      if (len + 1 < sizeof(name)) {
        name[len++] = static_cast<char>(ch);
      } else {
        fits = false;
      }
    }
    name[len] = '\0';
    const bool wanted =
        fits && (strcmp(name, "ac") == 0 || strcmp(name, "aircraft") == 0);
    ch = readNonSpace(reader);
    if (ch < 0) {
      return DeserializationError::IncompleteInput;
    }
    if (wanted && ch == ':') {
      break;
    }
    if (ch == '"') {
      reader.unread(ch);  // next string starts right away
    }
  }
  ch = readNonSpace(reader);
  if (ch != '[') {
    return ch < 0 ? DeserializationError::IncompleteInput
                  : DeserializationError::InvalidInput;
  }

  ch = readNonSpace(reader);
  if (ch == ']') {
    return DeserializationError::Ok;
  }
  while (true) {
    if (ch < 0) {
      return DeserializationError::IncompleteInput;
    }
    reader.unread(ch);
    // deserializeJson stops right after the object's closing brace.
    const DeserializationError err = deserializeJson(
        plane_doc, reader, DeserializationOption::Filter(aircraftFieldFilter()));
    if (err) {
      return err;
    }
    if (plane_doc.is<JsonObject>()) {
      on_plane(plane_doc.as<JsonObject>());
    }
    ch = readNonSpace(reader);
    if (ch == ']') {
      return DeserializationError::Ok;
    }
    if (ch != ',') {
      return ch < 0 ? DeserializationError::IncompleteInput
                    : DeserializationError::InvalidInput;
    }
    ch = readNonSpace(reader);
  }
}

/**
 * Common light piston types by ICAO type designator ("t" in adsb.fi). Many
 * of these fly with older transponders that send no emitter category (about
 * 40% of the light aircraft in a sample near London), so the category alone
 * misses them.
 */
inline bool isLightTypeDesignator(const char* type) {
  static constexpr char kLightTypes[][5] = {
      // Cessna
      "C150", "C152", "C162", "C170", "C172", "C175", "C177", "C180", "C182",
      "C185", "C206", "C210", "C310", "C337", "C340",
      // Piper
      "J3", "PA18", "PA22", "PA24", "PA30", "PA32", "PA34", "PA38", "PA44",
      "PA46", "P28A", "P28B", "P28R", "P28T", "P32R", "P32T",
      // Diamond, Cirrus, Mooney, Beech, Socata, Grumman
      "DA20", "DV20", "DA40", "DA42", "DA50", "DA62", "SR20", "SR22", "S22T",
      "M20P", "M20T", "BE33", "BE35", "BE36", "BE55", "BE58", "BE76", "TB9",
      "TB10", "TB20", "TB21", "AA1", "AA5",
      // Robin, Grob, Bulldog, Citabria, Vans, others seen over the UK
      "DR40", "HR20", "G115", "BDOG", "CH7A", "RV4", "RV6", "RV7", "RV8",
      "RV9", "RV10", "RV12", "RV14", "EFOX", "SKRA", "EV97", "GLID"};
  if (type == nullptr || type[0] == '\0') {
    return false;
  }
  for (const char* candidate : kLightTypes) {
    if (strcmp(type, candidate) == 0) {
      return true;
    }
  }
  return false;
}

/**
 * From the ADS-B emitter category, the ICAO type designator and adsb.fi's
 * dbFlags (bit 0 = military). Light: A1 (under 15,500 lb, e.g. a Cessna
 * 172), B1 glider, B4 ultralight / hang glider, or a known light type.
 */
inline AircraftKind classifyAircraft(const char* category, int db_flags,
                                     const char* type = nullptr) {
  if ((db_flags & 1) != 0) {
    return AircraftKind::kMilitary;
  }
  if (category != nullptr && category[0] != '\0' && category[1] != '\0' &&
      category[2] == '\0' &&
      ((category[0] == 'A' && category[1] == '1') ||
       (category[0] == 'B' && (category[1] == '1' || category[1] == '4')))) {
    return AircraftKind::kLight;
  }
  if (isLightTypeDesignator(type)) {
    return AircraftKind::kLight;
  }
  return AircraftKind::kStandard;
}

/**
 * Chooses which of an unbounded stream of candidates to keep so that the
 * N nearest (smallest distance) end up stored. Call slotFor() per candidate:
 * it returns the slot to write the candidate into, or -1 to skip it.
 */
template <size_t N>
class NearestSet {
 public:
  int slotFor(float dist_sq) {
    if (count_ < N) {
      dist_sq_[count_] = dist_sq;
      return static_cast<int>(count_++);
    }
    size_t farthest = 0;
    for (size_t i = 1; i < N; ++i) {
      if (dist_sq_[i] > dist_sq_[farthest]) {
        farthest = i;
      }
    }
    if (dist_sq >= dist_sq_[farthest]) {
      return -1;
    }
    dist_sq_[farthest] = dist_sq;
    return static_cast<int>(farthest);
  }

  size_t count() const { return count_; }

 private:
  float dist_sq_[N] = {};
  size_t count_ = 0;
};

/** Squared distance in km² on a local flat projection (for ranking only). */
inline float approxDistSqKm(double center_lat, double center_lon, float lat,
                            float lon) {
  constexpr double kKmPerDeg = 111.0;
  constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
  double dlon = static_cast<double>(lon) - center_lon;
  if (dlon > 180.0) {
    dlon -= 360.0;
  } else if (dlon < -180.0) {
    dlon += 360.0;
  }
  const double dx = dlon * kKmPerDeg * cos(center_lat * kDegToRad);
  const double dy = (static_cast<double>(lat) - center_lat) * kKmPerDeg;
  return static_cast<float>(dx * dx + dy * dy);
}

}  // namespace services::adsb
