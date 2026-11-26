#pragma once
// Detector de queda puro (sem Arduino): pico de impacto + assentamento + imobilidade.
#include <cstdint>

struct FallConfig {
  float impactG = 3.0f;            // pico de impacto em g
  float stillBandG = 0.25f;        // ||a| - 1.0| <= stillBandG
  float stillDeltaG = 0.15f;       // |a - a_prev| <= stillDeltaG
  uint32_t settleMs = 1500;        // ignora rebotes logo após o impacto
  uint32_t stillMs = 30000;        // imobilidade contínua para disparar SOS
  uint32_t moveCancelMs = 300;     // movimento sustentado que cancela StillWait
  uint32_t cooldownMs = 60000;     // pausa após disparo
  uint32_t quietToSleepMs = 3000;  // tempo quieto em Idle até voltar a dormir
};

enum class FallState : uint8_t { Idle, Settling, StillWait, Cooldown };
enum class FallEvent : uint8_t { None, SosCountdown };

class FallDetector {
 public:
  explicit FallDetector(const FallConfig& cfg = {});
  void onMotionWake(uint32_t now);
  void reset();  // aborta suspeita em andamento (ex.: falha de leitura I2C)
  FallEvent update(float axG, float ayG, float azG, uint32_t now);
  FallState state() const { return state_; }
  bool wantsSampling() const { return sampling_; }

 private:
  FallConfig cfg_;
  FallState state_ = FallState::Idle;
  bool sampling_ = false;
  bool hasPrev_ = false;
  float prevX_ = 0.0f;
  float prevY_ = 0.0f;
  float prevZ_ = 0.0f;
  uint32_t stateSince_ = 0;
  uint32_t quietSince_ = 0;
  bool moving_ = false;
  uint32_t moveSince_ = 0;
};
