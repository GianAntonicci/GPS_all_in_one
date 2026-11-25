#pragma once
// Driver do DX-LR22 (UART transparente). Só move bytes e configura o módulo por AT.
#include <cstddef>
#include <cstdint>
#include "framer.h"

bool loraBegin();                          // pinos, Serial2, sessão AT; false se o módulo não respondeu
void loraPoll(Framer& f, uint32_t now);    // lê todos os bytes disponíveis
bool loraWrite(const char* d, size_t n);   // espera AUX baixo (até 3 s) e escreve o frame de uma vez
