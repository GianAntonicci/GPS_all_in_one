#pragma once
// Configurações persistentes (Preferences/NVS). SD e GPX entram na etapa do mapa/trilha.
#include <cstdint>
#include "compass.h"

struct Settings {
  char id[3];          // ID do aparelho no protocolo (1-2 alfanuméricos)
  uint8_t brightness;  // 0-255
  uint8_t volume;      // 0-100
  float magDeclination;
  MagCal magCal;
};

void settingsLoad(Settings& s);
void settingsSave(const Settings& s);
