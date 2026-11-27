#include <Arduino.h>
#include "config.h"
#include "debug_console.h"
#include "i2c_bus.h"
#include "imu.h"
#include "radio_task.h"
#include "sd_task.h"
#include "spi_bus.h"
#include "ui.h"
#include "state.h"
#include "storage.h"

static Settings gSettings;

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 2000) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  LOGI("Comunicador GPS+LoRa v%s", FW_VERSION);
  LOGI("Flash %u bytes, PSRAM %u bytes", ESP.getFlashChipSize(), ESP.getPsramSize());

  stateInit();
  spiBusInit();
  i2cBusInit();
  i2cBusScan();
  settingsLoad(gSettings);
  imuConfigureCompass(gSettings.magCal, gSettings.magDeclination);
  imuConfigureMount(gSettings.imuMount);
  radioStart(gSettings);
  uiStart(gSettings);
  imuStart();
  sdStart();  // espera o display configurar o SPI antes de montar o SD
}

void loop() {
  consolePoll();
  vTaskDelay(pdMS_TO_TICKS(10));
}
