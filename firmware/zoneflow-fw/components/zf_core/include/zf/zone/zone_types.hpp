// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "zf/types.hpp"

namespace zf {

enum class ValveState : uint8_t {
    Off,
    Waiting, // accepted, waiting for master pre-open or inter-zone delay
    Open,
};

enum class StopReason : uint8_t {
    None,
    Completed,  // requested duration elapsed
    Requested,  // stop, skip or pause
    MaxRunTime, // hard per-zone limit hit
    Fault,      // diagnostics shut it off
    AcLost,     // 24 VAC disappeared
    Shutdown,   // restart, OTA, panic
    Maintenance,
};

enum class StartError : uint8_t {
    Ok,
    InvalidZone,
    Disabled,
    LockedOut, // latched fault on this zone
    Inhibited, // driver inhibited (maintenance, no 24 VAC)
    TooManyActive,
    AlreadyActive,
    ZeroDuration,
    Resting,         // zone reached its maximum run time and must stay closed for the rest time
    MasterLockedOut, // master valve locked out by repeated faults
};

struct StartResult {
    StartError error = StartError::Ok;
    uint32_t acceptedSec = 0; // after clamping to the zone's maximum run time
    bool ok() const { return error == StartError::Ok; }
};

const char *toString(ValveState s);
const char *toString(StopReason r);
const char *toString(StartError e);

// What the scheduler needs from the zone driver. Lets scheduler tests use a
// fake actuator.
class IZoneActuator {
public:
    virtual ~IZoneActuator() = default;
    virtual StartResult start(ZoneIndex zone, uint32_t durationSec) = 0;
    virtual void stop(ZoneIndex zone, StopReason reason) = 0;
    virtual ValveState state(ZoneIndex zone) const = 0;
    // Remaining run time; for a waiting zone, the full accepted duration.
    virtual uint32_t remainingMs(ZoneIndex zone) const = 0;
    // A latched condition of the zone that refuses any start now (LockedOut),
    // or Ok. Lets a manual run be refused when it is requested instead of
    // being dropped later at the head of the queue.
    virtual StartError startBlocker(ZoneIndex zone) const
    {
        (void)zone;
        return StartError::Ok;
    }
};

} // namespace zf
