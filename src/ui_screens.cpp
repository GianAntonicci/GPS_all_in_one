#include "ui_screens.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include "config.h"
#include "display.h"
#include "geo.h"
#include "ui_format.h"

namespace ui {

namespace {
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t kBg = rgb(0x10, 0x14, 0x18);
constexpr uint16_t kBlock = rgb(0x24, 0x30, 0x40);
constexpr uint16_t kSel = rgb(0xFF, 0xD4, 0x00);
constexpr uint16_t kGrey = rgb(0x88, 0x88, 0x88);
constexpr uint16_t kText = rgb(0xDD, 0xDD, 0xDD);
constexpr uint16_t kCountdownBg = rgb(0x60, 0x00, 0x00);
constexpr uint16_t kSendingBg = rgb(0xB0, 0x00, 0x00);
constexpr uint16_t kReceivedBg = rgb(0xA0, 0x60, 0x00);
constexpr uint16_t kSeenBg = rgb(0x00, 0x60, 0x00);
constexpr uint16_t kGreen = rgb(0x2E, 0x6B, 0x2E);
constexpr uint16_t kRed = rgb(0xE0, 0x30, 0x30);
constexpr int16_t kRowH = 30;

// Corta `s` com "..." até caber em `maxW` pixels na fonte atual.
void fitWidth(TFT_eSprite& f, char* s, size_t cap, int16_t maxW) {
  if (f.textWidth(s) <= maxW) return;
  size_t n = strlen(s);
  while (n > 3) {
    s[--n] = 0;
    char tmp[72];
    int m = snprintf(tmp, sizeof tmp, "%s...", s);
    if (m > 0 && (size_t)m < cap && f.textWidth(tmp) <= maxW) {
      memcpy(s, tmp, (size_t)m + 1);
      return;
    }
  }
}

// Linha de lista; sel = fundo amarelo.
void row(TFT_eSprite& f, int16_t y, const char* text, bool sel, uint16_t fg = kText,
         int16_t h = kRowH) {
  f.fillRect(0, y, SCREEN_W, h, sel ? kSel : kBg);
  f.setTextFont(2);
  f.setTextSize(1);
  f.setTextColor(sel ? TFT_BLACK : fg);
  f.setTextDatum(ML_DATUM);
  f.drawString(text, 10, y + h / 2);
}

void back(TFT_eSprite& f, bool sel) { row(f, 0, "<- Voltar", sel, kGrey, 28); }

// Seta apontando para `deg` (0 = cima, horário), centro (cx, cy), tamanho r.
void arrow(TFT_eSprite& f, int16_t cx, int16_t cy, int16_t r, double deg, uint16_t color) {
  const double a = deg * M_PI / 180.0;
  auto pt = [&](double ang, double len, double side, int16_t& x, int16_t& y) {
    // `len` ao longo da direção `ang`, `side` perpendicular a ela
    x = (int16_t)std::lround(cx + len * std::sin(ang) + side * std::cos(ang));
    y = (int16_t)std::lround(cy - len * std::cos(ang) + side * std::sin(ang));
  };
  int16_t tx, ty, lx, ly, rx, ry;
  pt(a, r, 0, tx, ty);              // ponta
  pt(a, 0, -r * 0.55, lx, ly);      // base da cabeça
  pt(a, 0, r * 0.55, rx, ry);
  f.fillTriangle(tx, ty, lx, ly, rx, ry, color);
  int16_t x1, y1, x2, y2, x3, y3, x4, y4;  // haste: retângulo atrás da cabeça
  const double w = r * 0.18;
  pt(a, 0, -w, x1, y1);
  pt(a, 0, w, x2, y2);
  pt(a, -r * 0.9, w, x3, y3);
  pt(a, -r * 0.9, -w, x4, y4);
  f.fillTriangle(x1, y1, x2, y2, x3, y3, color);
  f.fillTriangle(x1, y1, x3, y3, x4, y4, color);
}

// Direção e distância até o amigo. Retorna false se faltar posição.
struct FriendVec {
  bool ok;
  double dist, arrowDeg, rel;
  bool relative, stale;
};

FriendVec friendVec(const SharedState& s) {
  FriendVec v{};
  if (!s.peerKnown || !s.myPosKnown) return v;
  v.ok = true;
  v.dist = distanceM(s.lat, s.lon, s.peerLat, s.peerLon);
  double b = bearingDeg(s.lat, s.lon, s.peerLat, s.peerLon);
  if (s.courseKnown) {
    v.relative = true;
    v.rel = relativeBearingDeg(b, s.courseDeg);
    v.arrowDeg = v.rel;
    v.stale = s.courseStale;
  } else {
    v.arrowDeg = b;  // norte para cima
  }
  return v;
}

const char* sosLabel(SosState s) {
  switch (s) {
    case SosState::Sending: return "SOS ENVIANDO";
    case SosState::Received: return "SOS RECEBIDO";
    case SosState::Seen: return "SOS VISTO";
    default: return "";
  }
}

void centered(TFT_eSprite& f, const char* t, int16_t y, uint8_t font, uint8_t size,
              uint16_t color) {
  f.setTextFont(font);
  f.setTextSize(size);
  f.setTextColor(color);
  f.setTextDatum(MC_DATUM);
  f.drawString(t, SCREEN_W / 2, y);
}
}  // namespace

uint16_t cBg() { return kBg; }
uint16_t cBlock() { return kBlock; }
uint16_t cSel() { return kSel; }

const char* const kQuick[kQuickCount] = {"Estou bem",     "Preciso de ajuda", "Onde voce esta?",
                                         "Venha ate mim", "Ok",               "Ferido"};

void drawMenu(TFT_eSprite& f, uint8_t sel) {
  static const char* const kNames[6] = {"Amigo", "Mensagens", "Mapa", "GPS", "Scanner", "Config."};
  f.fillSprite(kBg);
  const int16_t gap = 6, w = (SCREEN_W - gap * 4) / 3, h = (SCREEN_H - gap * 3) / 2;
  for (uint8_t i = 0; i < 6; ++i) {
    int16_t x = gap + (i % 3) * (w + gap), y = gap + (i / 3) * (h + gap);
    bool s = i == sel;
    uint16_t bg = s ? kSel : kBlock, fg = s ? TFT_BLACK : kText;
    f.fillRoundRect(x, y, w, h, 8, bg);
    int16_t cx = x + w / 2, cy = y + h / 2 - 12;
    switch (i) {  // ícones simples
      case 0: arrow(f, cx, cy, 22, 45, fg); break;
      case 1:
        f.drawRect(cx - 20, cy - 13, 40, 26, fg);
        f.drawLine(cx - 20, cy - 13, cx, cy + 2, fg);
        f.drawLine(cx + 20, cy - 13, cx, cy + 2, fg);
        break;
      case 2:
        for (int k = 0; k < 3; ++k) f.drawRect(cx - 21 + k * 14, cy - 15, 14, 30, fg);
        break;
      case 3:
        f.drawCircle(cx, cy, 14, fg);
        f.drawFastHLine(cx - 20, cy, 40, fg);
        f.drawFastVLine(cx, cy - 20, 40, fg);
        break;
      case 4:
        for (int k = 0; k < 5; ++k) {
          int16_t bh = 6 + k * 6;
          f.fillRect(cx - 20 + k * 9, cy + 15 - bh, 6, bh, fg);
        }
        break;
      case 5:
        f.drawCircle(cx, cy, 13, fg);
        f.fillCircle(cx, cy, 5, fg);
        for (int k = 0; k < 8; ++k) {
          double a = k * M_PI / 4;
          f.fillCircle(cx + 15 * std::cos(a), cy + 15 * std::sin(a), 3, fg);
        }
        break;
    }
    f.setTextFont(2);
    f.setTextSize(1);
    f.setTextColor(fg);
    f.setTextDatum(MC_DATUM);
    f.drawString(kNames[i], cx, y + h - 18);
  }
}

void drawFriend(TFT_eSprite& f, const SharedState& s, uint32_t now) {
  f.fillSprite(kBg);
  back(f, true);
  FriendVec v = friendVec(s);
  char buf[40];
  if (!v.ok) {
    centered(f, s.peerKnown ? "Sem minha posicao" : "Sem posicao do amigo", 110, 4, 1, kText);
  } else {
    arrow(f, 70, 115, 55, v.arrowDeg, kSel);
    f.setTextFont(2);
    f.setTextSize(1);
    f.setTextColor(kGrey);
    f.setTextDatum(MC_DATUM);
    if (!v.relative) snprintf(buf, sizeof buf, "N^ (sem rumo)");
    else snprintf(buf, sizeof buf, "%+.0f do rumo", v.rel);
    f.drawString(buf, 70, 190);
    if (v.relative && v.stale) {
      f.setTextColor(kSel);
      f.drawString("rumo antigo - ande", 70, 208);
      f.drawString("alguns passos", 70, 224);
    }
    fmtDistance(v.dist, buf, sizeof buf);
    f.setTextFont(4);
    f.setTextColor(TFT_WHITE);
    f.setTextDatum(TL_DATUM);
    f.drawString(buf, 150, 40);
  }
  f.setTextFont(2);
  f.setTextSize(1);
  f.setTextColor(kText);
  f.setTextDatum(TL_DATUM);
  int16_t y = 85;
  if (s.hasRssi) {
    uint8_t bars = signalBars(true, s.rssi);
    snprintf(buf, sizeof buf, "Sinal %d dBm", s.rssi);
    f.drawString(buf, 150, y);
    for (uint8_t i = 0; i < 4; ++i) {
      int16_t bh = 4 + i * 4;
      if (i < bars) f.fillRect(280 + i * 8, y + 16 - bh, 6, bh, kText);
      else f.drawRect(280 + i * 8, y + 16 - bh, 6, bh, kGrey);
    }
  } else {
    f.drawString("Sinal: --", 150, y);
  }
  y += 24;
  char age[20];
  fmtAgeLong(s.peerRxMs != 0, now - s.peerRxMs, age, sizeof age);
  snprintf(buf, sizeof buf, "Ult. msg %s", age);
  f.drawString(buf, 150, y);
  y += 24;
  fmtAgeLong(s.peerKnown, now - s.peerPosMs, age, sizeof age);
  snprintf(buf, sizeof buf, "Posicao %s", age);
  f.drawString(buf, 150, y);
  y += 24;
  if (s.peerKnown || s.peerRxMs) {
    snprintf(buf, sizeof buf, "Bat. amigo %u%%", s.peerBat);
    f.drawString(buf, 150, y);
  }
}

void drawGps(TFT_eSprite& f, const SharedState& s) {
  f.fillSprite(kBg);
  back(f, true);
  f.setTextFont(2);
  f.setTextSize(1);
  f.setTextColor(kText);
  f.setTextDatum(TL_DATUM);
  char b[48];
  int16_t y = 36;
  auto line = [&](const char* t) {
    f.drawString(t, 10, y);
    y += 22;
  };
  snprintf(b, sizeof b, "Fix: %s   Satelites: %u", s.gpsFix ? "sim" : "nao", s.sats);
  line(b);
  if (s.myPosKnown) {
    snprintf(b, sizeof b, "%s", s.gpsFix ? "Posicao:" : "Ultima posicao:");
    line(b);
    snprintf(b, sizeof b, "  %.5f, %.5f", s.lat, s.lon);
    line(b);
  } else {
    line("Posicao: desconhecida");
    y += 22;
  }
  snprintf(b, sizeof b, "Altitude %.0f m   Vel. %.1f km/h", s.altM, s.speedKmh);
  line(b);
  if (s.courseKnown) snprintf(b, sizeof b, "Rumo %.0f graus%s", s.courseDeg, s.courseStale ? " (antigo)" : "");
  else snprintf(b, sizeof b, "Rumo: --");
  line(b);
  if (s.timeValid) {
    const DateTime& t = s.localTime;
    snprintf(b, sizeof b, "%02u/%02u/%04u %02u:%02u:%02u (UTC-3)", t.day, t.month, t.year, t.hour,
             t.minute, t.second);
  } else {
    snprintf(b, sizeof b, "Hora: --");
  }
  line(b);
  snprintf(b, sizeof b, "Bateria %u%% (%lu mV)%s", s.batteryPct, (unsigned long)s.batteryMv,
           s.batteryLow ? " BAIXA" : "");
  line(b);
  fmtTrackStatus(b, sizeof b, s.sdOk, s.trackRecording, s.trackPoints, s.timeValid, s.gpsFix);
  line(b);
}

void drawSoon(TFT_eSprite& f, const char* title, const char* msg) {
  f.fillSprite(kBg);
  back(f, true);
  centered(f, title, 100, 4, 1, kText);
  centered(f, msg, 140, 2, 1, kGrey);
}

void drawMessages(TFT_eSprite& f, const MsgHistory& h, uint8_t cursor, uint32_t) {
  f.fillSprite(kBg);
  const uint8_t total = 2 + kQuickCount + h.size();
  const uint8_t visible = 7;  // linhas abaixo do Voltar
  // Rolagem: mantém o cursor visível (item 0 = Voltar fica fixo no topo).
  uint8_t first = 1;
  if (cursor > visible) first = cursor - visible + 1;
  back(f, cursor == 0);
  char b[64];
  for (uint8_t i = 0; i < visible; ++i) {
    uint8_t item = first + i;
    if (item >= total) break;
    int16_t y = 28 + i * 30;
    if (item == 1) {
      row(f, y, "Escrever mensagem...", cursor == item);
    } else if (item < 2 + kQuickCount) {
      snprintf(b, sizeof b, "> %s", kQuick[item - 2]);
      row(f, y, b, cursor == item);
    } else {
      const MsgEntry& e = h.at(item - 2 - kQuickCount);
      const char* mark = e.mine ? (e.status == MsgStatus::Delivered ? " ok"
                                   : e.status == MsgStatus::Failed  ? " X"
                                                                    : " ...")
                                : "";
      char hm[8] = "";
      if (e.hasTime) snprintf(hm, sizeof hm, "%02u:%02u ", e.hour, e.minute);
      snprintf(b, sizeof b, "%s%s: %s", hm, e.from, e.text);
      // Corta pela largura real (pixels) e desenha a marca de estado alinhada à direita.
      f.setTextFont(2);
      f.setTextSize(1);
      const int16_t markW = mark[0] ? f.textWidth(mark) + 6 : 0;
      fitWidth(f, b, sizeof b, SCREEN_W - 20 - markW);
      row(f, y, b, cursor == item, e.mine ? kText : rgb(0x88, 0xBB, 0xDD));
      if (mark[0]) {
        f.setTextColor(cursor == item ? TFT_BLACK : kText);
        f.setTextDatum(MR_DATUM);
        f.drawString(mark, SCREEN_W - 8, y + kRowH / 2);
      }
    }
  }
}

void drawDetail(TFT_eSprite& f, const MsgEntry& e, uint8_t cursor, bool canResend) {
  f.fillSprite(kBg);
  back(f, cursor == 0);
  char b[64];
  f.setTextFont(2);
  f.setTextSize(1);
  f.setTextColor(kGrey);
  f.setTextDatum(TL_DATUM);
  const char* st = !e.mine ? "recebida"
                   : e.status == MsgStatus::Delivered ? "entregue"
                   : e.status == MsgStatus::Failed    ? "FALHOU"
                                                      : "enviando...";
  if (e.hasTime) snprintf(b, sizeof b, "%s  %02u:%02u  %s", e.from, e.hour, e.minute, st);
  else snprintf(b, sizeof b, "%s  %s", e.from, st);
  f.drawString(b, 10, 36);
  // Texto inteiro quebrado pela largura real em Font4.
  f.setTextFont(4);
  f.setTextColor(TFT_WHITE);
  const char* p = e.text;
  int16_t y = 62;
  while (*p && y < 190) {
    char line[50];
    size_t k = 0;
    while (p[k] && k < sizeof line - 1) {
      line[k] = p[k];
      line[k + 1] = 0;
      if (f.textWidth(line) > SCREEN_W - 20) {
        line[k] = 0;
        break;
      }
      ++k;
    }
    if (k == 0) k = 1;  // garante progresso
    line[k] = 0;
    f.drawString(line, 10, y);
    p += k;
    y += 30;
  }
  if (canResend) row(f, 205, "Reenviar", cursor == 1);
}

void drawKeyboard(TFT_eSprite& f, const KeyboardModel& kb, uint8_t cursor) {
  f.fillSprite(kBg);
  char b[24];
  snprintf(b, sizeof b, "<- Voltar   %u/%u", (unsigned)kb.length(), (unsigned)KeyboardModel::kMax);
  row(f, 0, b, cursor == 0, kGrey, 26);
  // Caixa de texto: mostra o final se não couber
  f.fillRect(8, 30, SCREEN_W - 16, 34, TFT_BLACK);
  f.drawRect(8, 30, SCREEN_W - 16, 34, kGrey);
  f.setTextFont(2);
  f.setTextSize(1);
  f.setTextColor(TFT_WHITE);
  f.setTextDatum(ML_DATUM);
  const char* t = kb.text();
  size_t n = kb.length();
  const size_t fit = 40;
  char shown[50];
  snprintf(shown, sizeof shown, "%s_", n > fit ? t + (n - fit) : t);
  f.drawString(shown, 14, 47);
  // Grade 10x5
  const int16_t gx = 8, gy = 70, kw = 28, kh = 30, gap = 3;  // 8 + 10*31 - 3 = 315 px
  for (size_t i = 0; i < kb.keyCount(); ++i) {
    const Key& k = kb.key(i);
    int16_t x = gx + k.col * (kw + gap), y = gy + k.row * (kh + gap);
    int16_t w = k.span * kw + (k.span - 1) * gap;
    bool sel = cursor == i + 1;
    uint16_t bg = sel ? kSel : (k.kind == KeyKind::Send ? kGreen : kBlock);
    f.fillRoundRect(x, y, w, kh, 3, bg);
    f.setTextColor(sel ? TFT_BLACK : kText);
    f.setTextDatum(MC_DATUM);
    char lbl[8];
    switch (k.kind) {
      case KeyKind::Char: snprintf(lbl, sizeof lbl, "%c", kb.label(i)); break;
      case KeyKind::Space: snprintf(lbl, sizeof lbl, "espaco"); break;
      case KeyKind::Backspace: snprintf(lbl, sizeof lbl, "<x"); break;
      case KeyKind::Shift: snprintf(lbl, sizeof lbl, kb.upper() ? "abc" : "ABC"); break;
      case KeyKind::Send: snprintf(lbl, sizeof lbl, "ENVIAR"); break;
    }
    f.drawString(lbl, x + w / 2, y + kh / 2);
  }
}

void drawSettings(TFT_eSprite& f, const SettingsView& v, uint8_t cursor) {
  f.fillSprite(kBg);
  if (v.restarting) {
    centered(f, "Reiniciando...", 120, 4, 1, kText);
    return;
  }
  back(f, cursor == 0);
  char b[40];
  if (v.editing == 1) {
    snprintf(b, sizeof b, "ID: [%c][%c]  (editando %u/2)", v.id[0], v.id[1] ? v.id[1] : ' ',
             v.idPos + 1);
  } else {
    snprintf(b, sizeof b, "ID: %s", v.id);
  }
  row(f, 36, b, cursor == 1);
  snprintf(b, sizeof b, "Brilho: %u%%%s", v.brightness, v.editing == 2 ? "  < girar >" : "");
  row(f, 72, b, cursor == 2);
  snprintf(b, sizeof b, "Volume: %u%%%s", v.volume, v.editing == 3 ? "  < girar >" : "");
  row(f, 108, b, cursor == 3);
  row(f, 144, "Canal: 921,15 MHz (fixo)", cursor == 4, kGrey);
  if (v.idConflict) centered(f, "ID repetido! Mude o ID", 210, 2, 1, kRed);
}

void drawOverlay(TFT_eSprite& f, Overlay o, const SharedState& s, uint8_t left, uint32_t now) {
  char b[48];
  switch (o) {
    case Overlay::Countdown:
      f.fillSprite(kCountdownBg);
      centered(f, "ENVIAR SOS EM", 40, 4, 1, TFT_WHITE);
      snprintf(b, sizeof b, "%u", left);
      centered(f, b, 115, 4, 3, TFT_WHITE);
      f.fillRoundRect(50, 180, 220, 36, 6, TFT_WHITE);
      centered(f, "CLIQUE = CANCELAR", 198, 2, 1, kCountdownBg);
      break;
    case Overlay::Sending:
    case Overlay::Received: {
      bool rec = o == Overlay::Received;
      f.fillSprite(rec ? kReceivedBg : kSendingBg);
      centered(f, rec ? "RECEBIDO" : "SOS ENVIANDO", 50, 4, 2, TFT_WHITE);
      if (rec) {
        centered(f, "o aparelho do amigo recebeu", 100, 2, 1, TFT_WHITE);
        centered(f, "aguardando ele ver...", 120, 2, 1, TFT_WHITE);
      } else {
        snprintf(b, sizeof b, "tentativa %u", s.sosAttempts);
        centered(f, b, 100, 2, 1, TFT_WHITE);
        if (s.myPosKnown) {
          snprintf(b, sizeof b, "posicao: %.5f %.5f", s.lat, s.lon);
          centered(f, b, 125, 2, 1, TFT_WHITE);
          centered(f, s.gpsFix ? "(fix atual)" : "(ultima posicao)", 145, 2, 1, TFT_WHITE);
        } else {
          centered(f, "sem posicao conhecida", 125, 2, 1, TFT_WHITE);
        }
      }
      f.drawRoundRect(50, 185, 220, 32, 6, TFT_WHITE);
      centered(f, "segure 3 s p/ cancelar", 201, 2, 1, TFT_WHITE);
      break;
    }
    case Overlay::Seen: {
      f.fillSprite(kSeenBg);
      centered(f, "VISTO", 45, 4, 2, TFT_WHITE);
      centered(f, "seu amigo viu o SOS", 95, 2, 1, TFT_WHITE);
      FriendVec v = friendVec(s);
      if (v.ok) {
        fmtDistance(v.dist, b, sizeof b);
        char t[48];
        snprintf(t, sizeof t, "ele esta a %s", b);
        centered(f, t, 125, 4, 1, TFT_WHITE);
        arrow(f, 290, 125, 18, v.arrowDeg, TFT_WHITE);
      }
      f.drawRoundRect(70, 185, 180, 32, 6, TFT_WHITE);
      centered(f, "clique = encerrar", 201, 2, 1, TFT_WHITE);
      break;
    }
    case Overlay::PeerSos: {
      f.fillSprite(kSel);
      centered(f, "AMIGO PEDIU SOCORRO", 28, 4, 1, TFT_BLACK);
      FriendVec v = friendVec(s);
      if (v.ok) {
        arrow(f, 90, 105, 45, v.arrowDeg, TFT_BLACK);
        fmtDistance(v.dist, b, sizeof b);
        f.setTextFont(4);
        f.setTextSize(1);
        f.setTextColor(TFT_BLACK);
        f.setTextDatum(ML_DATUM);
        f.drawString(b, 160, 95);
        if (v.relative && v.stale) {
          f.setTextFont(2);
          f.drawString("rumo antigo - ande", 160, 125);
        }
      } else {
        centered(f, s.peerKnown ? "sem minha posicao" : "sem posicao do amigo", 100, 4, 1,
                 TFT_BLACK);
      }
      char age[20];
      fmtAgeLong(s.peerKnown, now - s.peerPosMs, age, sizeof age);
      snprintf(b, sizeof b, "posicao %s  bat %u%%", age, s.peerBat);
      centered(f, b, 160, 2, 1, TFT_BLACK);
      f.fillRoundRect(80, 178, 160, 30, 6, TFT_BLACK);
      centered(f, "CLIQUE = VI", 193, 2, 1, kSel);
      break;
    }
    case Overlay::None:
      break;
  }
}

void drawMineStrip(TFT_eSprite& f, bool countdown, uint8_t left, SosState mine) {
  f.fillRect(0, SCREEN_H - 20, SCREEN_W, 20, kSendingBg);
  char b[48];
  if (countdown) snprintf(b, sizeof b, "Meu SOS em %u s - segure 3 s p/ cancelar", left);
  else snprintf(b, sizeof b, "%s - segure 3 s p/ cancelar", sosLabel(mine));
  f.setTextFont(2);
  f.setTextSize(1);
  f.setTextColor(TFT_WHITE);
  f.setTextDatum(MC_DATUM);
  f.drawString(b, SCREEN_W / 2, SCREEN_H - 10);
}

void drawBanner(TFT_eSprite& f, const char* text) {
  f.fillRect(0, 0, SCREEN_W, 40, kSel);
  f.setTextFont(2);
  f.setTextSize(1);
  f.setTextColor(TFT_BLACK);
  f.setTextDatum(ML_DATUM);
  f.drawString(text, 8, 20);
}

void drawScanner(TFT_eSprite& f, const ScannerModel& m, bool hwOk) {
  if (!hwOk) {
    drawSoon(f, "Scanner", "CC1101 nao detectado");
    return;
  }
  f.fillSprite(kBg);

  // Cabeçalho: faixa ativa à esquerda, maior pico à direita.
  const BandDef& b = m.band();
  f.fillRect(0, 0, SCREEN_W, 34, kBlock);
  f.setTextFont(2);
  f.setTextSize(1);
  f.setTextColor(TFT_WHITE);
  f.setTextDatum(ML_DATUM);
  char title[32];
  snprintf(title, sizeof title, "Scanner  %s", b.label);
  f.drawString(title, 8, 17);

  if (m.hasData()) {
    char pk[32];
    snprintf(pk, sizeof pk, "%.1fMHz %ddBm", m.peakFreqMhz(), (int)m.peakDbm());
    f.setTextColor(kSel);
    f.setTextDatum(MR_DATUM);
    f.drawString(pk, SCREEN_W - 8, 17);
  }

  // Área do espectro: 60 barras x 5 px = 300 px (margem 10 px de cada lado).
  constexpr int16_t kGraphX = 10;
  constexpr int16_t kGraphY = 40;
  constexpr int16_t kGraphW = ScannerModel::kBins * 5;
  constexpr int16_t kGraphH = 156;
  constexpr int16_t kBottom = kGraphY + kGraphH;
  const uint16_t kGrid = rgb(0x28, 0x34, 0x42);
  const uint16_t kBar = rgb(0x28, 0xB8, 0x60);
  const uint16_t kBarStrong = rgb(0xF0, 0x60, 0x20);
  const uint16_t kCursorCol = rgb(0x40, 0x68, 0x90);

  // Linhas de referência (-100, -80, -60 dBm)
  for (int16_t ref : {-100, -80, -60}) {
    const int16_t y = kBottom - (int16_t)rssiToBarHeight(ref, kGraphH);
    f.drawFastHLine(kGraphX, y, kGraphW, kGrid);
  }
  f.drawRect(kGraphX - 1, kGraphY - 1, kGraphW + 2, kGraphH + 2, kGrid);

  // Linha sutil indicando a posição atual da varredura
  const int16_t curX = kGraphX + m.cursor() * 5 + 1;
  f.drawFastVLine(curX, kGraphY, kGraphH, kCursorCol);

  for (uint8_t i = 0; i < ScannerModel::kBins; ++i) {
    const int16_t x = kGraphX + i * 5;
    const int16_t dbm = m.rssi(i);
    const uint16_t bh = rssiToBarHeight(dbm, kGraphH);
    if (bh > 0) {
      f.fillRect(x, kBottom - (int16_t)bh, 4, (int16_t)bh, dbm >= -65 ? kBarStrong : kBar);
    }
    const uint16_t ph = rssiToBarHeight(m.peak(i), kGraphH);
    if (ph > 0) {
      const int16_t py = kBottom - (int16_t)ph;
      f.fillRect(x, py > kGraphY ? py : kGraphY, 4, 2, kSel);
    }
  }

  // Rodapé: início / meio / fim da faixa + dica de controles.
  const float fStart = m.binFreqMhz(0);
  const float fMid = m.binFreqMhz(ScannerModel::kBins / 2);
  const float fEnd = m.binFreqMhz(ScannerModel::kBins - 1);
  char s0[16], s1[16], s2[16];
  snprintf(s0, sizeof s0, "%.1f", fStart);
  snprintf(s1, sizeof s1, "%.1f MHz", fMid);
  snprintf(s2, sizeof s2, "%.1f", fEnd);

  f.setTextFont(1);
  f.setTextSize(1);
  f.setTextColor(kGrey);
  f.setTextDatum(TL_DATUM);
  f.drawString(s0, kGraphX, kBottom + 6);
  f.setTextDatum(TC_DATUM);
  f.drawString(s1, SCREEN_W / 2, kBottom + 6);
  f.setTextDatum(TR_DATUM);
  f.drawString(s2, kGraphX + kGraphW, kBottom + 6);

  f.setTextFont(2);
  f.setTextDatum(BC_DATUM);
  f.setTextColor(kGrey);
  f.drawString("Giro: faixa   Clique: voltar", SCREEN_W / 2, SCREEN_H - 4);
}

}  // namespace ui
