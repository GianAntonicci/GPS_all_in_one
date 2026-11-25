#pragma once
// Protocolo de pacotes LoRa em texto ASCII.
// Frame: $<TIPO>,<ID>,<SEQ>,<campos...>*<CK>\n
// CK = XOR de todos os bytes entre '$' e '*', 2 hex maiúsculos.
// Somente bytes 0x20..0x7E + '\n': o byte de RSSI anexado pelo módulo é >= 0x80.

#include <cstddef>
#include <cstdint>

constexpr size_t MSG_MAX_LEN = 48;
constexpr size_t FRAME_MAX = 96;  // inclui '\n'

enum class PktType : uint8_t { MSG, POS, PING, ACK, SOS, SEEN, SOSOFF };

struct Packet {
  PktType type;
  char id[3];                   // 1-2 alfanuméricos
  uint8_t seq;
  char text[MSG_MAX_LEN + 1];   // MSG
  double lat, lon;              // POS, SOS
  int32_t alt;                  // POS (m)
  uint8_t bat;                  // POS, SOS (%)
  uint8_t ackSeq;               // ACK: seq confirmado; SEEN: seq do SOS que foi visto
};

uint8_t protoChecksum(const char* s, size_t n);

// Retorna o tamanho escrito (incluindo '\n'), ou 0 se o pacote é inválido ou não cabe.
size_t protoEncode(const Packet& p, char* out, size_t cap);

// `line` sem '\n'. Retorna false para frame malformado ou checksum inválido.
bool protoDecode(const char* line, size_t len, Packet& out);

// Copia `in` para `out` deixando só ASCII imprimível; '$', '*' e não-ASCII viram '?'.
// Corta em MSG_MAX_LEN (ou cap-1, o menor).
void protoSanitize(const char* in, char* out, size_t cap);

bool protoNeedsAck(PktType t);

const char* protoTypeName(PktType t);

// ID padrão de 2 caracteres [0-9A-Z] derivado do MAC inteiro (hash), para reduzir colisões.
void protoIdFromMac(const uint8_t mac[6], char out[3]);
