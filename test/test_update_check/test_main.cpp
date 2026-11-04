/** "Update available" version rule. */

#include <unity.h>

#include "../../src/services/firmware_image.cpp"
#include "../../src/services/update_check.cpp"

using services::updates::newerRelease;

void setUp() {}
void tearDown() {}

void test_newer() {
  TEST_ASSERT_TRUE(newerRelease("v2.0.0", "v2.0.1"));
  TEST_ASSERT_TRUE(newerRelease("v2.0.9", "v2.1.0"));
  TEST_ASSERT_TRUE(newerRelease("1.9.9", "v2.0.0"));
  // A build a few commits past v2.0.0 still counts as 2.0.0.
  TEST_ASSERT_TRUE(newerRelease("v2.0.0-3-gabc1234", "v2.0.1"));
}

void test_not_newer() {
  TEST_ASSERT_FALSE(newerRelease("v2.0.0", "v2.0.0"));
  TEST_ASSERT_FALSE(newerRelease("v2.1.0", "v2.0.9"));
  TEST_ASSERT_FALSE(newerRelease("v2.0.0-3-gabc1234", "v2.0.0"));
}

void test_unparsed_never_newer() {
  TEST_ASSERT_FALSE(newerRelease("dev", "v9.9.9"));
  TEST_ASSERT_FALSE(newerRelease("abc1234", "v9.9.9"));
  TEST_ASSERT_FALSE(newerRelease("v1.0.0", "nightly"));
  TEST_ASSERT_FALSE(newerRelease("v1.0.0", ""));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_newer);
  RUN_TEST(test_not_newer);
  RUN_TEST(test_unparsed_never_newer);
  return UNITY_END();
}
