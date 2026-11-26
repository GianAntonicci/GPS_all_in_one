#pragma once
// Task "sd" (core 0, prioridade 1): monta o cartão, grava a trilha GPX e mantém a trilha da
// sessão em PSRAM. O rádio só enfileira pontos; a UI lê a trilha e os tiles.
#include <cstdint>
#include "gps_logic.h"
#include "track_logic.h"

struct TrackPoint {
  double lat, lon;
  float alt;
  DateTime utc, local;  // zerados se a hora ainda não é válida
  bool newSegment;
};

void sdStart();                          // cria a task; ela espera displayReady() para montar
bool trackEnqueue(const TrackPoint& p);  // não bloqueia; false se a fila está cheia
bool sdMounted();                        // thread-safe
uint32_t sdGeneration();                 // muda a cada (re)montagem do cartão
void trailLock();
void trailUnlock();
const TrackBuffer& trail();  // ler só entre trailLock()/trailUnlock()
