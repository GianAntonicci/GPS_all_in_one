#include "framer.h"

#include <cstring>

void Framer::emitPending(bool hasRssi, int16_t rssi) {
  if (!pending_) return;
  pending_ = false;
  pendingFrame_.hasRssi = hasRssi;
  pendingFrame_.rssi = hasRssi ? rssi : 0;
  if (count_ == kQueue) {  // fila cheia: descarta o mais antigo
    head_ = (head_ + 1) % kQueue;
    --count_;
  }
  q_[(head_ + count_) % kQueue] = pendingFrame_;
  ++count_;
}

void Framer::feed(uint8_t b, uint32_t nowMs) {
  if (b >= 0x80) {
    // Byte de RSSI: só tem significado logo após um frame.
    if (pending_) emitPending(true, -(int16_t)(255 - b));
    return;
  }
  if (b == '$') {
    emitPending(false, 0);
    inLine_ = true;
    overflow_ = false;
    len_ = 0;
    buf_[len_++] = '$';
    return;
  }
  if (pending_) emitPending(false, 0);  // qualquer outro byte encerra a espera do RSSI
  if (!inLine_) return;
  if (b == '\n') {
    inLine_ = false;
    if (overflow_) return;
    size_t n = len_;
    if (n > 0 && buf_[n - 1] == '\r') --n;
    memcpy(pendingFrame_.line, buf_, n);
    pendingFrame_.line[n] = 0;
    pendingFrame_.len = n;
    pending_ = true;
    pendingSince_ = nowMs;
    return;
  }
  if (overflow_) return;
  if (len_ >= FRAME_MAX - 1) {
    overflow_ = true;
    return;
  }
  buf_[len_++] = (char)b;
}

void Framer::tick(uint32_t nowMs) {
  if (pending_ && nowMs - pendingSince_ > kRssiWaitMs) emitPending(false, 0);
}

bool Framer::pop(Frame& out) {
  if (count_ == 0) return false;
  out = q_[head_];
  head_ = (head_ + 1) % kQueue;
  --count_;
  return true;
}
