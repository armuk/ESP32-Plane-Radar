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

void test_asset_url() {
  char url[160];
  TEST_ASSERT_TRUE(services::updates::releaseAssetUrl("armuk/ESP32-Plane-Radar",
                                                      "v2.2.0", url, sizeof(url)));
  TEST_ASSERT_EQUAL_STRING(
      "https://github.com/armuk/ESP32-Plane-Radar/releases/download/v2.2.0/"
      "plane-radar-v2.2.0-ota.bin",
      url);
}

void test_asset_url_refuses_odd_input() {
  char url[160] = "untouched";
  using services::updates::releaseAssetUrl;
  TEST_ASSERT_FALSE(releaseAssetUrl("", "v2.2.0", url, sizeof(url)));
  TEST_ASSERT_FALSE(releaseAssetUrl("armuk", "v2.2.0", url, sizeof(url)));
  TEST_ASSERT_FALSE(releaseAssetUrl("/armuk", "v2.2.0", url, sizeof(url)));
  TEST_ASSERT_FALSE(releaseAssetUrl("a/b/c", "v2.2.0", url, sizeof(url)));
  TEST_ASSERT_FALSE(releaseAssetUrl("a/b?x", "v2.2.0", url, sizeof(url)));
  TEST_ASSERT_FALSE(releaseAssetUrl("a/b", "dev", url, sizeof(url)));
  TEST_ASSERT_FALSE(releaseAssetUrl("a/b", "v2.2.0-x/../y", url, sizeof(url)));
  TEST_ASSERT_FALSE(releaseAssetUrl("a/b", "v2.2.0", url, 20));
  TEST_ASSERT_EQUAL_STRING("untouched", url);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_newer);
  RUN_TEST(test_not_newer);
  RUN_TEST(test_unparsed_never_newer);
  RUN_TEST(test_asset_url);
  RUN_TEST(test_asset_url_refuses_odd_input);
  return UNITY_END();
}
