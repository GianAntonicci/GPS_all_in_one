#include "audio.h"

#include <Arduino.h>
#include <driver/i2s.h>
#include <atomic>
#include <cmath>
#include "config.h"

namespace {

enum class CmdKind : uint8_t { Play, Stop, Volume };
struct Cmd {
  CmdKind kind;
  Pattern p;
  uint8_t vol;
};

constexpr i2s_port_t kPort = I2S_NUM_0;
constexpr uint32_t kRate = 16000;
constexpr uint16_t kBlock = 128;   // frames por escrita (8 ms)
constexpr uint16_t kRamp = 48;     // 3 ms de rampa contra estalos
constexpr uint32_t kIdleMs = 50;   // silêncio antes de parar o I2S
constexpr uint16_t kTable = 256;

QueueHandle_t gQ = nullptr;
int16_t gSine[kTable];
ToneSequencer gSeq;
uint8_t gVolume = 70;
bool gRunning = false;
std::atomic<bool> gAlarmWanted{false};  // estado, não comando: não se perde na fila

void apply(const Cmd& c, uint32_t now) {
  switch (c.kind) {
    case CmdKind::Play: gSeq.play(c.p, now); break;
    case CmdKind::Stop: gSeq.stop(c.p); break;
    case CmdKind::Volume: gVolume = c.vol > 100 ? 100 : c.vol; break;
  }
}

bool i2sInit() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = kRate;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = 0;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  if (i2s_driver_install(kPort, &cfg, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_I2S_BCLK;
  pins.ws_io_num = PIN_I2S_LRC;
  pins.data_out_num = PIN_I2S_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(kPort, &pins) != ESP_OK) return false;
  i2s_zero_dma_buffer(kPort);
  i2s_stop(kPort);  // sem BCLK o MAX98357A entra em shutdown
  return true;
}

void audioTask(void*) {
  if (!i2sInit()) {
    LOGE("Audio: falha ao iniciar o I2S");
    vTaskDelete(nullptr);
    return;
  }
  for (uint16_t i = 0; i < kTable; ++i) gSine[i] = (int16_t)(32767.0 * sin(2 * PI * i / kTable));

  int16_t buf[kBlock * 2];
  uint32_t phase = 0;         // acumulador 16.16 sobre a tabela
  int32_t gain = 0;           // amplitude atual (rampa)
  uint16_t lastFreq = 0;
  uint32_t silentSince = 0;

  for (;;) {
    Cmd c;
    if (!gRunning) {
      // Parado: dorme até chegar um pedido (acorda a cada 100 ms para ver o alarme).
      if (xQueueReceive(gQ, &c, pdMS_TO_TICKS(100)) == pdTRUE) apply(c, millis());
      gSeq.setAlarm(gAlarmWanted, millis());
      if (gSeq.playing() == Pattern::None) continue;
      i2s_start(kPort);
      gRunning = true;
      silentSince = 0;
    }
    while (xQueueReceive(gQ, &c, 0) == pdTRUE) apply(c, millis());
    gSeq.setAlarm(gAlarmWanted, millis());

    const uint32_t now = millis();
    Tone t = gSeq.current(now);
    if (t.freqHz) lastFreq = t.freqHz;  // na rampa de saída continua na mesma frequência
    const int32_t target = t.freqHz ? toneAmplitude(gVolume, t.loud) : 0;
    const uint32_t step = (uint32_t)(((uint64_t)lastFreq * kTable << 16) / kRate);
    const int32_t dg = (target - gain) / kRamp;
    for (uint16_t i = 0; i < kBlock; ++i) {
      if (gain != target) {
        gain += dg ? dg : (target > gain ? 1 : -1);
        if ((dg > 0 && gain > target) || (dg < 0 && gain < target)) gain = target;
      }
      phase += step;
      int16_t s = (int16_t)((int32_t)gSine[(phase >> 16) & (kTable - 1)] * gain / 32767);
      buf[2 * i] = s;
      buf[2 * i + 1] = s;
    }
    size_t written = 0;
    i2s_write(kPort, buf, sizeof buf, &written, portMAX_DELAY);

    if (gSeq.playing() == Pattern::None && gain == 0) {
      if (!silentSince) silentSince = now ? now : 1;
      if (now - silentSince >= kIdleMs) {
        i2s_zero_dma_buffer(kPort);
        i2s_stop(kPort);
        gRunning = false;
        phase = 0;
      }
    } else {
      silentSince = 0;
    }
  }
}

void send(const Cmd& c) {
  if (gQ) xQueueSend(gQ, &c, 0);
}

}  // namespace

void audioBegin(uint8_t volumePct) {
  gVolume = volumePct > 100 ? 100 : volumePct;
  gQ = xQueueCreate(8, sizeof(Cmd));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 3, nullptr, 1);
}

void audioPlay(Pattern p) {
  if (p != Pattern::None) send({CmdKind::Play, p, 0});
}
void audioStop(Pattern p) { send({CmdKind::Stop, p, 0}); }
void audioSetAlarm(bool wanted) { gAlarmWanted = wanted; }
void audioSetVolume(uint8_t pct) { send({CmdKind::Volume, Pattern::None, pct}); }
