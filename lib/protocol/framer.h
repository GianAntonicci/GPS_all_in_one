#pragma once
// Separa o fluxo de bytes do módulo LoRa em frames.
// Com AT+DRSSI1 o módulo anexa 1 byte de RSSI (>= 0x80) logo após cada pacote.

#include <cstddef>
#include <cstdint>
#include "protocol.h"

struct Frame {
  char line[FRAME_MAX];  // sem '\n', terminada em '\0'
  size_t len;
  bool hasRssi;
  int16_t rssi;          // dBm
};

class Framer {
 public:
  static constexpr uint32_t kRssiWaitMs = 50;
  static constexpr size_t kQueue = 4;

  void feed(uint8_t b, uint32_t nowMs);
  void tick(uint32_t nowMs);  // libera frame pendente sem RSSI após kRssiWaitMs
  bool pop(Frame& out);

 private:
  void emitPending(bool hasRssi, int16_t rssi);

  char buf_[FRAME_MAX];
  size_t len_ = 0;
  bool inLine_ = false;
  bool overflow_ = false;

  bool pending_ = false;
  uint32_t pendingSince_ = 0;
  Frame pendingFrame_{};

  Frame q_[kQueue];
  size_t head_ = 0, count_ = 0;
};
