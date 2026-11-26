#include "cc1101.h"

#include <Arduino.h>
#include <SPI.h>
#include "config.h"
#include "scanner_model.h"
#include "spi_bus.h"
#include "state.h"

namespace {

// Registradores de configuração CC1101 (somente RX)
constexpr uint8_t kRegFsctrl1 = 0x0B;
constexpr uint8_t kRegFreq2   = 0x0D;
constexpr uint8_t kRegFreq1   = 0x0E;
constexpr uint8_t kRegFreq0   = 0x0F;
constexpr uint8_t kRegMdmcfg4 = 0x10;
constexpr uint8_t kRegMdmcfg3 = 0x11;
constexpr uint8_t kRegMdmcfg2 = 0x12;
constexpr uint8_t kRegMdmcfg1 = 0x13;
constexpr uint8_t kRegMdmcfg0 = 0x14;
constexpr uint8_t kRegMcsm0   = 0x18;
constexpr uint8_t kRegFoccfg  = 0x19;
constexpr uint8_t kRegAgcctrl2 = 0x1B;
constexpr uint8_t kRegAgcctrl1 = 0x1C;
constexpr uint8_t kRegAgcctrl0 = 0x1D;
constexpr uint8_t kRegFrend1  = 0x21;
constexpr uint8_t kRegFscal3  = 0x23;
constexpr uint8_t kRegFscal2  = 0x24;
constexpr uint8_t kRegFscal1  = 0x25;
constexpr uint8_t kRegFscal0  = 0x26;

// Strobes (sem STX: módulo estritamente somente-RX)
constexpr uint8_t kStrobeSres  = 0x30;
constexpr uint8_t kStrobeSrx   = 0x34;
constexpr uint8_t kStrobeSidle = 0x36;
constexpr uint8_t kStrobeSpwd  = 0x39;

// Registradores de status (lidos com bit READ | BURST = 0xC0)
constexpr uint8_t kStatVersion = 0x31;
constexpr uint8_t kStatRssi    = 0x34;
constexpr uint8_t kReadBurst   = 0xC0;

constexpr uint32_t kMisoTimeoutUs = 5000;
constexpr uint32_t kRssiSettleUs  = 1500;
const SPISettings kSpiCfg(4000000, MSBFIRST, SPI_MODE0);

bool gOk = false;

bool waitMisoLow() {
  const uint32_t t0 = micros();
  while (digitalRead(PIN_SPI_MISO) == HIGH) {
    if ((uint32_t)(micros() - t0) >= kMisoTimeoutUs) return false;
  }
  return true;
}

// Funções abaixo assumem spiLock() já adquirido pelo chamador.
bool strobeLocked(uint8_t cmd) {
  SPI.beginTransaction(kSpiCfg);
  digitalWrite(PIN_CC1101_CS, LOW);
  const bool ready = waitMisoLow();
  if (ready) SPI.transfer(cmd);
  digitalWrite(PIN_CC1101_CS, HIGH);
  SPI.endTransaction();
  return ready;
}

bool writeRegLocked(uint8_t addr, uint8_t val) {
  SPI.beginTransaction(kSpiCfg);
  digitalWrite(PIN_CC1101_CS, LOW);
  const bool ready = waitMisoLow();
  if (ready) {
    SPI.transfer(addr);
    SPI.transfer(val);
  }
  digitalWrite(PIN_CC1101_CS, HIGH);
  SPI.endTransaction();
  return ready;
}

bool readStatusLocked(uint8_t addr, uint8_t& out) {
  SPI.beginTransaction(kSpiCfg);
  digitalWrite(PIN_CC1101_CS, LOW);
  const bool ready = waitMisoLow();
  if (ready) {
    SPI.transfer(addr | kReadBurst);
    out = SPI.transfer(0x00);
  }
  digitalWrite(PIN_CC1101_CS, HIGH);
  SPI.endTransaction();
  return ready;
}

bool resetLocked() {
  // Sequência de reset manual do datasheet TI CC1101 (seção 19.1.2)
  digitalWrite(PIN_CC1101_CS, HIGH);
  delayMicroseconds(5);
  digitalWrite(PIN_CC1101_CS, LOW);
  delayMicroseconds(10);
  digitalWrite(PIN_CC1101_CS, HIGH);
  delayMicroseconds(45);

  SPI.beginTransaction(kSpiCfg);
  digitalWrite(PIN_CC1101_CS, LOW);
  bool ok = waitMisoLow();
  if (ok) {
    SPI.transfer(kStrobeSres);
    ok = waitMisoLow();
  }
  digitalWrite(PIN_CC1101_CS, HIGH);
  SPI.endTransaction();
  return ok;
}

void setCc1101State(bool ok) {
  gOk = ok;
  stateLock();
  state().cc1101Ok = ok;
  stateUnlock();
}

}  // namespace

