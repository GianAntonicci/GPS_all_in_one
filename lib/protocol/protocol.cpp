#include "protocol.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct TypeName {
  PktType type;
  const char* name;
};

const TypeName kTypes[] = {
    {PktType::MSG, "MSG"},   {PktType::POS, "POS"},   {PktType::PING, "PING"},
    {PktType::ACK, "ACK"},   {PktType::SOS, "SOS"},   {PktType::SEEN, "SEEN"},
    {PktType::SOSOFF, "SOSOFF"},
};

bool isAlnum(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool validId(const char* id) {
  size_t n = strnlen(id, 3);
  if (n < 1 || n > 2) return false;
  for (size_t i = 0; i < n; ++i)
    if (!isAlnum(id[i])) return false;
  return true;
}

bool safeTextChar(char c) {
  return c >= 0x20 && c <= 0x7E && c != '$' && c != '*';
}

bool validText(const char* t) {
  size_t n = strnlen(t, MSG_MAX_LEN + 1);
  if (n > MSG_MAX_LEN) return false;
  for (size_t i = 0; i < n; ++i)
    if (!safeTextChar(t[i])) return false;
  return true;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Campo [b, e) inteiro como long dentro de [lo, hi].
bool parseLong(const char* b, const char* e, long lo, long hi, long& out) {
  if (b == e || e - b > 15) return false;
  char tmp[16];
  memcpy(tmp, b, e - b);
  tmp[e - b] = 0;
  char* end = nullptr;
  errno = 0;
  long v = strtol(tmp, &end, 10);
  if (errno || *end != 0 || v < lo || v > hi) return false;
  out = v;
  return true;
}

bool parseDouble(const char* b, const char* e, double lo, double hi, double& out) {
  if (b == e || e - b > 23) return false;
  char tmp[24];
  memcpy(tmp, b, e - b);
  tmp[e - b] = 0;
  char* end = nullptr;
  errno = 0;
  double v = strtod(tmp, &end);
  if (errno || *end != 0 || !(v >= lo && v <= hi)) return false;
  out = v;
  return true;
}

}  // namespace

uint8_t protoChecksum(const char* s, size_t n) {
  uint8_t x = 0;
  for (size_t i = 0; i < n; ++i) x ^= (uint8_t)s[i];
  return x;
}

const char* protoTypeName(PktType t) {
  for (const auto& tn : kTypes)
    if (tn.type == t) return tn.name;
  return "?";
}

bool protoNeedsAck(PktType t) {
  return t == PktType::MSG || t == PktType::PING || t == PktType::SOS || t == PktType::SEEN ||
         t == PktType::SOSOFF;
}

size_t protoEncode(const Packet& p, char* out, size_t cap) {
  if (!validId(p.id)) return 0;
  char body[FRAME_MAX];
  int n;
  const char* tn = protoTypeName(p.type);
  switch (p.type) {
    case PktType::MSG:
      if (!validText(p.text)) return 0;
      n = snprintf(body, sizeof body, "%s,%s,%u,%s", tn, p.id, p.seq, p.text);
      break;
    case PktType::POS:
      if (p.hasBaroAlt) {
        n = snprintf(body, sizeof body, "%s,%s,%u,%.5f,%.5f,%ld,%u,%ld", tn, p.id, p.seq, p.lat,
                     p.lon, (long)p.alt, p.bat, (long)p.baroAlt);
      } else {
        n = snprintf(body, sizeof body, "%s,%s,%u,%.5f,%.5f,%ld,%u", tn, p.id, p.seq, p.lat, p.lon,
                     (long)p.alt, p.bat);
      }
      break;
    case PktType::ACK:
    case PktType::SEEN:
      n = snprintf(body, sizeof body, "%s,%s,%u,%u", tn, p.id, p.seq, p.ackSeq);
      break;
    case PktType::SOS:
      n = snprintf(body, sizeof body, "%s,%s,%u,%.5f,%.5f,%u", tn, p.id, p.seq, p.lat, p.lon,
                   p.bat);
      break;
    case PktType::PING:
    case PktType::SOSOFF:
      n = snprintf(body, sizeof body, "%s,%s,%u", tn, p.id, p.seq);
      break;
    default:
      return 0;
  }
  if (n <= 0 || (size_t)n >= sizeof body) return 0;
  // '$' + body + '*' + 2 hex + '\n' + '\0'
  if ((size_t)n + 6 > cap || (size_t)n + 5 > FRAME_MAX) return 0;
  int m = snprintf(out, cap, "$%s*%02X\n", body, protoChecksum(body, n));
  return (m > 0 && (size_t)m < cap) ? (size_t)m : 0;
}

bool protoDecode(const char* line, size_t len, Packet& out) {
  // Mínimo: "$X,I,0*CK"
  if (len < 9 || len >= FRAME_MAX || line[0] != '$') return false;
  if (line[len - 3] != '*') return false;
  for (size_t i = 0; i < len; ++i) {
    uint8_t c = (uint8_t)line[i];
    if (c < 0x20 || c > 0x7E) return false;
  }
  const char* body = line + 1;
  const char* star = line + len - 3;
  int h = hexVal(star[1]), l = hexVal(star[2]);
  if (h < 0 || l < 0) return false;
  if (protoChecksum(body, star - body) != (uint8_t)(h * 16 + l)) return false;
  if (memchr(body, '*', star - body) || memchr(body, '$', star - body)) return false;

  // Divide em até 8 campos; o texto de MSG é o 4º campo e vai até o '*'.
  const char* fb[8];
  const char* fe[8];
  int nf = 0;
  const char* p = body;
  while (nf < 8) {
    const char* c = (const char*)memchr(p, ',', star - p);
    fb[nf] = p;
    fe[nf] = c ? c : star;
    ++nf;
    if (!c) break;
    p = c + 1;
    if (nf == 3) {
      // Nome do tipo já conhecido: para MSG, o resto inteiro é o texto.
      size_t tl = fe[0] - fb[0];
      if (tl == 3 && memcmp(fb[0], "MSG", 3) == 0) {
        fb[nf] = p;
        fe[nf] = star;
        ++nf;
        break;
      }
    }
  }
  if (nf < 3) return false;

  Packet r{};
  bool found = false;
  size_t tl = fe[0] - fb[0];
  for (const auto& tn : kTypes) {
    if (strlen(tn.name) == tl && memcmp(tn.name, fb[0], tl) == 0) {
      r.type = tn.type;
      found = true;
      break;
    }
  }
  if (!found) return false;

  size_t il = fe[1] - fb[1];
  if (il < 1 || il > 2) return false;
  memcpy(r.id, fb[1], il);
  r.id[il] = 0;
  if (!validId(r.id)) return false;

  long v;
  if (!parseLong(fb[2], fe[2], 0, 255, v)) return false;
  r.seq = (uint8_t)v;

  switch (r.type) {
    case PktType::MSG: {
      if (nf != 4) return false;
      size_t n = fe[3] - fb[3];
      if (n > MSG_MAX_LEN) return false;
      memcpy(r.text, fb[3], n);
      r.text[n] = 0;
      break;
    }
    case PktType::POS: {
      if (nf != 7 && nf != 8) return false;
      long alt, bat;
      if (!parseDouble(fb[3], fe[3], -90, 90, r.lat)) return false;
      if (!parseDouble(fb[4], fe[4], -180, 180, r.lon)) return false;
      if (!parseLong(fb[5], fe[5], -10000, 100000, alt)) return false;
      if (!parseLong(fb[6], fe[6], 0, 100, bat)) return false;
      r.alt = (int32_t)alt;
      r.bat = (uint8_t)bat;
      if (nf == 8) {
        long bAlt;
        if (!parseLong(fb[7], fe[7], -10000, 100000, bAlt)) return false;
        r.hasBaroAlt = true;
        r.baroAlt = (int32_t)bAlt;
      }
      break;
    }
    case PktType::ACK:
    case PktType::SEEN: {
      if (nf != 4) return false;
      if (!parseLong(fb[3], fe[3], 0, 255, v)) return false;
      r.ackSeq = (uint8_t)v;
      break;
    }
    case PktType::SOS: {
      if (nf != 6) return false;
      long bat;
      if (!parseDouble(fb[3], fe[3], -90, 90, r.lat)) return false;
      if (!parseDouble(fb[4], fe[4], -180, 180, r.lon)) return false;
      if (!parseLong(fb[5], fe[5], 0, 100, bat)) return false;
      r.bat = (uint8_t)bat;
      break;
    }
    case PktType::PING:
    case PktType::SOSOFF:
      if (nf != 3) return false;
      break;
  }
  out = r;
  return true;
}

void protoSanitize(const char* in, char* out, size_t cap) {
  if (cap == 0) return;
  size_t max = cap - 1 < MSG_MAX_LEN ? cap - 1 : MSG_MAX_LEN;
  size_t o = 0;
  for (size_t i = 0; in[i] && o < max; ++i) out[o++] = safeTextChar(in[i]) ? in[i] : '?';
  out[o] = 0;
}

void protoIdFromMac(const uint8_t mac[6], char out[3]) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (int i = 0; i < 6; ++i) {
    h ^= mac[i];
    h *= 16777619u;
  }
  const char* digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  uint32_t v = h % (36 * 36);
  out[0] = digits[v / 36];
  out[1] = digits[v % 36];
  out[2] = 0;
}
