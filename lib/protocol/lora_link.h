#pragma once
// Camada de enlace entre os dois aparelhos: ACK, reenvio, deduplicação e SOS.
// Pura (sem Arduino): o tempo e o aleatório são injetados.

#include <cstddef>
#include <cstdint>
#include "framer.h"
#include "protocol.h"

enum class Delivery : uint8_t { Delivered, Failed };
enum class SosState : uint8_t { Idle, Sending, Received, Seen };

struct LinkTiming {
  // SF11/BW125/CR4-8 (336 bit/s): MSG de 48 caracteres ~2,2 s no ar + ACK ~1 s.
  uint32_t ackTimeoutMs = 7000, jitterMaxMs = 1000;
  uint8_t msgMaxAttempts = 3, sosFastAttempts = 6;
  uint32_t sosFastMs = 10000, sosSlowMs = 30000, dedupTtlMs = 60000;
  // Jitter do SOS/SEEN/SOSOFF maior que o airtime (~1,5 s) para dois SOS simultâneos
  // não colidirem sempre; atraso inicial aleatório pelo mesmo motivo.
  uint32_t sosJitterMaxMs = 5000, sosStartDelayMaxMs = 2000;
  // Depois de "Vi", SOS do amigo não reacende o alarme por este tempo (SOS em trânsito).
  uint32_t seenMuteMs = 40000;
};

class LinkSink {
 public:
  // Retorna quantos ms a escrita bloqueou (espera do AUX); os timers contam a partir dali.
  virtual uint32_t sendFrame(const char* data, size_t len) = 0;
  virtual void onPacket(const Packet& p, const Frame& f) = 0;  // pacote novo do amigo
  virtual void onDelivery(uint8_t seq, Delivery d) = 0;          // MSG/PING
  virtual void onSosState(SosState s) = 0;                       // o MEU SOS
  virtual void onPeerSos(bool active) = 0;                       // SOS do AMIGO
  virtual void onIdConflict() = 0;  // chegou frame com o MEU ID: outro aparelho usa o mesmo ID
  virtual ~LinkSink() = default;
};

class Link {
 public:
  Link(LinkSink& sink, const char* myId, uint8_t initialSeq, uint32_t (*rand32)(),
       LinkTiming t = {});

  void onFrame(const Frame& f, uint32_t now);
  void tick(uint32_t now);

  int sendMsg(const char* text, uint32_t now);  // seq, ou -1 se já há MSG/PING pendente
  int sendPing(uint32_t now);
  void sendPos(double lat, double lon, int32_t alt, uint8_t bat, uint32_t now);

  // SOS em dois níveis (spec): Sending -> Received (ACK) -> Seen (SEEN do amigo).
  void startSos(double lat, double lon, uint8_t bat, uint32_t now);
  void updateSosPosition(double lat, double lon, uint8_t bat);
  void cancelSos(uint32_t now);    // envia SOSOFF até ACK; estado -> Idle
  void confirmSeen(uint32_t now);  // usuário apertou "Vi": envia SEEN até ACK
  SosState sosState() const { return sos_; }
  bool peerSosActive() const { return peerSos_; }

  uint32_t lastPeerRxMs() const { return lastPeerRx_; }
  bool hasPeer() const { return hasPeer_; }

 private:
  struct Pending {
    bool active = false;
    Packet pkt{};
    uint8_t attempts = 0;
    uint32_t nextMs = 0;
  };
  static constexpr size_t kSosSeqs = 8;

  void setSos(SosState s);
  void sendSosNow(uint32_t now);
  uint32_t sosInterval(uint8_t attempts) const;
  void startRepeat(Pending& r, PktType t, uint32_t now);
  void tickRepeat(Pending& r, uint32_t now);
  void setPeerSos(bool a);
  struct SeenEntry {
    bool used = false;
    char id[3] = {};
    uint8_t seq = 0;
    uint32_t at = 0;
  };
  static constexpr size_t kDedup = 16;

  uint8_t nextSeq() { return seq_++; }
  Packet make(PktType t);
  uint32_t transmit(const Packet& p);  // retorna ms bloqueados
  uint32_t jitter();
  uint32_t sosJitter();
  int startReliable(Packet p, uint32_t now);
  void handleAck(uint8_t ackSeq);
  bool isDuplicate(const Packet& p, uint32_t now);

  LinkSink& sink_;
  char myId_[3];
  uint8_t seq_;
  uint32_t (*rand_)();
  LinkTiming t_;

  Pending msg_;
  SeenEntry dedup_[kDedup];
  size_t dedupNext_ = 0;

  // Meu SOS
  SosState sos_ = SosState::Idle;
  double sosLat_ = 0, sosLon_ = 0;
  uint8_t sosBat_ = 0;
  uint8_t sosAttempts_ = 0;
  uint32_t sosNextMs_ = 0, sosLastSendMs_ = 0;
  uint8_t sosSeqs_[kSosSeqs] = {};
  uint8_t sosSeqCount_ = 0;
  Pending sosOff_;   // SOSOFF até ACK

  // SOS do amigo
  bool peerSos_ = false;
  uint8_t peerSosSeq_ = 0;  // seq do último SOS do amigo (vai no SEEN)
  uint32_t seenAtMs_ = 0;
  Pending seenOut_;  // SEEN até ACK

  bool hasPeer_ = false;
  uint32_t lastPeerRx_ = 0;
};
