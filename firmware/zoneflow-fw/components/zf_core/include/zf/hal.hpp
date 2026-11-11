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
// step numbers in the comments. New values go at the end, before Count: the
// number is kept in RTC memory over a software restart (RetainedRecord), so
// an update must not give an old number a new meaning.
enum class HwCheck : uint8_t {
    None,
    IdleDriveEnabled,    // 1: A_RB high and the expander answers: detector A or the latch stuck on
    IdleRelayOn,         // 1: B_RB low with the keep-alive stopped: Q2 on, or V_BUCK missing (rule 15)
    IdleExpanderAnswers, // 1: expander acknowledges with A_RB low: DRV_EN stuck high
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
    StopDriveRelease,    // 9: A_RB released outside 0.15 to 1.0 s, or not within 3 s
    StopRelayRelease,    // 9: B_RB released outside 0.05 to 1.6 s, or not within 3 s
    StopExpanderAnswers, // 9: expander still answers after the stop
    BusError,            // I2C timeout or stuck bus
    LatchStuck,          // 19.8: OC still low 3 s after the keep-alive stopped
    TooManyOutputs,      // a write would drive more valve bits than allowed
    // Added after hardware review A2.
    IdleDriveSupplyOn,  // 1: A_RB high and no acknowledge: V_DRV on with DRV_EN low (Q3 conducting)
    IdleRelayGateDrain, // 1: GPIO7 reads 0.3 to 1.5 V at rest: Q2 shorted gate to drain (review A2 4.6 q7)
    FloatPinHigh,       // 2b: KA read high while it was an input
    FloatDriveEnabled,  // 2b: A_RB high after the float test
    FloatRelayOn,       // 2b: B_RB low after the float test
    WeldNoReadback,     // 5: the valve pin does not read low when its bit is driven low
    GateBleed,          // 5b: current with RLY_ON released undriven: R12 open
    StopDriveBand,      // 9: tA outside 0.40 to 1.70 times this board's stored value
    StopRelayBand,      // 9: tB outside 0.40 to 1.70 times this board's stored value
    KeepAlivePinRouted, // rule 16, 19.4: the keep-alive pin is not a plain GPIO (a peripheral drives it)
    // Added with the hardware answers to section 19 (issue #41).
    RectifierFault,    // 0, 19.10: VIN_MON under 0.10 V with mains edges for 2 s: D2, R1 or C1 failed
    SupplyOverVoltage, // 1, rule 18: both current channels rest above 0.90 V: 3V3 above about 3.6 V (U3)
    LatchInputHeld,    // 3, 19.8: OC low twice with no valve bit low: the latch's set input is held low
    Count,
};

const char *toString(StagePhase p);
const char *toString(HwCheck c);
// Section 19.7 step of a check: "1", "2b", "4a", "5b", "9". Empty for a
// check that is not a step of the sequence (bus error, the 19.8 latch, a
// firmware limit, rule 16).
const char *hwCheckStep(HwCheck c);
// Steps 1, 2, 2b, 5b and 9 mean one of the layers can no longer be relied on:
// the board needs repair, and acknowledging must be a deliberate second
// action (hardware design 19.7, the fault state). So do the rectifier fault
// of 19.10, the over-voltage of rule 18 and a held latch input (19.8).
bool hwCheckNeedsRepair(HwCheck c);

// Why the current measurement counts as lost (hardware design 19.1 rule 19,
// 19.10). Steps 5, 5b and 6 and rule 6 are skipped meanwhile, zones still
// run, and the condition is reported for as long as it lasts.
enum class CurrentLoss : uint8_t {
    None,
    Unavailable, // the ADC path did not start, or reports no readings
    NoWindow,    // a step waited for a current window and none arrived
    RestLevel,   // a current channel rests outside its limits (19.10)
};
const char *toString(CurrentLoss l);

