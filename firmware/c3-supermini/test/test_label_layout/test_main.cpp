/** Aircraft tag placement without overlaps (ui/label_layout.h). */

#include <unity.h>

#include "ui/label_layout.h"

using namespace ui::radar;

void setUp() {}
void tearDown() {}

LabelRect rect(int x, int y, int w, int h) {
  LabelRect r;
  r.x = static_cast<int16_t>(x);
  r.y = static_cast<int16_t>(y);
  r.w = static_cast<int16_t>(w);
  r.h = static_cast<int16_t>(h);
  return r;
}

void test_overlap() {
  TEST_ASSERT_TRUE(rectsOverlap(rect(0, 0, 10, 10), rect(5, 5, 10, 10)));
  // Touching edges don't overlap.
  TEST_ASSERT_FALSE(rectsOverlap(rect(0, 0, 10, 10), rect(10, 0, 10, 10)));
  TEST_ASSERT_FALSE(rectsOverlap(rect(0, 0, 10, 10), rect(0, 10, 10, 10)));
  TEST_ASSERT_FALSE(rectsOverlap(rect(0, 0, 10, 10), rect(50, 50, 5, 5)));
}

void test_first_candidate_beside_symbol() {
  // Symbol at (60, 120), tag toward the centre (right), 13 px out.
  const LabelRect r = candidateRect(0, 60, 120, 40, 30, 13, true, 240);
  TEST_ASSERT_EQUAL(73, r.x);
  TEST_ASSERT_EQUAL(105, r.y);
  const LabelRect left = candidateRect(0, 180, 120, 40, 30, 13, false, 240);
  TEST_ASSERT_EQUAL(180 - 13 - 40, left.x);
  // Spot 3 is the other side.
  const LabelRect other = candidateRect(3, 60, 120, 40, 30, 13, true, 240);
  TEST_ASSERT_EQUAL(60 - 13 - 40, other.x);
}

void test_candidates_stay_on_screen() {
  for (int i = 0; i < kLabelCandidateCount; ++i) {
    const LabelRect r = candidateRect(i, 5, 235, 60, 45, 13, true, 240);
    TEST_ASSERT_TRUE(r.x >= 1 && r.x + r.w <= 239);
    TEST_ASSERT_TRUE(r.y >= 1 && r.y + r.h <= 239);
  }
}

void test_free_spot_moves_away_from_taken() {
  LabelOccupancy<8> taken;
  // Own symbol (index 0) and an earlier tag covering the preferred spot.
  taken.add(rect(52, 112, 17, 17));
  taken.add(rect(73, 105, 40, 30));
  LabelRect spot;
  TEST_ASSERT_TRUE(
      findFreeLabelSpot(taken, 60, 120, 40, 30, 13, true, 240, 0, &spot));
  TEST_ASSERT_FALSE(taken.collides(spot, 0));
  TEST_ASSERT_FALSE(spot.x == 73 && spot.y == 105);
}

void test_own_symbol_ignored() {
  LabelOccupancy<4> taken;
  taken.add(rect(40, 100, 60, 40));  // big own symbol box
  LabelRect spot;
  TEST_ASSERT_TRUE(
      findFreeLabelSpot(taken, 60, 120, 40, 30, 13, true, 240, 0, &spot));
  TEST_ASSERT_EQUAL(73, spot.x);  // first choice despite touching it
}

void test_no_spot_when_crowded() {
  LabelOccupancy<4> taken;
  taken.add(rect(0, 0, 240, 240));
  LabelRect spot;
  TEST_ASSERT_FALSE(
      findFreeLabelSpot(taken, 60, 120, 40, 30, 13, true, 240, -1, &spot));
}

void test_capacity() {
  LabelOccupancy<2> taken;
  TEST_ASSERT_TRUE(taken.add(rect(0, 0, 1, 1)));
  TEST_ASSERT_TRUE(taken.add(rect(2, 2, 1, 1)));
  TEST_ASSERT_FALSE(taken.add(rect(4, 4, 1, 1)));
  TEST_ASSERT_EQUAL(2, static_cast<int>(taken.count()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_overlap);
  RUN_TEST(test_first_candidate_beside_symbol);
  RUN_TEST(test_candidates_stay_on_screen);
  RUN_TEST(test_free_spot_moves_away_from_taken);
  RUN_TEST(test_own_symbol_ignored);
  RUN_TEST(test_no_spot_when_crowded);
  RUN_TEST(test_capacity);
  return UNITY_END();
}
