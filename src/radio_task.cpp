#include "radio_task.h"

#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "battery.h"
#include "framer.h"
#include "geo.h"
#include "gps.h"
#include "gps_logic.h"
#include "lora.h"
#include "sd_task.h"
#include "track_logic.h"
#include "state.h"

namespace {

QueueHandle_t gCmdQ = nullptr;
QueueHandle_t gEvtQ = nullptr;
Settings gSettings;

void pushEvt(const RadioEvt& e) {
  if (xQueueSend(gEvtQ, &e, 0) != pdTRUE) LOGW("Radio: fila de eventos cheia");
}

class RadioSink : public LinkSink {
 public:
  uint32_t sendFrame(const char* data, size_t len) override {
    LOGD("LoRa TX %.*s", (int)(len ? len - 1 : 0), data);
    const uint32_t t0 = millis();
    loraWrite(data, len);
    return millis() - t0;
  }

  void onIdConflict() override {
    stateLock();
    bool first = !state().idConflict;
    state().idConflict = true;
    stateUnlock();
    if (first) {
      LOGE("Outro aparelho usa o mesmo ID '%s': mude o ID em Configuracoes", gSettings.id);
      RadioEvt e{};
      e.kind = RadioEvtKind::IdConflict;
      pushEvt(e);
    }
  }

  void onPacket(const Packet& p, const Frame& f) override {
    LOGD("LoRa RX %s (rssi %d)", f.line, f.hasRssi ? f.rssi : 0);
    stateLock();
    memcpy(state().peerId, p.id, sizeof state().peerId);
    stateUnlock();
    if (p.type == PktType::POS || p.type == PktType::SOS) {
      stateLock();
      SharedState& s = state();
      s.peerBat = p.bat;
      if (posKnown(p.lat, p.lon)) {  // 0,0 = amigo sem posição: não mover o marcador
        s.peerKnown = true;
        s.peerLat = p.lat;
        s.peerLon = p.lon;
        if (p.type == PktType::POS) s.peerAlt = p.alt;
        s.peerPosMs = millis();
      }
      stateUnlock();
    }
    RadioEvt e{};
    e.kind = RadioEvtKind::Packet;
    e.pkt = p;
    e.hasRssi = f.hasRssi;
    e.rssi = f.rssi;
    pushEvt(e);
  }

  void onDelivery(uint8_t seq, Delivery d) override {
    RadioEvt e{};
    e.kind = RadioEvtKind::Delivery;
    e.seq = seq;
    e.delivery = d;
    pushEvt(e);
  }

  void onSosState(SosState s) override {
    stateLock();
    state().mySos = s;
    stateUnlock();
    RadioEvt e{};
    e.kind = RadioEvtKind::MySos;
    e.sos = s;
    pushEvt(e);
  }

