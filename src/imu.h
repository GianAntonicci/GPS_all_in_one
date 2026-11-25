#pragma once
// Task do IMU (MPU6050 em 0x68, INT no GPIO 7): acorda o TFT por movimento e detecta quedas.
void imuStart();  // cria task "imu", core 1, prioridade 1, stack 4096
