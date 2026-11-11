// SPDX-License-Identifier: Apache-2.0
// The rev A output stage: a TCA9539 I/O expander that drives the valve
// outputs and the valve-common relay, held in reset by the keep-alive
// detectors (hardware design sections 7, 8 and 19).
//
// OutputStage implements IOutputHal for the zone driver and runs the
// procedures of hardware design section 19 on the control task:
//
//  - the self-test and run sequence of 19.7, steps 0 to 9 with the float
//    test (2b) and the gate bleed check (5b) of review A2, at start-up,
//    before and after every run;
//  - the limits of step 9: absolute limits, and a band of 0.40 to 1.70 times
//    this board's own release times, learned at commissioning (only while
//    the die temperature reads 25 to 50 C) and kept in non-volatile memory
//    (IDetectorReferenceStore);
//  - the supply monitor and the rest levels of the current channels
//    (19.10): step 0 waits for VIN_MON in its range, a missing rectified
//    supply is a fault, both rest levels high is the over-voltage of rule
//    18, one outside its limits is a lost current measurement (rule 19);
//  - the register sequence, a read-back after every write and a scrub of
//    every register on every control tick, never more valve bits low than
//    the board allows;
//  - relay sequencing: closed 20 ms before the first valve (step 6), open
//    40 ms after the last (step 8);
//  - the weld check (step 5) and the shorted-output check (step 6, rule 6);
//  - the polling cycle between runs (19.6);
//  - the over-current latch: read, hold the keep-alive low to clear it,
//    check it cleared (19.8); a trip with no valve bit low is not counted
//    against an output, and a second one is a held set input (U13);
//  - the fault state: keep-alive low, every output off and every expander
//    pin back to input while the expander still answers, nothing runs
//    until acknowledged.
//
// It owns the keep-alive request of OutputEnableGuard. The guard's timer
// interrupt still decides every edge from the control-loop heartbeat, and
// samples the two detector read-backs and the latch every period; this
// class reads that record to time the detectors to one period.
//
// I2C goes through IExpanderBus, whose every call is bounded in time. Any
// error ends with the keep-alive stopped, which resets the expander and opens
// the relay in hardware whatever the bus does next.
//
// Pure C++: host tests drive it with a simulated board.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "zf/hal.hpp"
#include "zf/time/clock.hpp"
#include "zf/zone/output_enable.hpp"

namespace zf::hw {

// TCA9539 registers, TI SCPS202C.
namespace reg {
inline constexpr uint8_t kInput0 = 0x00;
inline constexpr uint8_t kInput1 = 0x01;
inline constexpr uint8_t kOutput0 = 0x02;
inline constexpr uint8_t kOutput1 = 0x03;
inline constexpr uint8_t kPolarity0 = 0x04;
inline constexpr uint8_t kPolarity1 = 0x05;
inline constexpr uint8_t kConfig0 = 0x06; // 1 = input (reset state)
inline constexpr uint8_t kConfig1 = 0x07;
} // namespace reg

enum class BusStatus : uint8_t {
    Ok,
    Nack,     // no acknowledge: the device is in reset or absent
    Timeout,  // the transaction did not finish in time
    BusError, // arbitration lost, stuck line, driver error
};
const char *toString(BusStatus s);

class IExpanderBus {
public:
    virtual ~IExpanderBus() = default;
    // Every call returns within a bounded time (the device: the I2C
    // transaction timeout, plus the bus-clear time for recover()).
    virtual BusStatus write(uint8_t reg, uint8_t value) = 0;
    virtual BusStatus read(uint8_t reg, uint8_t &value) = 0;
    // Address only: Ok when the device acknowledges.
    virtual BusStatus probe() = 0;
    // After Timeout or BusError: clock a stuck slave free and reset the
    // controller.
    virtual void recover() = 0;
    // `reg` and the other register of its pair (0 and 1, 2 and 3, ...) in
    // one transaction: address, command byte `reg`, `v0`, `v1`. TI SCPS202C
    // figure 31: each port takes its byte at that byte's acknowledge, so
    // port 1 follows port 0 by nine SCL periods, 22.5 us at 400 kHz, plus
    // the output valid time of at most 0.2 us. The default is two
    // transactions, for buses that cannot send two data bytes.
    virtual BusStatus writePair(uint8_t reg, uint8_t v0, uint8_t v1)
    {
        const BusStatus st = write(reg, v0);
        return st != BusStatus::Ok ? st : write(static_cast<uint8_t>(reg + 1u), v1);
    }
};

// Hardware design 19.1 rule 3 and section 22 of the firmware architecture:
// the fault path switches every output off with one pair write, so that
// zones 5 to 8 follow zones 1 to 4, the master and the relay by at most this
// long (nine SCL periods at 400 kHz, rounded up, plus t_pv).
inline constexpr uint32_t kPairWriteSkewNs = 9u * 2500u + 200u;

struct BitRef {
    uint8_t port = 0;
    uint8_t bit = 0;
};

// Which expander bit does what. Built from the board header.
struct ExpanderLayout {
    std::array<BitRef, kMaxZones> zone{};
    BitRef master{};
    BitRef relay{};
    BitRef variant8ZoneN{};
    BitRef sensorFaultN{};

