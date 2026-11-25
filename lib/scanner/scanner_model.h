#pragma once
// Lógica pura do scanner CC1101 (sem Arduino): cálculo de frequência, RSSI e peak-hold.
#include <cstdint>

uint32_t cc1101FreqWord(float mhz);
int16_t cc1101RssiDbm(uint8_t raw);
uint16_t rssiToBarHeight(int16_t dbm, uint16_t maxH);

struct BandDef {
  const char* label;
  float startMhz;
  float stepMhz;
};

class ScannerModel {
 public:
  static constexpr uint8_t kBandCount = 3;
  static constexpr uint8_t kBins = 60;
  static constexpr int16_t kFloorDbm = -120;
  static constexpr int16_t kCeilDbm = -30;
  static constexpr int16_t kPeakDecayDb = 2;

  ScannerModel();
  void setBand(uint8_t idx);
  void stepBand(int8_t delta);
  void reset();

  uint8_t bandIndex() const { return bandIdx_; }
  const BandDef& band() const;
  uint8_t cursor() const { return cursor_; }
  float binFreqMhz(uint8_t bin) const;
  float nextFreqMhz() const { return binFreqMhz(cursor_); }

  void recordSample(int16_t dbm);
  int16_t rssi(uint8_t bin) const;
  int16_t peak(uint8_t bin) const;
  bool hasData() const { return hasData_; }

  uint8_t peakBin() const;
  float peakFreqMhz() const { return binFreqMhz(peakBin()); }
  int16_t peakDbm() const { return peak(peakBin()); }

 private:
  uint8_t bandIdx_ = 0;
  uint8_t cursor_ = 0;
  uint32_t sweeps_ = 0;
  bool hasData_ = false;
  int16_t rssi_[kBins]{};
  int16_t peak_[kBins]{};
};
