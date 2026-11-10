// SPDX-License-Identifier: Apache-2.0
#include "zf/diag/current_monitor.hpp"

#include <algorithm>

namespace zf {

const char *toString(CurrentFault f)
{
    switch (f) {
    case CurrentFault::None:
        return "none";
    case CurrentFault::OpenCircuit:
        return "open_circuit";
    case CurrentFault::OverCurrent:
        return "over_current";
    case CurrentFault::ShortCircuit:
        return "short_circuit";
    case CurrentFault::CurrentWhileIdle:
        return "current_while_idle";
    }
    return "?";
}

namespace {
// Over-current and short circuit are both "too much current"; a reading that
// straddles short_circuit_ma alternates between them from sample to sample.
bool tooHigh(CurrentFault f) { return f == CurrentFault::OverCurrent || f == CurrentFault::ShortCircuit; }
} // namespace

void CurrentMonitor::configure(const CurrentMonitorConfig &cfg)
{
    // New limits apply from the next sample. The confirmation state is kept:
    // reloading the config while a fault is being confirmed must not restart
    // the count.
    cfg_ = cfg;
    cfg_.confirmSamples = std::max<uint8_t>(cfg_.confirmSamples, 1);
}

void CurrentMonitor::rearm() { reported_ = CurrentFault::None; }

CurrentVerdict CurrentMonitor::evaluate(int32_t measuredMa, uint16_t outputMask, uint8_t lastOpenedBit,
                                        MonoMs lastChangeMs, MonoMs now)
{
    if (!cfg_.enabled || measuredMa < 0) {
        pending_ = CurrentFault::None;
        count_ = 0;
        return {};
    }
    if (outputMask != lastMask_) {
        lastMask_ = outputMask;
        pending_ = CurrentFault::None;
        count_ = 0;
        reported_ = CurrentFault::None;
    }

    CurrentFault cond = CurrentFault::None;
    const bool settled = now - lastChangeMs >= cfg_.settleMs;
    if (measuredMa > cfg_.shortCircuitMa) {
        cond = CurrentFault::ShortCircuit;
    } else if (settled && outputMask == 0) {
        if (measuredMa > cfg_.idleMaxMa) {
            cond = CurrentFault::CurrentWhileIdle;
        }
    } else if (settled) {
        uint32_t minSum = 0;
        uint32_t maxSum = 0;
        bool maxKnown = true;
        for (uint8_t bit = 0; bit <= kMasterBit; ++bit) {
            if ((outputMask & (1u << bit)) == 0) {
                continue;
            }
            const CurrentWindow &w = bit == kMasterBit ? cfg_.master : cfg_.zone[bit];
            minSum += w.minMa;
            maxSum += w.maxMa;
            maxKnown = maxKnown && w.maxMa > 0;
        }
        if (static_cast<uint32_t>(measuredMa) < minSum) {
            cond = CurrentFault::OpenCircuit;
        } else if (maxKnown && static_cast<uint32_t>(measuredMa) > maxSum) {
            cond = CurrentFault::OverCurrent;
        } else {
            learn(measuredMa, outputMask);
        }
    }

    if (cond != pending_) {
        if (!(tooHigh(cond) && tooHigh(pending_))) {
            count_ = 0;
        }
        pending_ = cond;
    }
    // One report per condition and output mask. A short circuit is still
    // reported after a milder condition (for example current_while_idle with
    // every output off, then a current above short_circuit_ma).
    const bool escalated = cond == CurrentFault::ShortCircuit && reported_ != CurrentFault::ShortCircuit;
    if (cond == CurrentFault::None || (reported_ != CurrentFault::None && !escalated)) {
        return {};
    }
    if (count_ < 0xFF) {
        ++count_;
    }
    const uint8_t needed =
        cond == CurrentFault::ShortCircuit ? std::min<uint8_t>(2, cfg_.confirmSamples) : cfg_.confirmSamples;
    if (count_ < needed) {
        return {};
    }
    reported_ = cond;
    CurrentVerdict v;
    v.fault = cond;
    const bool attributable = lastOpenedBit <= kMasterBit && (outputMask & (1u << lastOpenedBit)) != 0;
    v.outputBit = (cond != CurrentFault::CurrentWhileIdle && attributable) ? lastOpenedBit : 0xFF;
    return v;
}

void CurrentMonitor::learn(int32_t measuredMa, uint16_t mask)
{
    const auto ema = [](uint16_t &slot, int32_t v) {
        v = std::clamp<int32_t>(v, 0, 0xFFFF);
        slot = slot == 0 ? static_cast<uint16_t>(v) : static_cast<uint16_t>((slot * 7 + v) / 8);
    };
    const uint16_t zones = mask & static_cast<uint16_t>((1u << kMasterBit) - 1);
    const bool master = (mask & (1u << kMasterBit)) != 0;
    if (zones == 0) {
        if (master) {
            ema(observed_[kMasterBit], measuredMa);
        }
        return;
    }
    if ((zones & (zones - 1)) != 0) {
        return; // more than one zone: cannot separate
    }
    uint8_t bit = 0;
    while ((zones & (1u << bit)) == 0) {
        ++bit;
    }
    if (!master) {
        ema(observed_[bit], measuredMa);
    } else if (observed_[kMasterBit] != 0) {
        ema(observed_[bit], measuredMa - observed_[kMasterBit]);
    }
}

} // namespace zf
