#pragma once
// Lógica pura do GPS: hora local, rumo mantido parado e agendamento do POS.
#include <cstdint>

struct DateTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
};

// Soma `offsetHours` (ex.: -3 para o Brasil, sem horário de verão) tratando dia/mês/ano/bissexto.
DateTime utcToLocal(const DateTime& utc, int offsetHours);

// Rumo pelo gps.course só em movimento; parado, mantém o último (marcado como antigo).
class CourseTracker {
 public:
  static constexpr double kMinSpeedKmh = 2.0;
  explicit CourseTracker(double initialDeg = 0, bool known = false);
  void update(bool valid, double speedKmh, double courseDeg);
  double course() const { return course_; }
  bool known() const { return known_; }
  bool stale() const { return stale_; }

 private:
  double course_;
  bool known_;
  bool stale_;
};

// Decide quando enviar POS: na hora em que o fix chega e depois a cada período + jitter.
class PosScheduler {
 public:
  PosScheduler(uint32_t (*rand32)(), uint32_t periodMs = 30000, uint32_t jitterMaxMs = 3000);
  bool due(uint32_t now, bool hasFix);  // true = enviar agora (já reagenda)

 private:
  void schedule(uint32_t now);
  uint32_t (*rand_)();
  uint32_t period_, jitterMax_;
  bool hadFix_ = false;
  bool sentOnce_ = false;
  uint32_t next_ = 0;
};

// Quando gravar a melhor posição conhecida na NVS: 1º fix, perda do fix (túnel),
// deslocamento > 200 m (no mínimo 1 min depois da última gravação) e a cada 10 min.
class SavePolicy {
 public:
  static constexpr uint32_t kPeriodMs = 600000, kMinGapMs = 60000;
  static constexpr double kMoveM = 200.0;
  bool due(uint32_t now, bool hasFix, double lat, double lon);  // true = gravar lat/lon agora

 private:
  void mark(uint32_t now, double lat, double lon);
  bool saved_ = false, hadFix_ = false;
  uint32_t last_ = 0;
  double lat_ = 0, lon_ = 0;
};
