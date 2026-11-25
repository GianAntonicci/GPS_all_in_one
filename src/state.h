#pragma once
// Estado compartilhado entre as tasks. Sempre acessar entre stateLock()/stateUnlock().
#include <cstdint>
#include "lora_link.h"

struct SharedState {
  bool loraOk = false;
  bool peerKnown = false;
  double peerLat = 0, peerLon = 0;
  int32_t peerAlt = 0;
  uint8_t peerBat = 0;
  uint32_t peerPosMs = 0;  // millis() da última posição do amigo
  uint32_t peerRxMs = 0;   // millis() do último pacote válido do amigo
  bool hasRssi = false;
  int16_t rssi = 0;
  SosState mySos = SosState::Idle;
  bool peerSos = false;
  bool idConflict = false;  // outro aparelho usando o mesmo ID: enlace não funciona
};

void stateInit();  // cria o mutex; chamar no setup() antes de iniciar tasks
SharedState& state();
void stateLock();
void stateUnlock();
