#include "geo.h"

#include <cmath>

namespace {
constexpr double kEarthR = 6371000.0;
constexpr double kPi = 3.14159265358979323846;
double rad(double d) { return d * kPi / 180.0; }
double deg(double r) { return r * 180.0 / kPi; }
}  // namespace

double distanceM(double lat1, double lon1, double lat2, double lon2) {
  double p1 = rad(lat1), p2 = rad(lat2);
  double dp = p2 - p1, dl = rad(lon2 - lon1);
  double h = std::sin(dp / 2) * std::sin(dp / 2) +
             std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
  if (h > 1.0) h = 1.0;
  return 2 * kEarthR * std::asin(std::sqrt(h));
}

double bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = rad(lat1), p2 = rad(lat2), dl = rad(lon2 - lon1);
  double y = std::sin(dl) * std::cos(p2);
  double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
  double b = std::fmod(deg(std::atan2(y, x)) + 360.0, 360.0);
  return b >= 360.0 ? 0.0 : b;
}

double relativeBearingDeg(double targetDeg, double headingDeg) {
  double r = std::fmod(targetDeg - headingDeg + 180.0, 360.0);
  if (r < 0) r += 360.0;
  return r - 180.0;
}

bool posKnown(double lat, double lon) { return std::fabs(lat) >= 1e-6 || std::fabs(lon) >= 1e-6; }