    static constexpr uint8_t mask(BitRef b, uint8_t port)
    {
        return b.port == port ? static_cast<uint8_t>(1u << b.bit) : 0;
    }
    // Valve bits (zones below `zones`, and the master) on a port.
    constexpr uint8_t valveMask(uint8_t port, uint8_t zones) const
    {
        uint8_t m = mask(master, port);
        for (uint8_t z = 0; z < zones && z < kMaxZones; ++z) {
            m |= mask(zone[z], port);
        }
        return m;
    }
    constexpr uint8_t relayMask(uint8_t port) const { return mask(relay, port); }
    // Step 4a: every pin an input, so the pull resistors show: valve pins
    // high (pulled to V_DRV on both builds), relay and spares low, the
    // sensor-fault input high when healthy. The variant bit is information.
    constexpr uint8_t idleInputExpected(uint8_t port) const
    {
        return static_cast<uint8_t>(valveMask(port, kMaxZones) | mask(sensorFaultN, port));
    }
    // Bits step 4a compares: all but the variant and the sensor fault.
    constexpr uint8_t idleInputChecked(uint8_t port) const
    {
        return static_cast<uint8_t>(0xFFu & ~(mask(variant8ZoneN, port) | mask(sensorFaultN, port)));
    }
    // Step 4b: every valve off (high), the relay off (low).
    constexpr uint8_t outputsOff(uint8_t port) const { return static_cast<uint8_t>(0xFFu & ~relayMask(port)); }
    // Step 4c: valve and relay bits outputs (0), everything else input (1).
    constexpr uint8_t configRun(uint8_t port, uint8_t zones) const
    {
        return static_cast<uint8_t>(0xFFu & ~(valveMask(port, zones) | relayMask(port)));
    }
};

struct OutputStageConfig {
    // Hardware design 19.1 rule 3: never more valve bits low.
    uint8_t maxValveBitsLow = 3;
    // Power kept up after the last output went off, so back-to-back zones
    // and programs do not repeat the 2 s self-test between them.
    uint32_t lingerMs = 2000;
    // Polling cycle between runs, hardware design 19.6.
    uint32_t pollIntervalMs = 10u * 60u * 1000u;
    // Step 2 before every run (the default of 19.7); otherwise at start-up
    // and at least once every 24 h. Step 2b (the float test) runs at
    // start-up and before every run whatever this says (19.7, note to
    // step 2).
    bool pinHighTestEveryRun = true;
    // Control period: the start limit of step 3 is checked on a tick, so it
    // allows one period of slack for the acknowledge poll.
    uint32_t tickMs = 50;
    // Commissioning cycles (steps 0, 1, 3, 4a, 9) one attempt may run back
    // to back while there is no stored reference (19.7 item 4: ten at
    // most). An attempt that stores nothing is followed by another one
    // `commissionRetryMs` later, for as long as nothing is stored (item 1).
    uint8_t maxCommissionCycles = 10;
    uint32_t commissionRetryMs = 10u * 60u * 1000u;
};

class OutputStage final : public IOutputHal {
public:
    // Hardware design 19.4 and 19.7, as review A2 left them.
    static constexpr uint32_t kKeepAliveLowBeforeStartMs = 2500; // step 0
    // Step 2: 2.0 s since the hardware answers (was 1.0 s). The one rising
    // edge turns on a Q2 with a low threshold for up to 1.33 s at 70 C
    // (hardware/sim/failsafe/sequence.py, 19.7 note to step 2).
    static constexpr uint32_t kPinHighHoldMs = 2000;
    static constexpr uint32_t kPinHighReleaseMs = 100; // step 2
    static constexpr uint32_t kFloatTestMs = 500;      // step 2b
    static constexpr uint32_t kFloatReleaseMs = 100;   // step 2b
    // Step 2b: the load runs from 100 ms before the pin floats (19.7, note
    // to step 2b). After step 2 that is its 100 ms low; without step 2 the
    // stage waits this long with the load on.
    static constexpr uint32_t kFloatLoadLeadMs = 100;
    // Step 2b reads the pin every 1 ms. Fewer reads than this in the 0.5 s
    // means the test did not run as written, and it counts as failed.
    static constexpr uint32_t kFloatMinReads = 400;
    static constexpr uint32_t kStartLimitMs = 150;    // step 3
    static constexpr uint32_t kGateBleedHoldMs = 20;  // step 5b: RLY_ON set this long
    static constexpr uint32_t kGateBleedWaitMs = 10;  // step 5b: then released undriven this long
    static constexpr uint32_t kRelaySettleMs = 20;    // step 6, rule 4
    static constexpr uint32_t kRelayOpenDelayMs = 40; // step 8, rule 4
    // Step 9. The timeout and every limit leave room for the relay
    // detector's slow corner of review A2 (finding m10): 1.45 s after the
    // last edge with the pin driven low, which is how the firmware always
    // stops it, and 2.19 s if a fault leaves the pin high. Nothing here
    // assumes the one second of the specification.
    static constexpr uint32_t kStopTestMaxMs = 3000;
    static constexpr uint32_t kDriveReleaseMinMs = 150; // tA
    static constexpr uint32_t kDriveReleaseMaxMs = 1000;
    static constexpr uint32_t kRelayReleaseMinMs = 50; // tB
    static constexpr uint32_t kRelayReleaseMaxMs = 1600;
    // The band, 0.40 to 1.70 times the stored value (19.7, from
    // hardware/sim/failsafe/commission.py: no healthy stop test from -20 to
    // 70 C falls outside it; 0.5 to 1.5 failed 8).
    static constexpr uint32_t kBandLowPercent = 40;
    static constexpr uint32_t kBandHighPercent = 170;
    // Commissioning (19.7, "Commissioning", items 1 to 7): the medians of
    // five start-up or commissioning cycles in a row, never of the stop test
    // of a run or a polling cycle, each time inside the learning limits
    // below, the longest of the five at most 1.25 times the shortest, and
    // only while the die temperature reads 25 to 50 C. The learning limits
    // are the healthy boards between 10 and 40 C with a few percent of
    // room: they refuse every board with an open bleed resistor on
    // detector A and about a quarter of those with one on detector B. No
    // ratio of tB to tA: it does not separate the two (commission.py).
    static constexpr uint8_t kCommissionSamples = 5;
    static constexpr uint32_t kLearnDriveMinMs = 330;
    static constexpr uint32_t kLearnDriveMaxMs = 850;
    static constexpr uint32_t kLearnRelayMinMs = 120;
    static constexpr uint32_t kLearnRelayMaxMs = 1250;
    static constexpr uint32_t kLearnMaxSpreadPercent = 125;
    static constexpr float kCommissionTempMinC = 25.0f;
    static constexpr float kCommissionTempMaxC = 50.0f;
    // Supply monitor, 19.10: window means of VIN_MON.
    static constexpr int32_t kVinAbsentMv = 100; // below: no rectified supply
    static constexpr int32_t kVinMinMv = 600;    // step 0 needs at least this
    static constexpr int32_t kVinMaxMv = 1500;   // and at most this
    static constexpr uint32_t kRectifierFaultMs = 2000;
    // Rest levels of the current channels with the keep-alive stopped, 19.10.
    static constexpr int32_t kRestLoMinMv = 700;
    static constexpr int32_t kRestLoMaxMv = 880;
    static constexpr int32_t kRestHiMinMv = 680;
    static constexpr int32_t kRestHiMaxMv = 900;
    static constexpr int32_t kRestOverVoltageMv = 900; // both above: 3V3 above about 3.6 V, rule 18
    // Review A2 4.6 question 7: GPIO7 between 0.3 and 1.5 V at rest is Q2
    // shorted gate to drain.
    static constexpr int32_t kRelayGateDrainMinMv = 300;
    static constexpr int32_t kRelayGateDrainMaxMv = 1500;
    static constexpr uint32_t kTripHoldMs = 1500; // 19.8: keep-alive low this long
    static constexpr uint32_t kTripClearMaxMs = 3000;
    static constexpr uint32_t kWindowWaitMs = 500;       // a current window must arrive within this
    static constexpr uint32_t kAfterOffSettleMs = 20;    // valve current gone one half-cycle after off
    static constexpr uint32_t kRestWindowMaxAgeMs = 300; // a window this fresh lies inside the 2.5 s at rest
    static constexpr uint32_t kPinHighMaxAgeMs = 24u * 3600u * 1000u;
    // 19.7, toggling before a stop test: at least 0.3 s, or the release
    // times are short of their final values (review A2 4.6 question 1). The
    // firmware toggles at least 1 s before every stop test, the start-up,
    // polling and commissioning cycles included.
    static constexpr uint32_t kMinToggleRequiredMs = 300;
    static constexpr uint32_t kMinToggleBeforeStopMs = 1000;
    static_assert(kMinToggleBeforeStopMs >= kMinToggleRequiredMs, "hardware design 19.7: 0.3 s before a stop test");
    static_assert(kStopTestMaxMs > kRelayReleaseMaxMs && kKeepAliveLowBeforeStartMs > 2190,
                  "room for the relay detector's slowest release, 2.19 s with the pin high (review A2 m10)");

