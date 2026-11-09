/** CSRF same-origin check (Origin / Referer against Host). */

#include <unity.h>

#include "../../src/services/request_origin.cpp"

using services::request_origin::sameOrigin;

void setUp() {}
void tearDown() {}

void test_own_pages_accepted() {
  // Form POST from the device's own page: Origin is sent.
  TEST_ASSERT_TRUE(sameOrigin("http://plane-radar.local", "", "plane-radar.local"));
  TEST_ASSERT_TRUE(sameOrigin("http://192.168.2.172", nullptr, "192.168.2.172"));
  // GET link: only Referer, with a path and query.
  TEST_ASSERT_TRUE(sameOrigin("", "http://plane-radar.local/0wifi?x=1", "plane-radar.local"));
  TEST_ASSERT_TRUE(sameOrigin(nullptr, "http://192.168.4.1/", "192.168.4.1"));
}

void test_case_and_default_port() {
  TEST_ASSERT_TRUE(sameOrigin("HTTP://Plane-Radar.LOCAL", "", "plane-radar.local"));
  TEST_ASSERT_TRUE(sameOrigin("http://192.168.2.172:80", "", "192.168.2.172"));
  TEST_ASSERT_TRUE(sameOrigin("http://192.168.2.172", "", "192.168.2.172:80"));
  TEST_ASSERT_TRUE(sameOrigin("http://10.0.0.5:8080", "", "10.0.0.5:8080"));
  TEST_ASSERT_FALSE(sameOrigin("http://10.0.0.5:8080", "", "10.0.0.5"));
}

void test_other_sites_refused() {
  TEST_ASSERT_FALSE(sameOrigin("https://evil.example", "", "plane-radar.local"));
  TEST_ASSERT_FALSE(sameOrigin("http://evil.example", "http://plane-radar.local/", "plane-radar.local"));
  TEST_ASSERT_FALSE(sameOrigin("", "https://evil.example/page", "192.168.2.172"));
  // Look-alike hosts.
  TEST_ASSERT_FALSE(sameOrigin("http://plane-radar.local.evil.example", "", "plane-radar.local"));
  TEST_ASSERT_FALSE(sameOrigin("http://192.168.2.17", "", "192.168.2.172"));
  TEST_ASSERT_FALSE(sameOrigin("http://plane-radar.local@evil.example", "", "plane-radar.local"));
  // HTTPS origin for the same name is a different origin.
  TEST_ASSERT_FALSE(sameOrigin("https://plane-radar.local", "", "plane-radar.local"));
}

void test_hidden_source_refused() {
  TEST_ASSERT_FALSE(sameOrigin("null", "", "plane-radar.local"));
  TEST_ASSERT_FALSE(sameOrigin("null", "http://plane-radar.local/", "plane-radar.local"));
  TEST_ASSERT_FALSE(sameOrigin("", "", "plane-radar.local"));
  TEST_ASSERT_FALSE(sameOrigin(nullptr, nullptr, "plane-radar.local"));
  TEST_ASSERT_FALSE(sameOrigin("http://plane-radar.local", "", ""));
  TEST_ASSERT_FALSE(sameOrigin("http://plane-radar.local", "", nullptr));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_own_pages_accepted);
  RUN_TEST(test_case_and_default_port);
  RUN_TEST(test_other_sites_refused);
  RUN_TEST(test_hidden_source_refused);
  return UNITY_END();
}
