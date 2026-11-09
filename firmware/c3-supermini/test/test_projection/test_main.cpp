/** Radar projection: lat/lon -> km offsets and screen pixels. */

#include <unity.h>

#include <cmath>

#include "ui/radar_range.h"
#include "ui/radar_theme.h"

// Unit under test, compiled straight in (test_build_src = no).
#include "../../src/ui/radar_projection.cpp"

// --- Stubs for what radar_projection.cpp links against ---
namespace {
double g_center_lat = 52.3676;  // Amsterdam
double g_center_lon = 4.9041;
size_t g_range = 1;  // 10 km preset
}  // namespace

namespace services::location {
double lat() { return g_center_lat; }
double lon() { return g_center_lon; }
}  // namespace services::location

namespace ui::radar {
const RangePreset& rangeCurrent() { return kRangePresets[g_range]; }
}  // namespace ui::radar

using ui::radar::latLonToScreen;
using ui::radar::offsetKmFromCenter;

void setUp() {
  g_center_lat = 52.3676;
  g_center_lon = 4.9041;
  g_range = 1;
}
void tearDown() {}

void test_east_offset_scaled_by_latitude() {
  float dx, dy, d;
  offsetKmFromCenter(52.3676f, 5.0041f, &dx, &dy, &d);
  // 0.1 deg of longitude at 52.37 N is ~6.78 km (would be 11.1 at equator).
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 6.778f, dx);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, dy);
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 6.778f, d);
}

void test_north_offset() {
  float dx, dy, d;
  offsetKmFromCenter(52.4676f, 4.9041f, &dx, &dy, &d);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, dx);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 11.1f, dy);
}

void test_center_change_updates_cached_scale() {
  float dx, dy, d;
  offsetKmFromCenter(52.3676f, 5.0041f, &dx, &dy, &d);
  g_center_lat = 0.0;
  g_center_lon = 0.0;
  offsetKmFromCenter(0.0f, 0.1f, &dx, &dy, &d);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 11.1f, dx);
}

void test_antimeridian_takes_short_way() {
  g_center_lat = 0.0;
  g_center_lon = 179.9;
  float dx, dy, d;
  offsetKmFromCenter(0.0f, -179.9f, &dx, &dy, &d);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 22.2f, dx);  // 0.2 deg east, not 359.8 west
}

void test_screen_center_and_direction() {
  int x, y;
  latLonToScreen(static_cast<float>(g_center_lat),
                 static_cast<float>(g_center_lon), &x, &y);
  TEST_ASSERT_EQUAL_INT(ui::radar::kCenterX, x);
  TEST_ASSERT_EQUAL_INT(ui::radar::kCenterY, y);

  latLonToScreen(52.3676f, 5.0041f, &x, &y);  // east -> right
  TEST_ASSERT_GREATER_THAN_INT(ui::radar::kCenterX, x);
  latLonToScreen(52.4676f, 4.9041f, &x, &y);  // north -> up
  TEST_ASSERT_LESS_THAN_INT(ui::radar::kCenterY, y);
}

void test_outer_range_maps_to_outer_ring() {
  const float outer_km = ui::radar::kRangePresets[g_range].outer_km;
  const float north_deg = outer_km / 111.0f;
  int x, y;
  latLonToScreen(static_cast<float>(g_center_lat) + north_deg,
                 static_cast<float>(g_center_lon), &x, &y);
  TEST_ASSERT_INT_WITHIN(1, ui::radar::kCenterY - ui::radar::kGridOuterRadius,
                         y);
}

void test_rotation_east_up() {
  ui::radar::setUpBearingDeg(90);  // east at the top
  int x = 0;
  int y = 0;
  latLonToScreen(52.3676f, 5.0041f, &x, &y);  // east of center -> up
  TEST_ASSERT_INT_WITHIN(1, ui::radar::kCenterX, x);
  TEST_ASSERT_LESS_THAN_INT(ui::radar::kCenterY, y);
  latLonToScreen(52.4676f, 4.9041f, &x, &y);  // north -> left
  TEST_ASSERT_LESS_THAN_INT(ui::radar::kCenterX, x);
  TEST_ASSERT_INT_WITHIN(1, ui::radar::kCenterY, y);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, ui::radar::screenAngleDeg(90.0f));
  ui::radar::setUpBearingDeg(-90);  // normalised to 270
  TEST_ASSERT_EQUAL_INT(270, ui::radar::upBearingDeg());
  ui::radar::setUpBearingDeg(0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_east_offset_scaled_by_latitude);
  RUN_TEST(test_north_offset);
  RUN_TEST(test_center_change_updates_cached_scale);
  RUN_TEST(test_antimeridian_takes_short_way);
  RUN_TEST(test_screen_center_and_direction);
  RUN_TEST(test_outer_range_maps_to_outer_ring);
  RUN_TEST(test_rotation_east_up);
  return UNITY_END();
}
