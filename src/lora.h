#pragma once
// Driver do DX-LR22 (UART transparente). Só move bytes e configura o módulo por AT.
#include <cstddef>
#include <cstdint>
#include "framer.h"

enum class LoraStatus : uint8_t {
  Ok,           // configurado e em modo transparente
  ConfigError,  // respondeu, mas algum comando AT falhou (config pode diferir do amigo)
  NoResponse,   // não entrou/saiu do modo AT: tentar de novo mais tarde
};
LoraStatus loraBegin();                    // pinos, Serial2, sessão AT
void loraPoll(Framer& f, uint32_t now);    // lê todos os bytes disponíveis
bool loraWrite(const char* d, size_t n);   // espera AUX baixo (até 3 s) e escreve o frame de uma vez
