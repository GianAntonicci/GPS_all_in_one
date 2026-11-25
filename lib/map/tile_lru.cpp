#include "tile_lru.h"

bool operator==(const TileKey& a, const TileKey& b) { return a.z == b.z && a.x == b.x && a.y == b.y; }

TileLru::TileLru(uint8_t slots) : n_(slots > kMaxSlots ? kMaxSlots : slots) { clear(); }

int16_t TileLru::find(const TileKey& k) {
  for (uint8_t i = 0; i < n_; ++i) {
    if (slots_[i].used && slots_[i].key == k) {
      slots_[i].tick = ++tick_;
      return i;
    }
  }
  return -1;
}

int16_t TileLru::acquire(const TileKey& k) {
  if (n_ == 0) return -1;
  int16_t pick = -1;
  for (uint8_t i = 0; i < n_ && pick < 0; ++i)
    if (!slots_[i].used) pick = i;
  if (pick < 0) {
    pick = 0;
    for (uint8_t i = 1; i < n_; ++i)
      if (slots_[i].tick < slots_[pick].tick) pick = i;
  }
  slots_[pick].key = k;
  slots_[pick].used = true;
  slots_[pick].tick = ++tick_;
  return pick;
}

void TileLru::release(int16_t slot) {
  if (slot >= 0 && slot < n_) slots_[slot].used = false;
}

void TileLru::markMissing(const TileKey& k) {
  if (isMissing(k)) return;
  missing_[missingNext_] = k;
  missingNext_ = (uint8_t)((missingNext_ + 1) % kMissing);
  if (missingCount_ < kMissing) ++missingCount_;
}

bool TileLru::isMissing(const TileKey& k) const {
  for (uint8_t i = 0; i < missingCount_; ++i)
    if (missing_[i] == k) return true;
  return false;
}

void TileLru::clear() {
  for (uint8_t i = 0; i < kMaxSlots; ++i) slots_[i] = Slot{TileKey{0, 0, 0}, false, 0};
  missingCount_ = missingNext_ = 0;
  tick_ = 0;
}
