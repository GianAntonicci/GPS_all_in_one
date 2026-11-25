#include <unity.h>
#include "fall_detector.h"

void setUp() {}
void tearDown() {}

void test_starts_idle_and_sleeps_after_quiet() {
  FallDetector d;
  TEST_ASSERT_TRUE(d.state() == FallState::Idle);
  d.onMotionWake(0);
  TEST_ASSERT_TRUE(d.wantsSampling());
  for (uint32_t t = 0; t <= 2900; t += 100) {
    TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, t) == FallEvent::None);
    TEST_ASSERT_TRUE(d.wantsSampling());
  }
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, 3000) == FallEvent::None);
  TEST_ASSERT_FALSE(d.wantsSampling());
  TEST_ASSERT_TRUE(d.state() == FallState::Idle);
}

void test_below_impact_threshold_does_not_trigger() {
  FallDetector d;
  d.onMotionWake(0);
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 2.9f, 100) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::Idle);
  for (uint32_t t = 200; t <= 35000; t += 100) {
    TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, t) == FallEvent::None);
  }
  TEST_ASSERT_TRUE(d.state() == FallState::Idle);
}

void test_impact_plus_30s_still_triggers_sos() {
  FallDetector d;
  d.onMotionWake(0);
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 3.2f, 100) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::Settling);
  TEST_ASSERT_TRUE(d.wantsSampling());

  // Ainda em Settling antes de 1600 ms
  for (uint32_t t = 200; t < 1600; t += 100) {
    TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, t) == FallEvent::None);
    TEST_ASSERT_TRUE(d.state() == FallState::Settling);
  }

  // Em t = 1600 entra em StillWait
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, 1600) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::StillWait);

  // Permanece imóvel até 31500 (29.9 s)
  for (uint32_t t = 1700; t <= 31500; t += 100) {
    TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, t) == FallEvent::None);
    TEST_ASSERT_TRUE(d.state() == FallState::StillWait);
  }

  // Em t = 31600 completa 30 s imóvel e dispara SOS
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, 31600) == FallEvent::SosCountdown);
  TEST_ASSERT_TRUE(d.state() == FallState::Cooldown);
}

void test_rebound_during_settling_is_ignored() {
  FallDetector d;
  d.onMotionWake(0);
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 3.5f, 0) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::Settling);
  // Rebotes violentos durante os primeiros 1,5 s
  TEST_ASSERT_TRUE(d.update(2.5f, 0.0f, 0.0f, 500) == FallEvent::None);
  TEST_ASSERT_TRUE(d.update(-2.5f, 1.0f, 0.0f, 1000) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::Settling);

  // Entra em StillWait em t = 1500 e fica imóvel por 30 s
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, 1500) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::StillWait);
  FallEvent ev = FallEvent::None;
  for (uint32_t t = 1600; t <= 31500; t += 100) {
    ev = d.update(0.0f, 0.0f, 1.0f, t);
  }
  TEST_ASSERT_TRUE(ev == FallEvent::SosCountdown);
}

void test_brief_noise_does_not_cancel_still_wait() {
  FallDetector d;
  d.onMotionWake(0);
  d.update(0.0f, 0.0f, 3.2f, 0);
  d.update(0.0f, 0.0f, 1.0f, 1500);
  TEST_ASSERT_TRUE(d.state() == FallState::StillWait);

  // Uma amostra ruidosa em t = 5000 seguida de repouso em t = 5020 (< 300 ms)
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.8f, 5000) == FallEvent::None);
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, 5020) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::StillWait);

  // Continua imóvel até completar 30 s
  FallEvent ev = FallEvent::None;
  for (uint32_t t = 5100; t <= 31500; t += 100) {
    ev = d.update(0.0f, 0.0f, 1.0f, t);
  }
  TEST_ASSERT_TRUE(ev == FallEvent::SosCountdown);
}

void test_sustained_movement_cancels_fall() {
  FallDetector d;
  d.onMotionWake(0);
  d.update(0.0f, 0.0f, 3.2f, 0);
  d.update(0.0f, 0.0f, 1.0f, 1500);
  TEST_ASSERT_TRUE(d.state() == FallState::StillWait);

  // Movimento sustentado por >= 300 ms (5000..5320)
  for (uint32_t t = 5000; t <= 5320; t += 20) {
    TEST_ASSERT_TRUE(d.update(0.5f, 0.5f, 1.2f, t) == FallEvent::None);
  }
  TEST_ASSERT_TRUE(d.state() == FallState::Idle);

  // Mesmo ficando imóvel por mais 35 s depois, não dispara SOS
  for (uint32_t t = 5400; t <= 40400; t += 100) {
    TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, t) == FallEvent::None);
  }
  TEST_ASSERT_TRUE(d.state() == FallState::Idle);
}

void test_cooldown_blocks_immediate_retrigger() {
  FallDetector d;
  d.onMotionWake(0);
  d.update(0.0f, 0.0f, 3.2f, 0);
  d.update(0.0f, 0.0f, 1.0f, 1500);
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, 31500) == FallEvent::SosCountdown);
  TEST_ASSERT_TRUE(d.state() == FallState::Cooldown);
  TEST_ASSERT_FALSE(d.wantsSampling());

  // Novo impacto 10 s depois: ignorado pelo cooldown
  d.onMotionWake(41500);
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 4.0f, 41500) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::Cooldown);

  // Após 60 s de cooldown (31500 + 60000 = 91500), volta a aceitar impacto
  d.onMotionWake(91500);
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 3.5f, 91500) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::Settling);
  d.update(0.0f, 0.0f, 1.0f, 93000);
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 1.0f, 123000) == FallEvent::SosCountdown);
}

void test_millis_wraparound() {
  FallDetector d;
  uint32_t t = 0xFFFFF000u;
  d.onMotionWake(t);
  TEST_ASSERT_TRUE(d.update(0.0f, 0.0f, 3.5f, t) == FallEvent::None);
  TEST_ASSERT_TRUE(d.state() == FallState::Settling);

  FallEvent ev = FallEvent::None;
  for (uint32_t i = 1; i <= 320; ++i) {
    t += 100;  // cruza 0xFFFFFFFF -> 0
    ev = d.update(0.0f, 0.0f, 1.0f, t);
    if (ev == FallEvent::SosCountdown) break;
  }
  TEST_ASSERT_TRUE(ev == FallEvent::SosCountdown);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_idle_and_sleeps_after_quiet);
  RUN_TEST(test_below_impact_threshold_does_not_trigger);
  RUN_TEST(test_impact_plus_30s_still_triggers_sos);
  RUN_TEST(test_rebound_during_settling_is_ignored);
  RUN_TEST(test_brief_noise_does_not_cancel_still_wait);
  RUN_TEST(test_sustained_movement_cancels_fall);
  RUN_TEST(test_cooldown_blocks_immediate_retrigger);
  RUN_TEST(test_millis_wraparound);
  return UNITY_END();
}
