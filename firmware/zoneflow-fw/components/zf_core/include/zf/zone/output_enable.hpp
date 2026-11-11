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

// The keep-alive waveform contract with the rev A detectors (hardware design
// 19.4, changed by review A2 finding M6): an edge at least every 20 ms and no
// phase shorter than 2.5 ms. In Toggle mode the pin changes level only at a
// period boundary, so every phase is a whole number of periods; a period
// inside these limits meets the contract. Both firmwares assert it for their
// timer configuration.
namespace keepalive {
inline constexpr uint32_t kMaxEdgeIntervalUs = 20000;
inline constexpr uint32_t kMinPhaseUs = 2500;
constexpr bool periodMeetsContract(uint32_t periodUs)
{
    return periodUs >= kMinPhaseUs && periodUs <= kMaxEdgeIntervalUs;
}
} // namespace keepalive

enum class OutputEnableMode : uint8_t {
    // OE is a retriggerable monostable: firmware must produce edges.
    Toggle,
    // OE is a plain level: high while outputs are allowed.
    StaticLevel,
};

// What the control task asks of the keep-alive pin. The interrupt still
// decides every edge, and drives the pin low whatever is asked once the
// heartbeat is stale or the guard is tripped.
enum class KeepAliveRequest : uint8_t {
    Off,      // pin low
    Run,      // toggle (Toggle mode) or high (StaticLevel mode)
    HoldHigh, // steady high: the pin-high self-test (hardware design 19.7 step 2)
    Float,    // the pin an input with no pull: the float test (19.7 step 2b)
};

// What the keep-alive interrupt saw, for the self-test timing (hardware
// design 19.7 steps 3 and 9). Times are in the interrupt's own millisecond
// count, which advances by one period per interrupt. Written by the
// interrupt only; read with readbacks().
struct KeepAliveReadbacks {
    uint32_t nowMs = 0;                               // interrupt time of the latest sample
    KeepAliveRequest applied = KeepAliveRequest::Off; // what the pin did in the latest period
    uint32_t appliedSinceMs = 0;                      // when `applied` last changed
    bool driveEnabled = false;                        // A_RB, GPIO42: V_DRV high (A on, latch clear, Q3 on)
    uint32_t driveSinceMs = 0;                        // when driveEnabled last changed
    bool relayOff = false;                            // B_RB, GPIO7: relay transistor Q2 off
    uint32_t relaySinceMs = 0;                        // when relayOff last changed
    bool overCurrentLatched = false;                  // OC, GPIO40 low
    uint32_t samples = 0;                             // 0: the interrupt has not sampled yet
    // Step 2b: reads of the keep-alive pin as an input since `applied`
    // became Float, and how many of them were high.
    uint32_t floatReads = 0;
    uint32_t floatHighReads = 0;
};

class OutputEnableGuard {
public:
    OutputEnableGuard(OutputEnableMode mode, uint32_t heartbeatTimeoutMs) : mode_(mode), timeoutMs_(heartbeatTimeoutMs)
    {
    }

    // Control task: true while any output is on or about to turn on.
    void setDemand(bool outputsMayBeOn) { setRequest(outputsMayBeOn ? KeepAliveRequest::Run : KeepAliveRequest::Off); }
    void setRequest(KeepAliveRequest r)
    {
        request_.store(static_cast<uint8_t>(r), std::memory_order_relaxed);
        demand_.store(r != KeepAliveRequest::Off, std::memory_order_relaxed);
    }
    KeepAliveRequest request() const { return static_cast<KeepAliveRequest>(request_.load(std::memory_order_relaxed)); }

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
        if (stale_ || !allowed(nowMs) || request() == KeepAliveRequest::Float) {
            level_ = false;
        } else if (request() == KeepAliveRequest::HoldHigh) {
            level_ = true;
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
        auto req = static_cast<KeepAliveRequest>(request_.load(std::memory_order_relaxed));
        if (!alive || tripped()) {
            req = KeepAliveRequest::Off;
        }
        if (req == KeepAliveRequest::Off || req == KeepAliveRequest::Float) {
            timerLevel_ = false;
        } else if (req == KeepAliveRequest::HoldHigh || mode_ == OutputEnableMode::StaticLevel) {
            timerLevel_ = true;
        } else {
            timerLevel_ = !timerLevel_;
        }
        isrMs_ += periodMs;
        if (req != timerApplied_) {
            timerApplied_ = req;
            timerAppliedSinceMs_ = isrMs_;
            floatReads_ = 0;
            floatHighReads_ = 0;
        }
        return timerLevel_;
    }

