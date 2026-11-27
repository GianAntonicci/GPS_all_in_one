#pragma once
// Lógica pura do barômetro BME280 (sem Arduino): altitude barométrica calibrada
// pelo GPS, suavização e tendência de pressão nas últimas 3 horas.
#include <cstddef>
#include <cstdint>

enum class PressureTrend : uint8_t { Unknown, Rising, Steady, Falling };

// Altitude barométrica padrão (ISA) em metros.
float pressureToAltitudeM(float pressureHpa, float seaLevelHpa = 1013.25f);

// Calcula pressão de referência ao nível do mar a partir da pressão e altitude conhecidas.
float seaLevelFromAltitude(float pressureHpa, float altitudeM);

class BaroAltimeter {
 public:
  static constexpr float kDefaultSeaLevelHpa = 1013.25f;
  static constexpr float kMaxCalHdop = 2.0f;
  static constexpr uint8_t kMinCalSats = 6;

  bool updatePressure(float pressureHpa);
  bool calibrateWithGps(float pressureHpa, float gpsAltM, bool gpsFix, uint8_t sats, float hdop);

  bool hasAltitude() const { return hasAlt_; }
  float altitudeM() const { return altM_; }
  bool calibrated() const { return calibrated_; }
  float seaLevelHpa() const { return seaLevelHpa_; }

 private:
  float seaLevelHpa_ = kDefaultSeaLevelHpa;
  float altM_ = 0.0f;
  bool hasAlt_ = false;
  bool calibrated_ = false;
};

class PressureHistory {
 public:
  static constexpr size_t kCapacity = 37;           // 36 intervalos de 5 min = 3 h
  static constexpr uint32_t kSlotMs = 300000u;      // 5 min entre slots
  static constexpr uint32_t kWindowMs = 10800000u;  // 3 h
  static constexpr uint32_t kMinSpanMs = 900000u;   // mínimo 15 min para estimar tendência
  static constexpr float kTrendBandHpa = 0.5f;
  static constexpr float kRapidDropHpa = -2.0f;

  void reset();
  void addSample(float pressureHpa, uint32_t nowMs);

  // Variação de pressão (atual - mais antiga dentro da janela de 3 h).
  bool delta3h(float& outDeltaHpa) const;
  PressureTrend trend() const;
  bool rapidDrop() const;
  size_t count() const { return count_; }

 private:
  struct Entry {
    float hpa;
    uint32_t ms;
  };
  Entry ring_[kCapacity] = {};
  size_t head_ = 0;   // próximo índice de escrita
  size_t count_ = 0;
  float latestHpa_ = 0.0f;
  uint32_t latestMs_ = 0;
  uint32_t lastSlotMs_ = 0;
  bool hasLatest_ = false;
};

const char* pressureTrendSymbol(PressureTrend t);
