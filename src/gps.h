#pragma once
// NEO-6M em Serial1 via TinyGPSPlus. Chamado de dentro da task de rádio.
#include <cstdint>

void gpsBegin();               // Serial1 + última posição/rumo salvos na NVS
void gpsPoll(uint32_t now);    // alimenta o parser e atualiza SharedState
