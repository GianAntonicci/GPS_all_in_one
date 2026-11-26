#include <unity.h>
#include "scanner_model.h"

void setUp() {}
void tearDown() {}

void test_freq_word_known_frequencies() {
  TEST_ASSERT_INT32_WITHIN(1, 0x10B071, (int32_t)cc1101FreqWord(433.92f));
  TEST_ASSERT_INT32_WITHIN(1, 0x216276, (int32_t)cc1101FreqWord(868.0f));
  TEST_ASSERT_INT32_WITHIN(1, 0x236DC9, (int32_t)cc1101FreqWord(921.15f));
  TEST_ASSERT_EQUAL_UINT32(0, cc1101FreqWord(0.0f));
  TEST_ASSERT_EQUAL_UINT32(0, cc1101FreqWord(-10.0f));
}

void test_rssi_conversion() {
  TEST_ASSERT_EQUAL_INT16(-74, cc1101RssiDbm(0));
  TEST_ASSERT_EQUAL_INT16(-64, cc1101RssiDbm(20));
  TEST_ASSERT_EQUAL_INT16(-138, cc1101RssiDbm(128));
  TEST_ASSERT_EQUAL_INT16(-102, cc1101RssiDbm(200));
  TEST_ASSERT_EQUAL_INT16(-75, cc1101RssiDbm(254));
  TEST_ASSERT_EQUAL_INT16(-74, cc1101RssiDbm(255));
}

void test_rssi_to_bar_height() {
  TEST_ASSERT_EQUAL_UINT16(0, rssiToBarHeight(-130, 150));
  TEST_ASSERT_EQUAL_UINT16(0, rssiToBarHeight(-120, 150));
  TEST_ASSERT_EQUAL_UINT16(75, rssiToBarHeight(-75, 150));
  TEST_ASSERT_EQUAL_UINT16(150, rssiToBarHeight(-30, 150));
  TEST_ASSERT_EQUAL_UINT16(150, rssiToBarHeight(0, 150));
}

void test_band_frequencies() {
  ScannerModel m;
  m.setBand(0);  // 433 MHz
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 430.0f, m.binFreqMhz(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 435.9f, m.binFreqMhz(59));

  m.setBand(1);  // 868 MHz
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 865.0f, m.binFreqMhz(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 870.9f, m.binFreqMhz(59));

  m.setBand(2);  // 915 MHz
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 902.0f, m.binFreqMhz(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 931.5f, m.binFreqMhz(59));
}

void test_band_switching_wraps_and_resets() {
  ScannerModel m;
  TEST_ASSERT_EQUAL_UINT8(0, m.bandIndex());
  m.recordSample(-60);
  TEST_ASSERT_TRUE(m.hasData());

  m.stepBand(-1);
  TEST_ASSERT_EQUAL_UINT8(2, m.bandIndex());
  TEST_ASSERT_FALSE(m.hasData());
  TEST_ASSERT_EQUAL_UINT8(0, m.cursor());
  TEST_ASSERT_EQUAL_INT16(ScannerModel::kFloorDbm, m.rssi(0));
  TEST_ASSERT_EQUAL_INT16(ScannerModel::kFloorDbm, m.peak(0));

  m.stepBand(1);
  TEST_ASSERT_EQUAL_UINT8(0, m.bandIndex());
}

void test_peak_hold_and_decay() {
  ScannerModel m;
  // Volta 1: pico forte (-50 dBm) no bin 10, resto -100 dBm
  for (uint8_t i = 0; i < ScannerModel::kBins; ++i) {
    m.recordSample(i == 10 ? -50 : -100);
  }
  TEST_ASSERT_TRUE(m.hasData());
  TEST_ASSERT_EQUAL_UINT8(10, m.peakBin());
  TEST_ASSERT_EQUAL_INT16(-50, m.peakDbm());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 431.0f, m.peakFreqMhz());

  // Volta 2: tudo em -100 dBm -> ao fechar a volta, pico do bin 10 decai 2 dB (para -52)
  // e os demais bins não caem abaixo do RSSI atual (-100 dBm).
  for (uint8_t i = 0; i < ScannerModel::kBins; ++i) {
    m.recordSample(-100);
  }
  TEST_ASSERT_EQUAL_INT16(-52, m.peak(10));
  TEST_ASSERT_EQUAL_INT16(-100, m.peak(0));
  TEST_ASSERT_EQUAL_INT16(-100, m.rssi(10));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_freq_word_known_frequencies);
  RUN_TEST(test_rssi_conversion);
  RUN_TEST(test_rssi_to_bar_height);
  RUN_TEST(test_band_frequencies);
  RUN_TEST(test_band_switching_wraps_and_resets);
  RUN_TEST(test_peak_hold_and_decay);
  return UNITY_END();
}
