#include "battery_curve.h"

namespace {
struct Point {
  uint32_t mV;
  uint8_t pct;
};
// Decrescente em tensão.
const Point kCurve[] = {{4200, 100}, {4100, 90}, {4000, 80}, {3900, 65}, {3800, 50},
                        {3700, 35},  {3600, 20}, {3500, 10}, {3400, 5},  {3300, 0}};
const int kN = sizeof kCurve / sizeof kCurve[0];
}  // namespace

uint8_t batteryPercent(uint32_t mV) {
  if (mV >= kCurve[0].mV) return 100;
  if (mV <= kCurve[kN - 1].mV) return 0;
  for (int i = 1; i < kN; ++i) {
    if (mV >= kCurve[i].mV) {
      const Point& hi = kCurve[i - 1];
      const Point& lo = kCurve[i];
      return (uint8_t)(lo.pct + (uint32_t)(hi.pct - lo.pct) * (mV - lo.mV) / (hi.mV - lo.mV));
    }
  }
  return 0;
}

uint32_t BatteryFilter::update(uint32_t mV) {
  if (!init_) {
    init_ = true;
    v_ = mV;
  } else {
    v_ = (4 * v_ + mV + 2) / 5;
  }
  return v_;
}