  void onPeerSos(bool active) override {
    stateLock();
    state().peerSos = active;
    stateUnlock();
    RadioEvt e{};
    e.kind = RadioEvtKind::PeerSos;
    e.peerSos = active;
    pushEvt(e);
  }
};

struct MyPos {
  bool known, fix;
  double lat, lon;
  int32_t alt;
  uint8_t bat;
};

MyPos myPos() {
  stateLock();
  const SharedState& s = state();
  MyPos m{s.myPosKnown, s.gpsFix, s.lat, s.lon, (int32_t)s.altM, s.batteryPct};
  stateUnlock();
  if (!m.known) m.lat = m.lon = 0;  // convenção do protocolo: posição desconhecida
  return m;
}

bool sosManual = false;  // SOS iniciado com coordenadas digitadas: não sobrescrever

void handleCmd(Link& link, const RadioCmd& c, uint32_t now) {
  switch (c.kind) {
    case RadioCmdKind::Msg: {
      int seq = link.sendMsg(c.text, now);
      RadioEvt e{};
      if (seq < 0) {
        LOGW("Radio: MSG anterior ainda pendente");
        e.kind = RadioEvtKind::MsgBusy;
      } else {
        e.kind = RadioEvtKind::MsgQueued;
        e.seq = (uint8_t)seq;
        protoSanitize(c.text, e.pkt.text, sizeof e.pkt.text);  // o texto que de fato foi ao ar
      }
      pushEvt(e);
      break;
    }
    case RadioCmdKind::Ping:
      if (link.sendPing(now) < 0) LOGW("Radio: MSG/PING anterior ainda pendente");
      break;
    case RadioCmdKind::Pos:
      link.sendPos(c.lat, c.lon, c.alt, myPos().bat, now);
      break;
    case RadioCmdKind::SosStart: {
      sosManual = posKnown(c.lat, c.lon);
      // lat=lon=0 no comando = usar a minha posição (atual ou última conhecida).
      MyPos m = myPos();
      if (posKnown(c.lat, c.lon)) {
        link.startSos(c.lat, c.lon, m.bat, now);
      } else {
        link.startSos(m.lat, m.lon, m.bat, now);
      }
      break;
    }
    case RadioCmdKind::SosCancel:
      link.cancelSos(now);
      break;
    case RadioCmdKind::SosUpdate:
      link.updateSosPosition(c.lat, c.lon, c.bat);
      break;
    case RadioCmdKind::Seen:
      link.confirmSeen(now);
      break;
  }
}

constexpr uint32_t kLoraRetryMs = 30000;

void setLoraOk(bool ok) {
  stateLock();
  state().loraOk = ok;
  stateUnlock();
}

// Decide o ponto da trilha e só enfileira: o rádio nunca toca o SD nem o SPI.
void recordTrack(TrackPolicy& policy, uint32_t now) {
  TrackPoint p{};
  stateLock();
  const SharedState& s = state();
  bool fix = s.gpsFix;
  p.lat = s.lat;
  p.lon = s.lon;
  p.alt = s.altM;
  if (s.timeValid) {
    p.utc = s.utcTime;
    p.local = s.localTime;
  }
  stateUnlock();
  bool seg = false;
  if (!policy.due(now, fix, p.lat, p.lon, seg)) return;
  p.newSegment = seg;
  if (trackEnqueue(p)) policy.accepted(now, p.lat, p.lon);
  else LOGW("Trilha: fila cheia, ponto descartado");
}

void radioTask(void*) {
  LoraStatus st = loraBegin();
  setLoraOk(st == LoraStatus::Ok);
  uint32_t lastTry = millis();

  Framer framer;
  RadioSink sink;
  // Seq inicial aleatória: após reiniciar, o amigo não confunde pacotes novos com duplicatas.
  Link link(sink, gSettings.id, (uint8_t)(esp_random() & 0xFF), esp_random);

  gpsBegin();
  batteryBegin();
  PosScheduler posSched(esp_random);  // POS a cada 30 s + 0-3 s com fix
  TrackPolicy trackPolicy;  // ponto da trilha a cada 10 s ou 20 m
  uint32_t lastSosUpdate = 0;
  constexpr uint32_t kSosUpdateMs = 5000;

  for (;;) {
    if (st == LoraStatus::NoResponse && millis() - lastTry >= kLoraRetryMs) {
      // Módulo não respondeu ou ficou em modo AT: tenta configurar de novo.
      st = loraBegin();
      setLoraOk(st == LoraStatus::Ok);
      lastTry = millis();
    }
    // millis() é relido a cada passo: escritas podem bloquear esperando o AUX.
    loraPoll(framer, millis());
    framer.tick(millis());
    Frame f;
    while (framer.pop(f)) {
      const uint32_t now = millis();
      link.onFrame(f, now);
      // Link só marca lastPeerRxMs = now para frames válidos do amigo.
      if (link.hasPeer() && link.lastPeerRxMs() == now) {
        stateLock();
        state().peerRxMs = link.lastPeerRxMs();
        if (f.hasRssi) {
          state().hasRssi = true;
          state().rssi = f.rssi;
        }
        stateUnlock();
      }
    }
    gpsPoll(millis());
    batteryPoll(millis());
    recordTrack(trackPolicy, millis());
    {
      MyPos m = myPos();
      SosState ss = link.sosState();
      bool sosActive = ss == SosState::Sending || ss == SosState::Received;
      // Durante o SOS a posição já vai no próprio SOS: POS extra só gastaria ar e colidiria.
      if (posSched.due(millis(), m.fix) && !sosActive)
        link.sendPos(m.lat, m.lon, m.alt, m.bat, millis());
      if (sosActive && !sosManual && m.known && millis() - lastSosUpdate >= kSosUpdateMs) {
        link.updateSosPosition(m.lat, m.lon, m.bat);
        lastSosUpdate = millis();
      }
    }
    link.tick(millis());
    stateLock();
    state().sosAttempts = link.sosAttempts();
    stateUnlock();
    RadioCmd c;
    while (xQueueReceive(gCmdQ, &c, 0) == pdTRUE) handleCmd(link, c, millis());
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

void radioStart(const Settings& s) {
  gSettings = s;
  gCmdQ = xQueueCreate(8, sizeof(RadioCmd));
  gEvtQ = xQueueCreate(16, sizeof(RadioEvt));
  xTaskCreatePinnedToCore(radioTask, "radio", 8192, nullptr, 3, nullptr, 0);
}

bool radioSend(const RadioCmd& c) { return gCmdQ && xQueueSend(gCmdQ, &c, 0) == pdTRUE; }

bool radioNextEvent(RadioEvt& e) { return gEvtQ && xQueueReceive(gEvtQ, &e, 0) == pdTRUE; }
