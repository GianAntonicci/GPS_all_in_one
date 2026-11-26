#include "ui_format.h"

#include <cmath>
#include <cstdio>

void fmtDistance(double m, char* out, size_t cap) {
  if (m < 999.5) {
    snprintf(out, cap, "%d m", (int)std::lround(m < 0 ? 0 : m));
    return;
  }
  double km = m / 1000.0;
  if (km < 9.95) {
    int tenths = (int)std::lround(km * 10.0);
    snprintf(out, cap, "%d,%d km", tenths / 10, tenths % 10);
  } else {
    snprintf(out, cap, "%ld km", std::lround(km));
  }
}

void fmtAgeShort(bool known, uint32_t ms, char* out, size_t cap) {
  if (!known) {
    snprintf(out, cap, "--");
    return;
  }
  uint32_t s = ms / 1000;
  if (s < 100) snprintf(out, cap, "%lus", (unsigned long)s);
  else if (s / 60 < 100) snprintf(out, cap, "%lum", (unsigned long)(s / 60));
  else snprintf(out, cap, "%luh", (unsigned long)(s / 3600));
}

void fmtAgeLong(bool known, uint32_t ms, char* out, size_t cap) {
  if (!known) {
    snprintf(out, cap, "nunca");
    return;
  }
  uint32_t s = ms / 1000;
  if (s < 60) snprintf(out, cap, "ha %lu s", (unsigned long)s);
  else if (s < 3600) snprintf(out, cap, "ha %lu min", (unsigned long)(s / 60));
  else snprintf(out, cap, "ha %lu h", (unsigned long)(s / 3600));
}

uint8_t signalBars(bool hasRssi, int16_t rssi) {
  if (!hasRssi) return 0;
  if (rssi >= -90) return 4;
  if (rssi >= -100) return 3;
  if (rssi >= -110) return 2;
  if (rssi >= -120) return 1;
  return 0;
}

uint8_t brightnessPctFromRaw(uint8_t raw) {
  int pct = (raw * 100 + 127) / 255;
  int r = ((pct + 5) / 10) * 10;
  if (r < 10) r = 10;
  if (r > 100) r = 100;
  return (uint8_t)r;
}

uint8_t brightnessRawFromPct(uint8_t pct) {
  if (pct > 100) pct = 100;
  return (uint8_t)(pct * 255 / 100);
}

void oledLines(const OledInput& in, char* l1, char* l2, size_t cap) {
  char hm[8];
  if (in.timeValid) snprintf(hm, sizeof hm, "%02u:%02u", in.hour, in.minute);
  else snprintf(hm, sizeof hm, "--:--");
  if (in.batLow && !in.blinkPhase) snprintf(l1, cap, "%s  S%02u      ", hm, in.sats);
  else snprintf(l1, cap, "%s  S%02u  %3u%%", hm, in.sats, in.batPct);

  if (!in.loraOk) { snprintf(l2, cap, "LORA ERRO"); return; }
  if (in.idConflict) { snprintf(l2, cap, "ID REPETIDO"); return; }
  if (in.peerSos) { snprintf(l2, cap, "%s", in.blinkPhase ? "AMIGO SOS!" : ""); return; }
  if (in.mySos == SosState::Sending) { snprintf(l2, cap, "SOS ENVIANDO"); return; }
  if (in.mySos == SosState::Received) { snprintf(l2, cap, "SOS RECEBIDO"); return; }
  if (in.mySos == SosState::Seen && in.seenRecent) { snprintf(l2, cap, "SOS VISTO"); return; }

  char bars[5] = "----";
  if (in.peerEver) {
    uint8_t n = signalBars(in.hasRssi, in.rssi);
    for (uint8_t i = 0; i < 4; ++i) bars[i] = i < n ? '#' : '-';
  }
  char age[8];
  fmtAgeShort(in.peerEver, in.peerAgeMs, age, sizeof age);
  snprintf(l2, cap, "%s %s %s%s", in.myId, bars, age, (in.newMsg && in.blinkPhase) ? " *" : "");
}

void fmtTrackStatus(char* out, size_t cap, bool sdOk, bool recording, uint32_t points,
                    bool timeValid, bool fix) {
  if (!sdOk) snprintf(out, cap, "Trilha: sem cartao");
  else if (recording) snprintf(out, cap, "Trilha: gravando (%lu pts)", (unsigned long)points);
  else if (!fix) snprintf(out, cap, "Trilha: aguardando fix");
  else if (!timeValid) snprintf(out, cap, "Trilha: aguardando hora");
  else snprintf(out, cap, "Trilha: iniciando");
}
