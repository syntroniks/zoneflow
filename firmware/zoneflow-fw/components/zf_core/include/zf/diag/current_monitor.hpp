// SPDX-License-Identifier: Apache-2.0
// Valve current evaluation. The board measures one total current for all
// outputs (spec section 3.1), so the expected value is the sum of the
// windows of every output that is on, and a fault is attributed to the
// output that switched on most recently.
#pragma once

#include <array>
#include <cstdint>

#include "zf/types.hpp"

namespace zf {

struct CurrentWindow {
    uint16_t minMa = 0;
    uint16_t maxMa = 0;
};

struct CurrentMonitorConfig {
    bool enabled = true;
    // Ignore window comparisons this long after any output change (inrush,
    // spec: 1.5 A for 100 ms, plus filter settling in the front end).
    uint32_t settleMs = 500;
    // Above this with every output off: an output is stuck on.
    uint16_t idleMaxMa = 60;
    // Above this at any time, settled or not: short circuit.
    uint16_t shortCircuitMa = 3000;
    // Consecutive samples needed before a fault is declared.
    uint8_t confirmSamples = 3;
    std::array<CurrentWindow, kMaxZones> zone{};
    CurrentWindow master{};
};

enum class CurrentFault : uint8_t {
    None,
    OpenCircuit,      // below the expected window: valve or wiring open
    OverCurrent,      // above the expected window: shorted coil, wrong valve
    ShortCircuit,     // above the absolute limit
    CurrentWhileIdle, // current with every output off: output stuck on
};

const char *toString(CurrentFault f);

struct CurrentVerdict {
    CurrentFault fault = CurrentFault::None;
    uint8_t outputBit = 0xFF; // bit as in ZoneDriver::outputMask(); 0xFF = unknown
};

class CurrentMonitor {
public:
    static constexpr uint8_t kMasterBit = kMaxZones;

    void configure(const CurrentMonitorConfig &cfg);

    // Lets a condition that is still present be reported again (after the
    // user cleared the faults).
    void rearm();

    // outputMask: bit z = zone z on, bit kMasterBit = master on.
    // Returns a fault once, when it is confirmed; later samples of the same
    // condition return None until the output mask changes or rearm().
    // Over-current and short-circuit samples count towards one confirmation.
    CurrentVerdict evaluate(int32_t measuredMa, uint16_t outputMask, uint8_t lastOpenedBit, MonoMs lastChangeMs,
                            MonoMs now);

    // Mean current observed with only this output on (plus master, with the
    // master's own share removed). 0 if never observed. Basis for per-zone
    // learned current in a later phase.
    uint16_t observedMa(uint8_t bit) const { return bit <= kMasterBit ? observed_[bit] : 0; }

private:
    void learn(int32_t measuredMa, uint16_t mask);

    CurrentMonitorConfig cfg_{};
    uint16_t lastMask_ = 0xFFFF;
    CurrentFault pending_ = CurrentFault::None;
    uint8_t count_ = 0;
    bool reported_ = false;
    std::array<uint16_t, kMaxZones + 1> observed_{};
};

} // namespace zf
