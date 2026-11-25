#include "sd_task.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <atomic>
#include <cstring>
#include "config.h"
#include "display.h"
#include "gpx_format.h"
#include "sd_probe.h"
#include "spi_bus.h"
#include "state.h"

namespace {

constexpr uint8_t kQueueLen = 16;
constexpr uint32_t kRemountMs = 30000;

QueueHandle_t gQ = nullptr;
SemaphoreHandle_t gTrailMx = nullptr;
TrackPt* gTrailMem = nullptr;
TrackBuffer* gTrail = nullptr;
std::atomic<bool> gMounted{false};
std::atomic<uint32_t> gGeneration{0};  // +1 a cada montagem: a UI esquece tiles de outro cartão
SdProbe gProbe;

File gFile;
bool gOpen = false;
uint32_t gPoints = 0;
char gName[40] = "";

void publish() {
  stateLock();
  state().sdOk = gMounted;
  state().trackRecording = gOpen;
  state().trackPoints = gPoints;
  stateUnlock();
}

bool mount() {
#if ENABLE_SD
  spiLock();
  bool ok = SD.begin(PIN_SD_CS, displaySpi(), SD_SPI_HZ, "/sd", 5);
  if (ok && SD.cardType() == CARD_NONE) {
    SD.end();
    ok = false;
  }
  if (ok && !SD.exists("/tracks")) SD.mkdir("/tracks");
  spiUnlock();
  gMounted = ok;
  if (ok) {
    ++gGeneration;
    gProbe.reset(millis());
  }
  if (ok) LOGI("SD: montado (%llu MB)", SD.cardSize() / (1024ULL * 1024ULL));
  else LOGW("SD: cartao ausente ou ilegivel");
  return ok;
#else
  return false;
#endif
}

// Erro de escrita (cartão removido, cheio...): fecha tudo e tenta remontar depois.
void fail(uint32_t& nextMount) {
  LOGW("SD: falha de escrita em %s; remontando em 30 s", gName);
  spiLock();
  if (gOpen) gFile.close();
  SD.end();
  spiUnlock();
  gOpen = false;
  gMounted = false;
  gPoints = 0;
  nextMount = millis() + kRemountMs;
}

bool writeAll(const char* s, size_t n) { return gFile.write((const uint8_t*)s, n) == n; }

// Abre um arquivo novo para a sessão (sufixo _2, _3... se o nome já existir).
bool openFile(const DateTime& local) {
  for (uint8_t suf = 0; suf < 100; ++suf) {
    gpxFileName(gName, sizeof gName, local, suf);
    if (!SD.exists(gName)) break;
  }
  gFile = SD.open(gName, FILE_WRITE);
  if (!gFile) return false;
  char hdr[200];
  size_t n = gpxHeader(hdr, sizeof hdr);
  const size_t f = strlen(kGpxFooter);
  if (!writeAll(hdr, n) || !writeAll(kGpxFooter, f)) {
    gFile.close();
    return false;
  }
  gFile.flush();
  gOpen = true;
  gPoints = 0;
  LOGI("Trilha: gravando em %s", gName);
  return true;
}

// Escreve o ponto por cima do rodapé e reescreve o rodapé: o arquivo é sempre XML válido.
bool appendPoint(const TrackPoint& p) {
  char buf[200];
  size_t n = gpxPoint(buf, sizeof buf, p.lat, p.lon, p.alt, p.utc, p.newSegment && gPoints > 0);
  const size_t f = strlen(kGpxFooter);
  if (!n || gFile.size() < f) return false;
  if (!gFile.seek(gFile.size() - f)) return false;
  if (!writeAll(buf, n) || !writeAll(kGpxFooter, f)) return false;
  gFile.flush();
  ++gPoints;
  return true;
}

void handlePoint(const TrackPoint& p, uint32_t& nextMount) {
  trailLock();
  if (gTrail) gTrail->add(p.lat, p.lon, p.newSegment);
  trailUnlock();

  if (!gMounted || p.utc.year < 2024) return;  // sem cartão ou sem hora: só a trilha em PSRAM
  spiLock();
  bool ok = gOpen || openFile(p.local);
  if (ok) ok = appendPoint(p);
  spiUnlock();
  if (!ok) fail(nextMount);
  else gProbe.reset(millis());
}

// Sem escrita há 5 s: lê o setor 0 de verdade (cardType/exists usam cache) para notar remoção.
void probe(uint32_t& nextMount) {
  static uint8_t sector[512];
  spiLock();
  bool ok = SD.readRAW(sector, 0);
  spiUnlock();
  if (!ok) {
    LOGW("SD: cartao nao responde");
    fail(nextMount);
  }
}

void sdTask(void*) {
  while (!displayReady()) vTaskDelay(pdMS_TO_TICKS(50));
  uint32_t nextMount = 0;
  if (!mount()) nextMount = millis() + kRemountMs;
  publish();
  for (;;) {
    TrackPoint p;
    if (xQueueReceive(gQ, &p, pdMS_TO_TICKS(1000)) == pdTRUE) handlePoint(p, nextMount);
    if (gMounted && gProbe.due(millis())) probe(nextMount);
    if (ENABLE_SD && !gMounted && (int32_t)(millis() - nextMount) >= 0) {
      if (!mount()) nextMount = millis() + kRemountMs;
    }
    publish();
  }
}

}  // namespace

void sdStart() {
  gQ = xQueueCreate(kQueueLen, sizeof(TrackPoint));
  gTrailMx = xSemaphoreCreateMutex();
  gTrailMem = (TrackPt*)ps_malloc(TRACK_CAPACITY * sizeof(TrackPt));
  if (gTrailMem) gTrail = new TrackBuffer(gTrailMem, TRACK_CAPACITY);
  else LOGE("Trilha: sem PSRAM para %u pontos", (unsigned)TRACK_CAPACITY);
  xTaskCreatePinnedToCore(sdTask, "sd", 6144, nullptr, 1, nullptr, 0);
}

bool trackEnqueue(const TrackPoint& p) { return gQ && xQueueSend(gQ, &p, 0) == pdTRUE; }
bool sdMounted() { return gMounted; }
uint32_t sdGeneration() { return gGeneration; }
void trailLock() { xSemaphoreTake(gTrailMx, portMAX_DELAY); }
void trailUnlock() { xSemaphoreGive(gTrailMx); }

const TrackBuffer& trail() {
  static TrackPt dummy[2];
  static TrackBuffer empty(dummy, 0);
  return gTrail ? *gTrail : empty;
}
