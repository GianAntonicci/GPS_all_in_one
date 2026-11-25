#pragma once
// Decide quando tocar cada som a partir do estado da UI (chamado a cada iteração).
#include <cstdint>
#include "input_logic.h"
#include "lora_link.h"
#include "sos_overlay.h"
#include "tone_seq.h"

struct CueInput {
  Overlay top;
  bool counting;
  uint8_t countdownLeft;
  SosState mine;
};

struct CueOut {
  Pattern play[2];
  uint8_t n;
  bool stopAlarm;
};

class AudioCues {
 public:
  CueOut update(const CueInput& in);
  static Pattern forGesture(Gesture g);

 private:
  bool alarm_ = false;
  bool counting_ = false;
  uint8_t lastLeft_ = 0;
  SosState mine_ = SosState::Idle;
};
