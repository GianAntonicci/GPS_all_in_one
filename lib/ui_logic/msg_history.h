#pragma once
// Histórico das últimas mensagens (RAM), com estado de entrega casado pelo seq.
#include <cstddef>
#include <cstdint>

enum class MsgStatus : uint8_t { Received, Pending, Delivered, Failed };

struct MsgEntry {
  bool mine;
  char from[3];
  char text[49];
  MsgStatus status;
  uint8_t seq;
  bool hasTime;
  uint8_t hour, minute;
};

class MsgHistory {
 public:
  static constexpr size_t kMax = 20;
  void addReceived(const char* from, const char* text, bool hasTime, uint8_t h, uint8_t m);
  void addSent(uint8_t seq, const char* myId, const char* text, bool hasTime, uint8_t h,
               uint8_t m);
  void onDelivery(uint8_t seq, bool delivered);  // só a Pending mais nova com esse seq
  size_t size() const { return count_; }
  const MsgEntry& at(size_t i) const;  // 0 = mais nova
  bool hasPending() const;

 private:
  MsgEntry& push();
  MsgEntry& atMut(size_t i);
  MsgEntry ring_[kMax];
  size_t head_ = 0;  // próxima posição de escrita
  size_t count_ = 0;
};
