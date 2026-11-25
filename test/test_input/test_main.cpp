#include <unity.h>
#include "input_logic.h"

void setUp() {}
void tearDown() {}

// Gray code: 00 -> 01 -> 11 -> 10 -> 00 = um detent horário.
static int8_t feed(QuadDecoder& q, int ab) { return q.update(ab & 2, ab & 1); }

void test_quad_cw_one_detent() {
  QuadDecoder q;
  TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b01));
  TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b11));
  TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b10));
  TEST_ASSERT_EQUAL_INT8(1, feed(q, 0b00));
}

void test_quad_ccw_one_detent() {
  QuadDecoder q;
  TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b10));
  TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b11));
  TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b01));
  TEST_ASSERT_EQUAL_INT8(-1, feed(q, 0b00));
}

void test_quad_bounce_no_step() {
  QuadDecoder q;
  for (int i = 0; i < 10; ++i) {
    TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b01));
    TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b00));
  }
}

void test_quad_invalid_jump_ignored() {
  QuadDecoder q;
  TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b11));  // pulo inválido 00 -> 11
  TEST_ASSERT_EQUAL_INT8(0, feed(q, 0b00));  // volta
  int sum = 0;
  sum += feed(q, 0b01);
  sum += feed(q, 0b11);
  sum += feed(q, 0b10);
  sum += feed(q, 0b00);
  TEST_ASSERT_EQUAL_INT(1, sum);
}

void test_quad_fast_many_detents() {
  QuadDecoder q;
  int sum = 0;
  for (int i = 0; i < 10; ++i) {
    sum += feed(q, 0b01);
    sum += feed(q, 0b11);
    sum += feed(q, 0b10);
    sum += feed(q, 0b00);
  }
  TEST_ASSERT_EQUAL_INT(10, sum);
}

void test_quad_two_steps_per_detent() {
  QuadDecoder q(2);
  int sum = 0;
  sum += feed(q, 0b01);
  sum += feed(q, 0b11);
  TEST_ASSERT_EQUAL_INT(1, sum);
}

void test_btn_click() {
  ButtonGestures b;
  Gesture g = Gesture::None;
  for (uint32_t t = 0; t <= 150; t += 5) TEST_ASSERT_EQUAL(Gesture::None, b.update(true, t));
  for (uint32_t t = 155; t <= 250; t += 5) {
    Gesture x = b.update(false, t);
    if (x != Gesture::None) g = x;
  }
  TEST_ASSERT_EQUAL(Gesture::Click, g);
}

void test_btn_bounce_ignored() {
  ButtonGestures b;
  for (uint32_t t = 0; t < 500; t += 10) {
    TEST_ASSERT_EQUAL(Gesture::None, b.update(true, t));
    TEST_ASSERT_EQUAL(Gesture::None, b.update(false, t + 5));
  }
}

void test_btn_hold2s_fires_while_pressed() {
  ButtonGestures b;
  Gesture last = Gesture::None;
  for (uint32_t t = 0; t < 2000; t += 5) {
    last = b.update(true, t);
    TEST_ASSERT_EQUAL(Gesture::None, last);
  }
  TEST_ASSERT_EQUAL(Gesture::Hold2s, b.update(true, 2000));
}

void test_btn_hold3s_after_hold2s() {
  ButtonGestures b;
  int h2 = 0, h3 = 0;
  for (uint32_t t = 0; t <= 4000; t += 5) {
    Gesture g = b.update(true, t);
    h2 += g == Gesture::Hold2s;
    h3 += g == Gesture::Hold3s;
    if (g == Gesture::Hold3s) TEST_ASSERT_TRUE(t >= 3000);
  }
  TEST_ASSERT_EQUAL(1, h2);
  TEST_ASSERT_EQUAL(1, h3);
}

void test_btn_no_click_after_hold() {
  ButtonGestures b;
  for (uint32_t t = 0; t <= 2500; t += 5) b.update(true, t);
  for (uint32_t t = 2505; t <= 2700; t += 5) TEST_ASSERT_EQUAL(Gesture::None, b.update(false, t));
}

void test_btn_click_after_hold_next_press() {
  ButtonGestures b;
  for (uint32_t t = 0; t <= 2500; t += 5) b.update(true, t);
  for (uint32_t t = 2505; t <= 2700; t += 5) b.update(false, t);
  Gesture g = Gesture::None;
  for (uint32_t t = 2705; t <= 2900; t += 5) b.update(true, t);
  for (uint32_t t = 2905; t <= 3000; t += 5) {
    Gesture x = b.update(false, t);
    if (x != Gesture::None) g = x;
  }
  TEST_ASSERT_EQUAL(Gesture::Click, g);
}

void test_quad_dropped_transition_resyncs_at_rest() {
  QuadDecoder q;
  int sum = 0;
  sum += feed(q, 0b01);
  sum += feed(q, 0b10);  // 11 perdido (ISR atrasada): salto inválido
  sum += feed(q, 0b00);  // chegou no repouso: conta o detent
  TEST_ASSERT_EQUAL_INT(1, sum);
  // bounce no repouso não gera passo fantasma
  sum = 0;
  for (int i = 0; i < 5; ++i) {
    sum += feed(q, 0b01);
    sum += feed(q, 0b00);
  }
  TEST_ASSERT_EQUAL_INT(0, sum);
  // e o próximo detent no sentido contrário não se perde
  sum = feed(q, 0b10) + feed(q, 0b11) + feed(q, 0b01) + feed(q, 0b00);
  TEST_ASSERT_EQUAL_INT(-1, sum);
}

void test_quad_rest_state_from_reset() {
  QuadDecoder q;
  q.reset(true, true);  // encoder com pull-up repousa em 11
  int sum = feed(q, 0b10) + feed(q, 0b00) + feed(q, 0b01) + feed(q, 0b11);
  TEST_ASSERT_EQUAL_INT(1, sum);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_quad_cw_one_detent);
  RUN_TEST(test_quad_ccw_one_detent);
  RUN_TEST(test_quad_bounce_no_step);
  RUN_TEST(test_quad_invalid_jump_ignored);
  RUN_TEST(test_quad_fast_many_detents);
  RUN_TEST(test_quad_two_steps_per_detent);
  RUN_TEST(test_btn_click);
  RUN_TEST(test_btn_bounce_ignored);
  RUN_TEST(test_btn_hold2s_fires_while_pressed);
  RUN_TEST(test_btn_hold3s_after_hold2s);
  RUN_TEST(test_btn_no_click_after_hold);
  RUN_TEST(test_btn_click_after_hold_next_press);
  RUN_TEST(test_quad_dropped_transition_resyncs_at_rest);
  RUN_TEST(test_quad_rest_state_from_reset);
  return UNITY_END();
}