    // `reference` keeps this board's release times; nullptr keeps them in
    // RAM only (learned again after every start).
    OutputStage(IExpanderBus &bus, OutputEnableGuard &guard, const IClock &clock, const ExpanderLayout &layout,
                const OutputStageConfig &cfg = {}, IDetectorReferenceStore *reference = nullptr);

    // Before the first tick: a fault that was latched before a software
    // restart (RTC memory). The stage starts in the fault state.
    void restoreFault(HwCheck check) override;

    // IOutputHal
    void setZone(ZoneIndex zone, bool on) override;
    void setMaster(bool on) override;
    void allOff() override;
    void emergencyAllOff() override { emergency_.store(true, std::memory_order_release); }
    bool ownsKeepAlive() const override { return true; }
    void requestPower(bool wanted, uint8_t firstBit) override;
    bool powerReady() const override { return step_ == Step::Ready; }
    void tick(const StageTickInput &in) override;
    OutputStageStatus status() const override;
    bool acknowledgeFault(bool repairConfirmed) override;
    void requestPoll() override { pollRequested_ = true; }

    // For tests and diagnostics.
    uint8_t shadowOutput(uint8_t port) const { return outShadow_[port & 1u]; }
    uint8_t shadowConfig(uint8_t port) const { return cfgShadow_[port & 1u]; }
    uint8_t valveBitsLow() const;
    // The fault path's all-off write (doFault): every output off in one pair
    // write, then every pin an input in a second one. `recoverBus`: clock a
    // stuck bus free if the probe times out (the fault state's every tick).
    void releaseAll(bool recoverBus = false);
    // How long the keep-alive toggled before the latest stop test, ms.
    uint32_t toggledBeforeLastStopMs() const { return toggledBeforeStopMs_; }

private:
    enum class Step : uint8_t {
        NoAc,
        Idle,
        Precheck,       // 0
        IdleTest,       // 1
        PinHigh,        // 2, pin held high
        PinHighRelease, // 2, pin low again
        FloatLead,      // 2b without step 2: the load on, pin low, 100 ms
        Float,          // 2b, pin an input
        FloatRelease,   // 2b, pin low again
        Start,          // 3
        Setup,          // 4a to 4c
        PollRead,       // 19.6 and commissioning: input port 1 between 4a and 9
        Weld,           // 5
        GateBleed,      // 5b
        Relay,          // 6
        Ready,          // 7
        EndValves,      // 8: valves off, waiting 40 ms
        StopTest,       // 9
        NackPending,    // a write was not acknowledged: trip or fault, decided next tick
        TripClear,      // 19.8
        Fault,
        Halted,
    };
    enum class Proc : uint8_t { None, Startup, Run, Poll, Commission };

