/** Host extraction for the bounded DNS lookup. */

#include <unity.h>

#include "../../src/services/net_dns.cpp"

using services::net::hostFromUrl;

void setUp() {}
void tearDown() {}

static void expectHost(const char* url, const char* host) {
  char out[64];
  TEST_ASSERT_TRUE_MESSAGE(hostFromUrl(url, out, sizeof(out)), url);
  TEST_ASSERT_EQUAL_STRING(host, out);
}

void test_hosts() {
  expectHost("https://opendata.adsb.fi/api/v3/lat/51.4/lon/-0.4/dist/27", "opendata.adsb.fi");
  expectHost("https://api.adsbdb.com/v0/callsign/BAW9CG", "api.adsbdb.com");
  expectHost("http://192.168.2.50:8080/data/aircraft.json", "192.168.2.50");
  expectHost("http://piaware.local/skyaware/data/aircraft.json", "piaware.local");
  expectHost("https://api.open-meteo.com/v1/forecast?latitude=1", "api.open-meteo.com");
  expectHost("http://user:pw@host.example/x", "host.example");
  expectHost("host.example/path", "host.example");
}

void test_bad_input() {
  char out[8];
  TEST_ASSERT_FALSE(hostFromUrl("https:///path", out, sizeof(out)));
  TEST_ASSERT_FALSE(hostFromUrl("https://much-too-long.example/", out, sizeof(out)));
  TEST_ASSERT_FALSE(hostFromUrl(nullptr, out, sizeof(out)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hosts);
  RUN_TEST(test_bad_input);
  return UNITY_END();
}
