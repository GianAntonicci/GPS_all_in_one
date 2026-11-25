#pragma once
// Entrada pura: decodificação de quadratura do encoder e gestos do botão.
#include <cstdint>

class QuadDecoder {
 public:
  explicit QuadDecoder(uint8_t stepsPerDetent = 4) : steps_(stepsPerDetent) {}
  // Estado inicial = posição de repouso (detent). Chamar com o knob parado.
  void reset(bool a, bool b) {
    prev_ = rest_ = (uint8_t)((a ? 2 : 0) | (b ? 1 : 0));
    acc_ = 0;
  }
  // Chamado a cada mudança de A/B. Retorna -1, 0 ou +1 detent completado.
  // O detent só é contado ao chegar no repouso, e o acumulado zera ali: uma transição
  // perdida (ISR atrasada) ou bounce não deixa resto que gere passo fantasma depois.
  int8_t update(bool a, bool b);

 private:
  uint8_t steps_;
  uint8_t prev_ = 0, rest_ = 0;
  int8_t acc_ = 0;
};

enum class Gesture : uint8_t { None, Click, Hold2s, Hold3s };

class ButtonGestures {
 public:
  static constexpr uint32_t kDebounceMs = 30, kHold2Ms = 2000, kHold3Ms = 3000;
  // `pressed` ativo-alto (o driver inverte o pull-up). Chamar a cada ~5 ms.
  Gesture update(bool pressed, uint32_t now);
  bool pressed() const { return stable_; }

 private:
  bool lastRaw_ = false, stable_ = false;
  uint32_t lastChange_ = 0, pressStart_ = 0;
  bool held2_ = false, held3_ = false;
};
