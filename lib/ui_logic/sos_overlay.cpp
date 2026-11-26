#include "sos_overlay.h"

bool SosOverlay::mineActive() const {
  return pendingStart_ || mine_ == SosState::Sending || mine_ == SosState::Received;
}

void SosOverlay::queue(SosAction a) {
  if (qn_ < 2) q_[qn_++] = a;
}

void SosOverlay::requestCountdown(uint32_t now) {
  if (countdown_ || mineActive()) return;
  countdown_ = true;
  cdStart_ = now;
  seenOpen_ = false;
}

void SosOverlay::sync(SosState mine, bool peerSos, uint32_t) {
  // Só um estado "ativo" confirma o StartSos: um Seen antigo ainda no rádio não conta.
  if (mine == SosState::Sending || mine == SosState::Received) pendingStart_ = false;
  if (mine == SosState::Seen && mine_ != SosState::Seen) seenOpen_ = true;
  if (mine != SosState::Seen) seenOpen_ = false;
  mine_ = mine;
  if (!peerSos) peerSuppressed_ = false;
  peer_ = peerSos && !peerSuppressed_;
}

Overlay SosOverlay::top() const {
  if (peer_) return Overlay::PeerSos;
  if (countdown_) return Overlay::Countdown;
  if (pendingStart_ || mine_ == SosState::Sending) return Overlay::Sending;
  if (mine_ == SosState::Received) return Overlay::Received;
  if (seenOpen_) return Overlay::Seen;
  return Overlay::None;
}

bool SosOverlay::showMineStrip() const { return peer_ && (countdown_ || mineActive()); }

uint8_t SosOverlay::countdownLeft(uint32_t now) const {
  uint32_t el = now - cdStart_;
  if (el >= kCountdownMs) return 0;
  return (uint8_t)(10 - el / 1000);
}

SosAction SosOverlay::onGesture(Gesture g, uint32_t now) {
  // Segurar 2 s sobre Visto ou sobre o SOS do amigo também pede um SOS meu.
  if (g == Gesture::Hold2s && !countdown_ && !mineActive()) {
    requestCountdown(now);
    return SosAction::None;
  }
  switch (top()) {
    case Overlay::PeerSos:
      if (g == Gesture::Click) {
        peer_ = false;
        peerSuppressed_ = true;
        queue(SosAction::OpenFriend);
        return SosAction::SendSeen;
      }
      if (g == Gesture::Hold3s) {
        if (countdown_) {
          countdown_ = false;
          return SosAction::None;
        }
        if (mineActive()) {
          pendingStart_ = false;
          return SosAction::CancelSos;
        }
      }
      return SosAction::None;
    case Overlay::Countdown:
      if (g == Gesture::Click) countdown_ = false;
      return SosAction::None;
    case Overlay::Sending:
    case Overlay::Received:
      if (g == Gesture::Hold3s) {
        pendingStart_ = false;
        return SosAction::CancelSos;
      }
      return SosAction::None;
    case Overlay::Seen:
      if (g == Gesture::Click) {
        seenOpen_ = false;
        return SosAction::CloseOverlay;
      }
      return SosAction::None;
    case Overlay::None:
      return SosAction::None;
  }
  return SosAction::None;
}

SosAction SosOverlay::tick(uint32_t now) {
  if (qn_) {
    SosAction a = q_[0];
    q_[0] = q_[1];
    --qn_;
    return a;
  }
  if (countdown_ && now - cdStart_ >= kCountdownMs) {
    countdown_ = false;
    pendingStart_ = true;
    return SosAction::StartSos;
  }
  return SosAction::None;
}

bool WakePolicy::onInput(Gesture g, bool rotated, uint32_t now) {
  if (g == Gesture::None && !rotated) return false;
  if (!on_) {
    on_ = true;
    last_ = now;
    return g == Gesture::Hold2s;  // SOS nunca exige acordar antes
  }
  last_ = now;
  return true;
}

void WakePolicy::wake(uint32_t now) {
  on_ = true;
  last_ = now;
}

void WakePolicy::tick(uint32_t now, bool keepOn) {
  if (keepOn) {
    on_ = true;
    last_ = now;
    return;
  }
  if (on_ && now - last_ >= timeout_) on_ = false;
}
