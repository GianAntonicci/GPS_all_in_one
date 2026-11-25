#include "input_logic.h"

namespace {
// Índice (anterior << 2) | atual. 00->01->11->10->00 = sentido positivo.
const int8_t kQuad[16] = {0, 1, -1, 0, -1, 0, 0, 1, 1, 0, 0, -1, 0, -1, 1, 0};
}  // namespace

int8_t QuadDecoder::update(bool a, bool b) {
  uint8_t cur = (uint8_t)((a ? 2 : 0) | (b ? 1 : 0));
  acc_ += kQuad[(prev_ << 2) | cur];
  prev_ = cur;
  if (acc_ >= (int8_t)steps_) {
    acc_ -= steps_;
    return 1;
  }
  if (acc_ <= -(int8_t)steps_) {
    acc_ += steps_;
    return -1;
  }
  return 0;
}

Gesture ButtonGestures::update(bool raw, uint32_t now) {
  if (raw != lastRaw_) {
    lastRaw_ = raw;
    lastChange_ = now;
  }
  if (raw != stable_ && now - lastChange_ >= kDebounceMs) {
    stable_ = raw;
    if (stable_) {
      pressStart_ = lastChange_;
      held2_ = held3_ = false;
    } else if (!held2_) {
      return Gesture::Click;
    }
    return Gesture::None;
  }
  if (stable_) {
    if (!held2_ && now - pressStart_ >= kHold2Ms) {
      held2_ = true;
      return Gesture::Hold2s;
    }
    if (held2_ && !held3_ && now - pressStart_ >= kHold3Ms) {
      held3_ = true;
      return Gesture::Hold3s;
    }
  }
  return Gesture::None;
}