    void enter(Step s);
    void begin(Proc p);
    void finishProcedure();
    void fail(HwCheck check);
    void onTrip();
    void abortNoAc();
    bool keepAliveActive() const;

    void doPrecheck(const KeepAliveReadbacks &rb);
    void doIdleTest(const KeepAliveReadbacks &rb);
    void doPinHigh(const KeepAliveReadbacks &rb);
    void doPinHighRelease(const KeepAliveReadbacks &rb);
    void doFloatLead(const KeepAliveReadbacks &rb);
    void doFloat(const KeepAliveReadbacks &rb);
    void doFloatRelease(const KeepAliveReadbacks &rb);
    void doStart(const KeepAliveReadbacks &rb);
    void doSetup();
    void doPollRead(const KeepAliveReadbacks &rb);
    bool firstOutput(BitRef &b) const;
    void doWeld();
    void doGateBleed();
    void doRelay();
    void doReady(const KeepAliveReadbacks &rb);
    void doEnd(const KeepAliveReadbacks &rb);
    bool toggledLongEnough(const KeepAliveReadbacks &rb) const
    {
        return rb.applied == KeepAliveRequest::Run && rb.nowMs - rb.appliedSinceMs >= kMinToggleBeforeStopMs;
    }
    // Rule 19: steps 5, 5b and 6 and rule 6 need current windows, and a
    // channel resting inside its limits.
    bool currentUsable() const { return in_.currentSenseAvailable && !windowLost_ && !restLost_; }
    void beginStopTest(const KeepAliveReadbacks &rb);
    void doStopTest(const KeepAliveReadbacks &rb);
    // Step 9 passed its absolute limits: the band, or a commissioning sample.
    bool judgeAgainstReference(uint32_t tA, uint32_t tB);
    void learn(uint32_t tA, uint32_t tB);
    void loadReference();
    void forgetReference();
    bool restReadbackGateDrain() const;
    void doFault();
    // 19.10 and rules 18, 19: the supply monitor and the rest levels, from
    // every new window. False when it raised a fault.
    bool watchSupply(MonoMs now);
    bool restWindow(const CurrentReading &w) const;
    bool supplyInRange() const;
    bool dieTempForCommissioning() const;
    void noteSkippedChecks();

