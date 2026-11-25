#include "imu.h"

#include <Arduino.h>
#include <I2Cdev.h>
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
constexpr uint8_t kMaxReadErrors = 15;      // 300 ms de falhas seguidas no I2C → aborta amostragem

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

// Lê os 6 bytes de aceleração verificando o retorno do I2C: evita reutilizar buffer antigo
// em caso de NACK/desconexão (o que poderia simular 1 g imóvel e disparar SOS falso).
bool readAccel(int16_t& ax, int16_t& ay, int16_t& az) {
  uint8_t buf[6] = {};
  i2cLock();
  const int8_t n = I2Cdev::readBytes(kMpuAddr, MPU6050_RA_ACCEL_XOUT_H, 6, buf);
  i2cUnlock();
  if (n != 6) return false;
  ax = (int16_t)(((int16_t)buf[0] << 8) | buf[1]);
  ay = (int16_t)(((int16_t)buf[2] << 8) | buf[3]);
  az = (int16_t)(((int16_t)buf[4] << 8) | buf[5]);
  return true;
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
  i2cUnlock();
  return true;
}

void imuTask(void*) {
  // Anexa a ISR antes de habilitar/limpar o latch do INT para não perder borda no boot.
  pinMode(PIN_MPU_INT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_MPU_INT), onMotionIsr, RISING);

  if (!configureMpu()) {
    detachInterrupt(digitalPinToInterrupt(PIN_MPU_INT));
    LOGE("IMU: MPU6050 nao respondeu em 0x%02X", kMpuAddr);
    setImuOk(false);
    gImuTask = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  setImuOk(true);
  LOGI("IMU: MPU6050 pronto (±8g, INT GPIO %d)", PIN_MPU_INT);

  // Limpa latch inicial; se o pino já estiver alto após limpar/drenar, processa sem dormir.
  i2cLock();
  (void)gMpu.getIntStatus();
  i2cUnlock();
  (void)ulTaskNotifyTake(pdTRUE, 0);
  bool waitForInt = (digitalRead(PIN_MPU_INT) == LOW);

  FallDetector detector;
  bool hasWoken = false;
  uint32_t lastWake = 0;

  for (;;) {
    // Dorme em repouso: 0% CPU e zero tráfego I2C até o GPIO 7 disparar.
    if (waitForInt) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    waitForInt = true;

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
    uint8_t consecErrors = 0;
    while (detector.wantsSampling()) {
      int16_t ax = 0, ay = 0, az = 0;
      if (!readAccel(ax, ay, az)) {
        if (++consecErrors >= kMaxReadErrors) {
          LOGE("IMU: falha consecutiva de leitura I2C, abortando amostragem");
          setImuOk(false);
          detector.reset();
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(kSampleMs));
        continue;
      }
      if (consecErrors > 0) {
        consecErrors = 0;
        setImuOk(true);
      }

      const FallEvent ev = detector.update(ax / kAccelLsbPerG, ay / kAccelLsbPerG,
                                           az / kAccelLsbPerG, millis());
      if (ev == FallEvent::SosCountdown) {
        LOGW("IMU: queda detectada, iniciando contagem SOS");
        uiRequestSosCountdown();
      }
      vTaskDelay(pdMS_TO_TICKS(kSampleMs));
    }

    // Limpa latch e drena notificação residual; se PIN_MPU_INT voltou a subir na janela entre
    // getIntStatus() e ulTaskNotifyTake(0), evita travar em RISING-edge latched HIGH.
    i2cLock();
    (void)gMpu.getIntStatus();
    i2cUnlock();
    (void)ulTaskNotifyTake(pdTRUE, 0);
    if (digitalRead(PIN_MPU_INT) == HIGH) waitForInt = false;
  }
}

}  // namespace

void imuStart() {
  xTaskCreatePinnedToCore(imuTask, "imu", 4096, nullptr, 1, &gImuTask, 1);
}
