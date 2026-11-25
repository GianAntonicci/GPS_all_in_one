#pragma once
// Harness de teste: dois Link ligados por um "ar" controlado pelo teste.
#include <cstring>
#include <string>
#include <vector>
#include "lora_link.h"

struct FakeSink : LinkSink {
  std::vector<Packet> sent;       // tudo que este nó transmitiu (decodificado)
  std::vector<std::string> raw;   // frames crus
  size_t delivered = 0;           // quantos de `sent` já foram entregues ao outro nó
  std::vector<Packet> rx;         // onPacket
  std::vector<std::pair<uint8_t, Delivery>> deliveries;
  std::vector<SosState> sosStates;
  std::vector<bool> peerSosEvents;
  int idConflicts = 0;
  uint32_t blockMs = 0;  // simula loraWrite bloqueando até o AUX liberar

  uint32_t sendFrame(const char* data, size_t len) override {
    std::string s(data, len);
    raw.push_back(s);
    Packet p{};
    // remove '\n'
    bool ok = protoDecode(s.c_str(), len - 1, p);
    if (!ok) p.type = PktType::ACK, p.id[0] = '!';  // marca frame inválido
    sent.push_back(p);
    return blockMs;
  }
  void onIdConflict() override { ++idConflicts; }
  void onPacket(const Packet& p, const Frame&) override { rx.push_back(p); }
  void onDelivery(uint8_t seq, Delivery d) override { deliveries.push_back({seq, d}); }
  void onSosState(SosState s) override { sosStates.push_back(s); }
  void onPeerSos(bool a) override { peerSosEvents.push_back(a); }

  size_t countSent(PktType t) const {
    size_t n = 0;
    for (auto& p : sent) n += p.type == t;
    return n;
  }
  const Packet* lastSent(PktType t) const {
    for (auto it = sent.rbegin(); it != sent.rend(); ++it)
      if (it->type == t) return &*it;
    return nullptr;
  }
};

inline Frame frameFromRaw(const std::string& s) {
  Frame f{};
  size_t n = s.size();
  if (n && s[n - 1] == '\n') --n;
  memcpy(f.line, s.data(), n);
  f.line[n] = 0;
  f.len = n;
  f.hasRssi = true;
  f.rssi = -90;
  return f;
}

// Entrega os frames ainda não entregues de `from` para `to`.
inline size_t deliver(FakeSink& from, Link& to, uint32_t now) {
  size_t n = 0;
  while (from.delivered < from.raw.size()) {
    to.onFrame(frameFromRaw(from.raw[from.delivered++]), now);
    ++n;
  }
  return n;
}

// Descarta (perde no ar) os frames ainda não entregues.
inline void drop(FakeSink& from) { from.delivered = from.raw.size(); }

// Troca frames nos dois sentidos até não haver mais nada.
inline void pumpAll(FakeSink& sa, Link& a, FakeSink& sb, Link& b, uint32_t now) {
  for (int i = 0; i < 20; ++i) {
    size_t n = deliver(sa, b, now) + deliver(sb, a, now);
    if (!n) return;
  }
}

inline uint32_t rand0() { return 0; }
inline uint32_t rand500() { return 500; }
inline uint32_t& randValue() { static uint32_t v = 0; return v; }
inline uint32_t randVar() { return randValue(); }
