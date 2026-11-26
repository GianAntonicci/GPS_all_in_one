#include "spi_bus.h"

#include <Arduino.h>
#include "config.h"

namespace {
SemaphoreHandle_t gSpi = nullptr;
}

void spiBusInit() {
  // Todos os CS em HIGH antes de iniciar qualquer periférico: um CS flutuando faz o SD ou o
  // CC1101 responder junto com o TFT no MISO compartilhado.
  for (int pin : {PIN_TFT_CS, PIN_SD_CS, PIN_CC1101_CS}) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  if (!gSpi) gSpi = xSemaphoreCreateRecursiveMutex();
}
void spiLock() { xSemaphoreTakeRecursive(gSpi, portMAX_DELAY); }
void spiUnlock() { xSemaphoreGiveRecursive(gSpi); }
