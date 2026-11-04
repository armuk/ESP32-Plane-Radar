/** Settings backup file checks (the NVS side runs on the device only). */

#include <unity.h>

#include "../../src/services/settings_backup.cpp"

using namespace services::backup;

void setUp() {}
void tearDown() {}

void test_keys() {
  TEST_ASSERT_TRUE(keyAllowed("display", "textScale"));
  TEST_ASSERT_TRUE(keyAllowed("radar", "lat"));
  TEST_ASSERT_TRUE(keyAllowed("planeradar", "range"));
  // The settings password never goes in or out.
  TEST_ASSERT_FALSE(keyAllowed("display", "otaPass"));
  TEST_ASSERT_FALSE(keyAllowed("display", "otaPassNew"));
  // Nor Wi-Fi, the security log or the portal flag.
  TEST_ASSERT_FALSE(keyAllowed("nvs.net80211", "sta.pswd"));
  TEST_ASSERT_FALSE(keyAllowed("seclog", "seq"));
  TEST_ASSERT_FALSE(keyAllowed("wifi", "portal"));
  TEST_ASSERT_FALSE(keyAllowed("display", ""));
  TEST_ASSERT_FALSE(keyAllowed("display", "a-key-longer-than-15"));
}

static bool parse(const char* json, Value& v, const char*& err) {
  static JsonDocument doc;
  doc.clear();
  TEST_ASSERT_FALSE(deserializeJson(doc, json));
  return parseValue(doc.as<JsonVariantConst>(), v, err);
}

void test_values() {
  Value v;
  const char* err = nullptr;
  TEST_ASSERT_TRUE(parse("{\"u8\": 255}", v, err));
  TEST_ASSERT_EQUAL_INT64(255, v.number);
  TEST_ASSERT_TRUE(parse("{\"i32\": -2147483648}", v, err));
  TEST_ASSERT_TRUE(parse("{\"str\": \"pool.ntp.org\"}", v, err));
  TEST_ASSERT_EQUAL_STRING("pool.ntp.org", v.text);
  TEST_ASSERT_TRUE(parse("{\"blob\": \"0000000000c04940\"}", v, err));
  TEST_ASSERT_EQUAL(8, v.blob_len);
  TEST_ASSERT_EQUAL_HEX8(0x40, v.blob[7]);
}

void test_bad_values() {
  Value v;
  const char* err = nullptr;
  TEST_ASSERT_FALSE(parse("{\"u8\": 256}", v, err));
  TEST_ASSERT_FALSE(parse("{\"u8\": -1}", v, err));
  TEST_ASSERT_FALSE(parse("{\"i8\": 1.5}", v, err));
  TEST_ASSERT_FALSE(parse("{\"u32\": \"7\"}", v, err));
  TEST_ASSERT_FALSE(parse("{\"u64\": 1}", v, err));
  TEST_ASSERT_FALSE(parse("{\"u8\": 1, \"i8\": 1}", v, err));
  TEST_ASSERT_FALSE(parse("7", v, err));
  TEST_ASSERT_FALSE(parse("{\"blob\": \"abc\"}", v, err));
  TEST_ASSERT_FALSE(parse("{\"blob\": \"zz\"}", v, err));
  std::string long_text = "{\"str\": \"" + std::string(kMaxStringLen + 1, 'a') + "\"}";
  TEST_ASSERT_FALSE(parse(long_text.c_str(), v, err));
  TEST_ASSERT_NOT_NULL(err);
}

void test_header() {
  JsonDocument doc;
  const char* err = nullptr;
  deserializeJson(doc, "{\"plane_radar_settings\": 1, \"settings\": {}}");
  TEST_ASSERT_TRUE(checkHeader(doc, err));
  deserializeJson(doc, "{\"plane_radar_settings\": 2, \"settings\": {}}");
  TEST_ASSERT_FALSE(checkHeader(doc, err));
  deserializeJson(doc, "{\"settings\": {}}");
  TEST_ASSERT_FALSE(checkHeader(doc, err));
  deserializeJson(doc, "{\"plane_radar_settings\": 1}");
  TEST_ASSERT_FALSE(checkHeader(doc, err));
}

void test_hex() {
  const uint8_t data[] = {0x00, 0xab, 0x7f};
  char out[7];
  toHex(data, sizeof(data), out);
  TEST_ASSERT_EQUAL_STRING("00ab7f", out);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_keys);
  RUN_TEST(test_values);
  RUN_TEST(test_bad_values);
  RUN_TEST(test_header);
  RUN_TEST(test_hex);
  return UNITY_END();
}
