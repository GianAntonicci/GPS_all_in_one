#include "i2c_bus.h"

#include <Arduino.h>
#include <Wire.h>
#include "config.h"

namespace {
SemaphoreHandle_t gI2c = nullptr;
}

void i2cBusInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  if (!gI2c) gI2c = xSemaphoreCreateRecursiveMutex();
}

void i2cLock() {
  if (gI2c) xSemaphoreTakeRecursive(gI2c, portMAX_DELAY);
}

void i2cUnlock() {
  if (gI2c) xSemaphoreGiveRecursive(gI2c);
}
