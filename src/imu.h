#pragma once
// Task do IMU (ICM-20948 em 0x68/0x69, INT no GPIO 7): acorda o TFT por movimento,
// detecta quedas e calcula o rumo da bússola (AK09916).
#include <cstdint>
#include "compass.h"

void imuStart();  // cria task "imu", core 1, prioridade 1, stack 6144

// Carrega calibração e declinação salvas na NVS (chamar antes de imuStart()).
void imuConfigureCompass(const MagCal& cal, float declinationDeg);
void imuSetDeclination(float declinationDeg);

// Modo de calibração da bússola (~20 s girando em 8). Thread-safe.
bool imuStartCompassCal();
void imuCancelCompassCal();
// Retorna true se calibração está em andamento; quando termina, done=true e success indica êxito.
bool imuCompassCalStatus(uint8_t& progressPct, uint32_t& samples, bool& done, bool& success,
                         MagCal& calOut);
