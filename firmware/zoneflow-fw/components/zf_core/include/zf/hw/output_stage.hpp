// SPDX-License-Identifier: Apache-2.0
// The rev A output stage: a TCA9539 I/O expander that drives the valve
// outputs and the valve-common relay, held in reset by the keep-alive
// detectors (hardware design sections 7, 8 and 19).
//
// OutputStage implements IOutputHal for the zone driver and runs the
// procedures of hardware design section 19 on the control task:
//
//  - the self-test and run sequence of 19.7, steps 0 to 9, at start-up,
//    before and after every run;
//  - the register sequence, a read-back after every write and a scrub of
//    every register on every control tick, never more valve bits low than
//    the board allows;
//  - relay sequencing: closed 20 ms before the first valve (step 6), open
//    40 ms after the last (step 8);
//  - the weld check (step 5) and the shorted-output check (step 6, rule 6);
//  - the polling cycle between runs (19.6);
//  - the over-current latch: read, hold the keep-alive low to clear it,
//    check it cleared (19.8);
//  - the fault state: keep-alive low, every expander pin back to input
//    while the expander still answers, nothing runs until acknowledged.
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
};

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
    // Step 2 before every run (19.7 says "better"); otherwise at start-up
    // and at least once every 24 h.
    bool pinHighTestEveryRun = true;
    // Control period: the start limit of step 3 is checked on a tick, so it
    // allows one period of slack for the acknowledge poll.
    uint32_t tickMs = 50;
};

class OutputStage final : public IOutputHal {
public:
    // Hardware design 19.4 and 19.7.
    static constexpr uint32_t kKeepAliveLowBeforeStartMs = 2000; // step 0
    static constexpr uint32_t kPinHighHoldMs = 1000;             // step 2
    static constexpr uint32_t kPinHighReleaseMs = 100;           // step 2
    static constexpr uint32_t kStartLimitMs = 150;               // step 3
    static constexpr uint32_t kRelaySettleMs = 20;               // step 6, rule 4
    static constexpr uint32_t kRelayOpenDelayMs = 40;            // step 8, rule 4
    static constexpr uint32_t kStopTestMaxMs = 3000;             // step 9
    static constexpr uint32_t kDriveReleaseMinMs = 300;          // step 9, tA
    static constexpr uint32_t kDriveReleaseMaxMs = 1000;
    static constexpr uint32_t kRelayReleaseMinMs = 80; // step 9, tB
    static constexpr uint32_t kRelayReleaseMaxMs = 1000;
    static constexpr uint32_t kTripHoldMs = 1500; // 19.8: keep-alive low this long
    static constexpr uint32_t kTripClearMaxMs = 3000;
    static constexpr uint32_t kWindowWaitMs = 500;    // a current window must arrive within this
    static constexpr uint32_t kAfterOffSettleMs = 20; // valve current gone one half-cycle after off
    static constexpr uint32_t kPinHighMaxAgeMs = 24u * 3600u * 1000u;
    // Firmware choice, not from section 19: the keep-alive toggles at least
    // this long before a stop test, so that the detectors' reservoirs are
    // charged as in the simulations behind the step 9 limits. The start-up
    // and polling cycles would otherwise stop after about 0.1 s.
    static constexpr uint32_t kMinToggleBeforeStopMs = 1000;

    OutputStage(IExpanderBus &bus, OutputEnableGuard &guard, const IClock &clock, const ExpanderLayout &layout,
                const OutputStageConfig &cfg = {});

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

private:
    enum class Step : uint8_t {
        NoAc,
        Idle,
        Precheck,       // 0
        IdleTest,       // 1
        PinHigh,        // 2, pin held high
        PinHighRelease, // 2, pin low again
        Start,          // 3
        Setup,          // 4a to 4c
        PollRead,       // 19.6: input port 1 between 4a and 9
        Weld,           // 5
        Relay,          // 6
        Ready,          // 7
        EndValves,      // 8: valves off, waiting 40 ms
        StopTest,       // 9
        NackPending,    // a write was not acknowledged: trip or fault, decided next tick
        TripClear,      // 19.8
        Fault,
        Halted,
    };
    enum class Proc : uint8_t { None, Startup, Run, Poll };

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
    void doStart(const KeepAliveReadbacks &rb);
    void doSetup();
    void doPollRead(const KeepAliveReadbacks &rb);
    void doWeld();
    void doRelay();
    void doReady(const KeepAliveReadbacks &rb);
    void doEnd(const KeepAliveReadbacks &rb);
    bool toggledLongEnough(const KeepAliveReadbacks &rb) const
    {
        return rb.applied == KeepAliveRequest::Run && rb.nowMs - rb.appliedSinceMs >= kMinToggleBeforeStopMs;
    }
    void doStopTest(const KeepAliveReadbacks &rb);
    void doFault();

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
    bool startupDone_ = false;
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

    std::atomic<bool> emergency_{false};
};

} // namespace zf::hw
