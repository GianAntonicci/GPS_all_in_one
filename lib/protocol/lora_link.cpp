#include "lora_link.h"

#include <cstring>

namespace {
// true se `a` já chegou ou passou de `b` (seguro com wrap do millis()).
bool reached(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }
}  // namespace

Link::Link(LinkSink& sink, const char* myId, uint8_t initialSeq, uint32_t (*rand32)(),
           LinkTiming t)
    : sink_(sink), seq_(initialSeq), rand_(rand32), t_(t) {
  strncpy(myId_, myId, 2);
  myId_[2] = 0;
}

Packet Link::make(PktType t) {
  Packet p{};
  p.type = t;
  memcpy(p.id, myId_, sizeof p.id);
  p.seq = nextSeq();
  return p;
}

void Link::transmit(const Packet& p) {
  char buf[FRAME_MAX];
  size_t n = protoEncode(p, buf, sizeof buf);
  if (n) sink_.sendFrame(buf, n);
}

uint32_t Link::jitter() { return t_.jitterMaxMs ? rand_() % (t_.jitterMaxMs + 1) : 0; }

int Link::startReliable(Packet p, uint32_t now) {
  msg_.active = true;
  msg_.pkt = p;
  msg_.attempts = 1;
  msg_.nextMs = now + t_.ackTimeoutMs + jitter();
  transmit(p);
  return p.seq;
}

int Link::sendMsg(const char* text, uint32_t now) {
  if (msg_.active) return -1;
  Packet p = make(PktType::MSG);
  protoSanitize(text, p.text, sizeof p.text);
  return startReliable(p, now);
}

int Link::sendPing(uint32_t now) {
  if (msg_.active) return -1;
  return startReliable(make(PktType::PING), now);
}

void Link::sendPos(double lat, double lon, int32_t alt, uint8_t bat, uint32_t) {
  Packet p = make(PktType::POS);
  p.lat = lat;
  p.lon = lon;
  p.alt = alt;
  p.bat = bat;
  transmit(p);
}

void Link::handleAck(uint8_t ackSeq) {
  if (msg_.active && msg_.pkt.seq == ackSeq) {
    msg_.active = false;
    sink_.onDelivery(ackSeq, Delivery::Delivered);
  }
}

bool Link::isDuplicate(const Packet& p, uint32_t now) {
  for (auto& e : seen_) {
    if (e.used && e.seq == p.seq && strcmp(e.id, p.id) == 0) {
      if (!reached(now, e.at + t_.dedupTtlMs + 1)) return true;
      e.used = false;  // expirou
    }
  }
  SeenEntry& e = seen_[seenNext_];
  seenNext_ = (seenNext_ + 1) % kDedup;
  e.used = true;
  memcpy(e.id, p.id, sizeof e.id);
  e.seq = p.seq;
  e.at = now;
  return false;
}

void Link::onFrame(const Frame& f, uint32_t now) {
  Packet p{};
  if (!protoDecode(f.line, f.len, p)) return;
  if (strcmp(p.id, myId_) == 0) return;
  hasPeer_ = true;
  lastPeerRx_ = now;

  if (p.type == PktType::ACK) {
    handleAck(p.ackSeq);
    return;
  }
  if (protoNeedsAck(p.type)) {
    Packet a = make(PktType::ACK);
    a.ackSeq = p.seq;
    transmit(a);
  }
  if (isDuplicate(p, now)) return;
  sink_.onPacket(p, f);
}

void Link::tick(uint32_t now) {
  if (msg_.active && reached(now, msg_.nextMs)) {
    if (msg_.attempts >= t_.msgMaxAttempts) {
      msg_.active = false;
      sink_.onDelivery(msg_.pkt.seq, Delivery::Failed);
    } else {
      ++msg_.attempts;
      msg_.nextMs = now + t_.ackTimeoutMs + jitter();
      transmit(msg_.pkt);
    }
  }
}
