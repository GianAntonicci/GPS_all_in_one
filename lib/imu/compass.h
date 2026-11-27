#pragma once
// Lógica pura da bússola (sem Arduino): calibração hard/soft-iron,
// compensação de inclinação, declinação magnética e escolha de fonte de rumo.
#include <cstdint>

enum class HeadingSource : uint8_t { None, Compass, Gps, GpsStale };

struct MagCal {
  bool valid = false;
  float offsetX = 0.0f;
  float offsetY = 0.0f;
  float offsetZ = 0.0f;
  float scaleX = 1.0f;
  float scaleY = 1.0f;
  float scaleZ = 1.0f;
};

class MagCalibrator {
 public:
  static constexpr uint32_t kMinSamples = 40;
  static constexpr float kMinSpanUt = 10.0f;

  void reset();
  void addSample(float mx, float my, float mz);
  uint32_t sampleCount() const { return count_; }
  bool finish(MagCal& out) const;

 private:
  uint32_t count_ = 0;
  float minX_ = 0.0f, maxX_ = 0.0f;
  float minY_ = 0.0f, maxY_ = 0.0f;
  float minZ_ = 0.0f, maxZ_ = 0.0f;
};

float normalizeDeg(float deg);

void applyMagCal(const MagCal& cal, float rawX, float rawY, float rawZ, float& outX, float& outY,
                 float& outZ);

// Calcula o rumo verdadeiro [0, 360) com compensação de inclinação + declinação.
// Convenção de eixos (mão direita): +X frente (topo da tela), +Y esquerda, +Z cima.
// Retorna false se aceleração ou campo magnético forem inválidos/nulos.
bool tiltCompensatedHeading(float axG, float ayG, float azG, float mx, float my, float mz,
                            float declinationDeg, float& headingDegOut);

struct HeadingInput {
  bool gpsFix = false;
  double speedKmh = 0.0;
  double gpsCourseDeg = 0.0;
  bool gpsCourseKnown = false;
  bool gpsCourseStale = true;
  bool compassOk = false;
  bool compassCalibrated = false;
  float compassDeg = 0.0f;
};

struct HeadingResult {
  double deg = 0.0;
  bool known = false;
  bool stale = true;
  HeadingSource source = HeadingSource::None;
};

// Em movimento (>= 1,5 km/h com fix): usa gps.course.
// Parado (< 1,5 km/h) ou sem fix: usa bússola calibrada (sem aviso "rumo antigo").
// Fallback: último gps.course marcado como antigo.
HeadingResult selectHeading(const HeadingInput& in);

const char* headingSourceName(HeadingSource s);
