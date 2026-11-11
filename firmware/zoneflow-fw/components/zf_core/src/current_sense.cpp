// SPDX-License-Identifier: Apache-2.0
#include "zf/diag/current_sense.hpp"

#include <algorithm>
#include <cmath>

namespace zf {

double ChannelStats::meanMv() const { return n == 0 ? 0.0 : static_cast<double>(sumMv) / static_cast<double>(n); }

double ChannelStats::acRmsMv() const
{
    if (n == 0) {
        return 0.0;
    }
    const double mean = meanMv();
    const double meanSq = static_cast<double>(sumSqMv) / static_cast<double>(n);
    const double var = meanSq - mean * mean; // E[x^2] - E[x]^2
    return var > 0.0 ? std::sqrt(var) : 0.0;
}

namespace {

// Exponential average with weight 1/8; the first value is taken as it is.
void ema(double &slot, double v, uint32_t count) { slot = count == 0 ? v : slot + (v - slot) / 8.0; }

int32_t toMa(double v) { return static_cast<int32_t>(std::lround(std::max(0.0, v))); }

} // namespace

CurrentReading CurrentSense::process(const CurrentWindowStats &w, bool relayOpenIdle, uint16_t idleThresholdMa)
{
    CurrentReading r;
    r.seq = w.seq;
    r.startMs = w.startMs;
    r.endMs = w.endMs;
    // Window means first: the supply monitor and the rest levels (19.10) are
    // wanted even from a window whose current cannot be computed.
    const auto mean = [](const ChannelStats &c) {
        return c.n > 0 ? static_cast<int32_t>(std::lround(c.meanMv())) : -1;
    };
    r.relayReadbackMv = mean(w.relayRb);
    r.vinMonMv = mean(w.vinMon);
    r.meanLoMv = mean(w.lo);
    r.meanHiMv = mean(w.hi);
    if (w.hi.n < cfg_.minSamples) {
        return r;
    }
    r.hiClipped = w.hi.minMv < cfg_.hiMinMv || w.hi.maxMv > cfg_.hiMaxMv;
    const bool useLo = r.hiClipped;
    if (useLo && w.lo.n < cfg_.minSamples) {
        return r; // clipped, and no usable low-gain samples
    }
    const ChannelStats &ch = useLo ? w.lo : w.hi;
    const double scale = useLo ? cfg_.loMaPerMv : cfg_.hiMaPerMv;
    const double rawMa = ch.acRmsMv() * scale;
    r.channel = useLo ? CurrentChannel::Low : CurrentChannel::High;
    r.rawMa = toMa(rawMa);

    // Learn from windows that must read zero. A reading above the idle
    // threshold there is a fault (welded relay, shorted output, amplifier),
    // not noise, and must not raise the floor.
    if (relayOpenIdle && !useLo && rawMa < idleThresholdMa) {
        ema(floorHiMa_, rawMa, idleWindows_);
        ema(floorLoMa_, w.lo.acRmsMv() * cfg_.loMaPerMv, idleWindows_);
        ema(idleMeanHiMv_, w.hi.meanMv(), idleWindows_);
        ema(idleMeanLoMv_, w.lo.meanMv(), idleWindows_);
        ++idleWindows_;
    }

    const double floor = useLo ? floorLoMa_ : floorHiMa_;
    const double net = rawMa * rawMa - floor * floor; // quadrature
    r.ma = toMa(net > 0.0 ? std::sqrt(net) : 0.0);
    r.valid = true;

    if (baselineKnown()) {
        const double idleMean = useLo ? idleMeanLoMv_ : idleMeanHiMv_;
        const double dcMa = (ch.meanMv() - idleMean) * scale;
        r.dcMa = static_cast<int32_t>(std::lround(dcMa));
        const double limit = std::max<double>(cfg_.dcMinMa, cfg_.dcRatio * r.ma);
        r.dcComponent = r.ma > idleThresholdMa && std::fabs(dcMa) > limit;
    }
    return r;
}

} // namespace zf
