#include <unity.h>
#include <cmath>
#include "compass.h"

void setUp() {}
void tearDown() {}

static constexpr float kPi = 3.14159265358979323846f;

void test_flat_cardinal_directions() {
  float h = 0.0f;
  // Norte: campo magnético em +X
  TEST_ASSERT_TRUE(tiltCompensatedHeading(0, 0, 1, 30, 0, -20, 0, h));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, h);

  // Leste (90°): Norte fica à esquerda (+Y)
  TEST_ASSERT_TRUE(tiltCompensatedHeading(0, 0, 1, 0, 30, -20, 0, h));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 90.0f, h);

  // Sul (180°): Norte fica atrás (-X)
  TEST_ASSERT_TRUE(tiltCompensatedHeading(0, 0, 1, -30, 0, -20, 0, h));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 180.0f, h);

  // Oeste (270°): Norte fica à direita (-Y)
  TEST_ASSERT_TRUE(tiltCompensatedHeading(0, 0, 1, 0, -30, -20, 0, h));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 270.0f, h);
}

void test_declination_sao_paulo() {
  float h = 0.0f;
  // Norte magnético (0°) com declinação -21,5° -> 338,5° verdadeiro
  TEST_ASSERT_TRUE(tiltCompensatedHeading(0, 0, 1, 30, 0, 0, -21.5f, h));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 338.5f, h);

  // Leste magnético (90°) com declinação -21,5° -> 68,5° verdadeiro
  TEST_ASSERT_TRUE(tiltCompensatedHeading(0, 0, 1, 0, 30, 0, -21.5f, h));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 68.5f, h);
}

void test_tilt_compensation_pitch_and_roll() {
  // Campo mundial em coordenadas (Norte=+X, Oeste=+Y, Cima=+Z):
  // apontando para 37° (azimute): Bx = B_h * cos(37°), By = B_h * sin(37°), Bz = +25 uT (hemisfério sul)
  const float azRad = 37.0f * kPi / 180.0f;
  const float Bx = 30.0f * std::cos(azRad);
  const float By = 30.0f * std::sin(azRad);
  const float Bz = 25.0f;

  // Inclina: pitch = +25° (nariz para cima), roll = -20° (esquerda para baixo)
  const float th = 25.0f * kPi / 180.0f;
  const float ph = -20.0f * kPi / 180.0f;

  // Acelerômetro (reação à gravidade = Up no referencial do corpo)
  const float ax = std::sin(th);
  const float ay = std::cos(th) * std::sin(ph);
  const float az = std::cos(th) * std::cos(ph);

  // Campo magnético rotacionado para o referencial do corpo (pitch em Y, depois roll em X)
  const float mx1 = Bx * std::cos(th) + Bz * std::sin(th);
  const float my1 = By;
  const float mz1 = -Bx * std::sin(th) + Bz * std::cos(th);

  const float mx = mx1;
  const float my = my1 * std::cos(ph) + mz1 * std::sin(ph);
  const float mz = -my1 * std::sin(ph) + mz1 * std::cos(ph);

  float h = 0.0f;
  TEST_ASSERT_TRUE(tiltCompensatedHeading(ax, ay, az, mx, my, mz, 0.0f, h));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 37.0f, h);
}

void test_rejects_zero_vectors() {
  float h = 0.0f;
  TEST_ASSERT_FALSE(tiltCompensatedHeading(0, 0, 0, 30, 0, 0, 0, h));
  TEST_ASSERT_FALSE(tiltCompensatedHeading(0, 0, 1, 0, 0, 0, 0, h));
}

void test_calibrator_hard_and_soft_iron() {
  MagCalibrator cal;
  // Elipsoide deslocado em (+15, -20, +10) com semi-eixos (40, 20, 30)
  for (int i = 0; i < 60; ++i) {
    float a = (float)i * (2.0f * kPi / 60.0f);
    cal.addSample(15.0f + 40.0f * std::cos(a), -20.0f + 20.0f * std::sin(a),
                  10.0f + 30.0f * std::sin(a * 2.0f));
  }
  MagCal out{};
  TEST_ASSERT_TRUE(cal.finish(out));
  TEST_ASSERT_TRUE(out.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 15.0f, out.offsetX);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -20.0f, out.offsetY);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 10.0f, out.offsetZ);

  // Aplicando calibração nos extremos: todos os semi-eixos ficam equalizados (~30 uT)
  float cx = 0, cy = 0, cz = 0;
  applyMagCal(out, 15.0f + 40.0f, -20.0f, 10.0f, cx, cy, cz);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 30.0f, cx);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, cy);

  applyMagCal(out, 15.0f, -20.0f + 20.0f, 10.0f, cx, cy, cz);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, cx);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 30.0f, cy);
}

