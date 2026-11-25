#include "gps.h"

#include <Arduino.h>
#include <Preferences.h>
#include <TinyGPSPlus.h>
#include "config.h"
#include "gps_logic.h"
#include "state.h"

namespace {

constexpr uint32_t kFixMaxAgeMs = 3000;
constexpr uint32_t kStateEveryMs = 200;
constexpr int kUtcOffsetHours = -3;

TinyGPSPlus gGps;
CourseTracker gCourse;
SavePolicy gSave;  // 1º fix, perda do fix, > 200 m e a cada 10 min
uint32_t gLastState = 0;

void saveLastPosition(double lat, double lon, double crs) {
  Preferences p;
  p.begin("gps", false);
  p.putDouble("lat", lat);
  p.putDouble("lon", lon);
  p.putDouble("crs", crs);
  p.putBool("ok", true);
  p.end();
}

}  // namespace

void gpsBegin() {
  // loraWrite pode segurar a task por até 3 s: a 9600 baud chegam ~3 KB nesse tempo.
  Serial1.setRxBufferSize(2048);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);

  Preferences p;
  p.begin("gps", true);
  bool ok = p.getBool("ok", false);
  double lat = p.getDouble("lat", 0), lon = p.getDouble("lon", 0), crs = p.getDouble("crs", 0);
  p.end();

  gCourse = CourseTracker(crs, ok);
  stateLock();
  SharedState& s = state();
  s.myPosKnown = ok;
  s.lat = lat;
  s.lon = lon;
  s.courseDeg = crs;
  s.courseKnown = ok;
  s.courseStale = true;
  stateUnlock();
  if (ok) LOGI("GPS: ultima posicao salva %.5f, %.5f", lat, lon);
}

void gpsPoll(uint32_t now) {
  while (Serial1.available()) gGps.encode((char)Serial1.read());

  // Uma vez por sentença RMC (velocidade nova): atualiza o rumo.
  bool fix = gGps.location.isValid() && gGps.location.age() < kFixMaxAgeMs;
  if (gGps.speed.isUpdated()) {
    gCourse.update(fix && gGps.course.isValid(), gGps.speed.kmph(), gGps.course.deg());
  }

  if (now - gLastState < kStateEveryMs) return;
  gLastState = now;

  stateLock();
  SharedState& s = state();
  s.gpsFix = fix;
  s.sats = gGps.satellites.isValid() ? (uint8_t)gGps.satellites.value() : 0;
  if (fix) {
    s.lat = gGps.location.lat();
    s.lon = gGps.location.lng();
    s.myPosKnown = true;
    s.altM = gGps.altitude.isValid() ? (float)gGps.altitude.meters() : s.altM;
    s.speedKmh = gGps.speed.isValid() ? (float)gGps.speed.kmph() : 0;
  } else {
    s.speedKmh = 0;
  }
  s.courseDeg = gCourse.course();
  s.courseKnown = gCourse.known();
  s.courseStale = gCourse.stale();
  if (gGps.date.isValid() && gGps.time.isValid() && gGps.date.year() >= 2024) {
    DateTime utc{gGps.date.year(), gGps.date.month(), gGps.date.day(),
                 gGps.time.hour(), gGps.time.minute(), gGps.time.second()};
    s.localTime = utcToLocal(utc, kUtcOffsetHours);
    s.timeValid = true;
  }
  double lat = s.lat, lon = s.lon, crs = s.courseDeg;
  bool known = s.myPosKnown;
  stateUnlock();

  if (known && gSave.due(now, fix, lat, lon)) {
    saveLastPosition(lat, lon, crs);
    LOGD("GPS: posicao salva na NVS");
  }
}
