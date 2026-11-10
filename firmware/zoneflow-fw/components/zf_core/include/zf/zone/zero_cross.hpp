// SPDX-License-Identifier: Apache-2.0
// Zero-cross aware switching. When the board provides a mains zero-cross
// signal and driver.zero_cross_switching is on, the zone driver turns an
// output on at the next zero crossing of the 24 VAC supply instead of at a
// random phase. Turning off needs no help: a triac stops conducting at the
// next current zero by itself. Without a usable signal nothing waits and
// behaviour is as before.
#pragma once

#include <cstdint>

namespace zf {

// One reading of the zero-cross input. Times are microseconds of a free-
// running 32-bit counter (wraps after about 71 minutes; only differences are
// used).
struct ZeroCrossSample {
    bool present = false;    // the board has the signal and has seen edges
    uint32_t lastEdgeUs = 0; // most recent zero crossing
    uint32_t intervalUs = 0; // time between successive crossings (half a mains period)
    uint32_t nowUs = 0;      // same counter, read with the sample
};

class IZeroCrossHal {
public:
    virtual ~IZeroCrossHal() = default;
    virtual ZeroCrossSample sample() const = 0;
    // Busy-waits; called on the control task, at most one half mains period.
    virtual void delayUs(uint32_t us) = 0;
};

// Accepted crossing intervals: 50 Hz gives 10 000 us, 60 Hz 8 333 us.
inline constexpr uint32_t kZeroCrossMinIntervalUs = 7000;
inline constexpr uint32_t kZeroCrossMaxIntervalUs = 11000;
// A signal with no edge for this many intervals is treated as absent.
inline constexpr uint32_t kZeroCrossMaxAgeIntervals = 3;

// Microseconds to wait so an output switches at the next crossing plus
// offsetUs (the detector's lead or lag, from the board header). 0 means
// switch now: no signal, an implausible interval or a stale signal.
uint32_t zeroCrossWaitUs(const ZeroCrossSample &s, int32_t offsetUs);

} // namespace zf
