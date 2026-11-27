#include "bme.h"

#include <Adafruit_BME280.h>
#include <Wire.h>
#include <cmath>
#include "baro_logic.h"
#include "config.h"
#include "i2c_bus.h"
#include "state.h"

namespace {

constexpr uint32_t kBmePeriodMs = 5000;

Adafruit_BME280 gBme;
bool gBmeReady = false;
uint8_t gBmeAddr = 0;
uint32_t gLastReadMs = 0;
bool gReadOnce = false;
BaroAltimeter gAltimeter;
PressureHistory gHistory;

bool tryInitBme(uint8_t addr) {
  i2cLock();
  const bool ok = gBme.begin(addr, &Wire);
  if (ok) {
    // Modo FORCED: faz leitura sob demanda a cada 5 s, evitando autoaquecimento.
    gBme.setSampling(Adafruit_BME280::MODE_FORCED,
                     Adafruit_BME280::SAMPLING_X1,  // temperatura
                     Adafruit_BME280::SAMPLING_X1,  // pressão
                     Adafruit_BME280::SAMPLING_X1,  // umidade
                     Adafruit_BME280::FILTER_OFF);
  }
  i2cUnlock();
  if (ok) gBmeAddr = addr;
  return ok;
}

}  // namespace

bool bmeBegin() {
  gBmeReady = tryInitBme(I2C_ADDR_BME_PRIMARY) || tryInitBme(I2C_ADDR_BME_ALT);
  stateLock();
  SharedState& s = state();
  s.bmeOk = gBmeReady;
  s.bmeAddr = gBmeAddr;
  stateUnlock();
  if (gBmeReady) {
    LOGI("BME280: pronto em 0x%02X", gBmeAddr);
  } else {
    LOGW("BME280: nao respondeu em 0x%02X nem 0x%02X (tolerado)", I2C_ADDR_BME_PRIMARY,
         I2C_ADDR_BME_ALT);
  }
  return gBmeReady;
}

void bmePoll(uint32_t nowMs) {
  if (!gBmeReady) return;
  if (gReadOnce && (uint32_t)(nowMs - gLastReadMs) < kBmePeriodMs) return;
  gLastReadMs = nowMs;
  gReadOnce = true;

  float tempC = NAN, humPct = NAN, pressHpa = NAN;
  i2cLock();
  const bool measOk = gBme.takeForcedMeasurement();
  if (measOk) {
    tempC = gBme.readTemperature();
    humPct = gBme.readHumidity();
    pressHpa = gBme.readPressure() * 0.01f;  // Pa -> hPa
  }
  i2cUnlock();

  if (!measOk || !std::isfinite(pressHpa) || pressHpa < 300.0f || pressHpa > 1100.0f) {
    LOGW("BME280: falha na leitura I2C");
    return;
  }

  stateLock();
  const bool gpsFix = state().gpsFix;
  const uint8_t sats = state().sats;
  const float hdop = state().gpsHdop;
  const float gpsAltM = state().altM;
  stateUnlock();

  gAltimeter.calibrateWithGps(pressHpa, gpsAltM, gpsFix, sats, hdop);
  gAltimeter.updatePressure(pressHpa);
  gHistory.addSample(pressHpa, nowMs);

  float delta3h = 0.0f;
  const bool hasDelta = gHistory.delta3h(delta3h);

  stateLock();
  SharedState& s = state();
  s.bmeOk = true;
  s.tempC = tempC;
  s.humidityPct = humPct;
  s.pressureHpa = pressHpa;
  s.baroAltM = gAltimeter.altitudeM();
  s.baroAltValid = gAltimeter.hasAltitude();
  s.baroCalibrated = gAltimeter.calibrated();
  s.pressureTrend = gHistory.trend();
  s.hasPressureDelta3h = hasDelta;
  s.pressureDelta3h = delta3h;
  s.pressureRapidDrop = gHistory.rapidDrop();
  stateUnlock();
}
