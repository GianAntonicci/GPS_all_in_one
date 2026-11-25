#include <unity.h>
#include <cstring>
#include "ui_format.h"

void setUp() {}
void tearDown() {}

static const char* dist(double m) {
  static char b[16];
  fmtDistance(m, b, sizeof b);
  return b;
}
static const char* ageS(bool k, uint32_t ms) {
  static char b[16];
  fmtAgeShort(k, ms, b, sizeof b);
  return b;
}
static const char* ageL(bool k, uint32_t ms) {
  static char b[24];
  fmtAgeLong(k, ms, b, sizeof b);
  return b;
}

void test_distance_formats() {
  TEST_ASSERT_EQUAL_STRING("850 m", dist(850));
  TEST_ASSERT_EQUAL_STRING("999 m", dist(999));
  TEST_ASSERT_EQUAL_STRING("0 m", dist(0.2));
  TEST_ASSERT_EQUAL_STRING("1,0 km", dist(1000));
  TEST_ASSERT_EQUAL_STRING("1,8 km", dist(1849));
  TEST_ASSERT_EQUAL_STRING("10 km", dist(9999));
  TEST_ASSERT_EQUAL_STRING("23 km", dist(23400));
}

void test_age_short() {
  TEST_ASSERT_EQUAL_STRING("--", ageS(false, 0));
  TEST_ASSERT_EQUAL_STRING("25s", ageS(true, 25000));
  TEST_ASSERT_EQUAL_STRING("99s", ageS(true, 99000));
  TEST_ASSERT_EQUAL_STRING("1m", ageS(true, 100000));
  TEST_ASSERT_EQUAL_STRING("12m", ageS(true, 720000));
  TEST_ASSERT_EQUAL_STRING("3h", ageS(true, 10800000));
}

void test_age_long() {
  TEST_ASSERT_EQUAL_STRING("nunca", ageL(false, 0));
  TEST_ASSERT_EQUAL_STRING("ha 25 s", ageL(true, 25000));
  TEST_ASSERT_EQUAL_STRING("ha 12 min", ageL(true, 720000));
  TEST_ASSERT_EQUAL_STRING("ha 3 h", ageL(true, 10800000));
}

void test_signal_bars_thresholds() {
  TEST_ASSERT_EQUAL_UINT8(4, signalBars(true, -90));
  TEST_ASSERT_EQUAL_UINT8(3, signalBars(true, -91));
  TEST_ASSERT_EQUAL_UINT8(3, signalBars(true, -100));
  TEST_ASSERT_EQUAL_UINT8(2, signalBars(true, -101));
  TEST_ASSERT_EQUAL_UINT8(1, signalBars(true, -120));
  TEST_ASSERT_EQUAL_UINT8(0, signalBars(true, -121));
  TEST_ASSERT_EQUAL_UINT8(0, signalBars(false, -50));
}

void test_brightness_conversion() {
  TEST_ASSERT_EQUAL_UINT8(100, brightnessPctFromRaw(255));
  TEST_ASSERT_EQUAL_UINT8(70, brightnessPctFromRaw(180));
  TEST_ASSERT_EQUAL_UINT8(10, brightnessPctFromRaw(0));
  TEST_ASSERT_EQUAL_UINT8(255, brightnessRawFromPct(100));
  TEST_ASSERT_EQUAL_UINT8(25, brightnessRawFromPct(10));
}

static OledInput base() {
  OledInput in{};
  in.timeValid = true;
  in.hour = 12;
  in.minute = 4;
  in.sats = 8;
  in.batPct = 82;
  strcpy(in.myId, "A7");
  in.peerEver = true;
  in.peerAgeMs = 25000;
  in.hasRssi = true;
  in.rssi = -95;
  in.loraOk = true;
  in.mySos = SosState::Idle;
  in.blinkPhase = true;
  return in;
}

static void lines(const OledInput& in, char* l1, char* l2) { oledLines(in, l1, l2, 24); }

void test_oled_normal() {
  char l1[24], l2[24];
  OledInput in = base();
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("12:04  S08   82%", l1);
  TEST_ASSERT_EQUAL_STRING("A7 ###- 25s", l2);
  in.newMsg = true;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("A7 ###- 25s *", l2);
  in.blinkPhase = false;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("A7 ###- 25s", l2);
}

void test_oled_no_time_no_peer() {
  char l1[24], l2[24];
  OledInput in = base();
  in.timeValid = false;
  in.peerEver = false;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("--:--  S08   82%", l1);
  TEST_ASSERT_EQUAL_STRING("A7 ---- --", l2);
}

void test_oled_priority_lora_over_sos() {
  char l1[24], l2[24];
  OledInput in = base();
  in.loraOk = false;
  in.mySos = SosState::Sending;
  in.idConflict = true;
  in.peerSos = true;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("LORA ERRO", l2);
}

void test_oled_priority_id_conflict() {
  char l1[24], l2[24];
  OledInput in = base();
  in.idConflict = true;
  in.peerSos = true;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("ID REPETIDO", l2);
}

void test_oled_peer_sos_blinks() {
  char l1[24], l2[24];
  OledInput in = base();
  in.peerSos = true;
  in.mySos = SosState::Sending;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("AMIGO SOS!", l2);
  in.blinkPhase = false;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("", l2);
}

void test_oled_my_sos_states() {
  char l1[24], l2[24];
  OledInput in = base();
  in.mySos = SosState::Sending;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("SOS ENVIANDO", l2);
  in.mySos = SosState::Received;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("SOS RECEBIDO", l2);
}

void test_oled_seen_recent() {
  char l1[24], l2[24];
  OledInput in = base();
  in.mySos = SosState::Seen;
  in.seenRecent = true;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("SOS VISTO", l2);
  in.seenRecent = false;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("A7 ###- 25s", l2);
}

void test_oled_battery_low_blinks() {
  char l1[24], l2[24];
  OledInput in = base();
  in.batPct = 9;
  in.batLow = true;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("12:04  S08    9%", l1);
  in.blinkPhase = false;
  lines(in, l1, l2);
  TEST_ASSERT_EQUAL_STRING("12:04  S08      ", l1);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_distance_formats);
  RUN_TEST(test_age_short);
  RUN_TEST(test_age_long);
  RUN_TEST(test_signal_bars_thresholds);
  RUN_TEST(test_brightness_conversion);
  RUN_TEST(test_oled_normal);
  RUN_TEST(test_oled_no_time_no_peer);
  RUN_TEST(test_oled_priority_lora_over_sos);
  RUN_TEST(test_oled_priority_id_conflict);
  RUN_TEST(test_oled_peer_sos_blinks);
  RUN_TEST(test_oled_my_sos_states);
  RUN_TEST(test_oled_seen_recent);
  RUN_TEST(test_oled_battery_low_blinks);
  return UNITY_END();
}
