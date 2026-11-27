#pragma once
// Desenho das telas no quadro (sprite 320x240). Só desenha: as decisões ficam em ui.cpp/ui_logic.
#include <TFT_eSPI.h>
#include <cstdint>
#include "keyboard.h"
#include "msg_history.h"
#include "scanner_model.h"
#include "sos_overlay.h"
#include "state.h"

namespace ui {

// Paleta (spec da etapa 3)
uint16_t cBg();
uint16_t cBlock();
uint16_t cSel();

constexpr uint8_t kQuickCount = 6;
extern const char* const kQuick[kQuickCount];

void drawMenu(TFT_eSprite& f, uint8_t sel);
void drawFriend(TFT_eSprite& f, const SharedState& s, uint32_t now);
void drawGps(TFT_eSprite& f, const SharedState& s);
void drawScanner(TFT_eSprite& f, const ScannerModel& m, bool hwOk);
void drawSoon(TFT_eSprite& f, const char* title, const char* msg);
// cursor: 0 = Voltar, 1 = Escrever, 2..7 = rápidas, 8.. = histórico
void drawMessages(TFT_eSprite& f, const MsgHistory& h, uint8_t cursor, uint32_t now);
void drawDetail(TFT_eSprite& f, const MsgEntry& e, uint8_t cursor, bool canResend);
// cursor: 0 = Voltar, 1..44 = teclas
void drawKeyboard(TFT_eSprite& f, const KeyboardModel& kb, uint8_t cursor);

struct SettingsView {
  char id[3];
  uint8_t brightness, volume;  // %
  float declination;           // graus (-45.0 .. +45.0)
  int8_t editing;              // -1 nenhum, 1 = ID, 2 = brilho, 3 = volume, 4 = declinação
  uint8_t idPos;               // caractere do ID em edição (0/1)
  bool idConflict;
  bool restarting;
  bool compassOk;
  bool compassCalibrated;
  bool calibrating;
  bool calDone;
  bool calSuccess;
  uint8_t calPct;
  uint32_t calSamples;
};
// cursor: 0 = Voltar, 1 = ID, 2 = Brilho, 3 = Volume, 4 = Declinacao, 5 = Calibrar bussola, 6 = Canal
void drawSettings(TFT_eSprite& f, const SettingsView& v, uint8_t cursor);

void drawOverlay(TFT_eSprite& f, Overlay o, const SharedState& s, uint8_t countdownLeft,
                 uint32_t now);
void drawMineStrip(TFT_eSprite& f, bool countdown, uint8_t left, SosState mine);
void drawBanner(TFT_eSprite& f, const char* text);

}  // namespace ui
