#include "oled.h"

#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <cstring>
#include "config.h"
#include "i2c_bus.h"

namespace {
Adafruit_SSD1306 gOled(128, 32, &Wire, -1);
bool gOk = false;

void drawSatIcon(int16_t x, int16_t y) {
  gOled.drawCircle(x + 3, y + 3, 3, SSD1306_WHITE);
  gOled.drawLine(x + 3, y + 3, x + 7, y - 1, SSD1306_WHITE);
}

// Desenha a linha trocando as marcas por ícones: 'S' (satélite, só na linha 1),
// '#'/'-' (barras de sinal) e '*' (envelope).
void drawLine(const char* s, int16_t y, uint8_t size, bool line1) {
  gOled.setTextSize(size);
  int16_t x = 0;
  const int16_t cw = 6 * size;
  int bar = 0;
  for (const char* p = s; *p; ++p) {
    char c = *p;
    if (line1 && c == 'S') {
      drawSatIcon(x, y);
      x += cw;
    } else if (!line1 && (c == '#' || c == '-')) {
      int16_t h = 2 + bar * 2;
      if (c == '#') gOled.fillRect(x, y + 8 - h, 4, h, SSD1306_WHITE);
      else gOled.drawFastHLine(x, y + 7, 4, SSD1306_WHITE);
      x += 5;
      ++bar;
    } else if (!line1 && c == '*') {
      gOled.drawRect(x, y + 1, 9, 7, SSD1306_WHITE);
      gOled.drawLine(x, y + 1, x + 4, y + 4, SSD1306_WHITE);
      gOled.drawLine(x + 8, y + 1, x + 4, y + 4, SSD1306_WHITE);
      x += 10;
    } else {
      gOled.setCursor(x, y);
      gOled.write(c);
      x += cw;
    }
  }
}
}  // namespace

bool oledBegin() {
  i2cLock();
  gOk = gOled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!gOk) {
    i2cUnlock();
    LOGE("OLED nao respondeu em 0x3C");
    return false;
  }
  gOled.setTextColor(SSD1306_WHITE);
  gOled.clearDisplay();
  gOled.display();
  i2cUnlock();
  return true;
}

void oledShow(const char* l1, const char* l2) {
  if (!gOk) return;
  gOled.clearDisplay();
  drawLine(l1, 2, 1, true);
  // Avisos curtos em fonte grande; a linha normal em fonte pequena.
  bool warning = strchr(l2, '#') == nullptr && strchr(l2, '-') == nullptr;
  if (warning && strlen(l2) <= 10) drawLine(l2, 16, 2, false);
  else drawLine(l2, 18, 1, false);
  i2cLock();
  gOled.display();
  i2cUnlock();
}
