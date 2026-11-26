#include <unity.h>
#include "../link_harness.h"

void setUp() { randValue() = 0; }
void tearDown() {}

void test_sos_resends_10s_then_30s() {
  FakeSink sa;
  Link a(sa, "A1", 0, rand0);
  a.startSos(-23.5, -46.6, 80, 0);
  TEST_ASSERT_EQUAL(SosState::Sending, a.sosState());
  TEST_ASSERT_EQUAL(1, sa.countSent(PktType::SOS));
  for (uint32_t t = 10000; t <= 50000; t += 10000) a.tick(t);
  TEST_ASSERT_EQUAL(6, sa.countSent(PktType::SOS));
  a.tick(60000);
  a.tick(79999);
  TEST_ASSERT_EQUAL(6, sa.countSent(PktType::SOS));
  a.tick(80000);
  TEST_ASSERT_EQUAL(7, sa.countSent(PktType::SOS));
  a.tick(110000);
  TEST_ASSERT_EQUAL(8, sa.countSent(PktType::SOS));
}

void test_sos_ack_moves_to_received_keeps_sending() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(-23.5, -46.6, 80, 0);
  pumpAll(sa, a, sb, b, 0);
  TEST_ASSERT_EQUAL(SosState::Received, a.sosState());
  TEST_ASSERT_EQUAL(2, sa.sosStates.size());  // Sending, Received
  TEST_ASSERT_EQUAL(SosState::Received, sa.sosStates.back());
  a.tick(10000);
  a.tick(29999);
  TEST_ASSERT_EQUAL(1, sa.countSent(PktType::SOS));
  a.tick(30000);
  TEST_ASSERT_EQUAL(2, sa.countSent(PktType::SOS));
  // ACK do 2º SOS não muda o estado nem repete o evento
  pumpAll(sa, a, sb, b, 30000);
  TEST_ASSERT_EQUAL(2, sa.sosStates.size());
}

void test_sos_new_seq_and_position_each_send() {
  FakeSink sa;
  Link a(sa, "A1", 0, rand0);
  a.startSos(-23.5, -46.6, 80, 0);
  a.updateSosPosition(-23.6, -46.7, 79);
  a.tick(10000);
  TEST_ASSERT_EQUAL(2, sa.countSent(PktType::SOS));
  const Packet& p1 = sa.sent[0];
  const Packet& p2 = sa.sent[1];
  TEST_ASSERT_NOT_EQUAL(p1.seq, p2.seq);
  TEST_ASSERT_DOUBLE_WITHIN(0.000005, -23.6, p2.lat);
  TEST_ASSERT_DOUBLE_WITHIN(0.000005, -46.7, p2.lon);
  TEST_ASSERT_EQUAL_UINT8(79, p2.bat);
}

void test_ack_of_older_sos_counts() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  deliver(sa, b, 0);     // B recebe o 1º SOS e manda ACK
  a.tick(10000);         // A já mandou o 2º antes do ACK chegar
  deliver(sb, a, 10001); // chega ACK do 1º
  TEST_ASSERT_EQUAL(SosState::Received, a.sosState());
}

void test_seen_stops_sos() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  pumpAll(sa, a, sb, b, 0);
  TEST_ASSERT_TRUE(b.peerSosActive());
  b.confirmSeen(5000);
  TEST_ASSERT_FALSE(b.peerSosActive());
  TEST_ASSERT_EQUAL(2, sb.peerSosEvents.size());
  TEST_ASSERT_FALSE(sb.peerSosEvents.back());
  pumpAll(sa, a, sb, b, 5000);
  TEST_ASSERT_EQUAL(SosState::Seen, a.sosState());
  size_t n = sa.countSent(PktType::SOS);
  a.tick(100000);
  a.tick(200000);
  TEST_ASSERT_EQUAL(n, sa.countSent(PktType::SOS));
  // SEEN confirmado: B não reenvia
  size_t s = sb.countSent(PktType::SEEN);
  b.tick(100000);
  TEST_ASSERT_EQUAL(s, sb.countSent(PktType::SEEN));
}

