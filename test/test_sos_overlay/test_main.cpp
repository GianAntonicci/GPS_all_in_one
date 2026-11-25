#include <unity.h>
#include "sos_overlay.h"

void setUp() {}
void tearDown() {}

void test_countdown_starts_and_fires_at_10s() {
  SosOverlay o;
  o.requestCountdown(1000);
  TEST_ASSERT_EQUAL(Overlay::Countdown, o.top());
  TEST_ASSERT_TRUE(o.active());
  TEST_ASSERT_EQUAL(SosAction::None, o.tick(10999));
  TEST_ASSERT_EQUAL(SosAction::StartSos, o.tick(11000));
  TEST_ASSERT_EQUAL(Overlay::Sending, o.top());  // até o rádio confirmar
  TEST_ASSERT_EQUAL(SosAction::None, o.tick(12000));
  o.sync(SosState::Sending, false, 12000);
  TEST_ASSERT_EQUAL(Overlay::Sending, o.top());
}

void test_countdown_click_cancels() {
  SosOverlay o;
  o.requestCountdown(0);
  TEST_ASSERT_EQUAL(SosAction::None, o.onGesture(Gesture::Click, 3000));
  TEST_ASSERT_EQUAL(Overlay::None, o.top());
  TEST_ASSERT_EQUAL(SosAction::None, o.tick(20000));
  TEST_ASSERT_FALSE(o.active());
}

void test_countdown_left_values() {
  SosOverlay o;
  o.requestCountdown(0);
  TEST_ASSERT_EQUAL_UINT8(10, o.countdownLeft(0));
  TEST_ASSERT_EQUAL_UINT8(10, o.countdownLeft(999));
  TEST_ASSERT_EQUAL_UINT8(9, o.countdownLeft(1000));
  TEST_ASSERT_EQUAL_UINT8(1, o.countdownLeft(9999));
}

void test_countdown_hold2s_does_not_restart() {
  SosOverlay o;
  o.requestCountdown(0);
  o.onGesture(Gesture::Hold2s, 5000);
  o.requestCountdown(5000);
  TEST_ASSERT_EQUAL(SosAction::StartSos, o.tick(10000));
}

void test_sending_hold3s_cancels_click_ignored() {
  SosOverlay o;
  o.sync(SosState::Sending, false, 0);
  TEST_ASSERT_EQUAL(Overlay::Sending, o.top());
  TEST_ASSERT_EQUAL(SosAction::None, o.onGesture(Gesture::Click, 100));
  TEST_ASSERT_EQUAL(Overlay::Sending, o.top());
  TEST_ASSERT_EQUAL(SosAction::CancelSos, o.onGesture(Gesture::Hold3s, 200));
}

void test_received_hold3s_cancels() {
  SosOverlay o;
  o.sync(SosState::Received, false, 0);
  TEST_ASSERT_EQUAL(Overlay::Received, o.top());
  TEST_ASSERT_EQUAL(SosAction::CancelSos, o.onGesture(Gesture::Hold3s, 100));
  o.sync(SosState::Idle, false, 200);
  TEST_ASSERT_EQUAL(Overlay::None, o.top());
}

void test_seen_shows_once_and_closes() {
  SosOverlay o;
  o.sync(SosState::Received, false, 0);
  o.sync(SosState::Seen, false, 100);
  TEST_ASSERT_EQUAL(Overlay::Seen, o.top());
  TEST_ASSERT_EQUAL(SosAction::CloseOverlay, o.onGesture(Gesture::Click, 200));
  TEST_ASSERT_EQUAL(Overlay::None, o.top());
  o.sync(SosState::Seen, false, 300);
  TEST_ASSERT_EQUAL(Overlay::None, o.top());
}

void test_peer_sos_priority_and_strip() {
  SosOverlay o;
  o.sync(SosState::Sending, true, 0);
  TEST_ASSERT_EQUAL(Overlay::PeerSos, o.top());
  TEST_ASSERT_TRUE(o.showMineStrip());
  o.sync(SosState::Idle, true, 10);
  TEST_ASSERT_FALSE(o.showMineStrip());
}

void test_peer_click_sends_seen_then_opens_friend() {
  SosOverlay o;
  o.sync(SosState::Idle, true, 0);
  TEST_ASSERT_EQUAL(SosAction::SendSeen, o.onGesture(Gesture::Click, 100));
  TEST_ASSERT_EQUAL(SosAction::OpenFriend, o.tick(101));
  TEST_ASSERT_EQUAL(SosAction::None, o.tick(102));
  TEST_ASSERT_EQUAL(Overlay::None, o.top());
  o.sync(SosState::Idle, true, 103);  // rádio ainda não processou o "Vi"
  TEST_ASSERT_EQUAL(Overlay::None, o.top());
  o.sync(SosState::Idle, false, 200);
  o.sync(SosState::Idle, true, 90000);  // nova emergência do amigo
  TEST_ASSERT_EQUAL(Overlay::PeerSos, o.top());
}