// Detector release times this board gave at commissioning (hardware design
// 19.7, the limits of step 9). Step 9 then also requires each time to lie
// within 0.40 to 1.70 times its stored value.
struct DetectorReference {
    uint16_t driveMs = 0; // tA, median of the commissioning stop tests
    uint16_t relayMs = 0; // tB
    bool valid() const { return driveMs != 0 && relayMs != 0; }
};

// Non-volatile storage of the reference (standalone: NVS; ESPHome:
// preferences). Calls come from the control task only.
class IDetectorReferenceStore {
public:
    virtual ~IDetectorReferenceStore() = default;
    // False when nothing valid is stored.
    virtual bool load(DetectorReference &out) = 0;
    virtual bool save(const DetectorReference &r) = 0;
    virtual bool clear() = 0;
};

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
    // Step 2b runs, or is about to: the display, the backlight PWM and the
    // PSRAM must be in use (hardware design 19.1 rule 17).
    bool floatTest = false;
    // Last stop test (step 9), ms after the last edge; 0 = not measured.
    uint16_t releaseDriveMs = 0;
    uint16_t releaseRelayMs = 0;
    // Last start (step 3), ms after the first edge; 0 = not measured.
    uint16_t enableDriveMs = 0;
    uint16_t enableRelayMs = 0;
    uint32_t selfTestsPassed = 0;
    // This board's stored release times (19.7, the limits of step 9). Not
    // commissioned: only the absolute limits apply, and it is reported.
    bool commissioned = false;
    DetectorReference reference{};
    uint8_t commissionSamples = 0; // stop tests collected towards the reference
    // Commissioning waits for the die temperature (19.7 item 2): true while
    // nothing is stored and the latest reading is outside 25 to 50 C or
    // missing.
    bool commissionWaitingForTemperature = false;
    // Supply monitor (19.10): the latest VIN_MON window mean, mV; -1 if none.
    int32_t vinMonMv = -1;
    bool supplyLow = false;  // 0.10 to 0.60 V: step 0 waits; clears itself
    bool supplyHigh = false; // above 1.50 V: step 0 waits; clears itself
    // Rule 19: the current measurement is lost; steps 5, 5b and 6 and rule 6
    // are skipped while it lasts. Reported, never silent.
    CurrentLoss currentLost = CurrentLoss::None;
    uint32_t checksSkipped = 0; // starts that skipped steps 5, 5b and 6 for it
    // Rest levels of the current channels, mV, from the latest window with
    // the keep-alive stopped (19.10); -1 if none yet.
    int32_t restLoMv = -1;
    int32_t restHiMv = -1;
    uint32_t idleTrips = 0; // 19.8: OC low with no valve bit low, not counted against an output
};

// What the controller hands the output stage every tick.
struct StageTickInput {
    bool acPresent = false;   // an AC_ZC edge within 25 ms (raw, not debounced)
    CurrentReading current{}; // latest current window (with the GPIO7 mean, relayReadbackMv)
    bool currentSenseAvailable = false;
    uint16_t idleThresholdMa = 30; // hardware design 19.2
    bool sensorSupplyOn = false;   // GPIO45 driven high
    // Rule 16 and contract 19.4: the keep-alive pin is a plain GPIO with no
    // peripheral signal routed to it. The device reads the pin's IO_MUX and
    // GPIO matrix setting every tick.
    bool keepAlivePinPlain = true;
    // ESP32-S3 die temperature, C, for commissioning (19.7 item 2); a
    // commissioning sample is taken only while it reads 25 to 50 C.
    bool dieTempKnown = false;
    float dieTempC = 0.0f;
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
    // confirmed the board was repaired, which a detector fault needs; the
    // board then learns its detector release times again (19.7, step 9).
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
    // Rev A: the keep-alive pin is a plain GPIO with no peripheral routed to
    // it (hardware design 19.1 rule 16, 19.4).
    bool keepAlivePinPlain = true;
    // Rev A: the ESP32-S3 die temperature (19.7 commissioning).
    bool dieTempKnown = false;
    float dieTempC = 0.0f;
};

class ISensorHal {
public:
    virtual ~ISensorHal() = default;
    virtual SensorSample read() = 0;
};

} // namespace zf
