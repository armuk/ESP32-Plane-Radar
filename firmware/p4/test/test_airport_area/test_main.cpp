/** Local-airport download helpers: CSV reader, labels, runway geometry. */

#include <unity.h>

#include <string>
#include <vector>

#include "services/airport_area.h"

using namespace services::airports;

namespace {

constexpr const char* kAirportCols[] = {"id", "type", "latitude_deg",
                                        "longitude_deg", "icao_code", "ident"};

const char kAirportsCsv[] =
    "\"id\",\"ident\",\"type\",\"name\",\"latitude_deg\",\"longitude_deg\","
    "\"elevation_ft\",\"continent\",\"iso_country\",\"iso_region\","
    "\"municipality\",\"scheduled_service\",\"icao_code\",\"iata_code\","
    "\"gps_code\",\"local_code\",\"home_link\",\"wikipedia_link\","
    "\"keywords\"\r\n"
    "2434,\"EGLF\",\"medium_airport\",\"Farnborough Airport\",51.275799,"
    "-0.776333,238,\"EU\",\"GB\",\"GB-ENG\",\"Farnborough\",\"no\",\"EGLF\","
    "\"FAB\",\"EGLF\",,,\"https://en.wikipedia.org/wiki/Farnborough_Airport\","
    "\"RAE Farnborough, \"\"Royal\"\" Aircraft\"\r\n"
    "320000,\"GB-0123\",\"small_airport\",\"Long, Very Long Name Farm Strip "
    "With Commas, And More\",51.1,-1.2,,\"EU\",\"GB\",\"GB-ENG\",,\"no\",,,,,,,"
    "\r\n"
    "6523,\"00A\",\"heliport\",\"Total RF Heliport\",40.070985,-74.933689,11,"
    "\"NA\",\"US\",\"US-PA\",\"Bensalem\",\"no\",,,\"K00A\",\"00A\",,,";

struct Row {
  std::string id, type, lat, lon, icao, ident;
};

std::vector<Row> parseAll(size_t chunk) {
  std::vector<Row> rows;
  CsvColumns<6> csv(kAirportCols);
  const std::string data(kAirportsCsv);
  auto on_row = [&rows](const char* const* f) {
    rows.push_back({f[0], f[1], f[2], f[3], f[4], f[5]});
  };
  for (size_t i = 0; i < data.size(); i += chunk) {
    csv.feed(data.data() + i, std::min(chunk, data.size() - i), on_row);
  }
  csv.finish(on_row);
  TEST_ASSERT_TRUE(csv.headerComplete());
  return rows;
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_csv_columns_any_chunking() {
  for (size_t chunk : {1u, 3u, 7u, 64u, 4096u}) {
    const auto rows = parseAll(chunk);
    TEST_ASSERT_EQUAL_UINT(3, rows.size());
    TEST_ASSERT_EQUAL_STRING("2434", rows[0].id.c_str());
    TEST_ASSERT_EQUAL_STRING("medium_airport", rows[0].type.c_str());
    TEST_ASSERT_EQUAL_STRING("51.275799", rows[0].lat.c_str());
    TEST_ASSERT_EQUAL_STRING("-0.776333", rows[0].lon.c_str());
    TEST_ASSERT_EQUAL_STRING("EGLF", rows[0].icao.c_str());
    // Commas inside a quoted name don't shift the later columns.
    TEST_ASSERT_EQUAL_STRING("small_airport", rows[1].type.c_str());
    TEST_ASSERT_EQUAL_STRING("51.1", rows[1].lat.c_str());
    TEST_ASSERT_EQUAL_STRING("", rows[1].icao.c_str());
    TEST_ASSERT_EQUAL_STRING("GB-0123", rows[1].ident.c_str());
    // Last row has no trailing newline.
    TEST_ASSERT_EQUAL_STRING("heliport", rows[2].type.c_str());
    TEST_ASSERT_EQUAL_STRING("00A", rows[2].ident.c_str());
  }
}

void test_missing_column_detected() {
  constexpr const char* kCols[] = {"id", "no_such_column"};
  CsvColumns<2> csv(kCols);
  const char data[] = "\"id\",\"x\"\n1,2\n";
  csv.feed(data, sizeof(data) - 1, [](const char* const*) {});
  TEST_ASSERT_FALSE(csv.headerComplete());
}

void test_airport_type_and_label() {
  AirportType t;
  TEST_ASSERT_TRUE(parseAirportType("medium_airport", &t));
  TEST_ASSERT_TRUE(t == AirportType::kMedium);
  for (const char* other : {"small_airport", "large_airport", "heliport",
                            "closed", "seaplane_base", ""}) {
    TEST_ASSERT_FALSE(parseAirportType(other, &t));
  }
  char label[9];
  chooseLabel("EGLF", "EGLF", label);
  TEST_ASSERT_EQUAL_STRING("EGLF", label);
  chooseLabel("", "GB-0123", label);
  TEST_ASSERT_EQUAL_STRING("GB-0123", label);
  chooseLabel("", "US-12345X", label);  // cut to 8
  TEST_ASSERT_EQUAL_STRING("US-12345", label);
}

void test_runway_ends_from_coordinates() {
  double e[4];
  TEST_ASSERT_TRUE(runwayEnds("51.27", "-0.79", "51.28", "-0.76", "", "06",
                              "8000", 51.2, -0.7, e));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, static_cast<float>(51.27), static_cast<float>(e[0]));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, static_cast<float>(-0.76), static_cast<float>(e[3]));
}

void test_runway_ends_from_heading_or_number() {
  double e[4];
  // 09/27, 3,000 ft, no coordinates or true heading: east-west, centred.
  TEST_ASSERT_TRUE(
      runwayEnds("", "", "", "", "", "09", "3000", 51.0, -1.0, e));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, static_cast<float>(51.0), static_cast<float>(e[0]));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, static_cast<float>(51.0), static_cast<float>(e[2]));
  TEST_ASSERT_TRUE(e[1] < -1.0);
  TEST_ASSERT_TRUE(e[3] > -1.0);
  const float len = distanceKm(e[0], e[1], e[2], e[3]);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9144f, len);
  // True heading wins over the runway number.
  TEST_ASSERT_TRUE(
      runwayEnds("", "", "", "", "0", "09", "3000", 51.0, -1.0, e));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, static_cast<float>(-1.0), static_cast<float>(e[1]));
  // Nothing usable.
  TEST_ASSERT_FALSE(runwayEnds("", "", "", "", "", "N", "3000", 51, -1, e));
  TEST_ASSERT_FALSE(runwayEnds("", "", "", "", "", "09", "", 51, -1, e));
  TEST_ASSERT_FALSE(runwayEnds("", "", "", "", "", "40", "3000", 51, -1, e));
  TEST_ASSERT_FALSE(runwayEnds("51", "", "", "", "", "H1", "80", 51, -1, e));
}

void test_parse_double() {
  double v = 0;
  TEST_ASSERT_TRUE(parseDouble("-0.776333", &v));
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, static_cast<float>(-0.776333), static_cast<float>(v));
  TEST_ASSERT_FALSE(parseDouble("", &v));
  TEST_ASSERT_FALSE(parseDouble("12x", &v));
  TEST_ASSERT_FALSE(parseDouble(nullptr, &v));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_csv_columns_any_chunking);
  RUN_TEST(test_missing_column_detected);
  RUN_TEST(test_airport_type_and_label);
  RUN_TEST(test_runway_ends_from_coordinates);
  RUN_TEST(test_runway_ends_from_heading_or_number);
  RUN_TEST(test_parse_double);
  return UNITY_END();
}