void test_peer_screen_hold3s_cancels_mine() {
  SosOverlay o;
  o.sync(SosState::Received, true, 0);
  TEST_ASSERT_EQUAL(SosAction::CancelSos, o.onGesture(Gesture::Hold3s, 100));
  TEST_ASSERT_EQUAL(Overlay::PeerSos, o.top());
}

void test_peer_during_my_countdown_hold3s_cancels_countdown() {
  SosOverlay o;
  o.requestCountdown(0);
  o.sync(SosState::Idle, true, 1000);
  TEST_ASSERT_EQUAL(Overlay::PeerSos, o.top());
  TEST_ASSERT_TRUE(o.showMineStrip());
  TEST_ASSERT_EQUAL(SosAction::None, o.onGesture(Gesture::Hold3s, 2000));
  TEST_ASSERT_EQUAL(SosAction::None, o.tick(20000));
}

void test_no_overlay_gesture_returns_none() {
  SosOverlay o;
  TEST_ASSERT_EQUAL(SosAction::None, o.onGesture(Gesture::Click, 0));
  TEST_ASSERT_FALSE(o.active());
}

void test_request_countdown_ignored_while_my_sos_active() {
  SosOverlay o;
  o.sync(SosState::Sending, false, 0);
  o.requestCountdown(100);
  TEST_ASSERT_EQUAL(Overlay::Sending, o.top());
}

void test_wake_click_consumed_when_off() {
  WakePolicy w;
  w.tick(30000, false);
  TEST_ASSERT_FALSE(w.screenOn());
  TEST_ASSERT_FALSE(w.onInput(Gesture::Click, false, 30100));
  TEST_ASSERT_TRUE(w.screenOn());
  TEST_ASSERT_TRUE(w.onInput(Gesture::Click, false, 30200));
}

void test_wake_rotation_consumed_when_off() {
  WakePolicy w;
  w.tick(30000, false);
  TEST_ASSERT_FALSE(w.onInput(Gesture::None, true, 30100));
  TEST_ASSERT_TRUE(w.screenOn());
}

void test_wake_hold2s_passes_when_off() {
  WakePolicy w;
  w.tick(30000, false);
  TEST_ASSERT_TRUE(w.onInput(Gesture::Hold2s, false, 32000));
  TEST_ASSERT_TRUE(w.screenOn());
}

void test_wake_timeout_30s() {
  WakePolicy w;
  w.onInput(Gesture::Click, false, 1000);
  w.tick(30999, false);
  TEST_ASSERT_TRUE(w.screenOn());
  w.tick(31000, false);
  TEST_ASSERT_FALSE(w.screenOn());
}

void test_wake_keep_on_during_overlay() {
  WakePolicy w;
  w.tick(100000, true);
  TEST_ASSERT_TRUE(w.screenOn());
  w.tick(129999, false);
  TEST_ASSERT_TRUE(w.screenOn());  // conta a partir do fim do overlay
  w.tick(130000, false);
  TEST_ASSERT_FALSE(w.screenOn());
}

void test_wake_event_turns_on() {
  WakePolicy w;
  w.tick(30000, false);
  w.wake(40000);
  TEST_ASSERT_TRUE(w.screenOn());
  w.tick(69999, false);
  TEST_ASSERT_TRUE(w.screenOn());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_countdown_starts_and_fires_at_10s);
  RUN_TEST(test_countdown_click_cancels);
  RUN_TEST(test_countdown_left_values);
  RUN_TEST(test_countdown_hold2s_does_not_restart);
  RUN_TEST(test_sending_hold3s_cancels_click_ignored);
  RUN_TEST(test_received_hold3s_cancels);
  RUN_TEST(test_seen_shows_once_and_closes);
  RUN_TEST(test_peer_sos_priority_and_strip);
  RUN_TEST(test_peer_click_sends_seen_then_opens_friend);
  RUN_TEST(test_peer_screen_hold3s_cancels_mine);
  RUN_TEST(test_peer_during_my_countdown_hold3s_cancels_countdown);
  RUN_TEST(test_no_overlay_gesture_returns_none);
  RUN_TEST(test_request_countdown_ignored_while_my_sos_active);
  RUN_TEST(test_wake_click_consumed_when_off);
  RUN_TEST(test_wake_rotation_consumed_when_off);
  RUN_TEST(test_wake_hold2s_passes_when_off);
  RUN_TEST(test_wake_timeout_30s);
  RUN_TEST(test_wake_keep_on_during_overlay);
  RUN_TEST(test_wake_event_turns_on);
  return UNITY_END();
}
