/**
 * ADS-B response handling: streamed + filtered JSON parse, timeouts, and
 * nearest-N aircraft selection.
 */

#include <unity.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "services/adsb_stream.h"

using services::adsb::AircraftKind;
using services::adsb::classifyAircraft;
using services::adsb::forEachAircraft;
using services::adsb::approxDistSqKm;
using services::adsb::NearestSet;
using services::adsb::PollingStreamReader;

namespace {

int g_polls = 0;
void countPoll() { ++g_polls; }

/** Fake network stream: random-sized chunks, optional stall halfway. */
struct FakeStream {
  std::string data;
  size_t pos = 0;
  bool open = true;
  bool stall = false;
  unsigned seed = 1;

  int available() {
    if (pos >= data.size() || (stall && pos > data.size() / 2)) {
      return 0;
    }
    seed = seed * 1103515245u + 12345u;
    const size_t chunk = 1 + (seed >> 16) % 1500;
    return static_cast<int>(std::min(chunk, data.size() - pos));
  }
  int read(uint8_t* out, size_t n) {
    const size_t a = std::min(n, data.size() - pos);
    memcpy(out, data.data() + pos, a);
    pos += a;
    return static_cast<int>(a);
  }
  bool connected() { return open && (pos < data.size() || stall); }
};

/** One aircraft in the adsb.fi v3 shape, with many fields the UI ignores. */
std::string makeAircraft(int i) {
  char b[1200];
  snprintf(
      b, sizeof(b),
      "{\"hex\":\"%06x\",\"type\":\"adsb_icao\",\"flight\":\"BAW%-5d\","
      "\"r\":\"G-EUPA\",\"t\":\"A320\",\"desc\":\"AIRBUS A-320\","
      "\"ownOp\":\"British Airways\",\"alt_baro\":%s,\"alt_geom\":%d,"
      "\"gs\":%.1f,\"ias\":250,\"tas\":290,\"mach\":0.452,\"track\":%.2f,"
      "\"mag_heading\":%.2f,\"true_heading\":%.2f,\"baro_rate\":-640,"
      "\"squawk\":\"4512\",\"category\":\"A3\",\"lat\":%.6f,\"lon\":%.6f,"
      "\"nic\":8,\"rc\":186,\"seen_pos\":0.4,\"mlat\":[],\"tisb\":[],"
      "\"messages\":8211,\"seen\":0.1,\"rssi\":-18.3,\"dst\":12.3,"
      "\"dir\":%.1f,\"dbFlags\":%d}",
      0x400000 + i, i,
      i % 10 == 0 ? "\"ground\"" : std::to_string(3000 + i * 100).c_str(),
      3100 + i * 100, 220.0 + i, i * 4.5, i * 4.4, i * 4.6, 51.5 + i * 0.001,
      -1.7 - i * 0.001, i * 4.0, i % 13 == 0 ? 1 : 0);
  return b;
}

std::string makeResponse(int n) {
  std::string s = "{\"ac\":[";
  for (int i = 0; i < n; ++i) {
    if (i != 0) s += ",";
    s += makeAircraft(i);
  }
  s += "],\"msg\":\"No error\",\"now\":1730000000000,\"total\":" +
       std::to_string(n) + "}";
  return s;
}

constexpr int kCount = 100;

/** What the tests keep from each parsed aircraft. */
struct Parsed {
  float lat;
  std::string flight;
  std::string type;
  float dir;
  std::string alt_baro;  // "ground" or the number as text
  int db_flags;
  bool has_unused_fields;
};

std::vector<Parsed> g_parsed;

template <typename TReader>
DeserializationError parseAll(TReader& r) {
  g_parsed.clear();
  JsonDocument plane;
  return forEachAircraft(r, plane, [](JsonObject p) {
    Parsed out;
    out.lat = p["lat"].as<float>();
    out.flight = p["flight"] | "";
    out.type = p["t"] | "";
    out.dir = p["dir"].as<float>();
    out.alt_baro = p["alt_baro"].is<const char*>()
                       ? std::string(p["alt_baro"].as<const char*>())
                       : std::to_string(p["alt_baro"].as<int>());
    out.db_flags = p["dbFlags"] | 0;
    // Fields the display doesn't use are dropped by the filter.
    out.has_unused_fields = !p["rssi"].isNull() || !p["squawk"].isNull() ||
                            !p["ownOp"].isNull();
    g_parsed.push_back(out);
  });
}

void checkParsed() {
  TEST_ASSERT_EQUAL_UINT(kCount, g_parsed.size());
  const Parsed& p = g_parsed[7];
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 51.507f, p.lat);
  TEST_ASSERT_EQUAL_STRING("BAW7    ", p.flight.c_str());
  TEST_ASSERT_EQUAL_STRING("A320", p.type.c_str());
  TEST_ASSERT_EQUAL_FLOAT(28.0f, p.dir);
  for (const Parsed& each : g_parsed) {
    TEST_ASSERT_FALSE(each.has_unused_fields);
  }
  // alt_baro may be "ground" or a number.
  TEST_ASSERT_EQUAL_STRING("ground", g_parsed[0].alt_baro.c_str());
  TEST_ASSERT_EQUAL_STRING("3100", g_parsed[1].alt_baro.c_str());
  TEST_ASSERT_EQUAL_INT(1, g_parsed[13].db_flags);
  TEST_ASSERT_EQUAL_INT(0, g_parsed[14].db_flags);
}

}  // namespace

