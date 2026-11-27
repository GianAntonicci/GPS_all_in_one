#include "imu.h"

#include <Arduino.h>
#include <ICM_20948.h>
#include <Wire.h>
#include <atomic>
#include "bme.h"
#include "config.h"
#include "fall_detector.h"
#include "i2c_bus.h"
#include "state.h"
#include "ui.h"

namespace {

constexpr uint32_t kFallSampleMs = 20;      // 50 Hz durante movimento/queda
constexpr uint32_t kCompassPollMs = 50;     // 20 Hz com TFT aceso / calibração
constexpr uint32_t kIdleWakeMs = 5000;      // timeout de sono (pronto para sensores periódicos)
constexpr uint32_t kWakeCooldownMs = 1000;  // evita flood de uiWakeScreen em movimento contínuo
constexpr uint32_t kCalDurationMs = 20000;  // 20 s girando em 8
constexpr uint8_t kWomThreshold = 15;       // ~60 mg (4 mg/LSB); ajustar no hardware
constexpr uint8_t kMaxReadErrors = 15;      // 300 ms de falhas seguidas no I2C → aborta amostragem

TaskHandle_t gImuTask = nullptr;
ICM_20948_I2C gIcm;
bool gMagOk = false;
uint8_t gIcmAddr = 0;

// Estado de calibração compartilhado entre UI e task IMU (protegido por stateLock()).
MagCal gCal{};
float gDeclination = MAG_DECLINATION_DEFAULT;
bool gCalActive = false;
bool gCalDone = false;
bool gCalSuccess = false;
uint32_t gCalStartMs = 0;
uint8_t gCalPct = 0;
uint32_t gCalSamples = 0;
MagCalibrator gCalibrator;

std::atomic<bool> gMotionIrq{false};

void IRAM_ATTR onMotionIsr() {
  gMotionIrq.store(true, std::memory_order_relaxed);
  BaseType_t woken = pdFALSE;
  if (gImuTask) vTaskNotifyGiveFromISR(gImuTask, &woken);
  if (woken) portYIELD_FROM_ISR();
}

void setImuStatus(bool ok, uint8_t addr, bool magOk) {
  stateLock();
  SharedState& s = state();
  s.imuOk = ok;
  s.imuAddr = addr;
  s.compassOk = ok && magOk;
  s.compassCalibrated = gCal.valid;
  s.magDeclination = gDeclination;
  stateRefreshHeading(s);
  stateUnlock();
}

bool tryInitIcm(bool ad0Val, uint8_t addr) {
  i2cLock();
  gIcm.begin(Wire, ad0Val);
  const bool ok = (gIcm.status == ICM_20948_Stat_Ok);
  if (!ok) {
    i2cUnlock();
    return false;
  }

  gIcm.swReset();
  vTaskDelay(pdMS_TO_TICKS(20));
  gIcm.sleep(false);
  gIcm.lowPower(false);

  // Acelerômetro ±8 g (mede impactos > 3 g sem saturar em 4 g).
  ICM_20948_fss_t fss{};
  fss.a = gpm8;
  fss.g = dps250;
  gIcm.setFullScale(ICM_20948_Internal_Acc, fss);

  // Magnetômetro interno AK09916 via I2C master do ICM-20948.
  gMagOk = (gIcm.startupMagnetometer() == ICM_20948_Stat_Ok);
  if (!gMagOk) {
    LOGW("IMU: AK09916 nao respondeu no ICM-20948 (bussola indisponivel)");
  }

  // Wake-on-Motion via pino INT (GPIO 7): ativo alto, push-pull, latched até clearInterrupts().
  gIcm.cfgIntActiveLow(false);
  gIcm.cfgIntOpenDrain(false);
  gIcm.cfgIntLatch(true);
  gIcm.WOMThreshold(kWomThreshold);
  gIcm.WOMLogic(1, 1);  // habilita WoM comparando amostra atual com a anterior
  gIcm.intEnableWOM(true);
  gIcm.clearInterrupts();
  i2cUnlock();

  gIcmAddr = addr;
  return true;
}

bool configureIcm() {
  if (tryInitIcm(false, I2C_ADDR_ICM_PRIMARY)) return true;
  if (tryInitIcm(true, I2C_ADDR_ICM_ALT)) return true;
  return false;
}

// Lê aceleração (em g) e magnetômetro (em uT, alinhado aos eixos do acelerômetro).
bool readIcm(float& axG, float& ayG, float& azG, float& mxUt, float& myUt, float& mzUt,
             bool& hasMag) {
  i2cLock();
  gIcm.getAGMT();
  const bool ok = (gIcm.status == ICM_20948_Stat_Ok);
  if (ok) {
    // SparkFun ICM_20948 retorna aceleração em mg -> converte para g.
    axG = gIcm.accX() * 0.001f;
    ayG = gIcm.accY() * 0.001f;
    azG = gIcm.accZ() * 0.001f;
    // No ICM-20948, o AK09916 tem eixos Y e Z invertidos em relação ao acelerômetro.
    mxUt = gIcm.magX();
    myUt = -gIcm.magY();
    mzUt = -gIcm.magZ();
    hasMag = gMagOk;
  }
  i2cUnlock();
  return ok;
}

void processCompassSample(float axG, float ayG, float azG, float mxUt, float myUt, float mzUt,
                          uint32_t now) {
  bool calActive = false;
  MagCal calCopy{};
  float declCopy = MAG_DECLINATION_DEFAULT;

  stateLock();
  calActive = gCalActive;
  calCopy = gCal;
  declCopy = gDeclination;
  stateUnlock();

  if (calActive) {
    gCalibrator.addSample(mxUt, myUt, mzUt);
    const uint32_t elapsed = now - gCalStartMs;
    uint8_t pct = (elapsed >= kCalDurationMs) ? 100 : (uint8_t)((elapsed * 100u) / kCalDurationMs);
    if (elapsed >= kCalDurationMs) {
      MagCal newCal{};
      const bool ok = gCalibrator.finish(newCal);
      stateLock();
      gCalActive = false;
      gCalDone = true;
      gCalSuccess = ok;
      gCalPct = 100;
      gCalSamples = gCalibrator.sampleCount();
      if (ok) {
        gCal = newCal;
        state().compassCalibrated = true;
        LOGI("Bussola calibrada (%lu amostras): off(%.1f,%.1f,%.1f) esc(%.2f,%.2f,%.2f)",
             (unsigned long)gCalSamples, newCal.offsetX, newCal.offsetY, newCal.offsetZ,
             newCal.scaleX, newCal.scaleY, newCal.scaleZ);
      } else {
        LOGW("Calibracao da bussola falhou (%lu amostras; gire mais o aparelho)",
             (unsigned long)gCalSamples);
      }
      stateRefreshHeading(state());
      stateUnlock();
      return;
    }
    stateLock();
    gCalPct = pct;
    gCalSamples = gCalibrator.sampleCount();
    stateUnlock();
    return;
  }

  float cx = mxUt, cy = myUt, cz = mzUt;
  applyMagCal(calCopy, mxUt, myUt, mzUt, cx, cy, cz);
  float heading = 0.0f;
  if (tiltCompensatedHeading(axG, ayG, azG, cx, cy, cz, declCopy, heading)) {
    stateLock();
    SharedState& s = state();
    s.compassOk = true;
    s.compassDeg = heading;
    s.compassCalibrated = calCopy.valid;
    s.magDeclination = declCopy;
    stateRefreshHeading(s);
    stateUnlock();
  }
}

void imuTask(void*) {
  const bool bmeReady = bmeBegin();

  // Anexa a ISR antes de habilitar/limpar o latch do INT para não perder borda no boot.
  pinMode(PIN_IMU_INT, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_IMU_INT), onMotionIsr, RISING);

