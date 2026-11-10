// SPDX-License-Identifier: Apache-2.0
// Hardware abstraction used by the core. The device implementations live in
// the zf_platform component; host tests use fakes.
#pragma once

#include <cstdint>

#include "zf/types.hpp"

namespace zf {

// Valve output drivers. Active level, pin numbers and drive details are the
// implementation's business.
class IOutputHal {
public:
    virtual ~IOutputHal() = default;
    virtual void setZone(ZoneIndex zone, bool on) = 0;
    virtual void setMaster(bool on) = 0;
    // Drives every valve output off. Must be safe to call from any task and
    // repeatedly. The device implementation is also safe in a panic or
    // brownout context.
    virtual void allOff() = 0;
};

// Raw sensor readings, sampled once per control tick.
struct SensorSample {
    bool rainContactClosed = false;
    bool spareContactClosed = false;
    bool acPresent = false;
    uint32_t flowPulses = 0;     // free-running counter, wraps at 2^32
    int32_t valveCurrentMa = -1; // total valve current, RMS estimate; -1 if no reading this tick
    // False when the current-sense path could not be initialised or
    // calibrated, or gave no reading: raises current_sense_unavailable.
    bool currentSenseAvailable = true;
};

class ISensorHal {
public:
    virtual ~ISensorHal() = default;
    virtual SensorSample read() = 0;
};

} // namespace zf
