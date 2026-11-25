#include <unity.h>
#include <cstdio>
#include <cstring>
#include "msg_history.h"

void setUp() {}
void tearDown() {}

void test_newest_first() {
  MsgHistory h;
  h.addReceived("B2", "um", true, 10, 0);
  h.addReceived("B2", "dois", true, 10, 1);
  TEST_ASSERT_EQUAL(2, h.size());
  TEST_ASSERT_EQUAL_STRING("dois", h.at(0).text);
  TEST_ASSERT_EQUAL_STRING("um", h.at(1).text);
  TEST_ASSERT_FALSE(h.at(0).mine);
  TEST_ASSERT_EQUAL(MsgStatus::Received, h.at(0).status);
}

void test_ring_keeps_20() {
  MsgHistory h;
  char t[8];
  for (int i = 1; i <= 25; ++i) {
    snprintf(t, sizeof t, "m%d", i);
    h.addReceived("B2", t, false, 0, 0);
  }
  TEST_ASSERT_EQUAL(20, h.size());
  TEST_ASSERT_EQUAL_STRING("m25", h.at(0).text);
  TEST_ASSERT_EQUAL_STRING("m6", h.at(19).text);
}

void test_delivery_matches_seq() {
  MsgHistory h;
  h.addSent(5, "A1", "cinco", false, 0, 0);
  h.addSent(6, "A1", "seis", false, 0, 0);
  h.onDelivery(5, true);
  TEST_ASSERT_EQUAL(MsgStatus::Pending, h.at(0).status);    // seis
  TEST_ASSERT_EQUAL(MsgStatus::Delivered, h.at(1).status);  // cinco
  TEST_ASSERT_TRUE(h.at(1).mine);
}

void test_late_delivery_old_seq_ignored() {
  MsgHistory h;
  h.addSent(5, "A1", "cinco", false, 0, 0);
  h.onDelivery(5, false);
  h.addSent(6, "A1", "seis", false, 0, 0);
  h.onDelivery(5, true);  // ACK atrasado da que já falhou
  TEST_ASSERT_EQUAL(MsgStatus::Failed, h.at(1).status);
  TEST_ASSERT_EQUAL(MsgStatus::Pending, h.at(0).status);
}

void test_seq_reuse_matches_newest_pending() {
  MsgHistory h;
  h.addSent(7, "A1", "velha", false, 0, 0);
  h.onDelivery(7, true);
  h.addSent(7, "A1", "nova", false, 0, 0);  // seq deu a volta
  h.onDelivery(7, false);
  TEST_ASSERT_EQUAL(MsgStatus::Failed, h.at(0).status);
  TEST_ASSERT_EQUAL(MsgStatus::Delivered, h.at(1).status);
}

void test_has_pending() {
  MsgHistory h;
  TEST_ASSERT_FALSE(h.hasPending());
  h.addSent(1, "A1", "x", false, 0, 0);
  TEST_ASSERT_TRUE(h.hasPending());
  h.onDelivery(1, true);
  TEST_ASSERT_FALSE(h.hasPending());
}

void test_text_truncated_48() {
  MsgHistory h;
  char t[70];
  memset(t, 'y', 69);
  t[69] = 0;
  h.addReceived("B2", t, false, 0, 0);
  TEST_ASSERT_EQUAL(48, strlen(h.at(0).text));
}

void test_time_stored() {
  MsgHistory h;
  h.addReceived("B2", "x", true, 12, 34);
  TEST_ASSERT_TRUE(h.at(0).hasTime);
  TEST_ASSERT_EQUAL_UINT8(12, h.at(0).hour);
  TEST_ASSERT_EQUAL_UINT8(34, h.at(0).minute);
  TEST_ASSERT_EQUAL_STRING("B2", h.at(0).from);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_newest_first);
  RUN_TEST(test_ring_keeps_20);
  RUN_TEST(test_delivery_matches_seq);
  RUN_TEST(test_late_delivery_old_seq_ignored);
  RUN_TEST(test_seq_reuse_matches_newest_pending);
  RUN_TEST(test_has_pending);
  RUN_TEST(test_text_truncated_48);
  RUN_TEST(test_time_stored);
  return UNITY_END();
}
