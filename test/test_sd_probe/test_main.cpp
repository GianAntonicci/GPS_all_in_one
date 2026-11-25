#include <unity.h>
#include "sd_probe.h"

void setUp() {}
void tearDown() {}

void test_probe_not_due_before_period() {
  SdProbe p;
  p.reset(0);
  TEST_ASSERT_FALSE(p.due(4999));
}

void test_probe_due_after_period_and_rearms() {
  SdProbe p;
  p.reset(0);
  TEST_ASSERT_TRUE(p.due(5000));
  TEST_ASSERT_FALSE(p.due(5001));
  TEST_ASSERT_TRUE(p.due(10000));
}

void test_probe_postponed_by_successful_io() {
  SdProbe p;
  p.reset(0);
  p.reset(4000);  // uma escrita do GPX deu certo: o cartão está lá
  TEST_ASSERT_FALSE(p.due(8999));
  TEST_ASSERT_TRUE(p.due(9000));
}

void test_probe_wraps_millis() {
  SdProbe p;
  p.reset(0xFFFFF000u);
  TEST_ASSERT_FALSE(p.due(0x00000100u));
  TEST_ASSERT_TRUE(p.due(0x00001000u + 0x388u));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_probe_not_due_before_period);
  RUN_TEST(test_probe_due_after_period_and_rearms);
  RUN_TEST(test_probe_postponed_by_successful_io);
  RUN_TEST(test_probe_wraps_millis);
  return UNITY_END();
}