void setUp() { g_polls = 0; }
void tearDown() {}

void test_stream_with_content_length() {
  FakeStream s;
  s.data = makeResponse(kCount);
  s.seed = 7;
  PollingStreamReader<FakeStream> r(s, static_cast<int>(s.data.size()), 10000,
                                    countPoll);
  const auto err = parseAll(r);
  TEST_ASSERT_EQUAL_STRING("Ok", err.c_str());
  checkParsed();
  TEST_ASSERT_GREATER_THAN_INT(0, g_polls);
}

void test_stream_until_close() {
  FakeStream s;
  s.data = makeResponse(kCount);
  s.seed = 8;
  PollingStreamReader<FakeStream> r(s, -1, 10000, countPoll);
  const auto err = parseAll(r);
  TEST_ASSERT_EQUAL_STRING("Ok", err.c_str());
  checkParsed();
}

void test_stalled_stream_times_out() {
  FakeStream s;
  s.data = makeResponse(kCount);
  s.stall = true;
  const unsigned long start = millis();
  PollingStreamReader<FakeStream> r(s, static_cast<int>(s.data.size()), 300);
  const auto err = parseAll(r);
  TEST_ASSERT_TRUE(err != DeserializationError::Ok);
  TEST_ASSERT_TRUE(r.timedOut());
  TEST_ASSERT_LESS_THAN_UINT32(1000, millis() - start);
}

void test_stops_at_content_length() {
  FakeStream s;
  const std::string body = makeResponse(kCount);
  s.data = body + "GARBAGE";
  PollingStreamReader<FakeStream> r(s, static_cast<int>(body.size()), 5000);
  const auto err = parseAll(r);
  TEST_ASSERT_EQUAL_STRING("Ok", err.c_str());
  TEST_ASSERT_EQUAL_UINT(kCount, g_parsed.size());
  TEST_ASSERT_LESS_OR_EQUAL_UINT(body.size(), s.pos);  // never past the body
}

void test_empty_body_is_error() {
  FakeStream s;
  s.open = false;
  PollingStreamReader<FakeStream> r(s, 0, 1000);
  const auto err = parseAll(r);
  TEST_ASSERT_TRUE(err == DeserializationError::EmptyInput);
}

void test_empty_aircraft_list() {
  FakeStream s;
  s.data = "{\"msg\":\"No error\",\"ac\":[]}";
  PollingStreamReader<FakeStream> r(s, static_cast<int>(s.data.size()), 1000);
  const auto err = parseAll(r);
  TEST_ASSERT_EQUAL_STRING("Ok", err.c_str());
  TEST_ASSERT_EQUAL_UINT(0, g_parsed.size());
}

void test_whitespace_and_key_order() {
  FakeStream s;
  s.data = "{ \"msg\" : \"ac\" , \"ac\" : [ \n" + makeAircraft(1) + " ,\r\n " +
           makeAircraft(2) + " ] , \"total\" : 2 }";
  PollingStreamReader<FakeStream> r(s, -1, 1000);
  const auto err = parseAll(r);
  TEST_ASSERT_EQUAL_STRING("Ok", err.c_str());
  TEST_ASSERT_EQUAL_UINT(2, g_parsed.size());
  TEST_ASSERT_EQUAL_STRING("BAW2    ", g_parsed[1].flight.c_str());
}

void test_receiver_aircraft_key() {
  // readsb / tar1090 aircraft.json: "aircraft" instead of "ac", with other
  // keys and string values (including escapes) before it.
  FakeStream s;
  s.data = "{\"now\" : 1730700000.1,\"messages\":123,\"note\":\"a\\\"ac\",\"aircraftX\":[],"
           "\"aircraft\":[" + makeAircraft(1) + "," + makeAircraft(2) + "]}";
  PollingStreamReader<FakeStream> r(s, -1, 1000);
  const auto err = parseAll(r);
  TEST_ASSERT_EQUAL_STRING("Ok", err.c_str());
  TEST_ASSERT_EQUAL_UINT(2, g_parsed.size());
}

void test_truncated_array_is_error() {
  FakeStream s;
  const std::string full = makeResponse(5);
  s.data = full.substr(0, full.size() / 2);
  PollingStreamReader<FakeStream> r(s, -1, 1000);
  const auto err = parseAll(r);
  TEST_ASSERT_TRUE(err != DeserializationError::Ok);
}

