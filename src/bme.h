#pragma once
// Sensor ambiental BME280 (0x76 / 0x77) no barramento I2C compartilhado.
// Chamado pela task "imu" (core 1, prio 1) a cada 5 s para não bloquear a UI.
#include <cstdint>

bool bmeBegin();
void bmePoll(uint32_t nowMs);
