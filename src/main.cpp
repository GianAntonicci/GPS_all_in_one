#include <Arduino.h>
#include "config.h"

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  LOGI("Comunicador GPS+LoRa v%s", FW_VERSION);
  LOGI("Flash %u bytes, PSRAM %u bytes", ESP.getFlashChipSize(), ESP.getPsramSize());
}

void loop() {
  vTaskDelay(1);
}
