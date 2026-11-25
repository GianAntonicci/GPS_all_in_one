#include "track_logic.h"

#include "geo.h"

bool TrackPolicy::due(uint32_t now, bool hasFix, double lat, double lon, bool& newSegment) {
  if (!hasFix) {
    newSeg_ = true;
    have_ = false;
    return false;
  }
  newSegment = newSeg_;
  if (!have_) return true;
  return now - last_ >= kPeriodMs || distanceM(lat_, lon_, lat, lon) >= kMoveM;
}

void TrackPolicy::accepted(uint32_t now, double lat, double lon) {
  newSeg_ = false;
  have_ = true;
  last_ = now;
  lat_ = lat;
  lon_ = lon;
}

TrackBuffer::TrackBuffer(TrackPt* storage, uint32_t capacity) : p_(storage), cap_(capacity) {}

void TrackBuffer::add(double lat, double lon, bool newSegment) {
  if (!p_ || cap_ < 2) return;
  if (n_ > 0 && !newSegment && distanceM(p_[n_ - 1].lat, p_[n_ - 1].lon, lat, lon) < kMinStepM)
    return;
  if (n_ >= cap_) decimate();
  p_[n_] = TrackPt{(float)lat, (float)lon, newSegment || n_ == 0 || pendingStart_};
  pendingStart_ = false;
  ++n_;
}

void TrackBuffer::decimate() {
  uint32_t w = 0;
  bool carry = false;
  for (uint32_t i = 0; i < n_; ++i) {
    if (i % 2 == 1) {
      carry = carry || p_[i].segStart;
      continue;
    }
    TrackPt q = p_[i];
    if (carry) q.segStart = true;
    carry = false;
    p_[w++] = q;
  }
  n_ = w;
  // Início de segmento num último ponto ímpar descartado: o próximo ponto adicionado o herda.
  if (carry) pendingStart_ = true;
}