void test_calibrator_rejects_too_few_samples_or_static() {
  MagCalibrator cal;
  for (int i = 0; i < 20; ++i) cal.addSample(10.0f, 20.0f, 30.0f);
  MagCal out{};
  TEST_ASSERT_FALSE(cal.finish(out));

  for (int i = 0; i < 50; ++i) cal.addSample(10.0f, 20.0f, 30.0f);
  TEST_ASSERT_FALSE(cal.finish(out));  // sem variação (aparelho parado)
}

void test_select_heading_moving_uses_gps() {
  HeadingInput in{};
  in.gpsFix = true;
  in.speedKmh = 2.0;
  in.gpsCourseDeg = 123.0;
  in.gpsCourseKnown = true;
  in.gpsCourseStale = false;
  in.compassOk = true;
  in.compassCalibrated = true;
  in.compassDeg = 45.0f;

  HeadingResult r = selectHeading(in);
  TEST_ASSERT_TRUE(r.known);
  TEST_ASSERT_FALSE(r.stale);
  TEST_ASSERT_EQUAL(HeadingSource::Gps, r.source);
  TEST_ASSERT_DOUBLE_WITHIN(0.1, 123.0, r.deg);
}

void test_select_heading_stopped_uses_calibrated_compass_without_stale() {
  HeadingInput in{};
  in.gpsFix = true;
  in.speedKmh = 1.0;  // < 1.5 km/h
  in.gpsCourseDeg = 123.0;
  in.gpsCourseKnown = true;
  in.gpsCourseStale = true;
  in.compassOk = true;
  in.compassCalibrated = true;
  in.compassDeg = 210.0f;

  HeadingResult r = selectHeading(in);
  TEST_ASSERT_TRUE(r.known);
  TEST_ASSERT_FALSE(r.stale);
  TEST_ASSERT_EQUAL(HeadingSource::Compass, r.source);
  TEST_ASSERT_DOUBLE_WITHIN(0.1, 210.0, r.deg);
}

void test_select_heading_no_fix_uses_compass() {
  HeadingInput in{};
  in.gpsFix = false;
  in.speedKmh = 0.0;
  in.compassOk = true;
  in.compassCalibrated = true;
  in.compassDeg = 88.0f;

  HeadingResult r = selectHeading(in);
  TEST_ASSERT_TRUE(r.known);
  TEST_ASSERT_FALSE(r.stale);
  TEST_ASSERT_EQUAL(HeadingSource::Compass, r.source);
  TEST_ASSERT_DOUBLE_WITHIN(0.1, 88.0, r.deg);
}

void test_select_heading_uncalibrated_compass_falls_back_to_stale_gps() {
  HeadingInput in{};
  in.gpsFix = true;
  in.speedKmh = 0.5;
  in.gpsCourseDeg = 150.0;
  in.gpsCourseKnown = true;
  in.gpsCourseStale = true;
  in.compassOk = true;
  in.compassCalibrated = false;
  in.compassDeg = 88.0f;

  HeadingResult r = selectHeading(in);
  TEST_ASSERT_TRUE(r.known);
  TEST_ASSERT_TRUE(r.stale);
  TEST_ASSERT_EQUAL(HeadingSource::GpsStale, r.source);
  TEST_ASSERT_DOUBLE_WITHIN(0.1, 150.0, r.deg);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_flat_cardinal_directions);
  RUN_TEST(test_declination_sao_paulo);
  RUN_TEST(test_tilt_compensation_pitch_and_roll);
  RUN_TEST(test_rejects_zero_vectors);
  RUN_TEST(test_calibrator_hard_and_soft_iron);
  RUN_TEST(test_calibrator_rejects_too_few_samples_or_static);
  RUN_TEST(test_select_heading_moving_uses_gps);
  RUN_TEST(test_select_heading_stopped_uses_calibrated_compass_without_stale);
  RUN_TEST(test_select_heading_no_fix_uses_compass);
  RUN_TEST(test_select_heading_uncalibrated_compass_falls_back_to_stale_gps);
  return UNITY_END();
}
