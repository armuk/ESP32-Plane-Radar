#pragma once

#include <cstdint>

namespace services::wifi_signal {

/**
 * Decides when a Wi-Fi link is weak enough to rejoin, from RSSI samples
 * taken every 30 s.
 *
 * "6 weak samples in a row" never fired for a link hovering around the
 * threshold (e.g. -76 to -80 dBm against -78): one better reading reset the
 * count, while pages still stalled. This looks at the last kWindow samples
 * instead and rejoins when at least kWeakNeeded of them were weak. It can
 * fire after kWeakNeeded samples (2 minutes), so a weak join at boot is
 * corrected quickly.
 */
class WeakSignalWindow {
 public:
  static constexpr int kWindow = 6;      // 3 minutes of samples
  static constexpr int kWeakNeeded = 4;  // "mostly weak"

  explicit WeakSignalWindow(int threshold_dbm) : threshold_(threshold_dbm) {}

  void add(int rssi_dbm) {
    weak_bits_ = static_cast<uint8_t>(((weak_bits_ << 1) |
                                       (rssi_dbm < threshold_ ? 1 : 0)) &
                                      kMask);
    if (samples_ < kWindow) {
      ++samples_;
    }
  }

  int weakCount() const {
    int n = 0;
    for (uint8_t b = weak_bits_; b != 0; b &= static_cast<uint8_t>(b - 1)) {
      ++n;
    }
    return n;
  }

  int samples() const { return samples_; }

  bool mostlyWeak() const { return weakCount() >= kWeakNeeded; }

  /** After a rejoin or a lost link: judge the new link on its own. */
  void clear() {
    weak_bits_ = 0;
    samples_ = 0;
  }

 private:
  static constexpr uint8_t kMask = (1u << kWindow) - 1;
  int threshold_;
  uint8_t weak_bits_ = 0;
  int samples_ = 0;
};

}  // namespace services::wifi_signal
