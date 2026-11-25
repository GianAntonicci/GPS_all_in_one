#include <unity.h>
#include "tone_seq.h"

void setUp() {}
void tearDown() {}

void test_idle_is_silent() {
  ToneSequencer s;
  TEST_ASSERT_EQUAL_UINT16(0, s.current(0).freqHz);
  TEST_ASSERT_TRUE(s.playing() == Pattern::None);
}

void test_message_steps() {
  ToneSequencer s;
  s.play(Pattern::Message, 0);
  TEST_ASSERT_EQUAL_UINT16(1000, s.current(0).freqHz);
  TEST_ASSERT_FALSE(s.current(0).loud);
  TEST_ASSERT_EQUAL_UINT16(1000, s.current(99).freqHz);
  TEST_ASSERT_EQUAL_UINT16(0, s.current(100).freqHz);
  TEST_ASSERT_EQUAL_UINT16(0, s.current(159).freqHz);
  TEST_ASSERT_EQUAL_UINT16(1500, s.current(160).freqHz);
  TEST_ASSERT_EQUAL_UINT16(1500, s.current(259).freqHz);
  TEST_ASSERT_TRUE(s.playing() == Pattern::Message);
  TEST_ASSERT_EQUAL_UINT16(0, s.current(260).freqHz);
  TEST_ASSERT_TRUE(s.playing() == Pattern::None);
}

void test_alarm_repeats() {
  ToneSequencer s;
  s.play(Pattern::PeerAlarm, 0);
  TEST_ASSERT_EQUAL_UINT16(900, s.current(0).freqHz);
  TEST_ASSERT_TRUE(s.current(0).loud);
  TEST_ASSERT_EQUAL_UINT16(1400, s.current(250).freqHz);
  TEST_ASSERT_EQUAL_UINT16(900, s.current(500).freqHz);
  TEST_ASSERT_EQUAL_UINT16(900, s.current(10000010).freqHz);
  TEST_ASSERT_TRUE(s.playing() == Pattern::PeerAlarm);
}

void test_click_does_not_interrupt_alarm() {
  ToneSequencer s;
  s.play(Pattern::PeerAlarm, 0);
  s.play(Pattern::Click, 300);
  TEST_ASSERT_TRUE(s.playing() == Pattern::PeerAlarm);
  TEST_ASSERT_EQUAL_UINT16(1400, s.current(300).freqHz);
}

void test_play_same_pattern_keeps_phase() {
  ToneSequencer s;
  s.play(Pattern::PeerAlarm, 0);
  s.play(Pattern::PeerAlarm, 300);
  TEST_ASSERT_EQUAL_UINT16(1400, s.current(300).freqHz);
}

void test_sos_interrupts_message() {
  ToneSequencer s;
  s.play(Pattern::Message, 0);
  s.play(Pattern::SosReceived, 50);
  TEST_ASSERT_EQUAL_UINT16(800, s.current(50).freqHz);
  TEST_ASSERT_TRUE(s.playing() == Pattern::SosReceived);
}

void test_equal_priority_replaces() {
  ToneSequencer s;
  s.play(Pattern::Hold, 0);
  s.play(Pattern::Click, 10);
  TEST_ASSERT_TRUE(s.playing() == Pattern::Click);
  TEST_ASSERT_EQUAL_UINT16(2000, s.current(10).freqHz);
}

void test_stop_only_matching() {
  ToneSequencer s;
  s.play(Pattern::PeerAlarm, 0);
  s.stop(Pattern::Message);
  TEST_ASSERT_TRUE(s.playing() == Pattern::PeerAlarm);
  s.stop(Pattern::PeerAlarm);
  TEST_ASSERT_TRUE(s.playing() == Pattern::None);
  TEST_ASSERT_EQUAL_UINT16(0, s.current(10).freqHz);
}

void test_loud_patterns_ignore_volume() {
  TEST_ASSERT_EQUAL_INT16(26000, toneAmplitude(0, true));
  TEST_ASSERT_EQUAL_INT16(26000, toneAmplitude(100, true));
  TEST_ASSERT_EQUAL_INT16(0, toneAmplitude(0, false));
  TEST_ASSERT_EQUAL_INT16(26000, toneAmplitude(100, false));
  TEST_ASSERT_EQUAL_INT16(26000, toneAmplitude(150, false));
  TEST_ASSERT_EQUAL_INT16(6500, toneAmplitude(50, false));
  TEST_ASSERT_TRUE(patternLoud(Pattern::PeerAlarm));
  TEST_ASSERT_TRUE(patternLoud(Pattern::CountdownTick));
  TEST_ASSERT_TRUE(patternLoud(Pattern::SosSeen));
  TEST_ASSERT_FALSE(patternLoud(Pattern::Click));
  TEST_ASSERT_FALSE(patternLoud(Pattern::Message));
}

void test_priorities() {
  TEST_ASSERT_EQUAL_UINT8(1, patternPriority(Pattern::PeerAlarm));
  TEST_ASSERT_EQUAL_UINT8(2, patternPriority(Pattern::SosSeen));
  TEST_ASSERT_EQUAL_UINT8(2, patternPriority(Pattern::SosReceived));
  TEST_ASSERT_EQUAL_UINT8(3, patternPriority(Pattern::CountdownFinal));
  TEST_ASSERT_EQUAL_UINT8(3, patternPriority(Pattern::CountdownTick));
  TEST_ASSERT_EQUAL_UINT8(4, patternPriority(Pattern::Message));
  TEST_ASSERT_EQUAL_UINT8(5, patternPriority(Pattern::Hold));
  TEST_ASSERT_EQUAL_UINT8(5, patternPriority(Pattern::Click));
  TEST_ASSERT_EQUAL_UINT8(255, patternPriority(Pattern::None));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_idle_is_silent);
  RUN_TEST(test_message_steps);
  RUN_TEST(test_alarm_repeats);
  RUN_TEST(test_click_does_not_interrupt_alarm);
  RUN_TEST(test_play_same_pattern_keeps_phase);
  RUN_TEST(test_sos_interrupts_message);
  RUN_TEST(test_equal_priority_replaces);
  RUN_TEST(test_stop_only_matching);
  RUN_TEST(test_loud_patterns_ignore_volume);
  RUN_TEST(test_priorities);
  return UNITY_END();
}
