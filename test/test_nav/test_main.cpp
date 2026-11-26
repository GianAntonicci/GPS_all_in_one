#include <unity.h>
#include "nav.h"

void setUp() {}
void tearDown() {}

void test_cursor_wraps_forward() {
  Cursor c(6);
  c.move(5);
  TEST_ASSERT_EQUAL_UINT8(5, c.index());
  c.move(1);
  TEST_ASSERT_EQUAL_UINT8(0, c.index());
}
void test_cursor_wraps_backward() {
  Cursor c(6);
  c.move(-1);
  TEST_ASSERT_EQUAL_UINT8(5, c.index());
}
void test_cursor_big_delta() {
  Cursor c(6);
  c.move(13);
  TEST_ASSERT_EQUAL_UINT8(1, c.index());
  c.move(-14);
  TEST_ASSERT_EQUAL_UINT8(5, c.index());
}
void test_cursor_setcount_clamps() {
  Cursor c(6);
  c.move(5);
  c.setCount(3);
  TEST_ASSERT_EQUAL_UINT8(2, c.index());
}
void test_cursor_single_item() {
  Cursor c(1);
  c.move(3);
  TEST_ASSERT_EQUAL_UINT8(0, c.index());
  c.move(-7);
  TEST_ASSERT_EQUAL_UINT8(0, c.index());
}
void test_cursor_reset() {
  Cursor c(4);
  c.move(2);
  c.reset();
  TEST_ASSERT_EQUAL_UINT8(0, c.index());
}
void test_cursor_zero_count_safe() {
  Cursor c(0);
  c.move(1);
  TEST_ASSERT_EQUAL_UINT8(0, c.index());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cursor_wraps_forward);
  RUN_TEST(test_cursor_wraps_backward);
  RUN_TEST(test_cursor_big_delta);
  RUN_TEST(test_cursor_setcount_clamps);
  RUN_TEST(test_cursor_single_item);
  RUN_TEST(test_cursor_reset);
  RUN_TEST(test_cursor_zero_count_safe);
  return UNITY_END();
}
