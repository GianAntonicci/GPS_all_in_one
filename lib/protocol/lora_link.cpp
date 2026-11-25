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
  if (sos_ == SosState::Sending) {
    for (uint8_t i = 0; i < sosSeqCount_; ++i) {
      if (sosSeqs_[i] == ackSeq) {
        setSos(SosState::Received);
        sosNextMs_ = sosLastSendMs_ + t_.sosSlowMs + jitter();
        break;
      }
    }
  }
  if (sosOff_.active && sosOff_.pkt.seq == ackSeq) sosOff_.active = false;
  if (seenOut_.active && seenOut_.pkt.seq == ackSeq) seenOut_.active = false;
}

void Link::setSos(SosState s) {
  if (s == sos_) return;
  sos_ = s;
  sink_.onSosState(s);
}

void Link::setPeerSos(bool a) {
  if (a == peerSos_) return;
  peerSos_ = a;
  sink_.onPeerSos(a);
}

uint32_t Link::sosInterval(uint8_t attempts) const {
  return attempts < t_.sosFastAttempts ? t_.sosFastMs : t_.sosSlowMs;
}

void Link::sendSosNow(uint32_t now) {
  Packet p = make(PktType::SOS);
  p.lat = sosLat_;
  p.lon = sosLon_;
  p.bat = sosBat_;
  if (sosSeqCount_ < kSosSeqs) {
    sosSeqs_[sosSeqCount_++] = p.seq;
  } else {
    memmove(sosSeqs_, sosSeqs_ + 1, kSosSeqs - 1);
    sosSeqs_[kSosSeqs - 1] = p.seq;
  }
  if (sosAttempts_ < 255) ++sosAttempts_;
  sosLastSendMs_ = now;
  uint32_t iv = (sos_ == SosState::Received) ? t_.sosSlowMs : sosInterval(sosAttempts_);
  sosNextMs_ = now + iv + jitter();
  transmit(p);
}

void Link::startRepeat(Pending& r, PktType t, uint32_t now) {
  r.active = true;
  r.pkt = make(t);
  r.attempts = 1;
  r.nextMs = now + sosInterval(1) + jitter();
  transmit(r.pkt);
}

void Link::tickRepeat(Pending& r, uint32_t now) {
  if (!r.active || !reached(now, r.nextMs)) return;
  if (r.attempts < 255) ++r.attempts;
  r.nextMs = now + sosInterval(r.attempts) + jitter();
  transmit(r.pkt);
}

void Link::startSos(double lat, double lon, uint8_t bat, uint32_t now) {
  sosOff_.active = false;
  sosLat_ = lat;
  sosLon_ = lon;
  sosBat_ = bat;
  sosAttempts_ = 0;
  sosSeqCount_ = 0;
  setSos(SosState::Sending);
  sendSosNow(now);
}

void Link::updateSosPosition(double lat, double lon, uint8_t bat) {
  sosLat_ = lat;
  sosLon_ = lon;
  sosBat_ = bat;
}

void Link::cancelSos(uint32_t now) {
  bool wasActive = sos_ == SosState::Sending || sos_ == SosState::Received;
  setSos(SosState::Idle);
  if (wasActive) startRepeat(sosOff_, PktType::SOSOFF, now);
}

void Link::confirmSeen(uint32_t now) {
  if (!peerSos_) return;
  setPeerSos(false);
  startRepeat(seenOut_, PktType::SEEN, now);
}

bool Link::isDuplicate(const Packet& p, uint32_t now) {
  for (auto& e : dedup_) {
    if (e.used && e.seq == p.seq && strcmp(e.id, p.id) == 0) {
      if (!reached(now, e.at + t_.dedupTtlMs + 1)) return true;
      e.used = false;  // expirou
    }
  }
  SeenEntry& e = dedup_[dedupNext_];
  dedupNext_ = (dedupNext_ + 1) % kDedup;
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
  switch (p.type) {
    case PktType::SOS:
      // Enquanto meu SEEN não foi confirmado, SOS em trânsito não reacende o alarme.
      if (!seenOut_.active) setPeerSos(true);
      break;
    case PktType::SOSOFF:
      seenOut_.active = false;
      setPeerSos(false);
      break;
    case PktType::SEEN:
      if (sos_ == SosState::Sending || sos_ == SosState::Received) setSos(SosState::Seen);
      break;
    default:
      break;
  }
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
  if ((sos_ == SosState::Sending || sos_ == SosState::Received) && reached(now, sosNextMs_))
    sendSosNow(now);
  tickRepeat(sosOff_, now);
  tickRepeat(seenOut_, now);
}
