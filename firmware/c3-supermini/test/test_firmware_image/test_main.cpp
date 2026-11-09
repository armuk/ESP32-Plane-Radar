/** Firmware image version marker and downgrade rule. */

#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "../../src/services/firmware_image.cpp"

using namespace services::firmware_image;

void setUp() {}
void tearDown() {}

std::vector<uint8_t> imageWith(const std::string& version, size_t pad = 5000) {
  std::vector<uint8_t> img(pad, 0xAB);
  // A near miss first, then the real marker in the middle of the image.
  const std::string decoy = "PLANE-RADAR-FW-VERSIOX=v9.9.9";
  img.insert(img.begin() + 100, decoy.begin(), decoy.end());
  const std::string marker = "PLANE-RADAR-FW-VERSION=" + version;
  img.insert(img.begin() + pad / 2, marker.begin(), marker.end());
  img.insert(img.begin() + pad / 2 + marker.size(), '\0');
  return img;
}

void test_scanner_finds_marker_in_one_chunk() {
  const auto img = imageWith("v1.4.2");
  VersionScanner s;
  s.feed(img.data(), img.size());
  TEST_ASSERT_TRUE(s.found());
  TEST_ASSERT_EQUAL_STRING("v1.4.2", s.version());
}

void test_scanner_handles_every_chunk_boundary() {
  const auto img = imageWith("v2.0.10-3-gabc1234");
  for (size_t chunk : {1u, 2u, 3u, 7u, 13u, 64u, 4096u}) {
    VersionScanner s;
    for (size_t off = 0; off < img.size(); off += chunk) {
      s.feed(img.data() + off, std::min(chunk, img.size() - off));
    }
    TEST_ASSERT_TRUE(s.found());
    TEST_ASSERT_EQUAL_STRING("v2.0.10-3-gabc1234", s.version());
  }
}

void test_scanner_no_marker() {
  std::vector<uint8_t> img(3000, 0x41);
  VersionScanner s;
  s.feed(img.data(), img.size());
  TEST_ASSERT_FALSE(s.found());
}

void test_scanner_rejects_overlong_value() {
  const auto img = imageWith(std::string(40, '1'));
  VersionScanner s;
  s.feed(img.data(), img.size());
  TEST_ASSERT_FALSE(s.found());
}

void test_parse_versions() {
  Version v;
  TEST_ASSERT_TRUE(parseVersion("v1.2.3", &v));
  TEST_ASSERT_EQUAL_INT(1, v.major);
  TEST_ASSERT_EQUAL_INT(2, v.minor);
  TEST_ASSERT_EQUAL_INT(3, v.patch);
  TEST_ASSERT_TRUE(parseVersion("10.20.30-4-gdeadbee", &v));
  TEST_ASSERT_EQUAL_INT(30, v.patch);
  TEST_ASSERT_FALSE(parseVersion("dev", &v));
  TEST_ASSERT_FALSE(parseVersion("abc1234", &v));
  TEST_ASSERT_FALSE(parseVersion("v1.2", &v));
  TEST_ASSERT_FALSE(parseVersion("v1.2.3x", &v));
}

void test_downgrade_rule() {
  TEST_ASSERT_TRUE(isDowngrade("v1.3.0", "v1.2.9"));
  TEST_ASSERT_TRUE(isDowngrade("v2.0.0", "v1.9.9"));
  TEST_ASSERT_FALSE(isDowngrade("v1.3.0", "v1.3.0"));  // reinstall allowed
  TEST_ASSERT_FALSE(isDowngrade("v1.3.0", "v1.3.1"));
  TEST_ASSERT_FALSE(isDowngrade("v1.3.0", "v1.10.0"));
  // Development builds are not compared.
  TEST_ASSERT_FALSE(isDowngrade("dev", "v0.1.0"));
  TEST_ASSERT_FALSE(isDowngrade("v1.3.0", "dev"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_scanner_finds_marker_in_one_chunk);
  RUN_TEST(test_scanner_handles_every_chunk_boundary);
  RUN_TEST(test_scanner_no_marker);
  RUN_TEST(test_scanner_rejects_overlong_value);
  RUN_TEST(test_parse_versions);
  RUN_TEST(test_downgrade_rule);
  return UNITY_END();
}
