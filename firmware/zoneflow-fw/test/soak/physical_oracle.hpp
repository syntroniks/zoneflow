// SPDX-License-Identifier: Apache-2.0
// Invariants on what the valves physically do, checked every 5 ms against
// what the firmware commanded. Shared by the standalone and the ESPHome
// rigs; each fills a FirmwareView from its own glue.
//
//   S1  An output is energised only while the firmware commands it, or
//       for at most kUncommandedLimitMs while the firmware finds and stops
//       a fault (spec section 2 rules 1, 3 and 6).
//   S2  After the firmware halts, no output is energised later than the
//       last heartbeat plus the heartbeat timeout, one keep-alive period and
//       the hold-up of a working detector layer, with any one board fault
//       present (spec section 2 rule 3, hardware design 19.4). The
//       keep-alive net has no edge later than the timeout plus one period
//       after the last heartbeat while the timer interrupt still runs.
//   S3  At most driver.max_concurrent zones commanded and energised; never
//       more than three valve bits driven (system spec section 1, hardware
//       design 19.1 rule 3; the bit count is checked in ObservedBus).
//   S4  A zone's on-time, added over runs until it has been closed for the
//       minimum rest, never exceeds its maximum run time, across software
//       restarts (spec section 2 rule 4, firmware architecture section 20).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

#include "world.hpp"

namespace zf::soak {

struct FirmwareView {
    bool running = false; // control task ticking
    bool halted = false;  // control task hung, or the processor frozen
    bool frozen = false;  // the processor, with the timer interrupt
    bool frozenPinHigh = false;
    bool off = false;        // in reset or unpowered: no code runs, the pin is held low
    uint64_t lastBeatMs = 0; // phys of the last control tick
    std::array<bool, kOutputs> commanded{};
    uint8_t maxConcurrent = 2;
    std::array<uint64_t, kMaxZones> maxRunMs{};
    uint64_t minRestMs = 1800000;
};

class PhysicalOracle {
public:
    // An energised output the firmware did not command must stop within
    // this while the firmware runs: a current window (100 ms) to see it, one
    // tick to act, and the slowest relay detector release (1.45 s, hardware
    // design 19.4), with margin.
    static constexpr uint64_t kUncommandedLimitMs = 2500;
    // Heartbeat timeout plus one keep-alive period (firmware architecture
    // section 3).
    static constexpr uint64_t kEdgeStopMs = 505;
    // Hardware design 19.4: gate drive gone 0.83 s after the last edge with
    // the pin low, 1.25 s with it high; the relay transistor 1.45 s and
    // 2.19 s.
    static constexpr uint64_t kAReleaseLowMs = 830;
    static constexpr uint64_t kAReleaseHighMs = 1250;
    static constexpr uint64_t kBReleaseLowMs = 1450;
    static constexpr uint64_t kBReleaseHighMs = 2190;
    // A zone runs at most this much past its budget: one control tick and
    // one interrupt period.
    static constexpr uint64_t kBudgetSlackMs = 120;

    std::function<void(const char *, const std::string &)> violation;

    // dtMs: the time since the last call (5 ms, or a coarse step while idle).
    void step(World &w, const FirmwareView &fw, uint64_t dtMs = kIsrMs);
    // The firmware came up without its run budget (power-on, torn record).
    void budgetLost();
    uint64_t maxUncommandedMs() const { return maxUncommandedMs_; }
    uint64_t maxHaltToSafeMs() const { return maxHaltToSafeMs_; }

private:
    std::array<uint64_t, kOutputs> uncommandedSince_{};
    std::array<bool, kOutputs> uncommandedReported_{};
    struct Budget {
        uint64_t burstMs = 0;
        uint64_t closedMs = 0;
        bool reported = false;
        bool halted = false; // a halt happened during this burst
        bool lost = false;   // the firmware lost the budget during this burst
    };
    std::array<Budget, kMaxZones> budget_{};
    bool kaWasHigh_ = false;
    bool haltReported_ = false;
    bool edgeReported_ = false;
    uint64_t maxUncommandedMs_ = 0;
    uint64_t maxHaltToSafeMs_ = 0;
};

} // namespace zf::soak
