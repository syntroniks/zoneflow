// SPDX-License-Identifier: Apache-2.0
// Zero-cross aware switching. When the board provides a mains zero-cross
// signal and driver.zero_cross_switching is on, the zone driver turns an
// output on at the next zero crossing of the 24 VAC supply instead of at a
// random phase. Turning off needs no help: a triac stops conducting at the
// next current zero by itself. Without a usable signal nothing waits and
// behaviour is as before.
#pragma once

#include <atomic>
#include <cstdint>

#if defined(__GNUC__)
#define ZF_ZC_INLINE [[gnu::always_inline]] inline
#else
#define ZF_ZC_INLINE inline
#endif

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

// The rev A AC_ZC signal (hardware design 19.9): a square wave at mains
// frequency, not a pulse per crossing. High while the valve common is
// positive. The rising edge comes risingLagUs after the upward crossing, the
// falling edge fallingLeadUs before the downward one (0.30 ms and 0.09 ms at
// 24 VAC; both grow at lower supply voltage).
//
// ZeroCrossTracker takes every edge from the GPIO interrupt (both
// polarities) and gives:
//  - presence: an edge of either polarity within kAcEdgeTimeoutUs;
//  - the mains frequency: the reciprocal of the time between rising edges;
//  - a ZeroCrossSample whose lastEdgeUs is the last upward crossing and
//    whose interval is half the period, for zeroCrossWaitUs() (offset 0).
//
// onEdge() is for one interrupt; every other method is for any task. State
// is in atomics with a sequence lock, no allocation, inline for IRAM.
class ZeroCrossTracker {
public:
    static constexpr uint32_t kAcEdgeTimeoutUs = 25000; // 19.9: loss of 24 VAC
    // Plausible mains: 45 to 66 Hz. A reading of 100 Hz, or edges that
    // follow the keep-alive, means AC_ZC is bridged to KA (19.9).
    static constexpr uint32_t kMinPeriodUs = 15150;
    static constexpr uint32_t kMaxPeriodUs = 22300;
    // Edges closer than this are noise: half of the shortest half-period.
    static constexpr uint32_t kMinEdgeSpacingUs = 3000;

    ZeroCrossTracker(int32_t risingLagUs, int32_t fallingLeadUs)
        : risingLagUs_(risingLagUs), fallingLeadUs_(fallingLeadUs)
    {
    }

    ZF_ZC_INLINE void onEdge(uint32_t nowUs, bool rising)
    {
        const uint32_t edges = edges_.load(std::memory_order_relaxed);
        const uint32_t last = lastEdgeUs_.load(std::memory_order_relaxed);
        if (edges > 0 && nowUs - last < kMinEdgeSpacingUs) {
            ++noise_;
            return;
        }
        const uint32_t seq = seq_.load(std::memory_order_relaxed);
        seq_.store(seq + 1, std::memory_order_release);
        if (rising) {
            const uint32_t prev = lastRisingUs_.load(std::memory_order_relaxed);
            if (risings_.load(std::memory_order_relaxed) > 0) {
                periodUs_.store(nowUs - prev, std::memory_order_relaxed);
            }
            lastRisingUs_.store(nowUs, std::memory_order_relaxed);
            risings_.store(risings_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        }
        lastEdgeUs_.store(nowUs, std::memory_order_relaxed);
        seq_.store(seq + 2, std::memory_order_release);
        edges_.store(edges + 1, std::memory_order_release);
    }

    bool present(uint32_t nowUs) const
    {
        return edges_.load(std::memory_order_acquire) > 0 &&
               nowUs - lastEdgeUs_.load(std::memory_order_relaxed) < kAcEdgeTimeoutUs;
    }

    // 0 while unknown, stale or implausible.
    uint32_t periodUs(uint32_t nowUs) const
    {
        uint32_t period = 0;
        uint32_t rising = 0;
        if (!read(period, rising) || risings_.load(std::memory_order_relaxed) < 2) {
            return 0;
        }
        if (nowUs - rising > 3 * kMaxPeriodUs) {
            return 0;
        }
        return period;
    }
    // Mains frequency in mHz; 0 while unknown.
    uint32_t milliHz(uint32_t nowUs) const
    {
        const uint32_t p = periodUs(nowUs);
        return p == 0 ? 0 : static_cast<uint32_t>(1000000000ull / p);
    }
    bool plausible(uint32_t nowUs) const
    {
        const uint32_t p = periodUs(nowUs);
        return p >= kMinPeriodUs && p <= kMaxPeriodUs;
    }

    ZeroCrossSample sample(uint32_t nowUs) const
    {
        ZeroCrossSample s;
        uint32_t period = 0;
        uint32_t rising = 0;
        s.nowUs = nowUs;
        if (!read(period, rising) || !plausible(nowUs)) {
            return s;
        }
        s.present = true;
        s.lastEdgeUs = rising - static_cast<uint32_t>(risingLagUs_); // the upward crossing
        s.intervalUs = period / 2;
        return s;
    }

    uint32_t noiseEdges() const { return noise_; }
    int32_t fallingLeadUs() const { return fallingLeadUs_; }

private:
    bool read(uint32_t &period, uint32_t &rising) const
    {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint32_t before = seq_.load(std::memory_order_acquire);
            period = periodUs_.load(std::memory_order_relaxed);
            rising = lastRisingUs_.load(std::memory_order_relaxed);
            if (before == seq_.load(std::memory_order_acquire) && (before & 1u) == 0) {
                return true;
            }
        }
        return false;
    }

    const int32_t risingLagUs_;
    const int32_t fallingLeadUs_;
    std::atomic<uint32_t> seq_{0};
    std::atomic<uint32_t> edges_{0};
    std::atomic<uint32_t> risings_{0};
    std::atomic<uint32_t> lastEdgeUs_{0};
    std::atomic<uint32_t> lastRisingUs_{0};
    std::atomic<uint32_t> periodUs_{0};
    uint32_t noise_ = 0; // interrupt only
};

} // namespace zf
