#include "map_view.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <TJpg_Decoder.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "config.h"
#include "display.h"
#include "geo.h"
#include "map_math.h"
#include "sd_task.h"
#include "spi_bus.h"
#include "tile_lru.h"
#include "ui_format.h"

namespace {

constexpr uint8_t kSlots = 24;
constexpr uint32_t kTilePx = 256 * 256;
constexpr uint32_t kMaxFile = 64 * 1024;  // tile maior que isso = inválido
constexpr uint8_t kMaxVisible = 9;
constexpr uint8_t kNoSdZoom = 15;

const uint16_t kMissingBg = 0x4208;  // #404040
const uint16_t kLoadingBg = 0x31A6;  // #303430
const uint16_t kPlainBg = 0x2125;    // #202428
const uint16_t kTrail = 0xF81F;      // magenta: contrasta com quase qualquer mapa
const uint16_t kMe = 0x041F;         // azul
const uint16_t kMeStale = 0x8410;    // cinza
const uint16_t kFriend = 0xFC00;     // laranja

uint16_t* gSlots[kSlots] = {};
uint8_t* gFileBuf = nullptr;
TileLru gLru(kSlots);
ZoomLevels gZooms;
bool gScanned = false;
bool gWasMounted = false;
uint8_t gZoom = kNoSdZoom;
bool gZoomChosen = false;
uint16_t* gDecodeDst = nullptr;  // slot sendo preenchido pelo callback do decoder

bool onJpgBlock(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* data) {
  if (!gDecodeDst) return false;
  for (uint16_t r = 0; r < h; ++r) {
    int32_t yy = y + r;
    if (yy < 0 || yy >= 256) continue;
    for (uint16_t c = 0; c < w; ++c) {
      int32_t xx = x + c;
      if (xx >= 0 && xx < 256) gDecodeDst[yy * 256 + xx] = data[r * w + c];
    }
  }
  return true;
}

void scanZooms() {
  gZooms.clear();
  spiLock();
  File dir = SD.open("/tiles");
  if (dir && dir.isDirectory()) {
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      const char* n = e.name();
      const char* base = strrchr(n, '/');
      base = base ? base + 1 : n;
      if (e.isDirectory() && *base && strspn(base, "0123456789") == strlen(base)) {
        int z = atoi(base);
        if (z >= 0 && z <= 19) gZooms.add((uint8_t)z);
      }
      e.close();
    }
  }
  if (dir) dir.close();
  spiUnlock();
  gScanned = true;
  if (!gZoomChosen && !gZooms.empty()) {
    gZoom = gZooms.initial();
    gZoomChosen = true;
  } else if (!gZooms.empty() && !gZooms.has(gZoom)) {
    gZoom = gZooms.initial(gZoom);
  }
  LOGI("Mapa: %u niveis de zoom no SD", gZooms.count());
}

// Lê o arquivo sob o lock do SPI e decodifica fora dele.
void loadTile(const TileRef& t) {
  const TileKey k{t.z, t.x, t.y};
  char path[40];
  snprintf(path, sizeof path, "/tiles/%u/%lu/%lu.jpg", t.z, (unsigned long)t.x, (unsigned long)t.y);
  uint32_t len = 0;
  spiLock();
  File f = SD.open(path, FILE_READ);
  bool ok = (bool)f;
  if (ok) {
    len = f.size();
    ok = len > 0 && len <= kMaxFile && f.read(gFileBuf, len) == (int)len;
    f.close();
  }
  spiUnlock();
  if (!ok) {
    gLru.markMissing(k);
    return;
  }
  int16_t slot = gLru.acquire(k);
  if (slot < 0) return;
  gDecodeDst = gSlots[slot];
  JRESULT r = TJpgDec.drawJpg(0, 0, gFileBuf, len);
  gDecodeDst = nullptr;
  if (r != JDR_OK) {
    LOGW("Mapa: %s invalido (erro %d; progressivo?)", path, (int)r);
    gLru.release(slot);
    gLru.markMissing(k);
  }
}

void label(TFT_eSprite& f, const char* t, int16_t x, int16_t y, uint8_t datum, uint16_t fg) {
  f.setTextFont(2);
  f.setTextSize(1);
  f.setTextDatum(datum);
  f.setTextColor(TFT_BLACK);
  for (int dx = -1; dx <= 1; ++dx)
    for (int dy = -1; dy <= 1; ++dy)
      if (dx || dy) f.drawString(t, x + dx, y + dy);  // contorno para ler sobre o mapa
  f.setTextColor(fg);
  f.drawString(t, x, y);
}

