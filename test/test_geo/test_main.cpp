#include <unity.h>
#include "geo.h"

void setUp() {}
void tearDown() {}

void test_distance_one_degree_lat() {
  TEST_ASSERT_DOUBLE_WITHIN(1.0, 111195.0, distanceM(0, 0, 1, 0));
}

void test_distance_zero() {
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, distanceM(-23.5, -46.6, -23.5, -46.6));
}

void test_distance_sp_rio() {
  double d = distanceM(-23.55052, -46.63331, -22.90685, -43.17290);
  TEST_ASSERT_TRUE(d > 355000 && d < 362000);
}

void test_bearing_cardinals() {
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 0.0, bearingDeg(0, 0, 1, 0));
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 90.0, bearingDeg(0, 0, 0, 1));
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 180.0, bearingDeg(0, 0, -1, 0));
  TEST_ASSERT_DOUBLE_WITHIN(0.01, 270.0, bearingDeg(0, 0, 0, -1));
}

void test_bearing_range() {
  double b = bearingDeg(-23.5, -46.6, -23.6, -46.7);  // sudoeste
  TEST_ASSERT_TRUE(b >= 180.0 && b < 270.0);
}

void test_relative_wraps() {
  TEST_ASSERT_DOUBLE_WITHIN(0.001, -20.0, relativeBearingDeg(350, 10));
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 20.0, relativeBearingDeg(10, 350));
  TEST_ASSERT_DOUBLE_WITHIN(0.001, -180.0, relativeBearingDeg(180, 0));
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, relativeBearingDeg(90, 90));
}

void test_pos_unknown() {
  TEST_ASSERT_FALSE(posKnown(0, 0));
  TEST_ASSERT_FALSE(posKnown(0.0000001, -0.0000001));
  TEST_ASSERT_TRUE(posKnown(-23.5, -46.6));
  TEST_ASSERT_TRUE(posKnown(0, -46.6));  // equador ainda é posição válida
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_distance_one_degree_lat);
  RUN_TEST(test_distance_zero);
  RUN_TEST(test_distance_sp_rio);
  RUN_TEST(test_bearing_cardinals);
  RUN_TEST(test_bearing_range);
  RUN_TEST(test_relative_wraps);
  RUN_TEST(test_pos_unknown);
  return UNITY_END();
}
