#pragma once
// Cursor com volta para listas e para a grade do menu (percorrida em ordem de leitura).
#include <cstdint>

class Cursor {
 public:
  explicit Cursor(uint8_t count = 1) : count_(count) {}
  void setCount(uint8_t count);
  void move(int8_t delta);
  uint8_t index() const { return idx_; }
  uint8_t count() const { return count_; }
  void reset() { idx_ = 0; }  // item 0 = "<- Voltar"

 private:
  uint8_t count_;
  uint8_t idx_ = 0;
};
