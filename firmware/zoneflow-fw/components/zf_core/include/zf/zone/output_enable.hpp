// SPDX-License-Identifier: Apache-2.0
// Decision logic for the hardware output-enable (OE) signal, spec section 2
// rule 3. The board gates every output driver with OE. Firmware must keep
// proving it is alive, so a stuck firmware loses the outputs.
//
// Threads:
//  - The control task calls setDemand() and heartbeat() every tick.
//  - The keep-alive task calls nextLevel() at its own fixed period and writes
//    the returned level to the OE pin.
//  - Any context may call trip().
// All shared state is in lock-free atomics; nothing here allocates or blocks.
#pragma once

#include <atomic>
#include <cstdint>

namespace zf {

enum class OutputEnableMode : uint8_t {
    // OE is a retriggerable monostable: firmware must produce edges.
    Toggle,
    // OE is a plain level: high while outputs are allowed.
    StaticLevel,
};

class OutputEnableGuard {
public:
    OutputEnableGuard(OutputEnableMode mode, uint32_t heartbeatTimeoutMs) : mode_(mode), timeoutMs_(heartbeatTimeoutMs)
    {
    }

    // Control task: true while any output is on or about to turn on.
    void setDemand(bool outputsMayBeOn) { demand_.store(outputsMayBeOn, std::memory_order_relaxed); }

    // Control task: proves the control loop is running.
    void heartbeat(uint32_t nowMs)
    {
        lastBeatMs_.store(nowMs, std::memory_order_relaxed);
        // Single writer: a plain load and store, no read-modify-write.
        beats_.store(beats_.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    // Latches OE off until reset(). Used on faults and before a restart.
    void trip() { tripped_.store(true, std::memory_order_release); }
    void reset() { tripped_.store(false, std::memory_order_release); }
    bool tripped() const { return tripped_.load(std::memory_order_acquire); }

    // True when OE may be driven active.
    bool allowed(uint32_t nowMs) const
    {
        if (tripped() || !demand_.load(std::memory_order_relaxed) || beats_.load(std::memory_order_acquire) == 0) {
            return false;
        }
        return fresh(nowMs);
    }

    // Keep-alive task only: the level to drive now.
    bool nextLevel(uint32_t nowMs)
    {
        // A heartbeat that went stale stays stale until the next one arrives.
        // The 32-bit age of a heartbeat that stopped keeps growing and wraps
        // after 2^31 ms; without this latch it would read as fresh again.
        const uint32_t beats = beats_.load(std::memory_order_acquire);
        if (beats != seenBeats_) {
            seenBeats_ = beats;
            stale_ = false;
        }
        if (beats != 0 && !fresh(nowMs)) {
            stale_ = true;
        }
        if (stale_ || !allowed(nowMs)) {
            level_ = false;
        } else if (mode_ == OutputEnableMode::Toggle) {
            level_ = !level_;
        } else {
            level_ = true;
        }
        return level_;
    }

    OutputEnableMode mode() const { return mode_; }

private:
    bool fresh(uint32_t nowMs) const
    {
        // The heartbeat may carry a timestamp slightly later than nowMs (read
        // on another core or just before a preemption). Up to the timeout
        // ahead counts as fresh; further ahead is a wrapped, stale age.
        const auto age = static_cast<int32_t>(nowMs - lastBeatMs_.load(std::memory_order_relaxed));
        const auto limit = static_cast<int32_t>(timeoutMs_);
        return age >= -limit && age <= limit;
    }

    const OutputEnableMode mode_;
    const uint32_t timeoutMs_;
    std::atomic<bool> demand_{false};
    std::atomic<bool> tripped_{false};
    std::atomic<uint32_t> beats_{0}; // heartbeats so far; 0 = never
    std::atomic<uint32_t> lastBeatMs_{0};
    uint32_t seenBeats_ = 0; // keep-alive task only
    bool stale_ = false;     // keep-alive task only
    bool level_ = false;     // keep-alive task only
};

} // namespace zf
