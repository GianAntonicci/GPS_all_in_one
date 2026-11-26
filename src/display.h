#pragma once
// TFT ST7789 em paisagem (320x240) com quadro inteiro em PSRAM.
#include <TFT_eSPI.h>
#include <cstdint>

constexpr int16_t SCREEN_W = 320, SCREEN_H = 240;

bool displayBegin();                     // TFT + sprite + backlight PWM
TFT_eSprite& frame();                    // desenhar aqui
void displayPush();                      // envia o quadro (seção crítica do SPI)
void displaySetBrightness(uint8_t pct);  // 0 = apagado, 10..100
bool displayReady();                     // true depois de displayBegin() (thread-safe)
SPIClass& displaySpi();                  // SPIClass do TFT_eSPI (o SD usa o mesmo barramento)
