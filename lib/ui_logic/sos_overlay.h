#pragma once
// Overlays de SOS (contagem, enviando, recebido, visto, SOS do amigo) e regra da tela apagada.
#include <cstdint>
#include "input_logic.h"
#include "lora_link.h"

enum class Overlay : uint8_t { None, Countdown, Sending, Received, Seen, PeerSos };
enum class SosAction : uint8_t { None, StartSos, CancelSos, SendSeen, OpenFriend, CloseOverlay };

class SosOverlay {
 public:
  static constexpr uint32_t kCountdownMs = 10000;
  void requestCountdown(uint32_t now);                   // Hold2s ou queda (etapa 5)
  void sync(SosState mine, bool peerSos, uint32_t now);  // estado vindo do rádio
  SosAction onGesture(Gesture g, uint32_t now);
  SosAction tick(uint32_t now);  // ações pendentes primeiro; depois fim da contagem
  Overlay top() const;
  bool showMineStrip() const;    // SOS do amigo em cima com meu SOS/contagem ativo
  uint8_t countdownLeft(uint32_t now) const;
  bool active() const { return top() != Overlay::None; }

 private:
  bool mineActive() const;
  void queue(SosAction a);

  bool countdown_ = false;
  uint32_t cdStart_ = 0;
  bool pendingStart_ = false;   // StartSos emitido, rádio ainda não confirmou
  SosState mine_ = SosState::Idle;
  bool peer_ = false;
  bool peerSuppressed_ = false; // "Vi" enviado; ignora peer=true até o rádio dizer false
  bool seenOpen_ = false;
  SosAction q_[2] = {SosAction::None, SosAction::None};
  uint8_t qn_ = 0;
};

class WakePolicy {
 public:
  explicit WakePolicy(uint32_t timeoutMs = 30000) : timeout_(timeoutMs) {}
  // true = processar a entrada; false = só acordou a tela (Hold2s sempre passa).
  bool onInput(Gesture g, bool rotated, uint32_t now);
  void wake(uint32_t now);
  void tick(uint32_t now, bool keepOn);
  bool screenOn() const { return on_; }

 private:
  uint32_t timeout_;
  bool on_ = true;
  uint32_t last_ = 0;
};