    // Timer interrupt only, after step(): true while the pin must be an
    // input with no pull (the float test). The interrupt then turns the
    // pin's output driver off; otherwise it drives the level step()
    // returned. A stale heartbeat or a tripped guard turns Float into Off,
    // so the pin is driven low again.
    ZF_ALWAYS_INLINE bool pinIsInput() const { return timerApplied_ == KeepAliveRequest::Float; }

    // Timer interrupt only: one read of the keep-alive pin while it is an
    // input. The device calls it every 1 ms (19.7 step 2b).
    ZF_ALWAYS_INLINE void sampleFloat(bool high)
    {
        if (timerApplied_ != KeepAliveRequest::Float) {
            return;
        }
        ++floatReads_;
        floatHighReads_ += high ? 1u : 0u;
    }

    // Timer interrupt only, after step(): the levels of the read-back pins
    // in this period. Records when each changed, so the control task can time
    // the detectors to one period (hardware design 19.7 steps 3 and 9).
    ZF_ALWAYS_INLINE void sample(bool driveEnabled, bool relayOff, bool overCurrentLatched)
    {
        const uint32_t seq = rbSeq_.load(std::memory_order_relaxed);
        rbSeq_.store(seq + 1, std::memory_order_release); // odd: writing
        std::atomic_thread_fence(std::memory_order_release);
        if (rb_.samples == 0 || driveEnabled != rb_.driveEnabled) {
            rb_.driveSinceMs = isrMs_;
        }
        if (rb_.samples == 0 || relayOff != rb_.relayOff) {
            rb_.relaySinceMs = isrMs_;
        }
        rb_.driveEnabled = driveEnabled;
        rb_.relayOff = relayOff;
        rb_.overCurrentLatched = overCurrentLatched;
        rb_.applied = timerApplied_;
        rb_.appliedSinceMs = timerAppliedSinceMs_;
        rb_.nowMs = isrMs_;
        rb_.floatReads = floatReads_;
        rb_.floatHighReads = floatHighReads_;
        ++rb_.samples;
        std::atomic_thread_fence(std::memory_order_release);
        rbSeq_.store(seq + 2, std::memory_order_release);
    }

    // Any task: a consistent copy of the latest sample. Returns false if the
    // interrupt kept writing (it cannot, for more than a few retries, unless
    // the reader is preempted for a whole period each time).
    bool readbacks(KeepAliveReadbacks &out) const
    {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint32_t before = rbSeq_.load(std::memory_order_acquire);
            if ((before & 1u) != 0) {
                continue;
            }
            std::atomic_thread_fence(std::memory_order_acquire);
            KeepAliveReadbacks copy;
            copyReadbacks(copy);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (rbSeq_.load(std::memory_order_acquire) == before) {
                out = copy;
                return true;
            }
        }
        return false;
    }

    OutputEnableMode mode() const { return mode_; }

private:
    // Field by field through volatile reads: the interrupt may write at the
    // same time, and the sequence check above discards a torn copy.
    void copyReadbacks(KeepAliveReadbacks &c) const
    {
        const volatile KeepAliveReadbacks &v = rb_;
        c.nowMs = v.nowMs;
        c.applied = v.applied;
        c.appliedSinceMs = v.appliedSinceMs;
        c.driveEnabled = v.driveEnabled;
        c.driveSinceMs = v.driveSinceMs;
        c.relayOff = v.relayOff;
        c.relaySinceMs = v.relaySinceMs;
        c.overCurrentLatched = v.overCurrentLatched;
        c.samples = v.samples;
        c.floatReads = v.floatReads;
        c.floatHighReads = v.floatHighReads;
    }

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
    std::atomic<uint8_t> request_{static_cast<uint8_t>(KeepAliveRequest::Off)};
    std::atomic<bool> tripped_{false};
    std::atomic<uint32_t> beats_{0}; // heartbeats so far; 0 = never
    std::atomic<uint32_t> lastBeatMs_{0};
    uint32_t seenBeats_ = 0;                                // keep-alive task only
    bool stale_ = false;                                    // keep-alive task only
    bool level_ = false;                                    // keep-alive task only
    uint32_t timerSeenBeats_ = 0;                           // timer interrupt only
    uint32_t sinceBeatMs_ = 0;                              // timer interrupt only; saturates above timeoutMs_
    bool timerLevel_ = false;                               // timer interrupt only
    uint32_t isrMs_ = 0;                                    // timer interrupt only: periods so far, in ms
    KeepAliveRequest timerApplied_ = KeepAliveRequest::Off; // timer interrupt only
    uint32_t timerAppliedSinceMs_ = 0;                      // timer interrupt only
    uint32_t floatReads_ = 0;                               // timer interrupt only
    uint32_t floatHighReads_ = 0;                           // timer interrupt only
    KeepAliveReadbacks rb_{};                               // written by sample()
    std::atomic<uint32_t> rbSeq_{0};                        // sequence lock for rb_
};

} // namespace zf
