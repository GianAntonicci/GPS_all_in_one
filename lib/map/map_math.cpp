#include "map_math.h"

#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxLat = 85.05112878;
constexpr uint8_t kMaxZoom = 19;

int32_t floorDiv(int64_t a, int64_t b) { return (int32_t)std::floor((double)a / (double)b); }
}  // namespace

PixelPos latLonToPixel(double lat, double lon, uint8_t z) {
  if (lat > kMaxLat) lat = kMaxLat;
  if (lat < -kMaxLat) lat = -kMaxLat;
  const double n = 256.0 * std::ldexp(1.0, z);
  const double s = std::sin(lat * kPi / 180.0);
  PixelPos p;
  p.x = (lon + 180.0) / 360.0 * n;
  p.y = (0.5 - std::log((1 + s) / (1 - s)) / (4 * kPi)) * n;
  return p;
}

double metersPerPixel(double lat, uint8_t z) {
  return 156543.03392 * std::cos(lat * kPi / 180.0) / std::ldexp(1.0, z);
}

void pixelToLatLon(const PixelPos& p, uint8_t z, double& lat, double& lon) {
  const double n = 256.0 * std::ldexp(1.0, z);
  lon = p.x / n * 360.0 - 180.0;
  lat = std::atan(std::sinh(kPi * (1.0 - 2.0 * p.y / n))) * 180.0 / kPi;
}

void scaleBar(double metersPerPx, uint16_t maxPx, uint32_t& meters, uint16_t& px) {
  static const uint32_t kSteps[] = {5,    10,   20,    50,    100,   200,   500,
                                    1000, 2000, 5000, 10000, 20000, 50000, 100000};
  meters = kSteps[0];
  for (uint32_t s : kSteps)
    if (metersPerPx > 0 && s / metersPerPx <= maxPx) meters = s;
  double p = metersPerPx > 0 ? meters / metersPerPx : maxPx;
  if (p > maxPx) p = maxPx;
  px = (uint16_t)std::lround(p);
}

uint8_t visibleTiles(const PixelPos& center, uint8_t z, int16_t w, int16_t h, TileRef* out,
                     uint8_t max) {
  const int64_t left = (int64_t)std::floor(center.x) - w / 2;
  const int64_t top = (int64_t)std::floor(center.y) - h / 2;
  const int32_t c0 = floorDiv(left, 256), c1 = floorDiv(left + w - 1, 256);
  const int32_t r0 = floorDiv(top, 256), r1 = floorDiv(top + h - 1, 256);
  const int64_t tiles = (int64_t)1 << z;
  uint8_t n = 0;
  for (int32_t r = r0; r <= r1; ++r) {
    if (r < 0 || r >= tiles) continue;
    for (int32_t c = c0; c <= c1; ++c) {
      if (n >= max) return n;
      int64_t x = ((c % tiles) + tiles) % tiles;
      out[n].z = z;
      out[n].x = (uint32_t)x;
      out[n].y = (uint32_t)r;
      out[n].sx = (int16_t)((int64_t)c * 256 - left);
      out[n].sy = (int16_t)((int64_t)r * 256 - top);
      ++n;
    }
  }
  return n;
}

ScreenPt toScreen(const PixelPos& p, const PixelPos& center, int16_t w, int16_t h) {
  ScreenPt s;
  s.x = (int32_t)std::lround(p.x - center.x) + w / 2;
  s.y = (int32_t)std::lround(p.y - center.y) + h / 2;
  return s;
}

bool onScreen(ScreenPt p, int16_t w, int16_t h, int16_t margin) {
  return p.x >= margin && p.x < w - margin && p.y >= margin && p.y < h - margin;
}

EdgeArrow edgeArrow(ScreenPt target, int16_t w, int16_t h, int16_t inset) {
  const double cx = w / 2, cy = h / 2;
  const double dx = target.x - cx, dy = target.y - cy;
  const double minX = inset, maxX = w - 1 - inset, minY = inset, maxY = h - 1 - inset;
  double t = 1e18;
  if (dx > 0) t = std::fmin(t, (maxX - cx) / dx);
  if (dx < 0) t = std::fmin(t, (minX - cx) / dx);
  if (dy > 0) t = std::fmin(t, (maxY - cy) / dy);
  if (dy < 0) t = std::fmin(t, (minY - cy) / dy);
  if (t > 1e17) t = 0;  // alvo no centro
  EdgeArrow a;
  double x = cx + dx * t, y = cy + dy * t;
  if (x < minX) x = minX;
  if (x > maxX) x = maxX;
  if (y < minY) y = minY;
  if (y > maxY) y = maxY;
  a.x = (int16_t)std::lround(x);
  a.y = (int16_t)std::lround(y);
  double ang = std::atan2(dx, -dy) * 180.0 / kPi;
  if (ang < 0) ang += 360.0;
  if (ang >= 360.0) ang -= 360.0;
  a.angleDeg = ang;
  return a;
}

void ZoomLevels::add(uint8_t z) {
  if (z <= kMaxZoom) mask_ |= (1u << z);
}

bool ZoomLevels::has(uint8_t z) const { return z <= kMaxZoom && (mask_ & (1u << z)); }

uint8_t ZoomLevels::count() const {
  uint8_t n = 0;
  for (uint8_t z = 0; z <= kMaxZoom; ++z)
    if (has(z)) ++n;
  return n;
}

uint8_t ZoomLevels::initial(uint8_t pref) const {
  for (int z = pref > kMaxZoom ? kMaxZoom : pref; z >= 0; --z)
    if (has((uint8_t)z)) return (uint8_t)z;
  for (uint8_t z = 0; z <= kMaxZoom; ++z)
    if (has(z)) return z;
  return 0;
}

uint8_t ZoomLevels::step(uint8_t cur, int8_t dir) const {
  if (dir > 0) {
    for (int z = cur + 1; z <= kMaxZoom; ++z)
      if (has((uint8_t)z)) return (uint8_t)z;
  } else if (dir < 0) {
    for (int z = (int)cur - 1; z >= 0; --z)
      if (has((uint8_t)z)) return (uint8_t)z;
  }
  return cur;
}
