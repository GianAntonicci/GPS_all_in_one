#include <unity.h>
#include "gps_logic.h"

void setUp() {}
void tearDown() {}

static void assertDt(const DateTime& d, uint16_t y, uint8_t mo, uint8_t da, uint8_t h, uint8_t mi,
                     uint8_t s) {
  TEST_ASSERT_EQUAL_UINT16(y, d.year);
  TEST_ASSERT_EQUAL_UINT8(mo, d.month);
  TEST_ASSERT_EQUAL_UINT8(da, d.day);
  TEST_ASSERT_EQUAL_UINT8(h, d.hour);
  TEST_ASSERT_EQUAL_UINT8(mi, d.minute);
  TEST_ASSERT_EQUAL_UINT8(s, d.second);
}

void test_local_same_day() {
  assertDt(utcToLocal({2026, 11, 25, 15, 4, 5}, -3), 2026, 11, 25, 12, 4, 5);
}
void test_local_new_year_rollover() {
  assertDt(utcToLocal({2027, 1, 1, 1, 30, 0}, -3), 2026, 12, 31, 22, 30, 0);
}
void test_local_leap_day() {
  assertDt(utcToLocal({2028, 3, 1, 2, 0, 0}, -3), 2028, 2, 29, 23, 0, 0);
}
void test_local_non_leap() {
  assertDt(utcToLocal({2027, 3, 1, 2, 0, 0}, -3), 2027, 2, 28, 23, 0, 0);
}
void test_local_month_30() {
  assertDt(utcToLocal({2026, 5, 1, 0, 0, 0}, -3), 2026, 4, 30, 21, 0, 0);
}
void test_local_positive_offset_rollover() {
  assertDt(utcToLocal({2026, 12, 31, 23, 0, 0}, 2), 2027, 1, 1, 1, 0, 0);
}

void test_course_accepts_when_moving() {
  CourseTracker c;
  TEST_ASSERT_FALSE(c.known());
  c.update(true, 10, 45);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 45, c.course());
  TEST_ASSERT_TRUE(c.known());
  TEST_ASSERT_FALSE(c.stale());
}
void test_course_held_when_slow() {
  CourseTracker c;
  c.update(true, 10, 45);
  c.update(true, 1.5, 270);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 45, c.course());
  TEST_ASSERT_TRUE(c.stale());
}
void test_course_ignored_when_invalid() {
  CourseTracker c;
  c.update(true, 10, 45);
  c.update(false, 50, 90);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 45, c.course());
  TEST_ASSERT_TRUE(c.stale());
}
void test_course_initial_from_nvs() {
  CourseTracker c(120, true);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 120, c.course());
  TEST_ASSERT_TRUE(c.known());
  TEST_ASSERT_TRUE(c.stale());
}

static uint32_t r0() { return 0; }
static uint32_t r1000() { return 1000; }

void test_pos_scheduler_first_fix_immediate() {
  PosScheduler s(r0);
  TEST_ASSERT_TRUE(s.due(5000, true));
  TEST_ASSERT_FALSE(s.due(5001, true));
}
void test_pos_scheduler_period_with_jitter() {
  PosScheduler s(r1000);
  TEST_ASSERT_TRUE(s.due(0, true));
  TEST_ASSERT_FALSE(s.due(30999, true));
  TEST_ASSERT_TRUE(s.due(31000, true));
}
void test_pos_scheduler_no_fix_never() {
  PosScheduler s(r0);
  for (uint32_t t = 0; t < 200000; t += 1000) TEST_ASSERT_FALSE(s.due(t, false));
}
void test_pos_scheduler_fix_lost_and_back() {
  PosScheduler s(r0);
  TEST_ASSERT_TRUE(s.due(0, true));
  TEST_ASSERT_FALSE(s.due(10000, false));
  TEST_ASSERT_FALSE(s.due(12000, true));   // voltou cedo: mantém o horário agendado
  TEST_ASSERT_TRUE(s.due(30000, true));
  TEST_ASSERT_FALSE(s.due(40000, false));
  TEST_ASSERT_TRUE(s.due(90000, true));    // ficou muito tempo sem fix: envia na hora
}

void test_pos_scheduler_flapping_fix_no_burst() {
  // Mata fechada: o fix some e volta a cada poucos segundos. Não pode virar rajada de POS.
  PosScheduler s(r0);
  TEST_ASSERT_TRUE(s.due(0, true));
  for (uint32_t t = 2000; t < 30000; t += 4000) {
    TEST_ASSERT_FALSE(s.due(t, false));
    TEST_ASSERT_FALSE(s.due(t + 2000, true));
  }
  TEST_ASSERT_TRUE(s.due(30000, true));
}

void test_save_first_fix() {
  SavePolicy p;
  TEST_ASSERT_FALSE(p.due(0, false, 0, 0));
  TEST_ASSERT_TRUE(p.due(1000, true, -23.5, -46.6));
  TEST_ASSERT_FALSE(p.due(2000, true, -23.5, -46.6));
}

void test_save_when_fix_lost() {
  SavePolicy p;
  TEST_ASSERT_TRUE(p.due(0, true, -23.5, -46.6));
  TEST_ASSERT_FALSE(p.due(30000, true, -23.5001, -46.6));
  TEST_ASSERT_TRUE(p.due(31000, false, -23.5001, -46.6));  // entrou no túnel: grava a última
  TEST_ASSERT_FALSE(p.due(32000, false, -23.5001, -46.6));
}

void test_save_when_moved_far() {
  SavePolicy p;
  TEST_ASSERT_TRUE(p.due(0, true, -23.5, -46.6));
  TEST_ASSERT_FALSE(p.due(30000, true, -23.505, -46.6));   // ~550 m mas antes de 1 min
  TEST_ASSERT_TRUE(p.due(60000, true, -23.505, -46.6));    // > 200 m e >= 1 min
  TEST_ASSERT_FALSE(p.due(130000, true, -23.5055, -46.6)); // só ~55 m
}

void test_save_periodic() {
  SavePolicy p;
  TEST_ASSERT_TRUE(p.due(0, true, -23.5, -46.6));
  TEST_ASSERT_FALSE(p.due(599999, true, -23.5, -46.6));
  TEST_ASSERT_TRUE(p.due(600000, true, -23.5, -46.6));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_local_same_day);
  RUN_TEST(test_local_new_year_rollover);
  RUN_TEST(test_local_leap_day);
  RUN_TEST(test_local_non_leap);
  RUN_TEST(test_local_month_30);
  RUN_TEST(test_local_positive_offset_rollover);
  RUN_TEST(test_course_accepts_when_moving);
  RUN_TEST(test_course_held_when_slow);
  RUN_TEST(test_course_ignored_when_invalid);
  RUN_TEST(test_course_initial_from_nvs);
  RUN_TEST(test_pos_scheduler_first_fix_immediate);
  RUN_TEST(test_pos_scheduler_period_with_jitter);
  RUN_TEST(test_pos_scheduler_no_fix_never);
  RUN_TEST(test_pos_scheduler_fix_lost_and_back);
  RUN_TEST(test_pos_scheduler_flapping_fix_no_burst);
  RUN_TEST(test_save_first_fix);
  RUN_TEST(test_save_when_fix_lost);
  RUN_TEST(test_save_when_moved_far);
  RUN_TEST(test_save_periodic);
  return UNITY_END();
}