void test_seen_retried_until_ack() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  pumpAll(sa, a, sb, b, 0);
  b.confirmSeen(1000);
  drop(sb);  // SEEN perdido
  b.tick(10999);
  TEST_ASSERT_EQUAL(1, sb.countSent(PktType::SEEN));
  b.tick(11000);
  TEST_ASSERT_EQUAL(2, sb.countSent(PktType::SEEN));
  uint8_t seenSeqs[2];
  int k = 0;
  for (auto& p : sb.sent)
    if (p.type == PktType::SEEN) seenSeqs[k++] = p.seq;
  TEST_ASSERT_EQUAL_UINT8(seenSeqs[0], seenSeqs[1]);  // reenvio usa o mesmo seq
  pumpAll(sa, a, sb, b, 11000);
  TEST_ASSERT_EQUAL(SosState::Seen, a.sosState());
}

void test_cancel_sends_sosoff_and_stops_peer_alarm() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  pumpAll(sa, a, sb, b, 0);
  a.cancelSos(2000);
  TEST_ASSERT_EQUAL(SosState::Idle, a.sosState());
  pumpAll(sa, a, sb, b, 2000);
  TEST_ASSERT_FALSE(b.peerSosActive());
  TEST_ASSERT_FALSE(sb.peerSosEvents.back());
  size_t n = sa.countSent(PktType::SOS);
  size_t off = sa.countSent(PktType::SOSOFF);
  a.tick(100000);
  TEST_ASSERT_EQUAL(n, sa.countSent(PktType::SOS));
  TEST_ASSERT_EQUAL(off, sa.countSent(PktType::SOSOFF));  // SOSOFF confirmado, sem reenvio
}

void test_sosoff_retried_until_ack() {
  FakeSink sa;
  Link a(sa, "A1", 0, rand0);
  a.startSos(1, 2, 3, 0);
  a.cancelSos(1000);
  a.tick(10999);
  TEST_ASSERT_EQUAL(1, sa.countSent(PktType::SOSOFF));
  a.tick(11000);
  TEST_ASSERT_EQUAL(2, sa.countSent(PktType::SOSOFF));
  TEST_ASSERT_EQUAL(1, sa.countSent(PktType::SOS));
}

void test_simultaneous_sos() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 100, rand0);
  a.startSos(-23.5, -46.6, 80, 0);
  b.startSos(-23.6, -46.7, 70, 0);
  pumpAll(sa, a, sb, b, 0);
  TEST_ASSERT_TRUE(a.peerSosActive());
  TEST_ASSERT_TRUE(b.peerSosActive());
  TEST_ASSERT_EQUAL(SosState::Received, a.sosState());
  TEST_ASSERT_EQUAL(SosState::Received, b.sosState());
}

void test_peer_alarm_raised_once() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  deliver(sa, b, 0);
  a.tick(10000);
  deliver(sa, b, 10000);
  a.tick(20000);
  deliver(sa, b, 20000);
  TEST_ASSERT_EQUAL(1, sb.peerSosEvents.size());
  TEST_ASSERT_TRUE(sb.peerSosEvents[0]);
  TEST_ASSERT_EQUAL(3, sb.rx.size());  // cada SOS chega como pacote (posição)
}

void test_inflight_sos_after_seen_does_not_reraise() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  pumpAll(sa, a, sb, b, 0);
  a.tick(30000);        // A manda outro SOS...
  b.confirmSeen(30000); // ...enquanto B aperta "Vi"
  deliver(sa, b, 30001);
  TEST_ASSERT_FALSE(b.peerSosActive());
  TEST_ASSERT_EQUAL(2, sb.peerSosEvents.size());
}

void test_new_sos_after_seen_raises_alarm() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  pumpAll(sa, a, sb, b, 0);
  b.confirmSeen(1000);
  pumpAll(sa, a, sb, b, 1000);
  TEST_ASSERT_EQUAL(SosState::Seen, a.sosState());
  a.startSos(1, 2, 3, 500000);  // nova emergência
  TEST_ASSERT_EQUAL(SosState::Sending, a.sosState());
  pumpAll(sa, a, sb, b, 500000);
  TEST_ASSERT_TRUE(b.peerSosActive());
}

void test_restart_sos_cancels_pending_sosoff() {
  FakeSink sa;
  Link a(sa, "A1", 0, rand0);
  a.startSos(1, 2, 3, 0);
  a.cancelSos(1000);
  a.startSos(1, 2, 3, 2000);
  a.tick(50000);
  TEST_ASSERT_EQUAL(1, sa.countSent(PktType::SOSOFF));
}

