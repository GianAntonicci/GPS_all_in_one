#include "display.h"

#include "config.h"
#include "spi_bus.h"

namespace {
TFT_eSPI gTft;
TFT_eSprite gFrame(&gTft);
constexpr uint8_t kBlChannel = 0;
constexpr uint32_t kBlFreq = 5000;
}  // namespace

bool displayBegin() {
  spiLock();
  gTft.init();
  gTft.setRotation(1);
  gTft.fillScreen(TFT_BLACK);
  spiUnlock();

  ledcSetup(kBlChannel, kBlFreq, 8);
  ledcAttachPin(PIN_TFT_BL, kBlChannel);

  gFrame.setColorDepth(16);
  void* buf = gFrame.createSprite(SCREEN_W, SCREEN_H);  // PSRAM quando disponível
  if (!buf) LOGE("Display: sem memoria para o quadro 320x240");
  return buf != nullptr;
}

TFT_eSprite& frame() { return gFrame; }

void displayPush() {
  spiLock();
  gFrame.pushSprite(0, 0);
  spiUnlock();
}

void displaySetBrightness(uint8_t pct) {
  if (pct > 100) pct = 100;
  ledcWrite(kBlChannel, (uint32_t)pct * 255 / 100);
}
