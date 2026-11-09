/** Range preset selection from the Setup page dropdown. */

#include <unity.h>

// Unit under test, compiled straight in (test_build_src = no).
#include "../../src/ui/radar_range.cpp"

using namespace ui::radar;

void setUp() {
  Preferences::store().clear();
  rangeInit();
}
void tearDown() {}

void test_default_is_10km() {
  TEST_ASSERT_EQUAL_UINT8(1, rangeIndex());
  TEST_ASSERT_EQUAL_FLOAT(10.0f, rangeCurrent().ring3_km);
}

void test_save_valid_index_persists() {
  TEST_ASSERT_TRUE(saveRangeFromPortal("3"));
  TEST_ASSERT_EQUAL_FLOAT(25.0f, rangeCurrent().ring3_km);
  TEST_ASSERT_FALSE(saveRangeFromPortal("3"));  // unchanged
  rangeInit();  // reload from "flash"
  TEST_ASSERT_EQUAL_UINT8(3, rangeIndex());
}

void test_invalid_values_ignored() {
  for (const char* s : {"", "7", "9", "-1", "10", "a", "1 "}) {
    TEST_ASSERT_FALSE_MESSAGE(saveRangeFromPortal(s), s);
  }
  TEST_ASSERT_FALSE(saveRangeFromPortal(nullptr));
  TEST_ASSERT_EQUAL_UINT8(1, rangeIndex());
}

void test_wide_ranges() {
  TEST_ASSERT_EQUAL_size_t(7, kRangePresetCount);
  TEST_ASSERT_TRUE(saveRangeFromPortal("6"));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, rangeCurrent().ring3_km);
  char label[12];
  formatRing3Label(label, sizeof(label), 100.0f, true);
  TEST_ASSERT_EQUAL_STRING("62mi", label);
}

void test_button_cycle_still_wraps() {
  TEST_ASSERT_TRUE(saveRangeFromPortal("6"));
  rangeNext();
  TEST_ASSERT_EQUAL_UINT8(0, rangeIndex());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_default_is_10km);
  RUN_TEST(test_save_valid_index_persists);
  RUN_TEST(test_invalid_values_ignored);
  RUN_TEST(test_wide_ranges);
  RUN_TEST(test_button_cycle_still_wraps);
  return UNITY_END();
}
