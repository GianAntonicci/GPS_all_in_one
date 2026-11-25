#include "spi_bus.h"

#include <Arduino.h>

namespace {
SemaphoreHandle_t gSpi = nullptr;
}

void spiBusInit() {
  if (!gSpi) gSpi = xSemaphoreCreateRecursiveMutex();
}
void spiLock() { xSemaphoreTakeRecursive(gSpi, portMAX_DELAY); }
void spiUnlock() { xSemaphoreGiveRecursive(gSpi); }
