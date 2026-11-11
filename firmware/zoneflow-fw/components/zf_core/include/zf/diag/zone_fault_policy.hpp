// SPDX-License-Identifier: Apache-2.0
// What both firmwares do when a zone's current is wrong. The standalone
// Controller and the ESPHome Supervisor take every decision about stopping,
// locking out and clearing from here, so that the two cannot drift apart
// again (system soak simulation, firmware architecture section 22, gap 7).
//
// The policy, for a real installation (firmware architecture section 7):
//
//  - Open circuit: the zone is stopped and the fault reported, and it is not
//    locked out. An open valve wire is a fault in the field, not in the
//    controller: once someone fixes the wire, the next scheduled start runs
//    the zone, and a run whose current is inside the zone's window clears the
//    report. Nobody needs to walk to the controller for it. An open wire
//    draws no current, so retrying it harms nothing.
//  - Over-current and short circuit: every output off, the zone locked out
//    until a person clears the faults. Retrying a shorted coil or wire puts
//    fault current through the output each time.
//  - DC component: the zone is stopped and locked out. Current in one
//    direction only means one polarity of the output's triac or its gate
//    drive has failed: a half-dead output on the board, which needs a repair
//    and will not mend itself (hardware design 19.2).
//  - Current that rises with no output switched (CurrentFault::CurrentRose):
//    every output off at once. A triac that fails short adds its valve's
//    current to the run, and with every valve bit off the output stage's
//    rule 6 then finds it within one current window and opens the relay
//    (OutputShorted). If no idle current shows within kRiseDecideMs, the
//    extra current flowed through the zone that was on: it is reported and
//    locked out like an over-current.
//
// Pure C++; no state except ShortedOutputCheck.
#pragma once

#include <cstdint>

#include "zf/diag/current_monitor.hpp"
#include "zf/types.hpp"

namespace zf {

struct ZoneFaultPolicy {
    bool stopAll = false;         // every output off, not only the faulted one
    bool lockOut = false;         // the output stays off until a person clears the faults
    bool clearsOnGoodRun = false; // a later run inside the zone's window clears the report
};

constexpr ZoneFaultPolicy zoneFaultPolicy(CurrentFault f)
{
    switch (f) {
    case CurrentFault::OpenCircuit:
        return {false, false, true};
    case CurrentFault::OverCurrent:
    case CurrentFault::ShortCircuit:
        return {true, true, false};
    case CurrentFault::CurrentRose:
        return {true, false, false}; // lockout decided by ShortedOutputCheck
    case CurrentFault::None:
    case CurrentFault::CurrentWhileIdle:
        break;
    }
    return {};
}

// A DC component in a zone's current (hardware design 19.2).
inline constexpr ZoneFaultPolicy kDcComponentPolicy{false, true, false};

// After CurrentRose: was it a failed-short output device (found by the
// idle check with every valve off) or the zone itself?
class ShortedOutputCheck {
public:
    // Rule 6 needs one window that starts 20 ms after the valves went off
    // (at most 120 ms later) and ends 100 ms after that, plus a control tick
    // to act on it; the controller's own idle check on boards without an
    // output stage needs its settle time and three samples. 1.5 s covers both.
    static constexpr uint32_t kRiseDecideMs = 1500;

    void begin(uint8_t bit, MonoMs now)
    {
        bit_ = bit;
        sinceMs_ = now;
        active_ = true;
    }
    // The board found current with every output off: an output device.
    void idleCurrentFound() { active_ = false; }
    bool active() const { return active_; }
    // The output to blame once the time is up and no idle current was found;
    // 0xFF while undecided or when there is nothing to blame.
    uint8_t poll(MonoMs now)
    {
        if (!active_ || now - sinceMs_ < kRiseDecideMs) {
            return 0xFF;
        }
        active_ = false;
        return bit_;
    }

private:
    uint8_t bit_ = 0xFF;
    MonoMs sinceMs_ = 0;
    bool active_ = false;
};

} // namespace zf
