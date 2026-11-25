#pragma once
// Quando testar se o cartão SD ainda está lá: sem I/O bem-sucedido há 5 s (sem fix, nada é
// gravado e a remoção do cartão passaria despercebida).
#include <cstdint>

class SdProbe {
 public:
  static constexpr uint32_t kPeriodMs = 5000;
  void reset(uint32_t now) { last_ = now; }  // após montar ou após I/O bem-sucedido
  bool due(uint32_t now) {                   // true = testar agora (já rearma)
    if (now - last_ < kPeriodMs) return false;
    last_ = now;
    return true;
  }

 private:
  uint32_t last_ = 0;
};
