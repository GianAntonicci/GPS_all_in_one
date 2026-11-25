#include <unity.h>
#include "audio_cues.h"

void setUp() {}
void tearDown() {}

static bool has(const CueOut& o, Pattern p) {
  for (uint8_t i = 0; i < o.n; ++i)
    if (o.play[i] == p) return true;
  return false;
}

void test_nothing_when_idle() {
  AudioCues c;
  CueOut o = c.update({Overlay::None, false, 0, SosState::Idle});
  TEST_ASSERT_EQUAL_UINT8(0, o.n);
  TEST_ASSERT_FALSE(o.stopAlarm);
}

void test_alarm_while_peer_overlay() {
  AudioCues c;
  TEST_ASSERT_TRUE(has(c.update({Overlay::PeerSos, false, 0, SosState::Idle}), Pattern::PeerAlarm));
  TEST_ASSERT_TRUE(has(c.update({Overlay::PeerSos, false, 0, SosState::Idle}), Pattern::PeerAlarm));
}

void test_alarm_stops_when_peer_cancels() {
  AudioCues c;
  c.update({Overlay::PeerSos, false, 0, SosState::Idle});
  CueOut o = c.update({Overlay::None, false, 0, SosState::Idle});
  TEST_ASSERT_TRUE(o.stopAlarm);
  TEST_ASSERT_EQUAL_UINT8(0, o.n);
  TEST_ASSERT_FALSE(c.update({Overlay::None, false, 0, SosState::Idle}).stopAlarm);
}

void test_countdown_ticks_each_number() {
  AudioCues c;
  TEST_ASSERT_TRUE(has(c.update({Overlay::Countdown, true, 10, SosState::Idle}), Pattern::CountdownTick));
  TEST_ASSERT_EQUAL_UINT8(0, c.update({Overlay::Countdown, true, 10, SosState::Idle}).n);
  TEST_ASSERT_TRUE(has(c.update({Overlay::Countdown, true, 9, SosState::Idle}), Pattern::CountdownTick));
  TEST_ASSERT_TRUE(has(c.update({Overlay::Countdown, true, 3, SosState::Idle}), Pattern::CountdownFinal));
}

void test_countdown_restart_beeps_again() {
  AudioCues c;
  c.update({Overlay::Countdown, true, 10, SosState::Idle});
  c.update({Overlay::None, false, 0, SosState::Idle});
  TEST_ASSERT_TRUE(has(c.update({Overlay::Countdown, true, 10, SosState::Idle}), Pattern::CountdownTick));
}

void test_sos_received_and_seen_once() {
  AudioCues c;
  TEST_ASSERT_EQUAL_UINT8(0, c.update({Overlay::Sending, false, 0, SosState::Sending}).n);
  TEST_ASSERT_TRUE(has(c.update({Overlay::Received, false, 0, SosState::Received}), Pattern::SosReceived));
  TEST_ASSERT_EQUAL_UINT8(0, c.update({Overlay::Received, false, 0, SosState::Received}).n);
  TEST_ASSERT_TRUE(has(c.update({Overlay::Seen, false, 0, SosState::Seen}), Pattern::SosSeen));
}

void test_alarm_and_tick_together() {
  AudioCues c;
  CueOut o = c.update({Overlay::PeerSos, true, 10, SosState::Idle});
  TEST_ASSERT_EQUAL_UINT8(2, o.n);
  TEST_ASSERT_TRUE(has(o, Pattern::PeerAlarm));
  TEST_ASSERT_TRUE(has(o, Pattern::CountdownTick));
}

void test_gestures() {
  TEST_ASSERT_TRUE(AudioCues::forGesture(Gesture::Click) == Pattern::Click);
  TEST_ASSERT_TRUE(AudioCues::forGesture(Gesture::Hold2s) == Pattern::Hold);
  TEST_ASSERT_TRUE(AudioCues::forGesture(Gesture::Hold3s) == Pattern::None);
  TEST_ASSERT_TRUE(AudioCues::forGesture(Gesture::None) == Pattern::None);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_when_idle);
  RUN_TEST(test_alarm_while_peer_overlay);
  RUN_TEST(test_alarm_stops_when_peer_cancels);
  RUN_TEST(test_countdown_ticks_each_number);
  RUN_TEST(test_countdown_restart_beeps_again);
  RUN_TEST(test_sos_received_and_seen_once);
  RUN_TEST(test_alarm_and_tick_together);
  RUN_TEST(test_gestures);
  return UNITY_END();
}