    // Bus helpers: false on any error, after handling it (fault or trip).
    bool write(uint8_t r, uint8_t v, HwCheck onError);
    bool read(uint8_t r, uint8_t &v, HwCheck onError);
    bool writeChecked(uint8_t r, uint8_t v, HwCheck onMismatch);
    bool busFailed(BusStatus st, HwCheck onNack);
    bool setValve(BitRef b, bool on);
    // The latest current window that started at or after `fromMs`, if any.
    bool windowSince(MonoMs fromMs, CurrentReading &out) const;

    IExpanderBus &bus_;
    OutputEnableGuard &guard_;
    const IClock &clock_;
    const ExpanderLayout layout_;
    const OutputStageConfig cfg_;
    IDetectorReferenceStore *refStore_;

    Step step_ = Step::NoAc;
    Proc proc_ = Proc::None;
    MonoMs stepSinceMs_ = 0;
    StageTickInput in_{};
    bool stepFirstTick_ = true;

    // Demand from the zone driver.
    bool wanted_ = false;
    uint8_t firstBit_ = 0xFF;
    MonoMs unwantedSinceMs_ = 0;

    std::array<uint8_t, 2> outShadow_{0xFF, 0xFF};
    std::array<uint8_t, 2> cfgShadow_{0xFF, 0xFF};
    uint8_t zonesFitted_ = 0;
    bool relayClosed_ = false;
    MonoMs relayClosedMs_ = 0;
    MonoMs lastValveOffMs_ = 0;
    MonoMs weldStartMs_ = 0;
    bool weldBitOn_ = false;
    uint8_t bleedPhase_ = 0; // step 5b: 0 start, 1 RLY_ON set, 2 released, 3 output on
    MonoMs bleedSinceMs_ = 0;
    bool startupDone_ = false;
    bool bootScrubDone_ = false; // first step 0 after start-up: pins left driven back to input
    MonoMs endValvesMs_ = 0;
    bool endWritesDone_ = false;

