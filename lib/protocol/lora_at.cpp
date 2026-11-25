#include "lora_at.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {

// Copia `in` sem espaços/CR/LF para `out`.
void compact(const char* in, char* out, size_t cap) {
  size_t o = 0;
  for (; *in && o + 1 < cap; ++in)
    if (*in != ' ' && *in != '\r' && *in != '\n') out[o++] = *in;
  out[o] = 0;
}

bool parseNum(const char* s, int base, long& v) {
  if (!*s) return false;
  char* end = nullptr;
  v = strtol(s, &end, base);
  return *end == 0;
}

}  // namespace

size_t loraDesiredSettings(const char* channelHex, AtSetting* out, size_t cap) {
  const AtSetting s[] = {
      {"SWITCH", "0"},  // M0/M1 controlados por AT: pinos do ESP ficam como entrada
      {"MODE", "0"},    // transparente
      {"LEVEL", "0"},   // SF11/BW125/CR4-8, 336 bit/s: alcance máximo
      {"CHANNEL", channelHex},
      {"POWE", "22"},
      {"DRSSI", "1"},   // anexa byte de RSSI ao pacote recebido
      {"PACKET", "3"},  // 230 bytes
      {"BAUD", "3"},    // 9600
  };
  const size_t n = sizeof s / sizeof s[0];
  if (cap < n) return 0;
  for (size_t i = 0; i < n; ++i) out[i] = s[i];
  return n;
}

bool atParseQuery(const char* line, const char* key, char* value, size_t cap) {
  char c[64];
  compact(line, c, sizeof c);
  if (c[0] != '+') return false;
  const char* eq = strchr(c, '=');
  if (!eq) return false;
  size_t kl = eq - (c + 1);
  if (kl != strlen(key)) return false;
  for (size_t i = 0; i < kl; ++i)
    if (toupper((unsigned char)c[1 + i]) != toupper((unsigned char)key[i])) return false;
  const char* v = eq + 1;
  if (!*v || strlen(v) + 1 > cap) return false;
  strcpy(value, v);
  return true;
}

bool atIsOk(const char* line) {
  char c[8];
  compact(line, c, sizeof c);
  return strcmp(c, "OK") == 0;
}

bool atIsError(const char* line) {
  return strncmp(line, "ERROR", 5) == 0 || strncmp(line, "EEROR", 5) == 0;
}

bool atValueEquals(const char* key, const char* have, const char* want) {
  int base = strcmp(key, "CHANNEL") == 0 ? 16 : 10;
  long a, b;
  return parseNum(have, base, a) && parseNum(want, base, b) && a == b;
}