void drawTiles(TFT_eSprite& f, const PixelPos& c, bool& pending) {
  TileRef t[kMaxVisible];
  uint8_t n = visibleTiles(c, gZoom, SCREEN_W, SCREEN_H, t, kMaxVisible);
  bool loaded = false;
  for (uint8_t i = 0; i < n; ++i) {
    const TileKey k{t[i].z, t[i].x, t[i].y};
    int16_t s = gLru.find(k);
    if (s < 0 && !gLru.isMissing(k) && !loaded && gFileBuf && gSlots[0]) {
      loadTile(t[i]);  // no máximo 1 por quadro: a entrada (SOS) continua respondendo
      loaded = true;
      s = gLru.find(k);
    }
    if (s >= 0) {
      f.pushImage(t[i].sx, t[i].sy, 256, 256, gSlots[s]);
    } else if (gLru.isMissing(k)) {
      f.fillRect(t[i].sx, t[i].sy, 256, 256, kMissingBg);
      f.drawRect(t[i].sx, t[i].sy, 256, 256, kPlainBg);
      label(f, "sem mapa", t[i].sx + 128, t[i].sy + 128, MC_DATUM, TFT_LIGHTGREY);
    } else {
      f.fillRect(t[i].sx, t[i].sy, 256, 256, kLoadingBg);
      pending = true;
    }
  }
}

void drawTrail(TFT_eSprite& f, const PixelPos& c) {
  // Projeta só os pontos numa caixa folgada ao redor da tela (a projeção em double é cara).
  double latN, lonW, latS, lonE;
  pixelToLatLon({c.x - SCREEN_W, c.y - SCREEN_H}, gZoom, latN, lonW);
  pixelToLatLon({c.x + SCREEN_W, c.y + SCREEN_H}, gZoom, latS, lonE);
  trailLock();
  const TrackBuffer& tb = trail();
  bool havePrev = false;
  ScreenPt prev{0, 0};
  for (uint32_t i = 0; i < tb.size(); ++i) {
    const TrackPt& p = tb.at(i);
    if (p.lat > latN || p.lat < latS || p.lon < lonW || p.lon > lonE) {
      havePrev = false;
      continue;
    }
    ScreenPt s = toScreen(latLonToPixel(p.lat, p.lon, gZoom), c, SCREEN_W, SCREEN_H);
    if (havePrev && !p.segStart) {
      f.drawLine(prev.x, prev.y, s.x, s.y, kTrail);
      f.drawLine(prev.x + 1, prev.y, s.x + 1, s.y, kTrail);
      f.drawLine(prev.x, prev.y + 1, s.x, s.y + 1, kTrail);
    }
    prev = s;
    havePrev = true;
  }
  trailUnlock();
}

void arrowHead(TFT_eSprite& f, int16_t x, int16_t y, double deg, int16_t len, uint16_t col,
               bool filled) {
  const double a = deg * PI / 180.0;
  const double ux = sin(a), uy = -cos(a);  // para onde aponta
  const double px = -uy, py = ux;          // perpendicular
  int16_t tx = x + ux * len * 0.6, ty = y + uy * len * 0.6;
  int16_t lx = x - ux * len * 0.4 + px * len * 0.4, ly = y - uy * len * 0.4 + py * len * 0.4;
  int16_t rx = x - ux * len * 0.4 - px * len * 0.4, ry = y - uy * len * 0.4 - py * len * 0.4;
  if (filled) f.fillTriangle(tx, ty, lx, ly, rx, ry, col);
  f.drawTriangle(tx, ty, lx, ly, rx, ry, filled ? TFT_WHITE : col);
}

void drawFriend(TFT_eSprite& f, const SharedState& s, const PixelPos& c) {
  if (!s.peerKnown) return;
  ScreenPt p = toScreen(latLonToPixel(s.peerLat, s.peerLon, gZoom), c, SCREEN_W, SCREEN_H);
  const char* id = s.peerId[0] ? s.peerId : "?";
  if (onScreen(p, SCREEN_W, SCREEN_H, 8)) {
    f.fillCircle(p.x, p.y, 6, kFriend);
    f.drawCircle(p.x, p.y, 7, TFT_WHITE);
    label(f, id, p.x + 10, p.y, ML_DATUM, kFriend);
    return;
  }
  EdgeArrow a = edgeArrow(p, SCREEN_W, SCREEN_H, 14);
  arrowHead(f, a.x, a.y, a.angleDeg, 22, kFriend, true);
  if (s.myPosKnown) {
    char d[16], t[24];
    fmtDistance(distanceM(s.lat, s.lon, s.peerLat, s.peerLon), d, sizeof d);
    snprintf(t, sizeof t, "%s %s", id, d);
    // Texto para dentro da tela a partir da seta.
    int16_t tx = a.x - (a.x - SCREEN_W / 2) / 4, ty = a.y - (a.y - SCREEN_H / 2) / 4;
    label(f, t, tx, ty, MC_DATUM, kFriend);
  }
}

