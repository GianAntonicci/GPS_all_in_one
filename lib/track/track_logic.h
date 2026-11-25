#pragma once
// Trilha: quando gravar um ponto (GPX) e o buffer da trilha da sessão (desenhada no mapa).
#include <cstdint>

// Ponto a cada 10 s ou 20 m (o que vier primeiro), contados do último ponto aceito.
class TrackPolicy {
 public:
  static constexpr uint32_t kPeriodMs = 10000;
  static constexpr double kMoveM = 20.0;
  // true = enfileirar agora. newSegment = 1º ponto desde o boot ou desde a volta do fix;
  // continua true até accepted().
  bool due(uint32_t now, bool hasFix, double lat, double lon, bool& newSegment);
  void accepted(uint32_t now, double lat, double lon);  // só depois de enfileirar com sucesso

 private:
  bool newSeg_ = true;
  bool have_ = false;  // há ponto aceito neste segmento
  uint32_t last_ = 0;
  double lat_ = 0, lon_ = 0;
};

struct TrackPt {
  float lat, lon;
  bool segStart;
};

// Pontos da sessão em memória externa (PSRAM). Cheio: dizima (mantém 1 a cada 2).
class TrackBuffer {
 public:
  static constexpr double kMinStepM = 20.0;
  TrackBuffer(TrackPt* storage, uint32_t capacity);
  void add(double lat, double lon, bool newSegment);
  uint32_t size() const { return n_; }
  const TrackPt& at(uint32_t i) const { return p_[i]; }
  void clear() {
    n_ = 0;
    pendingStart_ = false;
  }

 private:
  void decimate();
  TrackPt* p_;
  uint32_t cap_, n_ = 0;
  bool pendingStart_ = false;  // início de segmento herdado de um ponto descartado
};
