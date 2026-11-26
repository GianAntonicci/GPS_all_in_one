#pragma once
// Bateria Li-ion 1S pelo ADC1 (divisor 100k/100k).
#include <cstdint>

void batteryBegin();
void batteryPoll(uint32_t now);  // a cada 1 s atualiza SharedState
