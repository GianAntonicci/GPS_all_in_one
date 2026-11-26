#include "nav.h"

void Cursor::setCount(uint8_t count) {
  count_ = count;
  if (count_ == 0) idx_ = 0;
  else if (idx_ >= count_) idx_ = count_ - 1;
}

void Cursor::move(int8_t delta) {
  if (count_ == 0) return;
  int v = ((int)idx_ + delta) % (int)count_;
  if (v < 0) v += count_;
  idx_ = (uint8_t)v;
}
