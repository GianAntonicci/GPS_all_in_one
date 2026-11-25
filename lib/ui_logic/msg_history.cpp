#include "msg_history.h"

#include <cstring>

namespace {
void copyText(char* dst, size_t cap, const char* src) {
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
}
}  // namespace

MsgEntry& MsgHistory::push() {
  MsgEntry& e = ring_[head_];
  head_ = (head_ + 1) % kMax;
  if (count_ < kMax) ++count_;
  e = MsgEntry{};
  e.uid = nextUid_++;
  return e;
}

MsgEntry& MsgHistory::atMut(size_t i) { return ring_[(head_ + kMax - 1 - i) % kMax]; }

const MsgEntry& MsgHistory::at(size_t i) const { return ring_[(head_ + kMax - 1 - i) % kMax]; }

void MsgHistory::addReceived(const char* from, const char* text, bool hasTime, uint8_t h,
                             uint8_t m) {
  MsgEntry& e = push();
  e.mine = false;
  copyText(e.from, sizeof e.from, from);
  copyText(e.text, sizeof e.text, text);
  e.status = MsgStatus::Received;
  e.hasTime = hasTime;
  e.hour = h;
  e.minute = m;
}

void MsgHistory::addSent(uint8_t seq, const char* myId, const char* text, bool hasTime, uint8_t h,
                         uint8_t m) {
  MsgEntry& e = push();
  e.mine = true;
  copyText(e.from, sizeof e.from, myId);
  copyText(e.text, sizeof e.text, text);
  e.status = MsgStatus::Pending;
  e.seq = seq;
  e.hasTime = hasTime;
  e.hour = h;
  e.minute = m;
}

void MsgHistory::onDelivery(uint8_t seq, bool delivered) {
  for (size_t i = 0; i < count_; ++i) {
    MsgEntry& e = atMut(i);
    if (e.mine && e.status == MsgStatus::Pending && e.seq == seq) {
      e.status = delivered ? MsgStatus::Delivered : MsgStatus::Failed;
      return;
    }
  }
}

int MsgHistory::find(uint32_t uid) const {
  for (size_t i = 0; i < count_; ++i)
    if (at(i).uid == uid) return (int)i;
  return -1;
}

bool MsgHistory::hasPending() const {
  for (size_t i = 0; i < count_; ++i)
    if (at(i).status == MsgStatus::Pending) return true;
  return false;
}
