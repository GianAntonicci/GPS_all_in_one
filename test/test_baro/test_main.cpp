#include <unity.h>
#include "baro_logic.h"

void setUp() {}
void tearDown() {}

void test_standard_sea_level_altitude() {
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, pressureToAltitudeM(1013.25f, 1013.25f));
  // São Paulo (~760 m) tem pressão padrão ~925.4 hPa
  TEST_ASSERT_FLOAT_WITHIN(5.0f, 760.0f, pressureToAltitudeM(925.4f, 1013.25f));
}

void test_sea_level_roundtrip() {
  const float p0 = seaLevelFromAltitude(928.0f, 760.0f);
  const float alt = pressureToAltitudeM(928.0f, p0);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 760.0f, alt);
}

void test_altimeter_gps_calibration_and_smoothing() {
  BaroAltimeter baro;
  TEST_ASSERT_FALSE(baro.hasAltitude());
  TEST_ASSERT_FALSE(baro.calibrated());

  // Sem fix bom (poucos satélites ou HDOP alto): não calibra
  TEST_ASSERT_FALSE(baro.calibrateWithGps(928.0f, 760.0f, true, 5, 1.2f));
  TEST_ASSERT_FALSE(baro.calibrateWithGps(928.0f, 760.0f, true, 8, 2.5f));
  TEST_ASSERT_FALSE(baro.calibrateWithGps(928.0f, 760.0f, false, 8, 1.0f));
  TEST_ASSERT_FALSE(baro.calibrated());

  // Com fix bom (>=6 sats, HDOP <= 2.0): calibra imediatamente
  TEST_ASSERT_TRUE(baro.calibrateWithGps(928.0f, 760.0f, true, 7, 1.3f));
  TEST_ASSERT_TRUE(baro.calibrated());
  TEST_ASSERT_TRUE(baro.hasAltitude());
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 760.0f, baro.altitudeM());

  // Queda de pressão de ~1.1 hPa (~10 m acima) suavizada pelo EMA
  TEST_ASSERT_TRUE(baro.updatePressure(926.9f));
  TEST_ASSERT_TRUE(baro.altitudeM() > 761.0f && baro.altitudeM() < 768.0f);
}

void test_pressure_history_trend_and_rapid_drop() {
  PressureHistory h;
  float d = 0.0f;
  TEST_ASSERT_FALSE(h.delta3h(d));
  TEST_ASSERT_EQUAL(PressureTrend::Unknown, h.trend());
  TEST_ASSERT_FALSE(h.rapidDrop());

  // 10 minutos: ainda abaixo do mínimo (15 min)
  h.addSample(1012.0f, 0);
  h.addSample(1011.5f, 600000u);
  TEST_ASSERT_FALSE(h.delta3h(d));

  // Pressão estável por 30 min
  h.addSample(1011.9f, 1800000u);
  TEST_ASSERT_TRUE(h.delta3h(d));
  TEST_ASSERT_FLOAT_WITHIN(0.05f, -0.1f, d);
  TEST_ASSERT_EQUAL(PressureTrend::Steady, h.trend());
  TEST_ASSERT_FALSE(h.rapidDrop());

  // Queda rápida de 2.5 hPa em 2 h
  for (uint32_t m = 35; m <= 120; m += 5) {
    float p = 1012.0f - (2.5f * (float)m / 120.0f);
    h.addSample(p, m * 60000u);
  }
  TEST_ASSERT_TRUE(h.delta3h(d));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -2.5f, d);
  TEST_ASSERT_EQUAL(PressureTrend::Falling, h.trend());
  TEST_ASSERT_TRUE(h.rapidDrop());
}

void test_pressure_history_rising_after_3h_window_rollover() {
  PressureHistory h;
  // 3 h de pressão constante em 1005 hPa
  for (uint32_t m = 0; m <= 180; m += 5) {
    h.addSample(1005.0f, m * 60000u);
  }
  // Mais 3 h subindo para 1007.5 hPa -> janela de 3 h vê +2.5 hPa
  for (uint32_t m = 185; m <= 360; m += 5) {
    float p = 1005.0f + (2.5f * (float)(m - 180) / 180.0f);
    h.addSample(p, m * 60000u);
  }
  float d = 0.0f;
  TEST_ASSERT_TRUE(h.delta3h(d));
  TEST_ASSERT_TRUE(d >= 2.0f);
  TEST_ASSERT_EQUAL(PressureTrend::Rising, h.trend());
  TEST_ASSERT_FALSE(h.rapidDrop());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_standard_sea_level_altitude);
  RUN_TEST(test_sea_level_roundtrip);
  RUN_TEST(test_altimeter_gps_calibration_and_smoothing);
  RUN_TEST(test_pressure_history_trend_and_rapid_drop);
  RUN_TEST(test_pressure_history_rising_after_3h_window_rollover);
  return UNITY_END();
}
