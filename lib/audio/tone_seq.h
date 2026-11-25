#pragma once
// Padrões de tom, precedência e amplitude. Os sons de SOS tocam alto mesmo com volume 0.
#include <cstdint>

enum class Pattern : uint8_t {
  None,
  PeerAlarm,       // SOS do amigo (repete até parar)
  SosSeen,         // meu SOS foi visto
  SosReceived,     // meu SOS chegou
  CountdownFinal,  // contagem: últimos 3 s
  CountdownTick,   // contagem: um por segundo
  Message,
  Hold,
  Click
};

struct Tone {
  uint16_t freqHz;  // 0 = silêncio
  bool loud;
};

uint8_t patternPriority(Pattern p);  // 1 = maior; None = 255
bool patternLoud(Pattern p);         // ignora o volume

class ToneSequencer {
 public:
  void play(Pattern p, uint32_t now);  // menor prioridade que o atual: ignorado
  void stop(Pattern p);                // só se p for o que está tocando
  Tone current(uint32_t now);          // padrão de uma vez que acabou vira None
  Pattern playing() const { return p_; }

 private:
  Pattern p_ = Pattern::None;
  uint32_t start_ = 0;
};

int16_t toneAmplitude(uint8_t volumePct, bool loud);  // 0..26000 (curva quadrática)
