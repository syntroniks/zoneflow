// SPDX-License-Identifier: Apache-2.0
// Decision logic for the hardware output-enable (OE) signal, spec section 2
// rule 3. The board gates every output driver with OE. Firmware must keep
// proving it is alive, so a stuck firmware loses the outputs.
//
// Threads:
//  - The control task calls setDemand() and heartbeat() every tick.
//  - The device's keep-alive is a hardware timer interrupt that calls step()
//    every period and writes the returned level to the OE pin. The interrupt
//    is IRAM-safe, so it keeps running while the flash cache is off. step()
//    needs no clock: it counts its own periods since the heartbeat counter
//    last changed, so a dead control loop still stops the toggling within
//    the timeout (see firmware-architecture.md, safety design).
//  - nextLevel() is the clock-based variant for a keep-alive task.
//  - Any context may call trip().
// All shared state is in lock-free atomics; nothing here allocates or blocks.
// step(), trip() and what they call are forced inline so the IRAM interrupt
// and the brownout handler never call into flash.
#pragma once

#include <atomic>
#include <cstdint>

// Forces inlining so code called from an IRAM interrupt stays in IRAM.
#if defined(__GNUC__)
#define ZF_ALWAYS_INLINE [[gnu::always_inline]] inline
#else
#define ZF_ALWAYS_INLINE inline
#endif

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
    ZF_ALWAYS_INLINE void trip() { tripped_.store(true, std::memory_order_release); }
    void reset() { tripped_.store(false, std::memory_order_release); }
    ZF_ALWAYS_INLINE bool tripped() const { return tripped_.load(std::memory_order_acquire); }

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

    // Timer interrupt only: the level to drive now. Call every periodMs. The
    // heartbeat is stale once more than the timeout has passed, counted in
    // periods, since the heartbeat counter last changed; it stays stale until
    // the next heartbeat. The count saturates, so it never wraps to fresh.
    ZF_ALWAYS_INLINE bool step(uint32_t periodMs)
    {
        const uint32_t beats = beats_.load(std::memory_order_acquire);
        if (beats != timerSeenBeats_) {
            timerSeenBeats_ = beats;
            sinceBeatMs_ = 0;
        } else if (sinceBeatMs_ <= timeoutMs_) {
            sinceBeatMs_ = periodMs > timeoutMs_ ? timeoutMs_ + 1 : sinceBeatMs_ + periodMs;
        }
        const bool alive = beats != 0 && sinceBeatMs_ <= timeoutMs_;
        if (!alive || tripped() || !demand_.load(std::memory_order_relaxed)) {
            timerLevel_ = false;
        } else if (mode_ == OutputEnableMode::Toggle) {
            timerLevel_ = !timerLevel_;
        } else {
            timerLevel_ = true;
        }
        return timerLevel_;
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
    uint32_t seenBeats_ = 0;      // keep-alive task only
    bool stale_ = false;          // keep-alive task only
    bool level_ = false;          // keep-alive task only
    uint32_t timerSeenBeats_ = 0; // timer interrupt only
    uint32_t sinceBeatMs_ = 0;    // timer interrupt only; saturates above timeoutMs_
    bool timerLevel_ = false;     // timer interrupt only
};

} // namespace zf
