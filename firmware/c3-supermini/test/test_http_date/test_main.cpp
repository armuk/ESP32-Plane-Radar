/** HTTP Date header parsing (backup clock before NTP syncs). */

#include <unity.h>

#include <initializer_list>

#include "services/http_date.h"

using services::http_date::parse;

void setUp() {}
void tearDown() {}

void test_parses_imf_fixdate() {
  time_t t = 0;
  TEST_ASSERT_TRUE(parse("Sun, 06 Nov 1994 08:49:37 GMT", &t));
  TEST_ASSERT_EQUAL_INT64(784111777, static_cast<int64_t>(t));
  TEST_ASSERT_TRUE(parse("Tue, 21 Jul 2026 12:00:00 GMT", &t));
  TEST_ASSERT_EQUAL_INT64(1784635200, static_cast<int64_t>(t));
  TEST_ASSERT_TRUE(parse("Sun, 29 Feb 2032 23:59:59 GMT", &t));  // leap day
  TEST_ASSERT_EQUAL_INT64(1961711999, static_cast<int64_t>(t));
}

void test_rejects_malformed() {
  time_t t = 0;
  for (const char* s :
       {"", "garbage", "Tue, 21 Jul 2026 12:00:00",  // no zone
        "Tue, 21 Jul 2026 12:00:00 UTC", "Tue, 21 Jux 2026 12:00:00 GMT",
        "Tue, 21 uly 2026 12:00:00 GMT",  // misaligned month match
        "Tue, 32 Jul 2026 12:00:00 GMT", "Tue, 21 Jul 2026 24:00:00 GMT",
        "Tue, 21 Jul 1969 12:00:00 GMT"}) {
    TEST_ASSERT_FALSE_MESSAGE(parse(s, &t), s);
  }
  TEST_ASSERT_FALSE(parse(nullptr, &t));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_imf_fixdate);
  RUN_TEST(test_rejects_malformed);
  return UNITY_END();
}
