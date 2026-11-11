// SPDX-License-Identifier: Apache-2.0
// Valve current from the two biased AC channels of the rev A front end
// (hardware design sections 10 and 19.2).
//
// The board does not rectify. Each channel is the shunt current as an AC
// waveform centred on about 0.79 V. The ADC samples both channels for windows
// of exactly 100 ms (five cycles at 50 Hz, six at 60 Hz). Per window and
// channel the device keeps a count, a sum, a sum of squares, a minimum and a
// maximum of the calibrated millivolts (ChannelStats::add). This module turns
// a window into one RMS current:
//
//  - mean removed: RMS of (sample - window mean). Averaging the samples gives
//    the bias, not the current;
//  - the high-gain channel unless any of its samples is below 60 mV or above
//    1500 mV (clipped), then the low-gain channel;
//  - the noise floor, learned in windows with the relay open and the
//    keep-alive stopped, subtracted in quadrature;
//  - a DC component: the window mean moving away from the idle mean while
//    current flows means one triac polarity is not conducting.
//
// Pure C++, no allocation; host-tested with synthetic waveforms.
#pragma once

#include <cstdint>
#include <limits>

#include "zf/types.hpp"

namespace zf {

// Running statistics of one channel over one window, in millivolts.
struct ChannelStats {
    uint32_t n = 0;
    int64_t sumMv = 0;
    uint64_t sumSqMv = 0; // 1600 mV squared times 65 536 samples still fits
    int32_t minMv = std::numeric_limits<int32_t>::max();
    int32_t maxMv = std::numeric_limits<int32_t>::min();

    void add(int32_t mv)
    {
        ++n;
        sumMv += mv;
        sumSqMv += static_cast<uint64_t>(static_cast<int64_t>(mv) * mv);
        minMv = mv < minMv ? mv : minMv;
        maxMv = mv > maxMv ? mv : maxMv;
    }
    void reset() { *this = ChannelStats{}; }
    double meanMv() const;
    // RMS of the samples after the window mean is removed.
    double acRmsMv() const;
};

// One window as the device delivers it.
struct CurrentWindowStats {
    uint32_t seq = 0; // increments per completed window; 0 = no window yet
    MonoMs startMs = 0;
    MonoMs endMs = 0;
    ChannelStats hi; // ISENSE_HI, 1.503 mA per mV
    ChannelStats lo; // ISENSE_LO, 7.50 mA per mV
    // GPIO7, the relay detector read-back (B_RB), as ADC1 channel 6: 3.05 V
    // with Q2 off, 0 V with Q2 on, 0.4 to 0.95 V with Q2 shorted gate to
    // drain (hardware design 7.4, review A2 4.6 question 7). Empty on boards
    // and fakes that do not sample it.
    ChannelStats relayRb;
    // GPIO3, VIN_MON, as ADC1 channel 2: the rectified supply divided by 31
    // (hardware design 19.10). Only its window mean is used: the half-wave
    // ripple swings single samples by up to 0.24 V. Empty if not sampled.
    ChannelStats vinMon;
};

enum class CurrentChannel : uint8_t { None, High, Low };

struct CurrentReading {
    uint32_t seq = 0; // the window's sequence number; 0 = none
    MonoMs startMs = 0;
    MonoMs endMs = 0;
    bool valid = false;
    int32_t ma = -1;    // RMS with the noise floor removed
    int32_t rawMa = -1; // RMS before the floor is removed
    CurrentChannel channel = CurrentChannel::None;
    bool hiClipped = false;
    int32_t meanHiMv = -1; // window mean of the high-gain channel (the bias, about 790 mV); -1 if not sampled
    int32_t meanLoMv = -1; // window mean of the low-gain channel; -1 if not sampled
    int32_t dcMa = 0;     // window mean minus the idle mean, as current; 0 without an idle mean
    bool dcComponent = false;
    int32_t relayReadbackMv = -1; // window mean of GPIO7; -1 if not sampled
    int32_t vinMonMv = -1;        // window mean of VIN_MON (GPIO3); -1 if not sampled
};

struct CurrentSenseConfig {
    // Hardware design 19.2 and hardware/pinmap.yaml.
    float hiMaPerMv = 1.503f;
    float loMaPerMv = 7.50f;
    int32_t hiMinMv = 60;
    int32_t hiMaxMv = 1500;
    // DC component: |mean shift| above this share of the RMS current, and
    // above dcMinMa. A half-wave sine gives a shift of 0.82 times its
    // mean-removed RMS; a full-wave load gives none.
    float dcRatio = 0.3f;
    int32_t dcMinMa = 20;
    // Windows with fewer samples than this are not used (the ADC dropped data).
    uint32_t minSamples = 100;
};

class CurrentSense {
public:
    void configure(const CurrentSenseConfig &cfg) { cfg_ = cfg; }

    // relayOpenIdle: the relay was open and the keep-alive stopped for the
    // whole window, so the true current is zero. Such windows teach the
    // noise floor and the idle mean (when below idleThresholdMa).
    CurrentReading process(const CurrentWindowStats &w, bool relayOpenIdle, uint16_t idleThresholdMa);

    // Learned values; 0 / false until an idle window was seen.
    int32_t noiseFloorMa() const { return static_cast<int32_t>(floorHiMa_ + 0.5); }
    bool baselineKnown() const { return idleWindows_ > 0; }
    double idleMeanHiMv() const { return idleMeanHiMv_; }

private:
    CurrentSenseConfig cfg_{};
    uint32_t idleWindows_ = 0;
    double floorHiMa_ = 0.0;
    double floorLoMa_ = 0.0;
    double idleMeanHiMv_ = 0.0;
    double idleMeanLoMv_ = 0.0;
};

} // namespace zf
