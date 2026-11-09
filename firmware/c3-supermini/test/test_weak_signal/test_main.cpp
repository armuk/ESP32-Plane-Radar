/** Weak-signal rejoin trigger. */

#include <unity.h>

#include <initializer_list>

#include "services/weak_signal.h"

using services::wifi_signal::WeakSignalWindow;

void setUp() {}
void tearDown() {}

static void feed(WeakSignalWindow& w, std::initializer_list<int> rssi) {
  for (int r : rssi) w.add(r);
}

void test_strong_never_fires() {
  WeakSignalWindow w(-78);
  feed(w, {-52, -55, -60, -50, -58, -61, -54, -57});
  TEST_ASSERT_FALSE(w.mostlyWeak());
}

void test_weak_join_fires_after_two_minutes() {
  WeakSignalWindow w(-78);
  feed(w, {-84, -83, -85});
  TEST_ASSERT_FALSE(w.mostlyWeak());
  w.add(-84);
  TEST_ASSERT_TRUE(w.mostlyWeak());
}

void test_hovering_link_fires() {
  // The old rule (6 in a row) never fired on this: one -76 reset it.
  WeakSignalWindow w(-78);
  feed(w, {-79, -80, -76, -79, -81});
  TEST_ASSERT_TRUE(w.mostlyWeak());
}

void test_occasional_dip_does_not_fire() {
  WeakSignalWindow w(-78);
  feed(w, {-70, -80, -72, -79, -71, -73, -82, -70, -74});
  TEST_ASSERT_FALSE(w.mostlyWeak());
}

void test_old_samples_age_out() {
  WeakSignalWindow w(-78);
  feed(w, {-85, -85, -85});  // weak, then the link recovers
  feed(w, {-60, -60, -60, -60, -60, -60});
  TEST_ASSERT_EQUAL(0, w.weakCount());
  TEST_ASSERT_EQUAL(WeakSignalWindow::kWindow, w.samples());
}

void test_threshold_is_strict() {
  WeakSignalWindow w(-78);
  feed(w, {-78, -78, -78, -78, -78, -78});
  TEST_ASSERT_FALSE(w.mostlyWeak());
}

void test_clear() {
  WeakSignalWindow w(-78);
  feed(w, {-85, -85, -85, -85});
  TEST_ASSERT_TRUE(w.mostlyWeak());
  w.clear();
  TEST_ASSERT_FALSE(w.mostlyWeak());
  TEST_ASSERT_EQUAL(0, w.samples());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_strong_never_fires);
  RUN_TEST(test_weak_join_fires_after_two_minutes);
  RUN_TEST(test_hovering_link_fires);
  RUN_TEST(test_occasional_dip_does_not_fire);
  RUN_TEST(test_old_samples_age_out);
  RUN_TEST(test_threshold_is_strict);
  RUN_TEST(test_clear);
  return UNITY_END();
}
