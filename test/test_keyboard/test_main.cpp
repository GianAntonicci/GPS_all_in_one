#include <unity.h>
#include <cstring>
#include "keyboard.h"

void setUp() {}
void tearDown() {}

static size_t findChar(const KeyboardModel& k, char c) {
  for (size_t i = 0; i < k.keyCount(); ++i)
    if (k.key(i).kind == KeyKind::Char && k.key(i).ch == c) return i;
  return 999;
}
static size_t findKind(const KeyboardModel& k, KeyKind kind) {
  for (size_t i = 0; i < k.keyCount(); ++i)
    if (k.key(i).kind == kind) return i;
  return 999;
}

void test_layout_counts_and_positions() {
  KeyboardModel k;
  TEST_ASSERT_EQUAL(44, k.keyCount());
  TEST_ASSERT_EQUAL_CHAR('A', k.key(0).ch);
  TEST_ASSERT_EQUAL_UINT8(0, k.key(0).col);
  TEST_ASSERT_EQUAL_UINT8(0, k.key(0).row);
  TEST_ASSERT_EQUAL_CHAR('!', k.key(39).ch);
  TEST_ASSERT_EQUAL_UINT8(9, k.key(39).col);
  TEST_ASSERT_EQUAL_UINT8(3, k.key(39).row);
  TEST_ASSERT_EQUAL(KeyKind::Space, k.key(40).kind);
  TEST_ASSERT_EQUAL_UINT8(3, k.key(40).span);
  TEST_ASSERT_EQUAL(KeyKind::Backspace, k.key(41).kind);
  TEST_ASSERT_EQUAL_UINT8(3, k.key(41).col);
  TEST_ASSERT_EQUAL(KeyKind::Shift, k.key(42).kind);
  TEST_ASSERT_EQUAL_UINT8(5, k.key(42).col);
  TEST_ASSERT_EQUAL(KeyKind::Send, k.key(43).kind);
  TEST_ASSERT_EQUAL_UINT8(7, k.key(43).col);
  TEST_ASSERT_EQUAL_UINT8(3, k.key(43).span);
  TEST_ASSERT_TRUE(k.upper());
}

void test_type_and_backspace() {
  KeyboardModel k;
  k.press(findChar(k, 'A'));
  k.press(findChar(k, 'B'));
  k.press(findKind(k, KeyKind::Backspace));
  TEST_ASSERT_EQUAL_STRING("A", k.text());
}

void test_backspace_empty_safe() {
  KeyboardModel k;
  k.press(findKind(k, KeyKind::Backspace));
  TEST_ASSERT_EQUAL_STRING("", k.text());
  TEST_ASSERT_EQUAL(0, k.length());
}

void test_limit_48() {
  KeyboardModel k;
  for (int i = 0; i < 50; ++i) k.press(findChar(k, 'X'));
  TEST_ASSERT_EQUAL(48, k.length());
}

void test_shift_lowercase() {
  KeyboardModel k;
  k.press(findKind(k, KeyKind::Shift));
  TEST_ASSERT_FALSE(k.upper());
  k.press(findChar(k, 'A'));
  k.press(findChar(k, '1'));
  TEST_ASSERT_EQUAL_STRING("a1", k.text());
}

void test_send_empty_false() {
  KeyboardModel k;
  TEST_ASSERT_FALSE(k.press(findKind(k, KeyKind::Send)));
}

void test_send_returns_true_keeps_text() {
  KeyboardModel k;
  k.press(findChar(k, 'O'));
  k.press(findChar(k, 'I'));
  TEST_ASSERT_TRUE(k.press(findKind(k, KeyKind::Send)));
  TEST_ASSERT_EQUAL_STRING("OI", k.text());
  k.clear();
  TEST_ASSERT_EQUAL_STRING("", k.text());
}

void test_space() {
  KeyboardModel k;
  k.press(findChar(k, 'A'));
  k.press(findKind(k, KeyKind::Space));
  k.press(findChar(k, 'B'));
  TEST_ASSERT_EQUAL_STRING("A B", k.text());
}

void test_out_of_range_key_ignored() {
  KeyboardModel k;
  TEST_ASSERT_FALSE(k.press(100));
  TEST_ASSERT_EQUAL(0, k.length());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_layout_counts_and_positions);
  RUN_TEST(test_type_and_backspace);
  RUN_TEST(test_backspace_empty_safe);
  RUN_TEST(test_limit_48);
  RUN_TEST(test_shift_lowercase);
  RUN_TEST(test_send_empty_false);
  RUN_TEST(test_send_returns_true_keeps_text);
  RUN_TEST(test_space);
  RUN_TEST(test_out_of_range_key_ignored);
  return UNITY_END();
}
