#pragma once
// Mutex do barramento I2C compartilhado (OLED 0x3C, ICM-20948 0x68/0x69, BME280 0x76/0x77).
void i2cBusInit();  // chamar no setup() antes de qualquer usuário do I2C
void i2cBusScan();  // faz varredura I2C no boot e registra dispositivos encontrados no log
void i2cLock();
void i2cUnlock();
