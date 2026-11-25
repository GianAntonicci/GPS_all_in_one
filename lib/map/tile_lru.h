#pragma once
// Cache LRU de tiles (só a contabilidade; os pixels ficam em slots alocados pela glue).
#include <cstdint>

struct TileKey {
  uint8_t z;
  uint32_t x, y;
};
bool operator==(const TileKey& a, const TileKey& b);

class TileLru {
 public:
  static constexpr uint8_t kMaxSlots = 32, kMissing = 64;
  explicit TileLru(uint8_t slots);
  int16_t find(const TileKey& k);     // slot pronto ou -1; marca como recente
  int16_t acquire(const TileKey& k);  // slot a preencher (livre ou o menos recente)
  void release(int16_t slot);         // decodificação falhou: slot volta a ficar livre
  void markMissing(const TileKey& k); // entrada negativa (anel; a mais antiga sai)
  bool isMissing(const TileKey& k) const;
  void clear();

 private:
  struct Slot {
    TileKey key;
    bool used;
    uint32_t tick;
  };
  uint8_t n_;
  uint32_t tick_ = 0;
  Slot slots_[kMaxSlots];
  TileKey missing_[kMissing];
  uint8_t missingCount_ = 0, missingNext_ = 0;
};
