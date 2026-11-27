#include "debug_console.h"

#include <Arduino.h>
#include "config.h"
#include "geo.h"
#include "imu.h"
#include "radio_task.h"
#include "state.h"
#include "ui.h"

namespace {

char gLine[96];
size_t gLen = 0;
bool gImuAxesActive = false;
uint32_t gImuAxesLastMs = 0;

const char* sosName(SosState s) {
  switch (s) {
    case SosState::Idle: return "INATIVO";
    case SosState::Sending: return "ENVIANDO";
    case SosState::Received: return "RECEBIDO";
    case SosState::Seen: return "VISTO";
  }
  return "?";
}

void printStatus() {
  stateLock();
  SharedState s = state();
  stateUnlock();
  uint32_t now = millis();
  Serial.printf("[STATUS] lora=%s imu=%s(0x%02X) mag=%s bme=%s(0x%02X) cc1101=%s sos=%s amigoSOS=%s%s\n",
                s.loraOk ? "ok" : "ERRO", s.imuOk ? "ok" : "ERRO", s.imuAddr,
                s.compassOk ? (s.compassCalibrated ? "cal" : "nao-cal") : "ERRO",
                s.bmeOk ? "ok" : "ERRO", s.bmeAddr,
                s.cc1101Ok ? "ok" : "ERRO", sosName(s.mySos), s.peerSos ? "SIM" : "nao",
                s.idConflict ? " ID-DUPLICADO" : "");
  if (s.peerRxMs)
    Serial.printf("[STATUS] ultimo pacote do amigo ha %lu s, rssi %s%d\n",
                  (unsigned long)((now - s.peerRxMs) / 1000), s.hasRssi ? "" : "? ", s.rssi);
  if (s.peerKnown)
    Serial.printf("[STATUS] amigo em %.5f, %.5f alt %ld bat %u%% (ha %lu s)\n", s.peerLat,
                  s.peerLon, (long)s.peerAlt, s.peerBat,
                  (unsigned long)((now - s.peerPosMs) / 1000));
  if (s.peerKnown && s.myPosKnown) {
    double d = distanceM(s.lat, s.lon, s.peerLat, s.peerLon);
    double b = bearingDeg(s.lat, s.lon, s.peerLat, s.peerLon);
    if (s.headingKnown) {
      Serial.printf("[STATUS] amigo a %.0f m, rumo %.0f graus (%+.0f em relacao ao meu rumo [%s])\n",
                    d, b, relativeBearingDeg(b, s.headingDeg), headingSourceName(s.headingSource));
    } else {
      Serial.printf("[STATUS] amigo a %.0f m, rumo %.0f graus\n", d, b);
    }
  }
}

void printSensors() {
  stateLock();
  SharedState s = state();
  stateUnlock();
  Serial.printf("[SENSORS] ICM-20948: %s (addr=0x%02X) mount=%s mag=%s cal=%s decl=%+.1f deg\n",
                s.imuOk ? "ok" : "AUSENTE", s.imuAddr, imuMountName(s.imuMount),
                s.compassOk ? "ok" : "AUSENTE", s.compassCalibrated ? "sim" : "nao",
                s.magDeclination);
  if (s.compassOk) {
    Serial.printf("[SENSORS] bussola: %.1f graus\n", s.compassDeg);
  }
  Serial.printf("[SENSORS] BME280: %s (addr=0x%02X)\n", s.bmeOk ? "ok" : "AUSENTE", s.bmeAddr);
  if (s.bmeOk) {
    Serial.printf("[SENSORS] temp=%.1f C umid=%.1f%% press=%.1f hPa tend=%s", s.tempC,
                  s.humidityPct, s.pressureHpa, pressureTrendSymbol(s.pressureTrend));
    if (s.hasPressureDelta3h) Serial.printf(" (d3h=%+.1f hPa)", s.pressureDelta3h);
    if (s.pressureRapidDrop) Serial.print(" [ALERTA: queda rapida]");
    Serial.println();
    if (s.baroAltValid) {
      Serial.printf("[SENSORS] alt baro: %.1f m (%s)\n", s.baroAltM,
                    s.baroCalibrated ? "calibrada pelo GPS" : "pressao padrao");
    }
  }
  if (s.headingKnown) {
    Serial.printf("[SENSORS] rumo atual: %.1f graus (fonte: %s)\n", s.headingDeg,
                  headingSourceName(s.headingSource));
  } else {
    Serial.println("[SENSORS] rumo atual: -- (fonte: nenhum)");
  }
}

void printGps() {
  stateLock();
  SharedState s = state();
  stateUnlock();
  Serial.printf("[GPS] fix=%s sats=%u\n", s.gpsFix ? "sim" : "nao", s.sats);
  if (s.myPosKnown)
    Serial.printf("[GPS] %s %.5f, %.5f alt %.0f m vel %.1f km/h\n",
                  s.gpsFix ? "posicao" : "ultima posicao", s.lat, s.lon, s.altM, s.speedKmh);
  if (s.courseKnown)
    Serial.printf("[GPS] rumo %.0f graus%s\n", s.courseDeg, s.courseStale ? " (antigo)" : "");
  if (s.timeValid) {
    const DateTime& t = s.localTime;
    Serial.printf("[GPS] %02u/%02u/%04u %02u:%02u:%02u (UTC-3)\n", t.day, t.month, t.year, t.hour,
                  t.minute, t.second);
  }
  Serial.printf("[BAT] %u%% (%lu mV)%s\n", s.batteryPct, (unsigned long)s.batteryMv,
                s.batteryLow ? " BAIXA" : "");
}

void send(const RadioCmd& c) {
  if (!radioSend(c)) Serial.println("[ERRO] fila do radio cheia");
}

void handleLine(char* line) {
  RadioCmd c{};
  double lat, lon;
  if (strcmp(line, "ping") == 0) {
    c.kind = RadioCmdKind::Ping;
    send(c);
  } else if (strncmp(line, "msg ", 4) == 0) {
    c.kind = RadioCmdKind::Msg;
    strncpy(c.text, line + 4, MSG_MAX_LEN);
    send(c);
  } else if (sscanf(line, "pos %lf %lf", &lat, &lon) == 2) {
    c.kind = RadioCmdKind::Pos;
    c.lat = lat;
    c.lon = lon;
    c.bat = 100;
    send(c);
  } else if (strcmp(line, "sos") == 0) {
    c.kind = RadioCmdKind::SosStart;  // lat=lon=0: usa a minha posição
    send(c);
  } else if (strcmp(line, "gps") == 0) {
    printGps();
  } else if (sscanf(line, "sos %lf %lf", &lat, &lon) == 2) {
    c.kind = RadioCmdKind::SosStart;
    c.lat = lat;
    c.lon = lon;
    c.bat = 100;
    send(c);
  } else if (strcmp(line, "cancel") == 0) {
    c.kind = RadioCmdKind::SosCancel;
    send(c);
  } else if (strcmp(line, "vi") == 0) {
    c.kind = RadioCmdKind::Seen;
    send(c);
  } else if (strcmp(line, "status") == 0) {
    printStatus();
  } else if (strcmp(line, "sensors") == 0) {
    printSensors();
  } else if (strcmp(line, "imu axes") == 0) {
    stateLock();
    const SharedState s = state();
    stateUnlock();
    if (!s.imuOk) {
      Serial.println("[IMU] ICM-20948 ausente");
    } else {
      gImuAxesActive = true;
      gImuAxesLastMs = 0;
      imuSetMonitorActive(true);
      Serial.printf("[IMU] Monitor de eixos (mount=%s). Tela p/ cima -> az~+1g; topo p/ baixo -> ax~-1g. Enter para parar.\n",
                    imuMountName(s.imuMount));
    }
  } else if (strcmp(line, "fall") == 0) {
    Serial.println("[IMU] simulando queda (contagem SOS)");
    uiRequestSosCountdown();
  } else if (*line) {
    Serial.println("Comandos: ping | msg <texto> | pos <lat> <lon> | sos [<lat> <lon>] | cancel | vi | gps | sensors | imu axes | fall | status");
  }
}

}  // namespace

