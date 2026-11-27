#pragma once
// Estado compartilhado entre as tasks. Sempre acessar entre stateLock()/stateUnlock().
#include <cstdint>
#include "compass.h"
#include "gps_logic.h"
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
  char peerId[3] = "";      // ID do amigo (último pacote válido)
  uint8_t sosAttempts = 0;  // tentativas do meu SOS atual

  // ---- Meu GPS ----
  bool gpsFix = false;      // fix atual válido (idade < 3 s)
  bool myPosKnown = false;  // lat/lon abaixo valem (fix atual ou última posição salva)
  double lat = 0, lon = 0;
  float altM = 0, speedKmh = 0;
  uint8_t sats = 0;
  double courseDeg = 0;     // rumo vindo do GPS (gps.course)
  bool courseKnown = false, courseStale = true;
  bool timeValid = false;
  DateTime localTime{};     // UTC-3
  DateTime utcTime{};       // hora do GPS em UTC (GPX)

  // ---- Rumo unificado (GPS em movimento >= 1,5 km/h; bússola calibrada parado/sem fix) ----
  double headingDeg = 0;
  bool headingKnown = false;
  bool headingStale = true;
  HeadingSource headingSource = HeadingSource::None;

  // ---- SD e trilha (task "sd") ----
  bool sdOk = false;            // cartão montado
  bool trackRecording = false;  // arquivo GPX aberto e gravando
  uint32_t trackPoints = 0;     // pontos no arquivo atual

  // ---- Bateria ----
  uint32_t batteryMv = 0;
  uint8_t batteryPct = 0;
  bool batteryLow = false;

  // ---- IMU / Bússola ICM-20948 (task "imu") ----
  bool imuOk = false;
  uint8_t imuAddr = 0;
  bool compassOk = false;
  bool compassCalibrated = false;
  float compassDeg = 0.0f;
  float magDeclination = -21.5f;

  // ---- UI ----
  bool tftOn = true;

  // ---- Scanner CC1101 (somente RX) ----
  bool cc1101Ok = false;
};

void stateInit();  // cria o mutex; chamar no setup() antes de iniciar tasks
SharedState& state();
void stateLock();
void stateUnlock();

// Recalcula headingDeg/headingKnown/headingStale/headingSource a partir do estado atual.
// Chamar com stateLock() já adquirido.
inline void stateRefreshHeading(SharedState& s) {
  HeadingInput in{};
  in.gpsFix = s.gpsFix;
  in.speedKmh = s.speedKmh;
  in.gpsCourseDeg = s.courseDeg;
  in.gpsCourseKnown = s.courseKnown;
  in.gpsCourseStale = s.courseStale;
  in.compassOk = s.compassOk;
  in.compassCalibrated = s.compassCalibrated;
  in.compassDeg = s.compassDeg;
  const HeadingResult r = selectHeading(in);
  s.headingDeg = r.deg;
  s.headingKnown = r.known;
  s.headingStale = r.stale;
  s.headingSource = r.source;
}