  const bool icmReady = configureIcm();
  if (!icmReady) {
    detachInterrupt(digitalPinToInterrupt(PIN_IMU_INT));
    LOGE("IMU: ICM-20948 nao respondeu em 0x%02X nem 0x%02X", I2C_ADDR_ICM_PRIMARY,
         I2C_ADDR_ICM_ALT);
    setImuStatus(false, 0, false);
    if (!bmeReady) {
      gImuTask = nullptr;
      vTaskDelete(nullptr);
      return;
    }
    // ICM ausente, mas BME280 presente: mantém apenas a leitura ambiental a cada 5 s.
    for (;;) {
      bmePoll(millis());
      vTaskDelay(pdMS_TO_TICKS(kIdleWakeMs));
    }
  }

  setImuStatus(true, gIcmAddr, gMagOk);
  LOGI("IMU: ICM-20948 pronto em 0x%02X (±8g, INT GPIO %d, mag=%s)", gIcmAddr, PIN_IMU_INT,
       gMagOk ? "ok" : "ERRO");

  i2cLock();
  gIcm.clearInterrupts();
  i2cUnlock();
  gMotionIrq.store(false, std::memory_order_relaxed);
  (void)ulTaskNotifyTake(pdTRUE, 0);

  FallDetector detector;
  bool hasWoken = false;
  uint32_t lastWake = 0;
  uint8_t consecErrors = 0;
  uint32_t lastCompassRead = 0;