void consolePoll() {
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      gLine[gLen] = 0;
      if (gImuAxesActive) {
        gImuAxesActive = false;
        imuSetMonitorActive(false);
        Serial.println("[IMU] Monitor de eixos encerrado.");
      } else {
        handleLine(gLine);
      }
      gLen = 0;
    } else if (gLen + 1 < sizeof gLine) {
      gLine[gLen++] = (char)ch;
    }
  }
  if (gImuAxesActive) {
    const uint32_t now = millis();
    if (gImuAxesLastMs == 0 || (uint32_t)(now - gImuAxesLastMs) >= 1000) {
      gImuAxesLastMs = now;
      stateLock();
      const SharedState s = state();
      stateUnlock();
      if (s.compassOk) {
        Serial.printf("[IMU] %s | accel=(%+.2f, %+.2f, %+.2f) g | rumo=%.1f deg (%s)\n",
                      imuMountName(s.imuMount), s.accelX, s.accelY, s.accelZ, s.compassDeg,
                      s.compassCalibrated ? "cal" : "nao-cal");
      } else {
        Serial.printf("[IMU] %s | accel=(%+.2f, %+.2f, %+.2f) g | rumo=--\n",
                      imuMountName(s.imuMount), s.accelX, s.accelY, s.accelZ);
      }
    }
  }
}

void consolePrintEvent(const RadioEvt& e) {
  switch (e.kind) {
    case RadioEvtKind::Packet: {
      const Packet& p = e.pkt;
      char rssi[16] = "";
      if (e.hasRssi) snprintf(rssi, sizeof rssi, " (rssi %d)", e.rssi);
      switch (p.type) {
        case PktType::MSG: Serial.printf("[RX] MSG de %s: %s%s\n", p.id, p.text, rssi); break;
        case PktType::POS:
          Serial.printf("[RX] POS de %s: %.5f, %.5f alt %ld bat %u%%%s\n", p.id, p.lat, p.lon,
                        (long)p.alt, p.bat, rssi);
          break;
        case PktType::SOS:
          Serial.printf("[RX] SOS de %s: %.5f, %.5f bat %u%%%s\n", p.id, p.lat, p.lon, p.bat, rssi);
          break;
        default: Serial.printf("[RX] %s de %s%s\n", protoTypeName(p.type), p.id, rssi); break;
      }
      break;
    }
    case RadioEvtKind::Delivery:
      Serial.printf("[TX] seq %u %s\n", e.seq, e.delivery == Delivery::Delivered ? "ENTREGUE" : "FALHOU");
      break;
    case RadioEvtKind::MySos:
      Serial.printf("[SOS] meu: %s\n", sosName(e.sos));
      break;
    case RadioEvtKind::MsgQueued:
      Serial.printf("[TX] MSG seq %u na fila: %s\n", e.seq, e.pkt.text);
      break;
    case RadioEvtKind::MsgBusy:
      Serial.println("[TX] aguarde: mensagem anterior ainda pendente");
      break;
    case RadioEvtKind::IdConflict:
      Serial.println("[ERRO] outro aparelho usa o MESMO ID: as mensagens nao vao funcionar");
      break;
    case RadioEvtKind::PeerSos:
      Serial.println(e.peerSos ? "[SOS] AMIGO PEDIU SOCORRO (digite 'vi')" : "[SOS] alarme do amigo encerrado");
      break;
  }
}
