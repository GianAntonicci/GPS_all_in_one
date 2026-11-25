#include <unity.h>
#include "track_logic.h"

void setUp() {}
void tearDown() {}

static const double kLat0 = -23.5, kLon0 = -46.6;
static double north(double m) { return kLat0 + m / 111195.0; }

void test_policy_no_fix_never_due() {
  TrackPolicy p;
  bool seg = false;
  TEST_ASSERT_FALSE(p.due(0, false, kLat0, kLon0, seg));
  TEST_ASSERT_FALSE(p.due(20000, false, kLat0, kLon0, seg));
}

void test_policy_first_fix_due_new_segment() {
  TrackPolicy p;
  bool seg = false;
  TEST_ASSERT_TRUE(p.due(100, true, kLat0, kLon0, seg));
  TEST_ASSERT_TRUE(seg);
}

void test_policy_period() {
  TrackPolicy p;
  bool seg;
  p.due(0, true, kLat0, kLon0, seg);
  p.accepted(0, kLat0, kLon0);
  TEST_ASSERT_FALSE(p.due(9999, true, kLat0, kLon0, seg));
  TEST_ASSERT_TRUE(p.due(10000, true, kLat0, kLon0, seg));
  TEST_ASSERT_FALSE(seg);
}

void test_policy_distance() {
  TrackPolicy p;
  bool seg;
  p.due(0, true, kLat0, kLon0, seg);
  p.accepted(0, kLat0, kLon0);
  TEST_ASSERT_TRUE(p.due(2000, true, north(25), kLon0, seg));
  TEST_ASSERT_FALSE(seg);
}

void test_policy_fix_loss_starts_segment() {
  TrackPolicy p;
  bool seg;
  p.due(0, true, kLat0, kLon0, seg);
  p.accepted(0, kLat0, kLon0);
  TEST_ASSERT_FALSE(p.due(1000, false, kLat0, kLon0, seg));
  TEST_ASSERT_TRUE(p.due(2000, true, kLat0, kLon0, seg));
  TEST_ASSERT_TRUE(seg);
}

void test_policy_new_segment_survives_rejected_enqueue() {
  TrackPolicy p;
  bool seg;
  p.due(0, true, kLat0, kLon0, seg);
  p.accepted(0, kLat0, kLon0);
  p.due(1000, false, kLat0, kLon0, seg);
  TEST_ASSERT_TRUE(p.due(2000, true, kLat0, kLon0, seg));
  TEST_ASSERT_TRUE(seg);  // fila cheia: não aceitou
  seg = false;
  TEST_ASSERT_TRUE(p.due(2100, true, kLat0, kLon0, seg));
  TEST_ASSERT_TRUE(seg);
}

void test_policy_distance_from_last_accepted() {
  TrackPolicy p;
  bool seg;
  p.due(0, true, kLat0, kLon0, seg);
  p.accepted(0, kLat0, kLon0);
  TEST_ASSERT_FALSE(p.due(1000, true, north(15), kLon0, seg));
  TEST_ASSERT_TRUE(p.due(1100, true, north(25), kLon0, seg));
}

void test_buffer_filters_close_points() {
  TrackPt st[8];
  TrackBuffer b(st, 8);
  b.add(kLat0, kLon0, true);
  b.add(north(5), kLon0, false);
  b.add(north(10), kLon0, false);
  TEST_ASSERT_EQUAL_UINT32(1, b.size());
  TEST_ASSERT_TRUE(b.at(0).segStart);
}

void test_buffer_first_point_is_segment_start() {
  TrackPt st[8];
  TrackBuffer b(st, 8);
  b.add(kLat0, kLon0, false);
  TEST_ASSERT_TRUE(b.at(0).segStart);
}

void test_buffer_new_segment_kept_even_if_close() {
  TrackPt st[8];
  TrackBuffer b(st, 8);
  b.add(kLat0, kLon0, true);
  b.add(north(5), kLon0, true);
  TEST_ASSERT_EQUAL_UINT32(2, b.size());
  TEST_ASSERT_TRUE(b.at(1).segStart);
}

void test_buffer_decimates_when_full() {
  TrackPt st[8];
  TrackBuffer b(st, 8);
  for (int i = 0; i < 9; ++i) b.add(north(30.0 * i), kLon0, i == 0);
  TEST_ASSERT_EQUAL_UINT32(5, b.size());
  TEST_ASSERT_FLOAT_WITHIN(1e-6, (float)north(0), b.at(0).lat);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, (float)north(240), b.at(4).lat);
}

void test_buffer_decimation_keeps_segment_start() {
  TrackPt st[8];
  TrackBuffer b(st, 8);
  for (int i = 0; i < 8; ++i) b.add(north(30.0 * i), kLon0, i == 0 || i == 3);
  b.add(north(30.0 * 8), kLon0, false);
  int starts = 0;
  for (uint32_t i = 0; i < b.size(); ++i) starts += b.at(i).segStart ? 1 : 0;
  TEST_ASSERT_EQUAL_INT(2, starts);
  TEST_ASSERT_TRUE(b.at(2).segStart);  // o antigo índice 4 recebeu o início do índice 3
  TEST_ASSERT_FLOAT_WITHIN(1e-6, (float)north(120), b.at(2).lat);
}

void test_buffer_clear() {
  TrackPt st[4];
  TrackBuffer b(st, 4);
  b.add(kLat0, kLon0, true);
  b.clear();
  TEST_ASSERT_EQUAL_UINT32(0, b.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_policy_no_fix_never_due);
  RUN_TEST(test_policy_first_fix_due_new_segment);
  RUN_TEST(test_policy_period);
  RUN_TEST(test_policy_distance);
  RUN_TEST(test_policy_fix_loss_starts_segment);
  RUN_TEST(test_policy_new_segment_survives_rejected_enqueue);
  RUN_TEST(test_policy_distance_from_last_accepted);
  RUN_TEST(test_buffer_filters_close_points);
  RUN_TEST(test_buffer_first_point_is_segment_start);
  RUN_TEST(test_buffer_new_segment_kept_even_if_close);
  RUN_TEST(test_buffer_decimates_when_full);
  RUN_TEST(test_buffer_decimation_keeps_segment_start);
  RUN_TEST(test_buffer_clear);
  return UNITY_END();
}
