#pragma once
// Matemática pura do mapa: Web Mercator (slippy map), tiles visíveis, seta de borda e níveis de zoom.
#include <cstdint>

struct PixelPos {
  double x, y;  // pixel global no zoom z (mundo = 256·2^z)
};

PixelPos latLonToPixel(double lat, double lon, uint8_t z);  // lat limitada a ±85.05112878
double metersPerPixel(double lat, uint8_t z);
void pixelToLatLon(const PixelPos& p, uint8_t z, double& lat, double& lon);  // inversa
// Barra de escala: maior comprimento "redondo" (5, 10, 20, 50 m ... 100 km) que cabe em maxPx.
void scaleBar(double metersPerPx, uint16_t maxPx, uint32_t& meters, uint16_t& px);

struct TileRef {
  uint8_t z;
  uint32_t x, y;
  int16_t sx, sy;  // canto superior esquerdo no quadro
};

// Tiles que cobrem um viewport w×h centrado em `center`. x faz wrap; linhas fora do mundo são puladas.
uint8_t visibleTiles(const PixelPos& center, uint8_t z, int16_t w, int16_t h, TileRef* out,
                     uint8_t max);

struct ScreenPt {
  int32_t x, y;
};

ScreenPt toScreen(const PixelPos& p, const PixelPos& center, int16_t w, int16_t h);
bool onScreen(ScreenPt p, int16_t w, int16_t h, int16_t margin);

struct EdgeArrow {
  int16_t x, y;
  double angleDeg;  // 0 = para cima, sentido horário
};

// Ponto na borda (encolhida por `inset`) na direção centro→alvo.
EdgeArrow edgeArrow(ScreenPt target, int16_t w, int16_t h, int16_t inset);

// Conjunto dos níveis de zoom (0..19) presentes no SD.
class ZoomLevels {
 public:
  void add(uint8_t z);
  bool has(uint8_t z) const;
  bool empty() const { return mask_ == 0; }
  uint8_t count() const;
  uint8_t initial(uint8_t pref = 16) const;    // maior presente ≤ pref; senão, o menor presente
  uint8_t step(uint8_t cur, int8_t dir) const;  // próximo presente na direção; trava nas pontas
  void clear() { mask_ = 0; }

 private:
  uint32_t mask_ = 0;
};