void test_sos_start_delay_random() {
  // Dois aparelhos disparando juntos (queda no mesmo acidente) não podem colidir sempre.
  FakeSink sa;
  randValue() = 1500;
  Link a(sa, "A1", 0, randVar);
  a.startSos(1, 2, 3, 0);
  TEST_ASSERT_EQUAL(SosState::Sending, a.sosState());
  TEST_ASSERT_EQUAL(0, sa.countSent(PktType::SOS));
  a.tick(1499);
  TEST_ASSERT_EQUAL(0, sa.countSent(PktType::SOS));
  a.tick(1500);
  TEST_ASSERT_EQUAL(1, sa.countSent(PktType::SOS));
}

void test_sos_retry_jitter_larger_than_airtime() {
  // Jitter do SOS até 5 s (> ~1,5 s de airtime de um SOS em SF11).
  FakeSink sa;
  randValue() = 4000;  // atraso inicial 4000 % 2001 = 1999; jitter 4000 % 5001 = 4000
  Link a(sa, "A1", 0, randVar);
  a.startSos(1, 2, 3, 0);
  a.tick(1999);
  TEST_ASSERT_EQUAL(1, sa.countSent(PktType::SOS));
  a.tick(15998);
  TEST_ASSERT_EQUAL(1, sa.countSent(PktType::SOS));
  a.tick(15999);
  TEST_ASSERT_EQUAL(2, sa.countSent(PktType::SOS));
}

void test_seen_carries_sos_seq() {
  FakeSink sa, sb;
  Link a(sa, "A1", 40, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  deliver(sa, b, 0);
  b.confirmSeen(100);
  const Packet* seen = sb.lastSent(PktType::SEEN);
  TEST_ASSERT_NOT_NULL(seen);
  TEST_ASSERT_EQUAL_UINT8(40, seen->ackSeq);  // seq do SOS que foi visto
}

void test_seen_for_old_episode_ignored() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  pumpAll(sa, a, sb, b, 0);
  b.confirmSeen(1000);
  drop(sb);              // SEEN perdido
  a.cancelSos(2000);     // A cancela...
  drop(sa);
  for (int i = 0; i < 9; ++i) a.startSos(1, 2, 3, 3000 + i);  // ...e começa nova emergência (seqs novos)
  drop(sa);
  b.tick(11000);         // reenvio atrasado do SEEN antigo
  deliver(sb, a, 11000);
  TEST_ASSERT_EQUAL(SosState::Sending, a.sosState());
}

void test_pending_seen_mutes_only_briefly() {
  FakeSink sa, sb;
  Link a(sa, "A1", 0, rand0), b(sb, "B2", 0, rand0);
  a.startSos(1, 2, 3, 0);
  pumpAll(sa, a, sb, b, 0);
  b.confirmSeen(1000);
  drop(sb);              // SEEN e ACKs perdidos (enlace assimétrico)
  a.cancelSos(2000);
  drop(sa);
  a.startSos(1, 2, 3, 60000);  // nova emergência bem depois
  deliver(sa, b, 60000);
  TEST_ASSERT_TRUE(b.peerSosActive());
}

void test_sos_attempts_counts_sends() {
  FakeSink sa;
  Link a(sa, "A1", 0, rand0);
  TEST_ASSERT_EQUAL_UINT8(0, a.sosAttempts());
  a.startSos(1, 2, 3, 0);
  a.tick(10000);
  a.tick(20000);
  TEST_ASSERT_EQUAL_UINT8(3, a.sosAttempts());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_sos_resends_10s_then_30s);
  RUN_TEST(test_sos_ack_moves_to_received_keeps_sending);
  RUN_TEST(test_sos_new_seq_and_position_each_send);
  RUN_TEST(test_ack_of_older_sos_counts);
  RUN_TEST(test_seen_stops_sos);
  RUN_TEST(test_seen_retried_until_ack);
  RUN_TEST(test_cancel_sends_sosoff_and_stops_peer_alarm);
  RUN_TEST(test_sosoff_retried_until_ack);
  RUN_TEST(test_simultaneous_sos);
  RUN_TEST(test_peer_alarm_raised_once);
  RUN_TEST(test_inflight_sos_after_seen_does_not_reraise);
  RUN_TEST(test_new_sos_after_seen_raises_alarm);
  RUN_TEST(test_restart_sos_cancels_pending_sosoff);
  RUN_TEST(test_sos_start_delay_random);
  RUN_TEST(test_sos_retry_jitter_larger_than_airtime);
  RUN_TEST(test_seen_carries_sos_seq);
  RUN_TEST(test_seen_for_old_episode_ignored);
  RUN_TEST(test_pending_seen_mutes_only_briefly);
  RUN_TEST(test_sos_attempts_counts_sends);
  return UNITY_END();
}