    HwCheck fault_ = HwCheck::None;
    HwCheck pendingNack_ = HwCheck::None;
    uint32_t faultSeq_ = 0;
    uint32_t tripSeq_ = 0;
    bool sensorFault_ = false;
    bool sensorKnown_ = false;
    uint16_t releaseDriveMs_ = 0;
    uint16_t releaseRelayMs_ = 0;
    uint16_t enableDriveMs_ = 0;
    uint16_t enableRelayMs_ = 0;
    uint32_t passed_ = 0;

    bool pollRequested_ = false;
    MonoMs lastActiveEndMs_ = 0; // keep-alive last stopped (for polling)
    bool pinHighDone_ = false;
    MonoMs lastPinHighMs_ = 0;
    bool pinHighThisProc_ = false;
    uint32_t toggledBeforeStopMs_ = 0;

    // Step 9 reference (19.7).
    bool refLoaded_ = false;
    DetectorReference reference_{};
    std::array<uint16_t, kCommissionSamples> sampleDrive_{};
    std::array<uint16_t, kCommissionSamples> sampleRelay_{};
    uint8_t samples_ = 0;
    uint8_t commissionCycles_ = 0; // this boot

    // Commissioning attempts (19.7 items 1 and 2).
    MonoMs nextCommissionMs_ = 0;

    // Supply monitor and rest levels (19.10).
    uint32_t lastWindowSeq_ = 0;
    int32_t vinMv_ = -1;
    bool vinAbsent_ = false;
    MonoMs vinAbsentSinceMs_ = 0;
    bool acSteady_ = false;
    MonoMs acSinceMs_ = 0;
    int32_t restLoMv_ = -1;
    int32_t restHiMv_ = -1;
    bool restLost_ = false;
    bool windowLost_ = false; // a step waited for a window in vain
    uint32_t checksSkipped_ = 0;
    bool skipNoted_ = false; // this start's skip is counted

    // 19.8: trips with no valve bit low, not counted against an output.
    uint32_t idleTrips_ = 0;
    uint8_t idleTripsInRow_ = 0;

    std::atomic<bool> emergency_{false};
};

} // namespace zf::hw
