#include "state.h"

#include <Arduino.h>

namespace {
SharedState gState;
SemaphoreHandle_t gMutex = nullptr;
}  // namespace

void stateInit() {
  if (!gMutex) gMutex = xSemaphoreCreateMutex();
}

SharedState& state() { return gState; }

void stateLock() { xSemaphoreTake(gMutex, portMAX_DELAY); }

void stateUnlock() { xSemaphoreGive(gMutex); }
