#include "tone_seq.h"

namespace {

struct Step {
  uint16_t freqHz;
  uint16_t ms;
};
struct Def {
  const Step* steps;
  uint8_t n;
  bool repeat;
};

constexpr Step kAlarm[] = {{900, 250}, {1400, 250}};
constexpr Step kSeen[] = {{1600, 300}, {0, 120}, {1600, 300}};
constexpr Step kReceived[] = {{800, 120}, {1200, 120}, {1600, 120}};
constexpr Step kFinal[] = {{2000, 150}};
constexpr Step kTick[] = {{1000, 80}};
constexpr Step kMessage[] = {{1000, 100}, {0, 60}, {1500, 100}};
constexpr Step kHold[] = {{1500, 80}};
constexpr Step kClick[] = {{2000, 15}};

constexpr int16_t kLoudAmp = 26000;

Def def(Pattern p) {
  switch (p) {
    case Pattern::PeerAlarm: return {kAlarm, 2, true};
    case Pattern::SosSeen: return {kSeen, 3, false};
    case Pattern::SosReceived: return {kReceived, 3, false};
    case Pattern::CountdownFinal: return {kFinal, 1, false};
    case Pattern::CountdownTick: return {kTick, 1, false};
    case Pattern::Message: return {kMessage, 3, false};
    case Pattern::Hold: return {kHold, 1, false};
    case Pattern::Click: return {kClick, 1, false};
    default: return {nullptr, 0, false};
  }
}

}  // namespace

uint8_t patternPriority(Pattern p) {
  switch (p) {
    case Pattern::PeerAlarm: return 1;
    case Pattern::SosSeen:
    case Pattern::SosReceived: return 2;
    case Pattern::CountdownFinal:
    case Pattern::CountdownTick: return 3;
    case Pattern::Message: return 4;
    case Pattern::Hold:
    case Pattern::Click: return 5;
    default: return 255;
  }
}

bool patternLoud(Pattern p) { return patternPriority(p) <= 3; }

void ToneSequencer::play(Pattern p, uint32_t now) {
  if (p == Pattern::None || p == p_) return;
  if (p_ != Pattern::None && patternPriority(p) > patternPriority(p_)) return;
  p_ = p;
  start_ = now;
}

void ToneSequencer::stop(Pattern p) {
  if (p == p_) p_ = Pattern::None;
}

Tone ToneSequencer::current(uint32_t now) {
  const Def d = def(p_);
  if (!d.n) return {0, false};
  uint32_t total = 0;
  for (uint8_t i = 0; i < d.n; ++i) total += d.steps[i].ms;
  uint32_t t = now - start_;
  if (t >= total) {
    if (!d.repeat) {
      p_ = Pattern::None;
      return {0, false};
    }
    t %= total;
  }
  for (uint8_t i = 0; i < d.n; ++i) {
    if (t < d.steps[i].ms) return {d.steps[i].freqHz, patternLoud(p_)};
    t -= d.steps[i].ms;
  }
  return {0, false};
}

int16_t toneAmplitude(uint8_t volumePct, bool loud) {
  if (loud) return kLoudAmp;
  uint32_t v = volumePct > 100 ? 100 : volumePct;
  return (int16_t)(kLoudAmp * v * v / 10000);
}
