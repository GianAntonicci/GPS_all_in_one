#include "compass.h"

#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kMinAccelG = 0.2f;
constexpr float kMinMagUt = 1.0f;
constexpr double kCompassSpeedThresholdKmh = 1.5;

float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}
}  // namespace

void MagCalibrator::reset() {
  count_ = 0;
  minX_ = maxX_ = 0.0f;
  minY_ = maxY_ = 0.0f;
  minZ_ = maxZ_ = 0.0f;
}

void MagCalibrator::addSample(float mx, float my, float mz) {
  if (!std::isfinite(mx) || !std::isfinite(my) || !std::isfinite(mz)) return;
  if (count_ == 0) {
    minX_ = maxX_ = mx;
    minY_ = maxY_ = my;
    minZ_ = maxZ_ = mz;
  } else {
    if (mx < minX_) minX_ = mx;
    if (mx > maxX_) maxX_ = mx;
    if (my < minY_) minY_ = my;
    if (my > maxY_) maxY_ = my;
    if (mz < minZ_) minZ_ = mz;
    if (mz > maxZ_) maxZ_ = mz;
  }
  ++count_;
}

bool MagCalibrator::finish(MagCal& out) const {
  if (count_ < kMinSamples) return false;
  const float spanX = maxX_ - minX_;
  const float spanY = maxY_ - minY_;
  const float spanZ = maxZ_ - minZ_;
  if (spanX < kMinSpanUt || spanY < kMinSpanUt || spanZ < kMinSpanUt) return false;

  const float halfX = spanX * 0.5f;
  const float halfY = spanY * 0.5f;
  const float halfZ = spanZ * 0.5f;
  const float avgHalf = (halfX + halfY + halfZ) / 3.0f;

  out.offsetX = (maxX_ + minX_) * 0.5f;
  out.offsetY = (maxY_ + minY_) * 0.5f;
  out.offsetZ = (maxZ_ + minZ_) * 0.5f;
  out.scaleX = clampf(avgHalf / halfX, 0.2f, 5.0f);
  out.scaleY = clampf(avgHalf / halfY, 0.2f, 5.0f);
  out.scaleZ = clampf(avgHalf / halfZ, 0.2f, 5.0f);
  out.valid = true;
  return true;
}

float normalizeDeg(float deg) {
  if (!std::isfinite(deg)) return 0.0f;
  float r = std::fmod(deg, 360.0f);
  if (r < 0.0f) r += 360.0f;
  if (r >= 360.0f) r -= 360.0f;
  return r;
}

void applyMagCal(const MagCal& cal, float rawX, float rawY, float rawZ, float& outX, float& outY,
                 float& outZ) {
  if (!cal.valid) {
    outX = rawX;
    outY = rawY;
    outZ = rawZ;
    return;
  }
  outX = (rawX - cal.offsetX) * cal.scaleX;
  outY = (rawY - cal.offsetY) * cal.scaleY;
  outZ = (rawZ - cal.offsetZ) * cal.scaleZ;
}

bool tiltCompensatedHeading(float axG, float ayG, float azG, float mx, float my, float mz,
                            float declinationDeg, float& headingDegOut) {
  if (!std::isfinite(axG) || !std::isfinite(ayG) || !std::isfinite(azG) || !std::isfinite(mx) ||
      !std::isfinite(my) || !std::isfinite(mz) || !std::isfinite(declinationDeg)) {
    return false;
  }
  const float aNorm = std::sqrt(axG * axG + ayG * ayG + azG * azG);
  const float mNorm = std::sqrt(mx * mx + my * my + mz * mz);
  if (aNorm < kMinAccelG || mNorm < kMinMagUt) return false;

  const float ax = axG / aNorm;
  const float ay = ayG / aNorm;
  const float az = azG / aNorm;

  const float roll = std::atan2(ay, az);
  const float pitch = std::atan2(ax, std::sqrt(ay * ay + az * az));

  const float sr = std::sin(roll);
  const float cr = std::cos(roll);
  const float sp = std::sin(pitch);
  const float cp = std::cos(pitch);

  const float yh = my * cr - mz * sr;
  const float mz1 = my * sr + mz * cr;
  const float xh = mx * cp - mz1 * sp;

  if (std::fabs(xh) < 1e-6f && std::fabs(yh) < 1e-6f) return false;

  const float magHeading = std::atan2(yh, xh) * kRadToDeg;
  headingDegOut = normalizeDeg(magHeading + declinationDeg);
  return true;
}

HeadingResult selectHeading(const HeadingInput& in) {
  HeadingResult r{};
  if (in.gpsFix && in.gpsCourseKnown && !in.gpsCourseStale &&
      in.speedKmh >= kCompassSpeedThresholdKmh) {
    r.deg = normalizeDeg((float)in.gpsCourseDeg);
    r.known = true;
    r.stale = false;
    r.source = HeadingSource::Gps;
    return r;
  }
  if (in.compassOk && in.compassCalibrated) {
    r.deg = normalizeDeg(in.compassDeg);
    r.known = true;
    r.stale = false;
    r.source = HeadingSource::Compass;
    return r;
  }
  if (in.gpsCourseKnown) {
    r.deg = normalizeDeg((float)in.gpsCourseDeg);
    r.known = true;
    r.stale = true;
    r.source = HeadingSource::GpsStale;
    return r;
  }
  return r;
}

const char* headingSourceName(HeadingSource s) {
  switch (s) {
    case HeadingSource::Compass: return "bussola";
    case HeadingSource::Gps: return "GPS";
    case HeadingSource::GpsStale: return "GPS (antigo)";
    case HeadingSource::None: return "nenhum";
  }
  return "?";
}
