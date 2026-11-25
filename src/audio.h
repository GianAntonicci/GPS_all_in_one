#pragma once
// Task "audio" (core 1, prioridade 3): dona do I2S (MAX98357A). Tons gerados, sem arquivos.
// Os sons de SOS tocam alto mesmo com o volume em 0. Nenhuma chamada bloqueia.
#include <cstdint>
#include "tone_seq.h"

void audioBegin(uint8_t volumePct);
void audioPlay(Pattern p);
void audioStop(Pattern p);
void audioSetVolume(uint8_t pct);
