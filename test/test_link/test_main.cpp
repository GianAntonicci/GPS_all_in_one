#include <unity.h>
#include "../link_harness.h"

void setUp() {}
void tearDown() {}

void test_msg_delivered_with_ack() {
  FakeSink sa, sb;
  Link a(sa, "A1", 10, rand0), b(sb, "B2", 50, rand0);
  int s = a.sendMsg("oi", 0);
  TEST_ASSERT_EQUAL(10, s);
  pumpAll(sa, a, sb, b, 100);
  TEST_ASSERT_EQUAL(1, sb.rx.size());
  TEST_ASSERT_EQUAL(PktType::MSG, sb.rx[0].type);
  TEST_ASSERT_EQUAL_STRING("oi", sb.rx[0].text);
  TEST_ASSERT_EQUAL(1, sa.deliveries.size());
  TEST_ASSERT_EQUAL_UINT8(10, sa.deliveries[0].first);
  TEST_ASSERT_EQUAL(Delivery::Delivered, sa.deliveries[0].second);
  // Depois de entregue, não reenvia
  a.tick(20000);
  TEST_ASSERT_EQUAL(1, sa.countSent(PktType::MSG));
}

void test_msg_retries_same_seq_then_fails() {
  FakeSink sa;
  Link a(sa, "A1", 0, rand0);
  int s = a.sendMsg("oi", 0);
  a.tick(3999);
  TEST_ASSERT_EQUAL(1, sa.sent.size());
  a.tick(4000);
  TEST_ASSERT_EQUAL(2, sa.sent.size());
  TEST_ASSERT_EQUAL_UINT8(s, sa.sent[1].seq);
  a.tick(8000);
  TEST_ASSERT_EQUAL(3, sa.sent.size());
  TEST_ASSERT_EQUAL(0, sa.deliveries.size());
  a.tick(12000);
  TEST_ASSERT_EQUAL(1, sa.deliveries.size());
  TEST_ASSERT_EQUAL(Delivery::Failed, sa.deliveries[0].second);
  a.tick(30000);
  TEST_ASSERT_EQUAL(3, sa.sent.size());
}

void test_lost_ack_duplicate_acked_not_redelivered() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.sendMsg("oi", 0);
  deliver(sa, b, 0);
  drop(sb);  // ACK perdido
  a.tick(4000);  // reenvio
  deliver(sa, b, 4000);
  TEST_ASSERT_EQUAL(2, sb.countSent(PktType::ACK));
  TEST_ASSERT_EQUAL(1, sb.rx.size());
  deliver(sb, a, 4000);
  TEST_ASSERT_EQUAL(1, sa.deliveries.size());
  TEST_ASSERT_EQUAL(Delivery::Delivered, sa.deliveries[0].second);
}

void test_dedup_expires() {
  FakeSink sa, sb;
  Link a(sa, "A1", 7, rand0), b(sb, "B2", 0, rand0);
  a.sendPing(0);
  deliver(sa, b, 0);
  TEST_ASSERT_EQUAL(1, sb.rx.size());
  // O mesmo (id, seq) dentro do TTL é duplicata
  sa.delivered = 0;
  deliver(sa, b, 59000);
  TEST_ASSERT_EQUAL(1, sb.rx.size());
  // Depois do TTL (amigo reiniciou e repetiu seq) é pacote novo
  sa.delivered = 0;
  deliver(sa, b, 60001);
  TEST_ASSERT_EQUAL(2, sb.rx.size());
}

void test_own_frames_ignored() {
  FakeSink sa, sa2;
  Link a(sa, "A1", 0, rand0), a2(sa2, "A1", 100, rand0);
  a2.sendPing(0);
  deliver(sa2, a, 0);
  TEST_ASSERT_EQUAL(0, sa.rx.size());
  TEST_ASSERT_EQUAL(0, sa.sent.size());
  TEST_ASSERT_FALSE(a.hasPeer());
}

void test_late_ack_ignored() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.sendMsg("oi", 0);
  drop(sa);
  a.tick(4000);
  drop(sa);
  a.tick(8000);
  // a 3ª tentativa chega, mas o ACK só volta depois da falha
  deliver(sa, b, 8000);
  a.tick(12000);
  TEST_ASSERT_EQUAL(1, sa.deliveries.size());
  TEST_ASSERT_EQUAL(Delivery::Failed, sa.deliveries[0].second);
  deliver(sb, a, 12500);
  TEST_ASSERT_EQUAL(1, sa.deliveries.size());
}

void test_second_msg_while_pending_rejected() {
  FakeSink sa;
  Link a(sa, "A1", 0, rand0);
  TEST_ASSERT_TRUE(a.sendMsg("um", 0) >= 0);
  TEST_ASSERT_EQUAL(-1, a.sendMsg("dois", 0));
  TEST_ASSERT_EQUAL(-1, a.sendPing(0));
}

void test_pos_no_ack() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.sendPos(-23.5, -46.6, 700, 90, 0);
  deliver(sa, b, 0);
  TEST_ASSERT_EQUAL(1, sb.rx.size());
  TEST_ASSERT_EQUAL(PktType::POS, sb.rx[0].type);
  TEST_ASSERT_EQUAL(0, sb.sent.size());
}

void test_seq_wraps() {
  FakeSink sa;
  Link a(sa, "A1", 255, rand0);
  a.sendPos(0, 0, 0, 0, 0);
  a.sendPos(0, 0, 0, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(255, sa.sent[0].seq);
  TEST_ASSERT_EQUAL_UINT8(0, sa.sent[1].seq);
}

void test_jitter_applied() {
  FakeSink sa;
  Link a(sa, "A1", 0, rand500);
  a.sendPing(0);
  a.tick(4000);
  TEST_ASSERT_EQUAL(1, sa.sent.size());
  a.tick(4500);
  TEST_ASSERT_EQUAL(2, sa.sent.size());
}

void test_peer_rx_time_tracked() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  TEST_ASSERT_FALSE(a.hasPeer());
  b.sendPos(1, 2, 3, 4, 0);
  deliver(sb, a, 1234);
  TEST_ASSERT_TRUE(a.hasPeer());
  TEST_ASSERT_EQUAL_UINT32(1234, a.lastPeerRxMs());
}

void test_invalid_frame_ignored() {
  FakeSink sa;
  Link a(sa, "A1", 0, rand0);
  Frame f = frameFromRaw("$PING,B2,1*00\n");  // checksum errado
  a.onFrame(f, 0);
  TEST_ASSERT_EQUAL(0, sa.rx.size());
  TEST_ASSERT_EQUAL(0, sa.sent.size());
  TEST_ASSERT_FALSE(a.hasPeer());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_msg_delivered_with_ack);
  RUN_TEST(test_msg_retries_same_seq_then_fails);
  RUN_TEST(test_lost_ack_duplicate_acked_not_redelivered);
  RUN_TEST(test_dedup_expires);
  RUN_TEST(test_own_frames_ignored);
  RUN_TEST(test_late_ack_ignored);
  RUN_TEST(test_second_msg_while_pending_rejected);
  RUN_TEST(test_pos_no_ack);
  RUN_TEST(test_seq_wraps);
  RUN_TEST(test_jitter_applied);
  RUN_TEST(test_peer_rx_time_tracked);
  RUN_TEST(test_invalid_frame_ignored);
  return UNITY_END();
}
