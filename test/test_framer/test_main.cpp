#include <unity.h>
#include <cstring>
#include "framer.h"

void setUp() {}
void tearDown() {}

static void feedStr(Framer& f, const char* s, uint32_t t) {
  while (*s) f.feed((uint8_t)*s++, t);
}

void test_frame_with_rssi() {
  Framer f;
  feedStr(f, "$PING,A1,7*00\n", 0);
  f.feed(0xAB, 1);
  Frame fr;
  TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_EQUAL_STRING("$PING,A1,7*00", fr.line);
  TEST_ASSERT_EQUAL(13, fr.len);
  TEST_ASSERT_TRUE(fr.hasRssi);
  TEST_ASSERT_EQUAL_INT16(-84, fr.rssi);
}

void test_frame_without_rssi_after_timeout() {
  Framer f;
  feedStr(f, "$PING,A1,7*00\n", 100);
  Frame fr;
  f.tick(149);
  TEST_ASSERT_FALSE(f.pop(fr));
  f.tick(151);
  TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_EQUAL_STRING("$PING,A1,7*00", fr.line);
  TEST_ASSERT_FALSE(fr.hasRssi);
}

void test_back_to_back_frames_without_rssi() {
  Framer f;
  feedStr(f, "$PING,A1,1*00\n", 0);
  Frame fr;
  TEST_ASSERT_FALSE(f.pop(fr));
  feedStr(f, "$PING,A1,2*00\n", 0);
  TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_EQUAL_STRING("$PING,A1,1*00", fr.line);
  TEST_ASSERT_FALSE(fr.hasRssi);
  f.feed(0xC0, 0);
  TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_EQUAL_STRING("$PING,A1,2*00", fr.line);
  TEST_ASSERT_TRUE(fr.hasRssi);
}

void test_garbage_and_power_on_discarded() {
  Framer f;
  feedStr(f, "Power On\r\nxx", 0);
  feedStr(f, "$PING,A1,1*00\n", 0);
  f.feed(0xC0, 0);
  Frame fr;
  TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_EQUAL_STRING("$PING,A1,1*00", fr.line);
  TEST_ASSERT_FALSE(f.pop(fr));
}

void test_overlong_line_dropped_next_ok() {
  Framer f;
  f.feed('$', 0);
  for (int i = 0; i < 200; ++i) f.feed('x', 0);
  f.feed('\n', 0);
  f.tick(1000);
  Frame fr;
  TEST_ASSERT_FALSE(f.pop(fr));
  feedStr(f, "$PING,A1,3*00\n", 1000);
  f.feed(0xC0, 1000);
  TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_EQUAL_STRING("$PING,A1,3*00", fr.line);
  TEST_ASSERT_FALSE(f.pop(fr));
}

void test_truncated_frame_restarts_on_dollar() {
  Framer f;
  feedStr(f, "$MSG,A1,3,ol", 0);
  feedStr(f, "$PING,A1,4*00\n", 0);
  f.feed(0xC0, 0);
  Frame fr;
  TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_EQUAL_STRING("$PING,A1,4*00", fr.line);
  TEST_ASSERT_FALSE(f.pop(fr));
}

void test_stray_high_byte_without_pending_ignored() {
  Framer f;
  f.feed(0xC0, 0);
  feedStr(f, "$PING,A1,5*00\n", 0);
  f.tick(100);
  Frame fr;
  TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_FALSE(fr.hasRssi);
}

void test_cr_before_newline_stripped() {
  Framer f;
  feedStr(f, "$PING,A1,6*00\r\n", 0);
  f.tick(100);
  Frame fr;
  TEST_ASSERT_TRUE(f.pop(fr));
  TEST_ASSERT_EQUAL_STRING("$PING,A1,6*00", fr.line);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_frame_with_rssi);
  RUN_TEST(test_frame_without_rssi_after_timeout);
  RUN_TEST(test_back_to_back_frames_without_rssi);
  RUN_TEST(test_garbage_and_power_on_discarded);
  RUN_TEST(test_overlong_line_dropped_next_ok);
  RUN_TEST(test_truncated_frame_restarts_on_dollar);
  RUN_TEST(test_stray_high_byte_without_pending_ignored);
  RUN_TEST(test_cr_before_newline_stripped);
  return UNITY_END();
}
