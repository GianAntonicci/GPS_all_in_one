#pragma once
// Estimativa de carga de uma célula Li-ion 1S pela tensão (sem carga).
#include <cstdint>

constexpr uint8_t BATTERY_LOW_PCT = 15;

uint8_t batteryPercent(uint32_t mV);  // tabela com interpolação linear, truncada

// Média exponencial (alfa = 0,2) para tirar o ruído do ADC; a 1ª amostra inicializa.
class BatteryFilter {
 public:
  uint32_t update(uint32_t mV);

 private:
  bool init_ = false;
  uint32_t v_ = 0;
};
