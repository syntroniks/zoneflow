// SPDX-License-Identifier: Apache-2.0
// Hardware abstraction used by the core. The device implementations live in
// the zf_platform component; host tests use fakes.
#pragma once

#include <cstdint>

#include "zf/diag/current_sense.hpp"
#include "zf/types.hpp"

namespace zf {

// ---- output stage status ---------------------------------------------------------

// Where a sequenced output stage (rev A: expander, relay, two keep-alive
// detectors; hardware design section 19.7) is in its cycle.
enum class StagePhase : uint8_t {
    None,        // no sequenced stage on this board: outputs switch directly
    NoAc,        // no 24 VAC: nothing is tested and nothing runs
    StartupTest, // the start-up self-test is running
    Idle,        // keep-alive stopped, expander in reset, relay open
    Starting,    // self-test before a run (steps 0 to 6)
    Ready,       // relay closed, valves may be switched (step 7)
    Stopping,    // end of run and stop test (steps 8 and 9)
    Polling,     // 10-minute polling cycle between runs (section 19.6)
    TripClear,   // over-current latch set: keep-alive held low to clear it
    Fault,       // a check failed: nothing runs until a person acknowledges
    Halted,      // keep-alive guard tripped (shutdown, panic path)
};

// The check that failed, for the fault report. Hardware design section 19.7
// step numbers in the comments.
enum class HwCheck : uint8_t {
    None,
    IdleDriveEnabled,    // 1: A_RB high with the keep-alive stopped
    IdleRelayOn,         // 1: B_RB low with the keep-alive stopped
    IdleExpanderAnswers, // 1: expander acknowledges while it should be in reset
    PinHighDriveEnabled, // 2: A_RB high with the pin held high (C22 shorted)
    PinHighRelayOn,      // 2: B_RB low with the pin held high (C51 shorted, bleed open)
    StartDriveTimeout,   // 3: A_RB not high within 150 ms of the first edge
    StartRelayTimeout,   // 3: B_RB not low within 150 ms
    StartNoAcknowledge,  // 3: expander does not answer within 150 ms
    ExpanderInputs,      // 4a: input ports not as the pull resistors set them
    ExpanderOutputs,     // 4b: output ports do not read back
    ExpanderConfig,      // 4c: configuration or polarity does not read back
    RelayWelded,         // 5: current with the relay open: welded contact, bridged JP1 or shorted Q1
    RelayNoReadback,     // 6: RLY_ON does not read back high
    OutputShorted,       // 6, rule 6: current with the relay closed and no valve on
    RunReadback,         // 7: output, configuration or valve inputs differ from the shadow copy
    RunDriveLost,        // 7: A_RB low while the keep-alive runs and OC is high
    RunRelayLost,        // 7: B_RB high while the keep-alive runs
    EndNoAcknowledge,    // 8: expander did not accept the end-of-run writes
    StopDriveRelease,    // 9: A_RB released outside 0.3 to 1.0 s
    StopRelayRelease,    // 9: B_RB released outside 0.08 to 1.0 s
    StopExpanderAnswers, // 9: expander still answers after the stop
    BusError,            // I2C timeout or stuck bus
    LatchStuck,          // 19.8: OC still low 3 s after the keep-alive stopped
    TooManyOutputs,      // a write would drive more valve bits than allowed
    Count,
};

const char *toString(StagePhase p);
const char *toString(HwCheck c);
// Section 19.7 step of a check (19.8 latch: 10; firmware limit: 0).
uint8_t hwCheckStep(HwCheck c);
// Steps 1, 2 and 9 mean a keep-alive detector can no longer be relied on: the
// board needs repair, and acknowledging must be a deliberate second action.
bool hwCheckNeedsRepair(HwCheck c);

struct OutputStageStatus {
    StagePhase phase = StagePhase::None;
    uint8_t zonesFitted = 0;        // from the variant strap; 0 = not read yet
    bool relayClosed = false;       // RLY_ON driven high
    bool sensorSupplyFault = false; // SENS_FLT_N low at the latest read
    bool sensorSupplyKnown = false; // read at least once
    HwCheck fault = HwCheck::None;  // latched until acknowledged
    uint32_t faultSeq = 0;          // increments on every new fault
    uint32_t tripSeq = 0;           // increments on every over-current trip
    bool tripClearing = false;      // keep-alive held low to clear the latch
    // Last stop test (step 9), ms after the last edge; 0 = not measured.
    uint16_t releaseDriveMs = 0;
    uint16_t releaseRelayMs = 0;
    // Last start (step 3), ms after the first edge; 0 = not measured.
    uint16_t enableDriveMs = 0;
    uint16_t enableRelayMs = 0;
    uint32_t selfTestsPassed = 0;
};

// What the controller hands the output stage every tick.
struct StageTickInput {
    bool acPresent = false;   // an AC_ZC edge within 25 ms (raw, not debounced)
    CurrentReading current{}; // latest current window
    bool currentSenseAvailable = false;
    uint16_t idleThresholdMa = 30; // hardware design 19.2
    bool sensorSupplyOn = false;   // GPIO45 driven high
};

// Valve output drivers. Active level, pin numbers and drive details are the
// implementation's business.
//
// Simple boards switch outputs directly: the defaults below make every
// output ready at once. Rev A powers its output stage only on request and
// only after a self-test (hardware design 19.7), so the zone driver asks
// for power and waits until powerReady().
class IOutputHal {
public:
    virtual ~IOutputHal() = default;
    // Control task only.
    virtual void setZone(ZoneIndex zone, bool on) = 0;
    virtual void setMaster(bool on) = 0;
    // Drives every valve output off. Control task; may be called repeatedly.
    virtual void allOff() = 0;
    // Any context, including panic and brownout: no bus traffic, no locks.
    // Removes output drive as far as the board allows without the control
    // task (rev A: the keep-alive guard is tripped by the caller).
    virtual void emergencyAllOff() { allOff(); }

