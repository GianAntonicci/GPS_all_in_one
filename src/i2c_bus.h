#pragma once
// Mutex do barramento I2C compartilhado (OLED 0x3C, MPU6050 0x68).
void i2cBusInit();  // chamar no setup() antes de qualquer usuário do I2C
void i2cLock();
void i2cUnlock();