void drawMe(TFT_eSprite& f, const SharedState& s, const PixelPos& c) {
  if (!s.myPosKnown) return;
  ScreenPt p = toScreen(latLonToPixel(s.lat, s.lon, gZoom), c, SCREEN_W, SCREEN_H);
  const uint16_t col = s.gpsFix ? kMe : kMeStale;
  if (s.courseKnown) {
    arrowHead(f, p.x, p.y, s.courseDeg, 20, col, !s.courseStale);
  } else {
    f.fillCircle(p.x, p.y, 6, col);
    f.drawCircle(p.x, p.y, 7, TFT_WHITE);
  }
  if (!s.gpsFix) label(f, "ultima posicao", p.x, p.y + 14, TC_DATUM, TFT_WHITE);
}

void drawScale(TFT_eSprite& f, double lat) {
  uint32_t m;
  uint16_t px;
  scaleBar(metersPerPixel(lat, gZoom), 80, m, px);
  char t[24];
  if (m >= 1000) snprintf(t, sizeof t, "z%u  %lu km", gZoom, (unsigned long)(m / 1000));
  else snprintf(t, sizeof t, "z%u  %lu m", gZoom, (unsigned long)m);
  const int16_t x = 8, y = SCREEN_H - 10;
  f.fillRect(x - 1, y - 1, px + 2, 5, TFT_BLACK);
  f.fillRect(x, y, px, 3, TFT_WHITE);
  label(f, t, x, y - 4, BL_DATUM, TFT_WHITE);
}

}  // namespace

void mapInit() {
  for (uint8_t i = 0; i < kSlots; ++i) {
    gSlots[i] = (uint16_t*)ps_malloc(kTilePx * sizeof(uint16_t));
    if (!gSlots[i]) {
      LOGE("Mapa: sem PSRAM para o slot %u do cache", i);
      gLru = TileLru(i);  // usa só os slots que deu para alocar
      break;
    }
  }
  gFileBuf = (uint8_t*)ps_malloc(kMaxFile);
  if (!gFileBuf) LOGE("Mapa: sem PSRAM para o buffer de arquivo");
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);  // ordem de bytes do sprite: a cópia para o quadro é memcpy
  TJpgDec.setCallback(onJpgBlock);
}

void mapZoom(int8_t detents) {
  if (gZooms.empty()) return;
  while (detents > 0) {
    gZoom = gZooms.step(gZoom, +1);
    --detents;
  }
  while (detents < 0) {
    gZoom = gZooms.step(gZoom, -1);
    ++detents;
  }
}

bool mapDraw(TFT_eSprite& f, const SharedState& s) {
  const bool mounted = sdMounted();
  if (mounted != gWasMounted) {
    // Cartão entrou ou saiu (pode ser outro): esquece níveis e tiles.
    gWasMounted = mounted;
    gScanned = false;
    gZooms.clear();
    gLru.clear();
  }
  if (mounted && !gScanned) scanZooms();

  double clat, clon;
  bool center = true;
  if (s.myPosKnown) {
    clat = s.lat;
    clon = s.lon;
  } else if (s.peerKnown) {
    clat = s.peerLat;
    clon = s.peerLon;
  } else {
    center = false;
  }

  f.fillSprite(kPlainBg);
  if (!center) {
    label(f, "Sem posicao - aguardando GPS", SCREEN_W / 2, SCREEN_H / 2, MC_DATUM, TFT_WHITE);
    return false;
  }
  const PixelPos c = latLonToPixel(clat, clon, gZoom);
  bool pending = false;
  if (mounted && !gZooms.empty()) drawTiles(f, c, pending);
  else label(f, mounted ? "Sem mapas no cartao" : "Sem cartao SD", SCREEN_W / 2, 12, TC_DATUM,
             TFT_LIGHTGREY);
  drawTrail(f, c);
  drawFriend(f, s, c);
  drawMe(f, s, c);
  drawScale(f, clat);
  return pending;
}
