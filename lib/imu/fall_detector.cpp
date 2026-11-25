#include "fall_detector.h"

#include <cmath>

FallDetector::FallDetector(const FallConfig& cfg) : cfg_(cfg) {}

void FallDetector::onMotionWake(uint32_t now) {
  if (state_ == FallState::Cooldown) {
    if ((uint32_t)(now - stateSince_) < cfg_.cooldownMs) {
      sampling_ = false;
      return;
    }
    state_ = FallState::Idle;
  }
  sampling_ = true;
  quietSince_ = now;
  hasPrev_ = false;
}

FallEvent FallDetector::update(float axG, float ayG, float azG, uint32_t now) {
  const float mag = sqrtf(axG * axG + ayG * ayG + azG * azG);
  float delta = 0.0f;
  if (hasPrev_) {
    const float dx = axG - prevX_;
    const float dy = ayG - prevY_;
    const float dz = azG - prevZ_;
    delta = sqrtf(dx * dx + dy * dy + dz * dz);
  }
  prevX_ = axG;
  prevY_ = ayG;
  prevZ_ = azG;
  hasPrev_ = true;

  const bool still = (fabsf(mag - 1.0f) <= cfg_.stillBandG) && (delta <= cfg_.stillDeltaG);

  if (state_ == FallState::Cooldown) {
    if ((uint32_t)(now - stateSince_) < cfg_.cooldownMs) {
      sampling_ = false;
      return FallEvent::None;
    }
    state_ = FallState::Idle;
    quietSince_ = now;
  }

  switch (state_) {
    case FallState::Idle:
      if (mag >= cfg_.impactG) {
        state_ = FallState::Settling;
        stateSince_ = now;
        sampling_ = true;
        return FallEvent::None;
      }
      if (!still) {
        quietSince_ = now;
      } else if ((uint32_t)(now - quietSince_) >= cfg_.quietToSleepMs) {
        sampling_ = false;
      }
      return FallEvent::None;

    case FallState::Settling:
      sampling_ = true;
      if ((uint32_t)(now - stateSince_) >= cfg_.settleMs) {
        state_ = FallState::StillWait;
        stateSince_ = now;
        moving_ = false;
      }
      return FallEvent::None;

    case FallState::StillWait:
      sampling_ = true;
      if (!still) {
        if (!moving_) {
          moving_ = true;
          moveSince_ = now;
        } else if ((uint32_t)(now - moveSince_) >= cfg_.moveCancelMs) {
          state_ = FallState::Idle;
          quietSince_ = now;
          moving_ = false;
        }
        return FallEvent::None;
      }
      moving_ = false;
      if ((uint32_t)(now - stateSince_) >= cfg_.stillMs) {
        state_ = FallState::Cooldown;
        stateSince_ = now;
        sampling_ = false;
        return FallEvent::SosCountdown;
      }
      return FallEvent::None;

    case FallState::Cooldown:
      return FallEvent::None;
  }
  return FallEvent::None;
}
