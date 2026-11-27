#include "i2c_bus.h"

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

namespace {
SemaphoreHandle_t gI2c = nullptr;

const char* knownI2cName(uint8_t addr) {
  switch (addr) {
    case 0x3C:
    case 0x3D: return "OLED SSD1306";
    case 0x68:
    case 0x69: return "ICM-20948";
    case 0x76:
    case 0x77: return "BME280";
    default: return "desconhecido";
  }
}
}  // namespace

void i2cBusInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  if (!gI2c) gI2c = xSemaphoreCreateRecursiveMutex();
}

void i2cBusScan() {
  i2cLock();
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      LOGI("I2C: dispositivo em 0x%02X (%s)", addr, knownI2cName(addr));
      ++found;
    }
  }
  if (found == 0) LOGW("I2C: nenhum dispositivo respondeu (SDA=%d SCL=%d)", PIN_I2C_SDA, PIN_I2C_SCL);
  i2cUnlock();
}

void i2cLock() {
  if (gI2c) xSemaphoreTakeRecursive(gI2c, portMAX_DELAY);
}

void i2cUnlock() {
  if (gI2c) xSemaphoreGiveRecursive(gI2c);
}