  for (;;) {
    const uint32_t now = millis();
    if (bmeReady) bmePoll(now);

    // Verifica se houve movimento por hardware (WoM no GPIO 7).
    const bool motionTriggered =
        gMotionIrq.exchange(false, std::memory_order_relaxed) || (digitalRead(PIN_IMU_INT) == HIGH);
    if (motionTriggered) {
      i2cLock();
      gIcm.clearInterrupts();
      i2cUnlock();

      if (!hasWoken || (uint32_t)(now - lastWake) >= kWakeCooldownMs) {
        uiWakeScreen();
        lastWake = now;
        hasWoken = true;
      }
      detector.onMotionWake(now);
    }

    stateLock();
    const bool needCompass = gMagOk && (state().tftOn || gCalActive);
    stateUnlock();

    if (detector.wantsSampling()) {
      float ax = 0, ay = 0, az = 0, mx = 0, my = 0, mz = 0;
      bool hasMag = false;
      if (!readIcm(ax, ay, az, mx, my, mz, hasMag)) {
        if (++consecErrors >= kMaxReadErrors) {
          LOGE("IMU: falha consecutiva de leitura I2C, abortando amostragem");
          setImuStatus(false, gIcmAddr, false);
          detector.reset();
        }
        vTaskDelay(pdMS_TO_TICKS(kFallSampleMs));
        continue;
      }
      if (consecErrors > 0) {
        consecErrors = 0;
        setImuStatus(true, gIcmAddr, gMagOk);
      }

      const uint32_t tNow = millis();
      const FallEvent ev = detector.update(ax, ay, az, tNow);
      if (ev == FallEvent::SosCountdown) {
        LOGW("IMU: queda detectada, iniciando contagem SOS");
        uiRequestSosCountdown();
      }
      if (hasMag && (needCompass && (tNow - lastCompassRead >= kCompassPollMs))) {
        processCompassSample(ax, ay, az, mx, my, mz, tNow);
        lastCompassRead = tNow;
      }

      if (!detector.wantsSampling()) {
        i2cLock();
        gIcm.clearInterrupts();
        i2cUnlock();
      }
      vTaskDelay(pdMS_TO_TICKS(kFallSampleMs));
      continue;
    }

    if (needCompass) {
      float ax = 0, ay = 0, az = 0, mx = 0, my = 0, mz = 0;
      bool hasMag = false;
      if (readIcm(ax, ay, az, mx, my, mz, hasMag)) {
        if (consecErrors > 0) {
          consecErrors = 0;
          setImuStatus(true, gIcmAddr, gMagOk);
        }
        if (hasMag) processCompassSample(ax, ay, az, mx, my, mz, millis());
      } else if (++consecErrors >= kMaxReadErrors) {
        setImuStatus(false, gIcmAddr, false);
      }
      (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kCompassPollMs));
      continue;
    }

    // TFT apagado e sem suspeita de queda: dorme aguardando INT no GPIO 7.
    i2cLock();
    gIcm.clearInterrupts();
    i2cUnlock();
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kIdleWakeMs));
  }
}

}  // namespace

void imuConfigureCompass(const MagCal& cal, float declinationDeg) {
  stateLock();
  gCal = cal;
  gDeclination = declinationDeg;
  SharedState& s = state();
  s.compassCalibrated = cal.valid;
  s.magDeclination = declinationDeg;
  stateRefreshHeading(s);
  stateUnlock();
}

void imuSetDeclination(float declinationDeg) {
  stateLock();
  gDeclination = declinationDeg;
  state().magDeclination = declinationDeg;
  stateUnlock();
}

bool imuStartCompassCal() {
  stateLock();
  if (!state().imuOk || !gMagOk) {
    stateUnlock();
    return false;
  }
  gCalibrator.reset();
  gCalStartMs = millis();
  gCalActive = true;
  gCalDone = false;
  gCalSuccess = false;
  gCalPct = 0;
  gCalSamples = 0;
  stateUnlock();
  if (gImuTask) xTaskNotifyGive(gImuTask);
  return true;
}

void imuCancelCompassCal() {
  stateLock();
  gCalActive = false;
  gCalDone = false;
  gCalSuccess = false;
  gCalPct = 0;
  gCalSamples = 0;
  stateUnlock();
}

bool imuCompassCalStatus(uint8_t& progressPct, uint32_t& samples, bool& done, bool& success,
                         MagCal& calOut) {
  stateLock();
  const bool active = gCalActive;
  progressPct = gCalPct;
  samples = gCalSamples;
  done = gCalDone;
  success = gCalSuccess;
  calOut = gCal;
  stateUnlock();
  return active;
}

void imuStart() {
  xTaskCreatePinnedToCore(imuTask, "imu", 6144, nullptr, 1, &gImuTask, 1);
}
