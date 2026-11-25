#pragma once
// Comandos AT do DX-LR22 (formato do guia UART DX-SMART): AT+<KEY><valor>\r\n,
// consulta AT+<KEY> -> "+KEY=valor", sucesso "OK", erro "ERROR=<n>".

#include <cstddef>

struct AtSetting {
  const char* key;
  const char* value;
};

// Preenche a configuração desejada (ordem fixa). `channelHex` precisa continuar vivo.
// Retorna o número de itens, ou 0 se `cap` for pequeno demais.
size_t loraDesiredSettings(const char* channelHex, AtSetting* out, size_t cap);

// "+LEVEL =2" / "+ CHANNEL=41" -> value. Ignora espaços e CR; key sem diferenciar maiúsculas.
bool atParseQuery(const char* line, const char* key, char* value, size_t cap);

bool atIsOk(const char* line);
bool atIsError(const char* line);  // "ERROR..." ou "EEROR..." (grafia do manual)

// CHANNEL compara como hexadecimal; as demais chaves, como inteiro decimal.
bool atValueEquals(const char* key, const char* have, const char* want);
