#pragma once
// Tela Mapa: tiles do SD (cache LRU em PSRAM), marcadores, seta do amigo e trilha da sessão.
// Roda na task de UI. A leitura do arquivo usa o SPI; a decodificação do JPEG não.
#include <TFT_eSPI.h>
#include "state.h"

void mapInit();                                      // aloca o cache e o buffer de arquivo em PSRAM
void mapZoom(int8_t detents);                        // horário (+) aproxima
bool mapDraw(TFT_eSprite& f, const SharedState& s);  // true = ainda há tile para carregar