    // True when the board drives the keep-alive itself (rev A): the zone
    // driver then does not set the guard's demand.
    virtual bool ownsKeepAlive() const { return false; }
    // Control task, every tick: whether outputs are wanted, and which one
    // will switch on first (bit as in ZoneDriver::outputMask(); 0xFF if not
    // known). The first output is used for the relay weld check.
    virtual void requestPower(bool wanted, uint8_t firstBit)
    {
        (void)wanted;
        (void)firstBit;
    }
    // True when outputs may be switched on now.
    virtual bool powerReady() const { return true; }
    // Control task, every tick, before the zone driver.
    virtual void tick(const StageTickInput &in) { (void)in; }
    virtual OutputStageStatus status() const { return {}; }
    // A person acknowledged the latched fault. repairConfirmed: they also
    // confirmed the board was repaired, which a detector fault needs.
    // Returns false if the fault stays.
    virtual bool acknowledgeFault(bool repairConfirmed)
    {
        (void)repairConfirmed;
        return true;
    }
    // Asks for a polling cycle between runs (hardware design 19.6).
    virtual void requestPoll() {}
    // Before the first tick: a fault latched before a software restart.
    virtual void restoreFault(HwCheck check) { (void)check; }
};

// Raw sensor readings, sampled once per control tick.
struct SensorSample {
    bool rainContactClosed = false;
    bool spareContactClosed = false;
    bool acPresent = false;
    uint32_t flowPulses = 0; // free-running counter, wraps at 2^32
    // Total valve current, RMS, already scaled; -1 if no reading. Boards
    // that deliver raw windows (currentWindow) leave this at -1 and the
    // controller computes it.
    int32_t valveCurrentMa = -1;
    // Raw 100 ms window of the two current channels (rev A). Empty (no
    // samples) on boards and fakes that give valveCurrentMa directly.
    CurrentWindowStats currentWindow{};
    // False when the current-sense path could not be initialised or
    // calibrated, or gave no reading: raises current_sense_unavailable.
    bool currentSenseAvailable = true;
    // Hardware over-current comparator has removed gate drive and latched.
    bool overCurrentTripped = false;
    // Mains frequency from the zero-cross edges, in mHz; 0 if unknown.
    uint32_t mainsMilliHz = 0;
    // The 5 V sensor supply is switched on (rev A: GPIO45).
    bool sensorSupplyOn = false;
};

class ISensorHal {
public:
    virtual ~ISensorHal() = default;
    virtual SensorSample read() = 0;
};

} // namespace zf
