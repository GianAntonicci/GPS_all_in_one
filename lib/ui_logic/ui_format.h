#pragma once
// Formatação de textos da UI e das duas linhas do OLED (ASCII).
#include <cstddef>
#include <cstdint>
#include "lora_link.h"

void fmtDistance(double m, char* out, size_t cap);                 // "850 m", "1,8 km", "23 km"
void fmtAgeShort(bool known, uint32_t ms, char* out, size_t cap);  // "25s", "12m", "3h", "--"
void fmtAgeLong(bool known, uint32_t ms, char* out, size_t cap);   // "ha 25 s", "nunca"
uint8_t signalBars(bool hasRssi, int16_t rssi);                    // 0..4
uint8_t brightnessPctFromRaw(uint8_t raw);  // múltiplo de 10 mais próximo, mínimo 10
uint8_t brightnessRawFromPct(uint8_t pct);
// Linha da trilha na tela GPS ("Trilha: gravando (N pts)", "sem cartao", ...).
void fmtTrackStatus(char* out, size_t cap, bool sdOk, bool recording, uint32_t points,
                    bool timeValid, bool fix);

struct OledInput {
  bool timeValid;
  uint8_t hour, minute;
  uint8_t sats;
  uint8_t batPct;
  bool batLow;
  char myId[3];
  bool peerEver;
  uint32_t peerAgeMs;
  bool hasRssi;
  int16_t rssi;
  bool newMsg;
  bool loraOk;
  bool idConflict;
  bool peerSos;
  SosState mySos;
  bool seenRecent;
  bool blinkPhase;
};

// Linha 1: "HH:MM  Snn  bat%". Linha 2: avisos por prioridade ou "ID barras idade [*]".
// Marcas: 'S' = satélite, '#'/'-' = barras de sinal, '*' = mensagem nova (o driver desenha ícones).
void oledLines(const OledInput& in, char* line1, char* line2, size_t cap);
