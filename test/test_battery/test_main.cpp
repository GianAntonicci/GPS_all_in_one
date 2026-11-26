#include <unity.h>
#include "battery_curve.h"

void setUp() {}
void tearDown() {}

void test_pct_limits() {
  TEST_ASSERT_EQUAL_UINT8(100, batteryPercent(4300));
  TEST_ASSERT_EQUAL_UINT8(100, batteryPercent(4200));
  TEST_ASSERT_EQUAL_UINT8(0, batteryPercent(3300));
  TEST_ASSERT_EQUAL_UINT8(0, batteryPercent(3000));
  TEST_ASSERT_EQUAL_UINT8(0, batteryPercent(0));
}

void test_pct_interp() {
  TEST_ASSERT_EQUAL_UINT8(42, batteryPercent(3750));
  TEST_ASSERT_EQUAL_UINT8(72, batteryPercent(3950));
  TEST_ASSERT_EQUAL_UINT8(7, batteryPercent(3450));
  TEST_ASSERT_EQUAL_UINT8(50, batteryPercent(3800));
}

void test_filter_first_sample() {
  BatteryFilter f;
  TEST_ASSERT_EQUAL_UINT32(4000, f.update(4000));
}

void test_filter_smooths() {
  BatteryFilter f;
  f.update(4000);
  TEST_ASSERT_EQUAL_UINT32(3800, f.update(3000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pct_limits);
  RUN_TEST(test_pct_interp);
  RUN_TEST(test_filter_first_sample);
  RUN_TEST(test_filter_smooths);
  return UNITY_END();
}
