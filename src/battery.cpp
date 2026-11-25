#include "battery.h"

#include <Arduino.h>
#include "battery_curve.h"
#include "config.h"
#include "state.h"

namespace {
constexpr uint32_t kEveryMs = 1000;
constexpr uint32_t kDividerNum = 2;  // 100k/100k; ajustar se o multímetro discordar
BatteryFilter gFilter;
uint32_t gLast = 0;
bool gFirst = true;
}  // namespace

void batteryBegin() { analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db); }

void batteryPoll(uint32_t now) {
  if (!gFirst && now - gLast < kEveryMs) return;
  gFirst = false;
  gLast = now;
  uint32_t mv = gFilter.update(analogReadMilliVolts(PIN_BAT_ADC) * kDividerNum);
  uint8_t pct = batteryPercent(mv);
  stateLock();
  SharedState& s = state();
  const bool low = pct < BATTERY_LOW_PCT;
  const bool wasLow = s.batteryLow;
  s.batteryMv = mv;
  s.batteryPct = pct;
  s.batteryLow = low;
  stateUnlock();
  if (low && !wasLow) LOGW("Bateria baixa: %u%% (%lu mV)", pct, (unsigned long)mv);
}
