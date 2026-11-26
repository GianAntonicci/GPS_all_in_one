#include "gps_logic.h"

#include "geo.h"

namespace {
bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (m == 2 && isLeap(y)) ? 29 : d[m - 1];
}
}  // namespace

DateTime utcToLocal(const DateTime& utc, int offsetHours) {
  int y = utc.year, mo = utc.month, d = utc.day, h = utc.hour + offsetHours;
  while (h < 0) {
    h += 24;
    if (--d < 1) {
      if (--mo < 1) {
        mo = 12;
        --y;
      }
      d = daysInMonth(y, mo);
    }
  }
  while (h >= 24) {
    h -= 24;
    if (++d > daysInMonth(y, mo)) {
      d = 1;
      if (++mo > 12) {
        mo = 1;
        ++y;
      }
    }
  }
  return DateTime{(uint16_t)y, (uint8_t)mo, (uint8_t)d, (uint8_t)h, utc.minute, utc.second};
}

CourseTracker::CourseTracker(double initialDeg, bool known)
    : course_(initialDeg), known_(known), stale_(known) {}

void CourseTracker::update(bool valid, double speedKmh, double courseDeg) {
  if (valid && speedKmh > kMinSpeedKmh) {
    course_ = courseDeg;
    known_ = true;
    stale_ = false;
  } else {
    stale_ = true;
  }
}

PosScheduler::PosScheduler(uint32_t (*rand32)(), uint32_t periodMs, uint32_t jitterMaxMs)
    : rand_(rand32), period_(periodMs), jitterMax_(jitterMaxMs) {}

void PosScheduler::schedule(uint32_t now) {
  uint32_t j = jitterMax_ ? rand_() % (jitterMax_ + 1) : 0;
  next_ = now + period_ + j;
}

bool PosScheduler::due(uint32_t now, bool hasFix) {
  if (!hasFix) {
    hadFix_ = false;
    return false;
  }
  hadFix_ = true;
  // Fix que oscila não gera rajada: só envia quando chega o horário agendado
  // (ou na hora, se nunca enviou ou se o horário já passou).
  if (!sentOnce_ || (int32_t)(now - next_) >= 0) {
    sentOnce_ = true;
    schedule(now);
    return true;
  }
  return false;
}

void SavePolicy::mark(uint32_t now, double lat, double lon) {
  saved_ = true;
  last_ = now;
  lat_ = lat;
  lon_ = lon;
}

bool SavePolicy::due(uint32_t now, bool hasFix, double lat, double lon) {
  if (!hasFix) {
    bool lost = hadFix_;
    hadFix_ = false;
    if (lost) mark(now, lat, lon);
    return lost;
  }
  hadFix_ = true;
  bool doSave = !saved_ || now - last_ >= kPeriodMs ||
                (now - last_ >= kMinGapMs && distanceM(lat_, lon_, lat, lon) > kMoveM);
  if (doSave) mark(now, lat, lon);
  return doSave;
}
