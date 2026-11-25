#include "imu.h"

#include <Arduino.h>
#include <MPU6050.h>
#include "config.h"
#include "fall_detector.h"
#include "i2c_bus.h"
#include "state.h"
#include "ui.h"

namespace {

constexpr uint8_t kMpuAddr = 0x68;
constexpr float kAccelLsbPerG = 4096.0f;    // ±8 g (MPU6050_ACCEL_FS_8)
constexpr uint32_t kSampleMs = 20;          // 50 Hz durante movimento/queda
constexpr uint32_t kWakeCooldownMs = 1000;  // evita flood de uiWakeScreen em movimento contínuo
constexpr uint8_t kMotionThr = 20;          // ~40 mg (2 mg/LSB); ajustar no hardware
constexpr uint8_t kMotionDur = 2;           // 2 ms; ajustar no hardware

TaskHandle_t gImuTask = nullptr;
MPU6050 gMpu(kMpuAddr);

void IRAM_ATTR onMotionIsr() {
  BaseType_t woken = pdFALSE;
  if (gImuTask) vTaskNotifyGiveFromISR(gImuTask, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void setImuOk(bool ok) {
  stateLock();
  state().imuOk = ok;
  stateUnlock();
}

bool configureMpu() {
  i2cLock();
  gMpu.initialize();
  const bool ok = gMpu.testConnection();
  if (!ok) {
    i2cUnlock();
    return false;
  }
  gMpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_8);
  gMpu.setStandbyXGyroEnabled(true);
  gMpu.setStandbyYGyroEnabled(true);
  gMpu.setStandbyZGyroEnabled(true);
  gMpu.setTempSensorEnabled(false);

  // Filtro passa-alta (5 Hz) para o detector de movimento por hardware ignorar a gravidade.
  gMpu.setDHPFMode(MPU6050_DHPF_5);
  gMpu.setMotionDetectionThreshold(kMotionThr);
  gMpu.setMotionDetectionDuration(kMotionDur);
  gMpu.setInterruptMode(0);          // ativo alto
  gMpu.setInterruptDrive(0);         // push-pull
  gMpu.setInterruptLatch(true);      // mantém nível até leitura de INT_STATUS
  gMpu.setInterruptLatchClear(false);
  gMpu.setIntMotionEnabled(true);
  (void)gMpu.getIntStatus();         // limpa eventual interrupção pendente
  i2cUnlock();
  return true;
}

void imuTask(void*) {
  if (!configureMpu()) {
    LOGE("IMU: MPU6050 nao respondeu em 0x%02X", kMpuAddr);
    setImuOk(false);
    gImuTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  setImuOk(true);
  LOGI("IMU: MPU6050 pronto (±8g, INT GPIO %d)", PIN_MPU_INT);

  pinMode(PIN_MPU_INT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_MPU_INT), onMotionIsr, RISING);

  FallDetector detector;
  bool hasWoken = false;
  uint32_t lastWake = 0;

  for (;;) {
    // Dorme em repouso: 0% CPU e zero tráfego I2C até o GPIO 7 disparar.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    i2cLock();
    (void)gMpu.getIntStatus();  // limpa latch do INT
    i2cUnlock();

    uint32_t now = millis();
    if (!hasWoken || (uint32_t)(now - lastWake) >= kWakeCooldownMs) {
      uiWakeScreen();
      lastWake = now;
      hasWoken = true;
    }

    detector.onMotionWake(now);
    while (detector.wantsSampling()) {
      int16_t ax = 0, ay = 0, az = 0;
      i2cLock();
      gMpu.getAcceleration(&ax, &ay, &az);
      i2cUnlock();

      const FallEvent ev = detector.update(ax / kAccelLsbPerG, ay / kAccelLsbPerG,
                                           az / kAccelLsbPerG, millis());
      if (ev == FallEvent::SosCountdown) {
        LOGW("IMU: queda detectada, iniciando contagem SOS");
        uiRequestSosCountdown();
      }
      vTaskDelay(pdMS_TO_TICKS(kSampleMs));
    }

    // Limpa latch e eventual notificação acumulada durante a amostragem antes de dormir.
    i2cLock();
    (void)gMpu.getIntStatus();
    i2cUnlock();
    (void)ulTaskNotifyTake(pdTRUE, 0);
  }
}

}  // namespace

void imuStart() {
  xTaskCreatePinnedToCore(imuTask, "imu", 4096, nullptr, 1, &gImuTask, 1);
}
