#include "ui.h"

#include <Arduino.h>
#include <atomic>
#include <cstring>
#include "config.h"
#include "debug_console.h"
#include "display.h"
#include "map_view.h"
#include "audio.h"
#include "audio_cues.h"
#include "input.h"
#include "input_logic.h"
#include "keyboard.h"
#include "msg_history.h"
#include "nav.h"
#include "oled.h"
#include "radio_task.h"
#include "sos_overlay.h"
#include "state.h"
#include "ui_format.h"
#include "ui_screens.h"

namespace {

enum class Screen : uint8_t { Menu, Friend, Messages, Detail, Keyboard, Gps, Settings, Map, Scanner };

constexpr uint32_t kLoopMs = 5;
constexpr uint32_t kMinFrameMs = 50;     // máx. 20 fps
constexpr uint32_t kRefreshMs = 1000;    // relógios e idades
constexpr uint32_t kOledMs = 500;
constexpr uint32_t kBannerMs = 5000;
constexpr uint32_t kSeenOledMs = 30000;
constexpr uint8_t kMaxStack = 3;

Settings* gSet = nullptr;
std::atomic<bool> gSosRequest{false};

// ---- Estado da UI (só a task de UI acessa) ----
Screen gStack[kMaxStack] = {Screen::Menu};
uint8_t gDepth = 1;
Cursor gCursor(6);
Cursor gMenuCursor(6);  // lembra o bloco do menu ao voltar
SosOverlay gOverlay;
WakePolicy gWake;
ButtonGestures gButton;
MsgHistory gHistory;
KeyboardModel gKeyboard;
uint32_t gDetailUid = 0;  // mensagem aberta no Detalhe (estável quando chegam outras)
RadioCmdKind gRetry[4];   // comandos de SOS que a fila do rádio recusou
uint8_t gRetryN = 0;
uint8_t gLastCountdown = 0;
bool gNewMsg = false;
char gBanner[64] = "";
uint32_t gBannerUntil = 0;
uint32_t gSeenAt = 0;
bool gSeenEver = false;
bool gDirty = true;
AudioCues gCues;
bool gMapPending = false;  // a tela Mapa ainda tem tile para carregar
bool gScreenWasOn = true;

// Configurações em edição
ui::SettingsView gSetView{};

Screen top() { return gStack[gDepth - 1]; }

uint8_t itemCount(Screen s) {
  switch (s) {
    case Screen::Menu: return 6;
    case Screen::Messages: return (uint8_t)(2 + ui::kQuickCount + gHistory.size());
    case Screen::Detail: {
      int i = gHistory.find(gDetailUid);
      if (i < 0) return 1;
      const MsgEntry& e = gHistory.at(i);
      return (e.mine && e.status == MsgStatus::Failed) ? 2 : 1;
    }
    case Screen::Keyboard: return (uint8_t)(1 + gKeyboard.keyCount());
    case Screen::Settings: return 5;
    default: return 1;  // só "Voltar"
  }
}

void push(Screen s) {
  if (gDepth < kMaxStack) gStack[gDepth++] = s;
  else gStack[gDepth - 1] = s;
  gCursor = Cursor(itemCount(s));
  if (s == Screen::Messages) gNewMsg = false;
  gDirty = true;
}

void pop() {
  if (gDepth > 1) --gDepth;
  gCursor = (top() == Screen::Menu) ? gMenuCursor : Cursor(itemCount(top()));
  gDirty = true;
}

void goHome() {
  gDepth = 1;
  gStack[0] = Screen::Menu;
  gCursor = gMenuCursor;
  gDirty = true;
}

void banner(const char* text, uint32_t now, uint32_t ms = kBannerMs) {
  strncpy(gBanner, text, sizeof gBanner - 1);
  gBanner[sizeof gBanner - 1] = 0;
  gBannerUntil = now + ms;
  gDirty = true;
}

SharedState snapshot() {
  stateLock();
  SharedState s = state();
  stateUnlock();
  return s;
}

void sendMessage(const char* text, uint32_t now) {
  if (gHistory.hasPending()) {
    banner("Aguarde... mensagem anterior pendente", now, 2000);
    return;
  }
  RadioCmd c{};
  c.kind = RadioCmdKind::Msg;
  strncpy(c.text, text, MSG_MAX_LEN);
  if (!radioSend(c)) banner("Radio ocupado, tente de novo", now, 2000);
}

bool trySendCmd(RadioCmdKind k) {
  RadioCmd c{};
  c.kind = k;  // SosStart com lat=lon=0: o rádio usa a minha posição
  return radioSend(c);
}

// Comandos de SOS nunca se perdem: se a fila estiver cheia, tenta de novo a cada volta.
void sendCmd(RadioCmdKind k) {
  if (gRetryN == 0 && trySendCmd(k)) return;
  if (gRetryN < 4) gRetry[gRetryN++] = k;
  else LOGE("UI: fila de reenvio de comandos SOS cheia");
}

void flushRetries() {
  while (gRetryN && trySendCmd(gRetry[0])) {
    for (uint8_t i = 1; i < gRetryN; ++i) gRetry[i - 1] = gRetry[i];
    --gRetryN;
  }
}

// Nova entrada no histórico: o cursor acompanha a mensagem que estava destacada.
void onHistoryAdded() {
  if (top() != Screen::Messages) return;
  gCursor.setCount(itemCount(Screen::Messages));
  if (gCursor.index() >= 2 + ui::kQuickCount) gCursor.move(1);
}

void applyBrightness(bool on) { displaySetBrightness(on ? gSetView.brightness : 0); }

void saveSettings() {
  gSet->brightness = brightnessRawFromPct(gSetView.brightness);
  gSet->volume = gSetView.volume;
  settingsSave(*gSet);
}

char nextIdChar(char c, int8_t d) {
  static const char kSet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  const int n = sizeof kSet - 1;
  const char* p = strchr(kSet, c);
  int i = p ? (int)(p - kSet) : 0;
  i = ((i + d) % n + n) % n;
  return kSet[i];
}

// ---- Entrada nas telas (sem overlay) ----

void onSettingsInput(int8_t d, Gesture g, uint32_t now) {
  ui::SettingsView& v = gSetView;
  if (v.editing == 1) {
    if (d) v.id[v.idPos] = nextIdChar(v.id[v.idPos], d);
    if (g == Gesture::Click) {
      if (v.idPos == 0) {
        v.idPos = 1;
      } else {
        v.editing = -1;
        if (strcmp(v.id, gSet->id) != 0) {
          memcpy(gSet->id, v.id, sizeof gSet->id);
          saveSettings();
          v.restarting = true;
          gDirty = true;
          LOGI("ID alterado para %s: reiniciando", v.id);
        }
      }
    }
    gDirty = true;
    return;
  }
  if (v.editing == 2 || v.editing == 3) {
    uint8_t& val = v.editing == 2 ? v.brightness : v.volume;
    int nv = (int)val + d * 10;
    int lo = v.editing == 2 ? 10 : 0;
    if (nv < lo) nv = lo;
    if (nv > 100) nv = 100;
    val = (uint8_t)nv;
    if (v.editing == 2) applyBrightness(true);
    if (v.editing == 3 && d) {
      audioSetVolume(v.volume);
      audioPlay(Pattern::Click);  // prévia no volume novo
    }
    if (g == Gesture::Click) {
      v.editing = -1;
      saveSettings();
    }
    gDirty = true;
    return;
  }
  if (d) gCursor.move(d);
  if (g == Gesture::Click) {
    switch (gCursor.index()) {
      case 0: pop(); return;
      case 1:
        v.editing = 1;
        v.idPos = 0;
        if (!v.id[1]) v.id[1] = 'A';
        break;
      case 2: v.editing = 2; break;
      case 3: v.editing = 3; break;
      default: break;  // canal: só leitura
    }
  }
  (void)now;
  gDirty = true;
}

void onScreenInput(int8_t d, Gesture g, uint32_t now) {
  Screen s = top();
  if (s == Screen::Map) {  // tela cheia, sem cursor: giro = zoom, clique = voltar
    if (d) {
      mapZoom(d);
      gDirty = true;
    }
    if (g == Gesture::Click) pop();
    return;
  }
  if (s == Screen::Settings) {
    onSettingsInput(d, g, now);
    return;
  }
  if (d) {
    gCursor.setCount(itemCount(s));
    gCursor.move(d);
    if (s == Screen::Menu) gMenuCursor = gCursor;
    gDirty = true;
  }
  if (g != Gesture::Click) return;
  uint8_t i = gCursor.index();
  switch (s) {
    case Screen::Menu: {
      static const Screen kTargets[6] = {Screen::Friend, Screen::Messages, Screen::Map,
                                         Screen::Gps,    Screen::Scanner,  Screen::Settings};
      gMenuCursor = gCursor;
      if (kTargets[i] == Screen::Settings) {
        memcpy(gSetView.id, gSet->id, sizeof gSetView.id);
        gSetView.brightness = brightnessPctFromRaw(gSet->brightness);
        gSetView.volume = (uint8_t)((gSet->volume + 5) / 10 * 10);
        gSetView.editing = -1;
        gSetView.restarting = false;
      }
      push(kTargets[i]);
      break;
    }
    case Screen::Messages:
      if (i == 0) pop();
      else if (i == 1) {
        gKeyboard.clear();
        push(Screen::Keyboard);
      } else if (i < 2 + ui::kQuickCount) {
        sendMessage(ui::kQuick[i - 2], now);
      } else {
        gDetailUid = gHistory.at(i - 2 - ui::kQuickCount).uid;
        push(Screen::Detail);
      }
      break;
    case Screen::Detail: {
      int k = gHistory.find(gDetailUid);
      if (i != 0 && k >= 0) sendMessage(gHistory.at(k).text, now);  // reenviar
      pop();
      break;
    }
    case Screen::Keyboard:
      if (i == 0) pop();
      else if (gKeyboard.press(i - 1)) {
        sendMessage(gKeyboard.text(), now);
        gKeyboard.clear();
        pop();
      }
      gDirty = true;
      break;
    default:
      pop();  // telas só com "Voltar"
      break;
  }
}

void handleAction(SosAction a, uint32_t now) {
  switch (a) {
    case SosAction::StartSos: sendCmd(RadioCmdKind::SosStart); break;
    case SosAction::CancelSos: sendCmd(RadioCmdKind::SosCancel); break;
    case SosAction::SendSeen: sendCmd(RadioCmdKind::Seen); break;
    case SosAction::OpenFriend:
      goHome();
      push(Screen::Friend);
      break;
    default: break;
  }
  if (a != SosAction::None) {
    gWake.wake(now);
    gDirty = true;
  }
}

void onRadioEvent(const RadioEvt& e, const SharedState& s, uint32_t now) {
#if DEBUG_LOG
  consolePrintEvent(e);
#endif
  switch (e.kind) {
    case RadioEvtKind::Packet:
      if (e.pkt.type == PktType::MSG) {
        audioPlay(Pattern::Message);
        gHistory.addReceived(e.pkt.id, e.pkt.text, s.timeValid, s.localTime.hour,
                             s.localTime.minute);
        onHistoryAdded();
        char b[64];
        snprintf(b, sizeof b, "%s: %s", e.pkt.id, e.pkt.text);
        banner(b, now);
        if (top() != Screen::Messages) gNewMsg = true;
        gWake.wake(now);
      }
      break;
    case RadioEvtKind::MsgQueued:
      gHistory.addSent(e.seq, gSet->id, e.pkt.text, s.timeValid, s.localTime.hour,
                       s.localTime.minute);
      onHistoryAdded();
      break;
    case RadioEvtKind::MsgBusy:
      banner("Aguarde... mensagem anterior pendente", now, 2000);
      break;
    case RadioEvtKind::Delivery:
      gHistory.onDelivery(e.seq, e.delivery == Delivery::Delivered);
      break;
    case RadioEvtKind::MySos:
      if (e.sos == SosState::Seen) {
        gSeenAt = now;
        gSeenEver = true;
      }
      gWake.wake(now);
      break;
    case RadioEvtKind::PeerSos:
      if (e.peerSos) gWake.wake(now);
      break;
    case RadioEvtKind::IdConflict:
      break;
  }
  gDirty = true;
}

void draw(const SharedState& s, uint32_t now) {
  TFT_eSprite& f = frame();
  Overlay o = gOverlay.top();
  gMapPending = false;  // só a tela Mapa visível pede quadros extras para carregar tiles
  if (o != Overlay::None) {
    ui::drawOverlay(f, o, s, gOverlay.countdownLeft(now), now);
    if (gOverlay.showMineStrip()) {
      ui::drawMineStrip(f, gOverlay.counting(), gOverlay.countdownLeft(now), s.mySos);
    }
  } else {
    uint8_t c = gCursor.index();
    switch (top()) {
      case Screen::Menu: ui::drawMenu(f, c); break;
      case Screen::Friend: ui::drawFriend(f, s, now); break;
      case Screen::Messages: ui::drawMessages(f, gHistory, c, now); break;
      case Screen::Detail: {
        int k = gHistory.find(gDetailUid);
        if (k < 0) {
          ui::drawSoon(f, "Mensagem", "saiu do historico");
        } else {
          const MsgEntry& e = gHistory.at(k);
          ui::drawDetail(f, e, c, e.mine && e.status == MsgStatus::Failed);
        }
        break;
      }
      case Screen::Keyboard: ui::drawKeyboard(f, gKeyboard, c); break;
      case Screen::Gps: ui::drawGps(f, s); break;
      case Screen::Settings:
        gSetView.idConflict = s.idConflict;
        ui::drawSettings(f, gSetView, c);
        break;
      case Screen::Map: gMapPending = mapDraw(f, s); break;
      case Screen::Scanner: ui::drawSoon(f, "Scanner", "Em breve (etapa 5)"); break;
    }
  }
  // Banner nunca cobre um overlay de SOS.
  if (o == Overlay::None && gBanner[0] && (int32_t)(now - gBannerUntil) < 0)
    ui::drawBanner(f, gBanner);
  displayPush();
}

void updateOled(const SharedState& s, uint32_t now, bool blink) {
  OledInput in{};
  in.timeValid = s.timeValid;
  in.hour = s.localTime.hour;
  in.minute = s.localTime.minute;
  in.sats = s.sats;
  in.batPct = s.batteryPct;
  in.batLow = s.batteryLow;
  memcpy(in.myId, gSet->id, sizeof in.myId);
  in.peerEver = s.peerRxMs != 0;
  in.peerAgeMs = now - s.peerRxMs;
  in.hasRssi = s.hasRssi;
  in.rssi = s.rssi;
  in.newMsg = gNewMsg;
  in.loraOk = s.loraOk;
  in.idConflict = s.idConflict;
  in.peerSos = s.peerSos;
  in.mySos = s.mySos;
  in.seenRecent = gSeenEver && now - gSeenAt < kSeenOledMs;
  in.blinkPhase = blink;
  char l1[24], l2[24];
  oledLines(in, l1, l2, sizeof l1);
  oledShow(l1, l2);
}

void uiTask(void*) {
  if (!displayBegin()) LOGE("UI: display indisponivel");
  mapInit();
  oledBegin();
  inputBegin();
  audioBegin(gSet->volume);
  gSetView.brightness = brightnessPctFromRaw(gSet->brightness);
  applyBrightness(true);
  uint32_t lastFrame = 0, lastOled = 0, lastRefresh = 0;
  bool blink = false;
  bool restartPending = false;
  uint32_t restartAt = 0;

  for (;;) {
    const uint32_t now = millis();
    SharedState s = snapshot();

    // Eventos do rádio
    RadioEvt e;
    while (radioNextEvent(e)) onRadioEvent(e, s, now);

    // Entrada
    int8_t d = inputTakeDetents();
    Gesture g = gButton.update(inputButtonPressed(), now);
    if (d || g != Gesture::None) {
      gDirty = true;
      if (!gWake.onInput(g, d != 0, now)) {
        d = 0;
        g = Gesture::None;  // só acordou a tela
      }
    }
    audioPlay(AudioCues::forGesture(g));
    if (gSosRequest.exchange(false)) {
      gOverlay.requestCountdown(now);
      gWake.wake(now);
    }
    gOverlay.sync(s.mySos, s.peerSos, now);
    if (g == Gesture::Hold2s && !gOverlay.active()) {
      gOverlay.requestCountdown(now);
    } else if (gOverlay.active()) {
      handleAction(gOverlay.onGesture(g, now), now);
    } else if (d || g != Gesture::None) {
      onScreenInput(d, g, now);
    }
    for (SosAction a = gOverlay.tick(now); a != SosAction::None; a = gOverlay.tick(now))
      handleAction(a, now);
    {
      // Sons do SOS: roda com a tela acesa ou apagada.
      CueOut o = gCues.update({gOverlay.top(), gOverlay.counting(), gOverlay.countdownLeft(now),
                               s.mySos});
      static uint32_t lastAlarm = 0;
      for (uint8_t i = 0; i < o.n; ++i) {
        // O alarme é reafirmado a cada iteração; manda no máximo 2x/s para não lotar a fila.
        if (o.play[i] == Pattern::PeerAlarm) {
          if (lastAlarm && now - lastAlarm < 500) continue;
          lastAlarm = now ? now : 1;
        }
        audioPlay(o.play[i]);
      }
      if (o.stopAlarm) lastAlarm = 0;
      if (o.stopAlarm) audioStop(Pattern::PeerAlarm);
    }
    flushRetries();

    // Tela acesa/apagada
    gWake.tick(now, gOverlay.active());
    bool on = gWake.screenOn();
    if (on != gScreenWasOn) {
      applyBrightness(on);
      gScreenWasOn = on;
      gDirty = true;
    }

    // Reinício após trocar o ID (mostra "Reiniciando..." por 1 s)
    if (gSetView.restarting && !restartPending) {
      restartPending = true;
      restartAt = now + 1000;
    }
    if (restartPending && (int32_t)(now - restartAt) >= 0) ESP.restart();

    // Quadro
    if (gBanner[0] && (int32_t)(now - gBannerUntil) >= 0) {
      gBanner[0] = 0;
      gDirty = true;
    }
    // Contagem: redesenha só quando o número muda (o quadro inteiro custa ~30 ms de SPI).
    if (gOverlay.counting() && gOverlay.countdownLeft(now) != gLastCountdown) {
      gLastCountdown = gOverlay.countdownLeft(now);
      gDirty = true;
    }
    if (top() == Screen::Map && gMapPending) gDirty = true;  // carrega o próximo tile
    if (now - lastRefresh >= kRefreshMs) {
      gDirty = true;
      lastRefresh = now;
    }
    if (on && gDirty && now - lastFrame >= kMinFrameMs) {
      draw(s, now);
      gDirty = false;
      lastFrame = now;
    }

    // OLED
    if (now - lastOled >= kOledMs) {
      blink = !blink;
      updateOled(s, now, blink);
      lastOled = now;
    }
    vTaskDelay(pdMS_TO_TICKS(kLoopMs));
  }
}

}  // namespace

void uiStart(Settings& s) {
  gSet = &s;
  xTaskCreatePinnedToCore(uiTask, "ui", 8192, nullptr, 2, nullptr, 1);
}

void uiRequestSosCountdown() { gSosRequest.store(true); }
