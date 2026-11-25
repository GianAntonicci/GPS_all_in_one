#pragma once
// Encoder rotativo (A/B por interrupção) e botão.
#include <cstdint>

#ifndef ENC_STEPS_PER_DETENT
#define ENC_STEPS_PER_DETENT 4  // ajustar no hardware (2 ou 4)
#endif

void inputBegin();
int8_t inputTakeDetents();   // detents desde a última chamada (chamar a cada ~5 ms)
bool inputButtonPressed();   // nível atual (ativo-baixo no pino); debounce no ButtonGestures
