#include "baro_logic.h"

#include <cmath>

namespace {
constexpr float kMinPressureHpa = 300.0f;
constexpr float kMaxPressureHpa = 1100.0f;
constexpr float kAltEmaAlpha = 0.25f;
constexpr float kSeaLevelEmaAlpha = 0.10f;

bool validPressure(float p) {
  return std::isfinite(p) && p >= kMinPressureHpa && p <= kMaxPressureHpa;
}
}  // namespace

float pressureToAltitudeM(float pressureHpa, float seaLevelHpa) {
  if (!validPressure(pressureHpa) || !validPressure(seaLevelHpa)) return 0.0f;
  return 44330.0f * (1.0f - std::pow(pressureHpa / seaLevelHpa, 0.19029495f));
}

float seaLevelFromAltitude(float pressureHpa, float altitudeM) {
  if (!validPressure(pressureHpa) || !std::isfinite(altitudeM) || altitudeM < -500.0f ||
      altitudeM > 12000.0f) {
    return BaroAltimeter::kDefaultSeaLevelHpa;
  }
  const float base = 1.0f - (altitudeM / 44330.0f);
  if (base <= 0.01f) return BaroAltimeter::kDefaultSeaLevelHpa;
  return pressureHpa / std::pow(base, 5.255f);
}

bool BaroAltimeter::updatePressure(float pressureHpa) {
  if (!validPressure(pressureHpa)) return false;
  const float rawAlt = pressureToAltitudeM(pressureHpa, seaLevelHpa_);
  if (!hasAlt_) {
    altM_ = rawAlt;
    hasAlt_ = true;
  } else {
    altM_ = altM_ * (1.0f - kAltEmaAlpha) + rawAlt * kAltEmaAlpha;
  }
  return true;
}

bool BaroAltimeter::calibrateWithGps(float pressureHpa, float gpsAltM, bool gpsFix, uint8_t sats,
                                     float hdop) {
  if (!gpsFix || sats < kMinCalSats || !std::isfinite(hdop) || hdop <= 0.0f ||
      hdop > kMaxCalHdop) {
    return false;
  }
  if (!validPressure(pressureHpa) || !std::isfinite(gpsAltM) || gpsAltM < -500.0f ||
      gpsAltM > 12000.0f) {
    return false;
  }
  const float p0 = seaLevelFromAltitude(pressureHpa, gpsAltM);
  if (!validPressure(p0)) return false;

  if (!calibrated_) {
    seaLevelHpa_ = p0;
    calibrated_ = true;
    altM_ = gpsAltM;
    hasAlt_ = true;
  } else {
    seaLevelHpa_ = seaLevelHpa_ * (1.0f - kSeaLevelEmaAlpha) + p0 * kSeaLevelEmaAlpha;
  }
  return true;
}

void PressureHistory::reset() {
  head_ = 0;
  count_ = 0;
  latestHpa_ = 0.0f;
  latestMs_ = 0;
  lastSlotMs_ = 0;
  hasLatest_ = false;
}

void PressureHistory::addSample(float pressureHpa, uint32_t nowMs) {
  if (!validPressure(pressureHpa)) return;
  latestHpa_ = pressureHpa;
  latestMs_ = nowMs;
  if (!hasLatest_ || (uint32_t)(nowMs - lastSlotMs_) >= kSlotMs) {
    ring_[head_] = {pressureHpa, nowMs};
    head_ = (head_ + 1) % kCapacity;
    if (count_ < kCapacity) ++count_;
    lastSlotMs_ = nowMs;
    hasLatest_ = true;
  }
}

bool PressureHistory::delta3h(float& outDeltaHpa) const {
  if (!hasLatest_ || count_ == 0) return false;
  const size_t oldestIdx = (head_ + kCapacity - count_) % kCapacity;
  bool found = false;
  Entry ref{0.0f, 0};
  for (size_t i = 0; i < count_; ++i) {
    const Entry& e = ring_[(oldestIdx + i) % kCapacity];
    const uint32_t age = latestMs_ - e.ms;
    if (age <= kWindowMs + kSlotMs) {
      ref = e;
      found = true;
      break;
    }
  }
  if (!found) return false;
  if ((uint32_t)(latestMs_ - ref.ms) < kMinSpanMs) return false;
  outDeltaHpa = latestHpa_ - ref.hpa;
  return true;
}

PressureTrend PressureHistory::trend() const {
  float d = 0.0f;
  if (!delta3h(d)) return PressureTrend::Unknown;
  if (d >= kTrendBandHpa) return PressureTrend::Rising;
  if (d <= -kTrendBandHpa) return PressureTrend::Falling;
  return PressureTrend::Steady;
}

bool PressureHistory::rapidDrop() const {
  float d = 0.0f;
  return delta3h(d) && d <= kRapidDropHpa;
}

const char* pressureTrendSymbol(PressureTrend t) {
  switch (t) {
    case PressureTrend::Rising: return "↑";
    case PressureTrend::Steady: return "→";
    case PressureTrend::Falling: return "↓";
    case PressureTrend::Unknown: return "--";
  }
  return "--";
}
