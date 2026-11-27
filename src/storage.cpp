#include "storage.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <cmath>
#include "config.h"
#include "protocol.h"

namespace {
const char* kNs = "cfg";

bool validId(const String& id) {
  if (id.length() < 1 || id.length() > 2) return false;
  for (char c : id)
    if (!isalnum((unsigned char)c)) return false;
  return true;
}

void defaultId(char out[3]) {
  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  protoIdFromMac(mac, out);  // hash do MAC inteiro: menos chance de os dois terem o mesmo ID
}
}  // namespace

void settingsLoad(Settings& s) {
  Preferences p;
  p.begin(kNs, true);
  String id = p.getString("id", "");
  s.brightness = p.getUChar("bri", 180);
  s.volume = p.getUChar("vol", 60);
  s.magDeclination = p.getFloat("mdecl", MAG_DECLINATION_DEFAULT);
  if (!std::isfinite(s.magDeclination) || s.magDeclination < -45.0f || s.magDeclination > 45.0f) {
    s.magDeclination = MAG_DECLINATION_DEFAULT;
  }
  s.magCal.valid = p.getBool("mcal", false);
  s.magCal.offsetX = p.getFloat("mox", 0.0f);
  s.magCal.offsetY = p.getFloat("moy", 0.0f);
  s.magCal.offsetZ = p.getFloat("moz", 0.0f);
  s.magCal.scaleX = p.getFloat("msx", 1.0f);
  s.magCal.scaleY = p.getFloat("msy", 1.0f);
  s.magCal.scaleZ = p.getFloat("msz", 1.0f);
  if (!std::isfinite(s.magCal.offsetX) || !std::isfinite(s.magCal.offsetY) ||
      !std::isfinite(s.magCal.offsetZ) || !std::isfinite(s.magCal.scaleX) ||
      !std::isfinite(s.magCal.scaleY) || !std::isfinite(s.magCal.scaleZ) ||
      s.magCal.scaleX <= 0.0f || s.magCal.scaleY <= 0.0f || s.magCal.scaleZ <= 0.0f) {
    s.magCal = MagCal{};
  }
  p.end();
  if (validId(id)) {
    strncpy(s.id, id.c_str(), sizeof s.id);
    s.id[2] = 0;
  } else {
    defaultId(s.id);
  }
  LOGI("Settings: id=%s brilho=%u volume=%u decl=%.1f cal=%s", s.id, s.brightness, s.volume,
       s.magDeclination, s.magCal.valid ? "sim" : "nao");
}

void settingsSave(const Settings& s) {
  Preferences p;
  p.begin(kNs, false);
  p.putString("id", s.id);
  p.putUChar("bri", s.brightness);
  p.putUChar("vol", s.volume);
  p.putFloat("mdecl", s.magDeclination);
  p.putBool("mcal", s.magCal.valid);
  p.putFloat("mox", s.magCal.offsetX);
  p.putFloat("moy", s.magCal.offsetY);
  p.putFloat("moz", s.magCal.offsetZ);
  p.putFloat("msx", s.magCal.scaleX);
  p.putFloat("msy", s.magCal.scaleY);
  p.putFloat("msz", s.magCal.scaleZ);
  p.end();
}
