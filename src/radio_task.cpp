#include "radio_task.h"

#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "framer.h"
#include "lora.h"
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
    if (p.type == PktType::POS || p.type == PktType::SOS) {
      stateLock();
      SharedState& s = state();
      s.peerKnown = true;
      s.peerLat = p.lat;
      s.peerLon = p.lon;
      if (p.type == PktType::POS) s.peerAlt = p.alt;
      s.peerBat = p.bat;
      s.peerPosMs = millis();
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

void handleCmd(Link& link, const RadioCmd& c, uint32_t now) {
  switch (c.kind) {
    case RadioCmdKind::Msg: {
      int seq = link.sendMsg(c.text, now);
      if (seq < 0) LOGW("Radio: MSG anterior ainda pendente");
      break;
    }
    case RadioCmdKind::Ping:
      if (link.sendPing(now) < 0) LOGW("Radio: MSG/PING anterior ainda pendente");
      break;
    case RadioCmdKind::Pos:
      link.sendPos(c.lat, c.lon, c.alt, c.bat, now);
      break;
    case RadioCmdKind::SosStart:
      link.startSos(c.lat, c.lon, c.bat, now);
      break;
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

void radioTask(void*) {
  LoraStatus st = loraBegin();
  setLoraOk(st == LoraStatus::Ok);
  uint32_t lastTry = millis();

  Framer framer;
  RadioSink sink;
  // Seq inicial aleatória: após reiniciar, o amigo não confunde pacotes novos com duplicatas.
  Link link(sink, gSettings.id, (uint8_t)(esp_random() & 0xFF), esp_random);

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
    link.tick(millis());
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
  xTaskCreatePinnedToCore(radioTask, "radio", 6144, nullptr, 3, nullptr, 0);
}

bool radioSend(const RadioCmd& c) { return gCmdQ && xQueueSend(gCmdQ, &c, 0) == pdTRUE; }

bool radioNextEvent(RadioEvt& e) { return gEvtQ && xQueueReceive(gEvtQ, &e, 0) == pdTRUE; }