void test_bad_separator_is_error() {
  FakeStream s;
  s.data = "{\"ac\":[" + makeAircraft(1) + " x " + makeAircraft(2) + "]}";
  PollingStreamReader<FakeStream> r(s, -1, 1000);
  TEST_ASSERT_TRUE(parseAll(r) == DeserializationError::InvalidInput);
}

void test_classify_aircraft() {
  TEST_ASSERT_TRUE(classifyAircraft("A1", 0) == AircraftKind::kLight);
  TEST_ASSERT_TRUE(classifyAircraft("B1", 0) == AircraftKind::kLight);
  TEST_ASSERT_TRUE(classifyAircraft("B4", 0) == AircraftKind::kLight);
  TEST_ASSERT_TRUE(classifyAircraft("A3", 0) == AircraftKind::kStandard);
  TEST_ASSERT_TRUE(classifyAircraft("A7", 0) == AircraftKind::kStandard);
  TEST_ASSERT_TRUE(classifyAircraft("A", 0) == AircraftKind::kStandard);
  TEST_ASSERT_TRUE(classifyAircraft("A12", 0) == AircraftKind::kStandard);
  TEST_ASSERT_TRUE(classifyAircraft("", 0) == AircraftKind::kStandard);
  TEST_ASSERT_TRUE(classifyAircraft(nullptr, 0) == AircraftKind::kStandard);
  // Military wins over category.
  TEST_ASSERT_TRUE(classifyAircraft("A1", 1) == AircraftKind::kMilitary);
  TEST_ASSERT_TRUE(classifyAircraft(nullptr, 3) == AircraftKind::kMilitary);
  TEST_ASSERT_TRUE(classifyAircraft("A3", 2) == AircraftKind::kStandard);
  // No category (older transponder): fall back to the type designator.
  TEST_ASSERT_TRUE(classifyAircraft(nullptr, 0, "P28A") == AircraftKind::kLight);
  TEST_ASSERT_TRUE(classifyAircraft("A2", 0, "C172") == AircraftKind::kLight);
  TEST_ASSERT_TRUE(classifyAircraft(nullptr, 0, "J3") == AircraftKind::kLight);
  TEST_ASSERT_TRUE(classifyAircraft(nullptr, 0, "A320") == AircraftKind::kStandard);
  TEST_ASSERT_TRUE(classifyAircraft(nullptr, 0, "P28") == AircraftKind::kStandard);
  TEST_ASSERT_TRUE(classifyAircraft(nullptr, 0, "") == AircraftKind::kStandard);
  TEST_ASSERT_TRUE(classifyAircraft(nullptr, 1, "P28A") == AircraftKind::kMilitary);
}

void test_nearest_set_keeps_closest() {
  // 200 candidates at distance^2 = (i*37 % 200); keep the nearest 8.
  NearestSet<8> set;
  float kept[8] = {};
  for (int i = 0; i < 200; ++i) {
    const float d = static_cast<float>((i * 37) % 200);
    const int slot = set.slotFor(d);
    if (slot >= 0) {
      kept[slot] = d;
    }
  }
  TEST_ASSERT_EQUAL_UINT(8, set.count());
  std::sort(kept, kept + 8);
  for (int i = 0; i < 8; ++i) {
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(i), kept[i]);
  }
}

void test_nearest_set_under_capacity() {
  NearestSet<4> set;
  TEST_ASSERT_EQUAL_INT(0, set.slotFor(5.0f));
  TEST_ASSERT_EQUAL_INT(1, set.slotFor(1.0f));
  TEST_ASSERT_EQUAL_UINT(2, set.count());
}

void test_approx_distance() {
  // 0.1 deg north = 11.1 km; 0.1 deg east at 51.56 N ~ 6.9 km.
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 11.1f * 11.1f,
                           approxDistSqKm(51.5, -1.78, 51.6f, -1.78f));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 6.90f * 6.90f,
                           approxDistSqKm(51.558, -1.78, 51.558f, -1.68f));
  // Antimeridian: 0.2 deg apart, not 359.8.
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 22.2f * 22.2f,
                           approxDistSqKm(0.0, 179.9, 0.0f, -179.9f));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stream_with_content_length);
  RUN_TEST(test_stream_until_close);
  RUN_TEST(test_stalled_stream_times_out);
  RUN_TEST(test_stops_at_content_length);
  RUN_TEST(test_empty_body_is_error);
  RUN_TEST(test_empty_aircraft_list);
  RUN_TEST(test_whitespace_and_key_order);
  RUN_TEST(test_receiver_aircraft_key);
  RUN_TEST(test_truncated_array_is_error);
  RUN_TEST(test_bad_separator_is_error);
  RUN_TEST(test_classify_aircraft);
  RUN_TEST(test_nearest_set_keeps_closest);
  RUN_TEST(test_nearest_set_under_capacity);
  RUN_TEST(test_approx_distance);
  return UNITY_END();
}