bool cc1101Begin() {
  spiLock();
  bool ok = resetLocked();
  uint8_t ver = 0;
  if (ok) ok = readStatusLocked(kStatVersion, ver);

  if (!ok || (ver != 0x14 && ver != 0x04)) {
    spiUnlock();
    LOGE("CC1101 nao respondeu (ok=%d ver=0x%02X)", ok ? 1 : 0, ver);
    setCc1101State(false);
    return false;
  }

  // Configuração RX para varredura de RSSI (cristal 26 MHz, BW ~203 kHz, autocalibração IDLE->RX)
  ok = ok && writeRegLocked(kRegFsctrl1, 0x06);
  ok = ok && writeRegLocked(kRegMdmcfg4, 0x8B);
  ok = ok && writeRegLocked(kRegMdmcfg3, 0x83);
  ok = ok && writeRegLocked(kRegMdmcfg2, 0x00);  // 2-FSK, sem sync word
  ok = ok && writeRegLocked(kRegMdmcfg1, 0x22);
  ok = ok && writeRegLocked(kRegMdmcfg0, 0xF8);
  ok = ok && writeRegLocked(kRegMcsm0, 0x18);    // autocalibra ao ir de IDLE para RX
  ok = ok && writeRegLocked(kRegFoccfg, 0x16);
  ok = ok && writeRegLocked(kRegAgcctrl2, 0x43);
  ok = ok && writeRegLocked(kRegAgcctrl1, 0x40);
  ok = ok && writeRegLocked(kRegAgcctrl0, 0x91);
  ok = ok && writeRegLocked(kRegFrend1, 0x56);
  ok = ok && writeRegLocked(kRegFscal3, 0xE9);
  ok = ok && writeRegLocked(kRegFscal2, 0x2A);
  ok = ok && writeRegLocked(kRegFscal1, 0x00);
  ok = ok && writeRegLocked(kRegFscal0, 0x1F);
  // Entra em sleep logo após configurar (só acorda na tela Scanner)
  if (ok) {
    strobeLocked(kStrobeSidle);
    strobeLocked(kStrobeSpwd);
  }
  spiUnlock();

  setCc1101State(ok);
  if (ok) LOGI("CC1101 pronto (ver=0x%02X, somente RX, sleep)", ver);
  else LOGE("CC1101 falhou na configuracao inicial");
  return ok;
}

bool cc1101Ok() { return gOk; }

bool cc1101MeasureRssi(float mhz, int16_t& dbm) {
  if (!gOk) return false;
  const uint32_t word = cc1101FreqWord(mhz);

  spiLock();
  bool ok = strobeLocked(kStrobeSidle);
  ok = ok && writeRegLocked(kRegFreq2, static_cast<uint8_t>((word >> 16) & 0xFF));
  ok = ok && writeRegLocked(kRegFreq1, static_cast<uint8_t>((word >> 8) & 0xFF));
  ok = ok && writeRegLocked(kRegFreq0, static_cast<uint8_t>(word & 0xFF));
  ok = ok && strobeLocked(kStrobeSrx);
  spiUnlock();
  if (!ok) return false;

  // Solta o SPI durante a sintonia/calibração/AGC para não bloquear TFT/SD.
  delayMicroseconds(kRssiSettleUs);

  uint8_t raw = 0;
  spiLock();
  ok = readStatusLocked(kStatRssi, raw);
  spiUnlock();
  if (!ok) return false;

  dbm = cc1101RssiDbm(raw);
  return true;
}

void cc1101Sleep() {
  if (!gOk) return;
  spiLock();
  strobeLocked(kStrobeSidle);
  strobeLocked(kStrobeSpwd);
  spiUnlock();
}
