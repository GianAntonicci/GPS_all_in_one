#include "scanner_model.h"

#include <cmath>

namespace {

constexpr double kXoscMhz = 26.0;
constexpr BandDef kBands[ScannerModel::kBandCount] = {
    {"433 MHz", 430.0f, 0.1f},
    {"868 MHz", 865.0f, 0.1f},
    {"915 MHz", 902.0f, 0.5f},
};

}  // namespace

uint32_t cc1101FreqWord(float mhz) {
  if (mhz <= 0.0f) return 0;
  const double raw = (static_cast<double>(mhz) * 65536.0) / kXoscMhz;
  if (raw <= 0.0) return 0;
  const uint32_t word = static_cast<uint32_t>(llround(raw));
  return word & 0x00FFFFFFu;
}

int16_t cc1101RssiDbm(uint8_t raw) {
  const int16_t signedVal = (raw >= 128) ? static_cast<int16_t>(raw) - 256 : static_cast<int16_t>(raw);
  return static_cast<int16_t>(signedVal / 2 - 74);
}

uint16_t rssiToBarHeight(int16_t dbm, uint16_t maxH) {
  if (maxH == 0 || dbm <= ScannerModel::kFloorDbm) return 0;
  if (dbm >= ScannerModel::kCeilDbm) return maxH;
  const int32_t span = ScannerModel::kCeilDbm - ScannerModel::kFloorDbm;
  const int32_t num = static_cast<int32_t>(dbm - ScannerModel::kFloorDbm) * maxH;
  return static_cast<uint16_t>((num + span / 2) / span);
}

ScannerModel::ScannerModel() { reset(); }

void ScannerModel::setBand(uint8_t idx) {
  bandIdx_ = idx % kBandCount;
  reset();
}

void ScannerModel::stepBand(int8_t delta) {
  const int next = ((static_cast<int>(bandIdx_) + delta) % kBandCount + kBandCount) % kBandCount;
  setBand(static_cast<uint8_t>(next));
}

void ScannerModel::reset() {
  cursor_ = 0;
  sweeps_ = 0;
  hasData_ = false;
  for (uint8_t i = 0; i < kBins; ++i) {
    rssi_[i] = kFloorDbm;
    peak_[i] = kFloorDbm;
  }
}

const BandDef& ScannerModel::band() const { return kBands[bandIdx_]; }

float ScannerModel::binFreqMhz(uint8_t bin) const {
  if (bin >= kBins) bin = kBins - 1;
  const BandDef& b = kBands[bandIdx_];
  return b.startMhz + b.stepMhz * static_cast<float>(bin);
}

void ScannerModel::recordSample(int16_t dbm) {
  rssi_[cursor_] = dbm;
  if (dbm > peak_[cursor_]) peak_[cursor_] = dbm;
  hasData_ = true;

  if (++cursor_ >= kBins) {
    cursor_ = 0;
    ++sweeps_;
    if (sweeps_ >= 2) {
      for (uint8_t i = 0; i < kBins; ++i) {
        int16_t decayed = static_cast<int16_t>(peak_[i] - kPeakDecayDb);
        if (decayed < rssi_[i]) decayed = rssi_[i];
        if (decayed < kFloorDbm) decayed = kFloorDbm;
        peak_[i] = decayed;
      }
    }
  }
}

int16_t ScannerModel::rssi(uint8_t bin) const {
  return (bin < kBins) ? rssi_[bin] : kFloorDbm;
}

int16_t ScannerModel::peak(uint8_t bin) const {
  return (bin < kBins) ? peak_[bin] : kFloorDbm;
}

uint8_t ScannerModel::peakBin() const {
  uint8_t best = 0;
  for (uint8_t i = 1; i < kBins; ++i) {
    if (peak_[i] > peak_[best]) best = i;
  }
  return best;
}
