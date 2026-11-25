#pragma once
// Task de rádio (core 0): LoRa + Framer + Link. Conversa com o resto por filas.
#include <cstdint>
#include "lora_link.h"
#include "protocol.h"
#include "storage.h"

enum class RadioCmdKind : uint8_t { Msg, Ping, Pos, SosStart, SosCancel, SosUpdate, Seen };

struct RadioCmd {
  RadioCmdKind kind;
  char text[MSG_MAX_LEN + 1];
  double lat, lon;
  int32_t alt;
  uint8_t bat;
};

enum class RadioEvtKind : uint8_t { Packet, Delivery, MySos, PeerSos, IdConflict, MsgQueued, MsgBusy };

struct RadioEvt {
  RadioEvtKind kind;
  Packet pkt;          // Packet
  bool hasRssi;        // Packet
  int16_t rssi;        // Packet
  uint8_t seq;         // Delivery, MsgQueued (pkt.text = texto enviado)
  Delivery delivery;   // Delivery
  SosState sos;        // MySos
  bool peerSos;        // PeerSos
};

void radioStart(const Settings& s);   // cria a task "radio" (core 0, prio 3, stack 8192)
bool radioSend(const RadioCmd& c);    // não bloqueia; false se a fila está cheia
bool radioNextEvent(RadioEvt& e);     // não bloqueia
