#include "storage.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_mac.h>
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
  p.end();
  if (validId(id)) {
    strncpy(s.id, id.c_str(), sizeof s.id);
    s.id[2] = 0;
  } else {
    defaultId(s.id);
  }
  LOGI("Settings: id=%s brilho=%u volume=%u", s.id, s.brightness, s.volume);
}

void settingsSave(const Settings& s) {
  Preferences p;
  p.begin(kNs, false);
  p.putString("id", s.id);
  p.putUChar("bri", s.brightness);
  p.putUChar("vol", s.volume);
  p.end();
}
