#include "debug_console.h"

#include <Arduino.h>
#include "config.h"
#include "radio_task.h"
#include "state.h"

namespace {

char gLine[96];
size_t gLen = 0;

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
  Serial.printf("[STATUS] lora=%s sos=%s amigoSOS=%s%s\n", s.loraOk ? "ok" : "ERRO",
                sosName(s.mySos), s.peerSos ? "SIM" : "nao",
                s.idConflict ? " ID-DUPLICADO" : "");
  if (s.peerRxMs)
    Serial.printf("[STATUS] ultimo pacote do amigo ha %lu s, rssi %s%d\n",
                  (unsigned long)((now - s.peerRxMs) / 1000), s.hasRssi ? "" : "? ", s.rssi);
  if (s.peerKnown)
    Serial.printf("[STATUS] amigo em %.5f, %.5f alt %ld bat %u%% (ha %lu s)\n", s.peerLat,
                  s.peerLon, (long)s.peerAlt, s.peerBat,
                  (unsigned long)((now - s.peerPosMs) / 1000));
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
  } else if (*line) {
    Serial.println("Comandos: ping | msg <texto> | pos <lat> <lon> | sos <lat> <lon> | cancel | vi | status");
  }
}

void printEvent(const RadioEvt& e) {
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
    case RadioEvtKind::IdConflict:
      Serial.println("[ERRO] outro aparelho usa o MESMO ID: as mensagens nao vao funcionar");
      break;
    case RadioEvtKind::PeerSos:
      Serial.println(e.peerSos ? "[SOS] AMIGO PEDIU SOCORRO (digite 'vi')" : "[SOS] alarme do amigo encerrado");
      break;
  }
}

}  // namespace

void consolePoll() {
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      gLine[gLen] = 0;
      handleLine(gLine);
      gLen = 0;
    } else if (gLen + 1 < sizeof gLine) {
      gLine[gLen++] = (char)ch;
    }
  }
  RadioEvt e;
  while (radioNextEvent(e)) printEvent(e);
}
