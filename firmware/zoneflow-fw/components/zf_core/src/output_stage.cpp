// SPDX-License-Identifier: Apache-2.0
#include "zf/hw/output_stage.hpp"

#include <algorithm>

namespace zf {

const char *toString(StagePhase p)
{
    switch (p) {
    case StagePhase::None:
        return "none";
    case StagePhase::NoAc:
        return "no_ac";
    case StagePhase::StartupTest:
        return "startup_test";
    case StagePhase::Idle:
        return "idle";
    case StagePhase::Starting:
        return "starting";
    case StagePhase::Ready:
        return "ready";
    case StagePhase::Stopping:
        return "stopping";
    case StagePhase::Polling:
        return "polling";
    case StagePhase::TripClear:
        return "trip_clear";
    case StagePhase::Fault:
        return "fault";
    case StagePhase::Halted:
        return "halted";
    }
    return "?";
}

const char *toString(HwCheck c)
{
    switch (c) {
    case HwCheck::None:
        return "none";
    case HwCheck::IdleDriveEnabled:
        return "idle_drive_enabled";
    case HwCheck::IdleRelayOn:
        return "idle_relay_on";
    case HwCheck::IdleExpanderAnswers:
        return "idle_expander_answers";
    case HwCheck::PinHighDriveEnabled:
        return "pin_high_drive_enabled";
    case HwCheck::PinHighRelayOn:
        return "pin_high_relay_on";
    case HwCheck::StartDriveTimeout:
        return "start_drive_timeout";
    case HwCheck::StartRelayTimeout:
        return "start_relay_timeout";
    case HwCheck::StartNoAcknowledge:
        return "start_no_acknowledge";
    case HwCheck::ExpanderInputs:
        return "expander_inputs";
    case HwCheck::ExpanderOutputs:
        return "expander_outputs";
    case HwCheck::ExpanderConfig:
        return "expander_config";
    case HwCheck::RelayWelded:
        return "relay_welded";
    case HwCheck::RelayNoReadback:
        return "relay_no_readback";
    case HwCheck::OutputShorted:
        return "output_shorted";
    case HwCheck::RunReadback:
        return "run_readback";
    case HwCheck::RunDriveLost:
        return "run_drive_lost";
    case HwCheck::RunRelayLost:
        return "run_relay_lost";
    case HwCheck::EndNoAcknowledge:
        return "end_no_acknowledge";
    case HwCheck::StopDriveRelease:
        return "stop_drive_release";
    case HwCheck::StopRelayRelease:
        return "stop_relay_release";
    case HwCheck::StopExpanderAnswers:
        return "stop_expander_answers";
    case HwCheck::BusError:
        return "bus_error";
    case HwCheck::LatchStuck:
        return "latch_stuck";
    case HwCheck::TooManyOutputs:
        return "too_many_outputs";
    case HwCheck::Count:
        break;
    }
    return "?";
}

uint8_t hwCheckStep(HwCheck c)
{
    switch (c) {
    case HwCheck::IdleDriveEnabled:
    case HwCheck::IdleRelayOn:
    case HwCheck::IdleExpanderAnswers:
        return 1;
    case HwCheck::PinHighDriveEnabled:
    case HwCheck::PinHighRelayOn:
        return 2;
    case HwCheck::StartDriveTimeout:
    case HwCheck::StartRelayTimeout:
    case HwCheck::StartNoAcknowledge:
        return 3;
    case HwCheck::ExpanderInputs:
    case HwCheck::ExpanderOutputs:
    case HwCheck::ExpanderConfig:
        return 4;
    case HwCheck::RelayWelded:
        return 5;
    case HwCheck::RelayNoReadback:
    case HwCheck::OutputShorted:
        return 6;
    case HwCheck::RunReadback:
    case HwCheck::RunDriveLost:
    case HwCheck::RunRelayLost:
        return 7;
    case HwCheck::EndNoAcknowledge:
        return 8;
    case HwCheck::StopDriveRelease:
    case HwCheck::StopRelayRelease:
    case HwCheck::StopExpanderAnswers:
        return 9;
    case HwCheck::LatchStuck:
        return 10;
    case HwCheck::None:
    case HwCheck::BusError:
    case HwCheck::TooManyOutputs:
    case HwCheck::Count:
        break;
    }
    return 0;
}

bool hwCheckNeedsRepair(HwCheck c)
{
    const uint8_t step = hwCheckStep(c);
    return step == 1 || step == 2 || step == 9;
}

} // namespace zf

namespace zf::hw {

const char *toString(BusStatus s)
{
    switch (s) {
    case BusStatus::Ok:
        return "ok";
    case BusStatus::Nack:
        return "nack";
    case BusStatus::Timeout:
        return "timeout";
    case BusStatus::BusError:
        return "bus_error";
    }
    return "?";
}

namespace {

unsigned popcount8(uint8_t v)
{
    unsigned n = 0;
    for (; v != 0; v = static_cast<uint8_t>(v & (v - 1))) {
        ++n;
    }
    return n;
}

constexpr uint8_t outReg(uint8_t port) { return port == 0 ? reg::kOutput0 : reg::kOutput1; }
constexpr uint8_t inReg(uint8_t port) { return port == 0 ? reg::kInput0 : reg::kInput1; }
constexpr uint8_t cfgReg(uint8_t port) { return port == 0 ? reg::kConfig0 : reg::kConfig1; }

// Milliseconds between two interrupt times, never negative.
uint32_t since(uint32_t later, uint32_t earlier) { return later >= earlier ? later - earlier : 0; }

} // namespace

OutputStage::OutputStage(IExpanderBus &bus, OutputEnableGuard &guard, const IClock &clock, const ExpanderLayout &layout,
                         const OutputStageConfig &cfg)
    : bus_(bus), guard_(guard), clock_(clock), layout_(layout), cfg_(cfg)
{
    guard_.setRequest(KeepAliveRequest::Off);
    stepSinceMs_ = clock_.monoMs();
}

void OutputStage::restoreFault(HwCheck check)
{
    if (check == HwCheck::None || check >= HwCheck::Count) {
        return;
    }
    fault_ = check;
    ++faultSeq_;
    guard_.setRequest(KeepAliveRequest::Off);
    enter(Step::Fault);
}

// ---- state handling ------------------------------------------------------------------

void OutputStage::enter(Step s)
{
    step_ = s;
    endWritesDone_ = false;
    stepSinceMs_ = clock_.monoMs();
    stepFirstTick_ = true;
}

void OutputStage::begin(Proc p)
{
    proc_ = p;
    pinHighThisProc_ = false;
    enter(Step::Precheck);
}

bool OutputStage::keepAliveActive() const
{
    switch (step_) {
    case Step::PinHigh:
    case Step::Start:
    case Step::Setup:
    case Step::PollRead:
    case Step::Weld:
    case Step::Relay:
    case Step::Ready:
    case Step::EndValves:
        return true;
    default:
        return false;
    }
}

void OutputStage::finishProcedure()
{
    if (proc_ == Proc::Startup) {
        startupDone_ = true;
    }
    ++passed_;
    proc_ = Proc::None;
    lastActiveEndMs_ = clock_.monoMs();
    enter(Step::Idle);
}

void OutputStage::fail(HwCheck check)
{
    // Keep-alive low first: whatever the bus does next, the expander resets
    // and the relay opens in hardware.
    guard_.setRequest(KeepAliveRequest::Off);
    if (fault_ == HwCheck::None) {
        fault_ = check;
        ++faultSeq_;
    }
    relayClosed_ = false;
    weldBitOn_ = false;
    outShadow_ = {0xFF, 0xFF};
    cfgShadow_ = {0xFF, 0xFF};
    proc_ = Proc::None;
    lastActiveEndMs_ = clock_.monoMs();
    enter(Step::Fault);
}

void OutputStage::onTrip()
{
    guard_.setRequest(KeepAliveRequest::Off);
    ++tripSeq_;
    relayClosed_ = false;
    weldBitOn_ = false;
    outShadow_ = {0xFF, 0xFF};
    cfgShadow_ = {0xFF, 0xFF};
    proc_ = Proc::None;
    enter(Step::TripClear);
}

void OutputStage::abortNoAc()
{
    // No 24 VAC: B_RB reads low without it, so nothing can be tested
    // (step 0). The expander loses its drive enable with the keep-alive.
    guard_.setRequest(KeepAliveRequest::Off);
    relayClosed_ = false;
    weldBitOn_ = false;
    outShadow_ = {0xFF, 0xFF};
    cfgShadow_ = {0xFF, 0xFF};
    proc_ = Proc::None;
    lastActiveEndMs_ = clock_.monoMs();
    enter(Step::NoAc);
}

void OutputStage::requestPower(bool wanted, uint8_t firstBit)
{
    if (wanted) {
        firstBit_ = firstBit;
    } else if (wanted_) {
        unwantedSinceMs_ = clock_.monoMs();
    }
    wanted_ = wanted;
}

bool OutputStage::acknowledgeFault(bool repairConfirmed)
{
    if (fault_ == HwCheck::None) {
        return true;
    }
    if (hwCheckNeedsRepair(fault_) && !repairConfirmed) {
        return false;
    }
    fault_ = HwCheck::None;
    pendingNack_ = HwCheck::None;
    if (step_ == Step::Fault) {
        lastActiveEndMs_ = clock_.monoMs();
        enter(Step::Idle);
    }
    return true;
}

OutputStageStatus OutputStage::status() const
{
    OutputStageStatus s;
    switch (step_) {
    case Step::NoAc:
        s.phase = StagePhase::NoAc;
        break;
    case Step::Idle:
        s.phase = StagePhase::Idle;
        break;
    case Step::Ready:
        s.phase = StagePhase::Ready;
        break;
    case Step::EndValves:
        s.phase = StagePhase::Stopping;
        break;
    case Step::StopTest:
        s.phase = proc_ == Proc::Startup ? StagePhase::StartupTest
                  : proc_ == Proc::Poll  ? StagePhase::Polling
                                         : StagePhase::Stopping;
        break;
    case Step::TripClear:
    case Step::NackPending:
        s.phase = StagePhase::TripClear;
        break;
    case Step::Fault:
        s.phase = StagePhase::Fault;
        break;
    case Step::Halted:
        s.phase = StagePhase::Halted;
        break;
    default:
        s.phase = proc_ == Proc::Startup ? StagePhase::StartupTest
                  : proc_ == Proc::Poll  ? StagePhase::Polling
                                         : StagePhase::Starting;
        break;
    }
    s.zonesFitted = zonesFitted_;
    s.relayClosed = relayClosed_;
    s.sensorSupplyFault = sensorFault_;
    s.sensorSupplyKnown = sensorKnown_;
    s.fault = fault_;
    s.faultSeq = faultSeq_;
    s.tripSeq = tripSeq_;
    s.tripClearing = step_ == Step::TripClear;
    s.releaseDriveMs = releaseDriveMs_;
    s.releaseRelayMs = releaseRelayMs_;
    s.enableDriveMs = enableDriveMs_;
    s.enableRelayMs = enableRelayMs_;
    s.selfTestsPassed = passed_;
    return s;
}

uint8_t OutputStage::valveBitsLow() const
{
    const uint8_t zones = zonesFitted_ == 0 ? kMaxZones : zonesFitted_;
    unsigned n = 0;
    for (uint8_t p = 0; p < 2; ++p) {
        n += popcount8(static_cast<uint8_t>(~outShadow_[p] & layout_.valveMask(p, zones)));
    }
    return static_cast<uint8_t>(n);
}

// ---- bus helpers -----------------------------------------------------------------------

bool OutputStage::busFailed(BusStatus st, HwCheck onNack)
{
    if (st == BusStatus::Ok) {
        return false;
    }
    if (st == BusStatus::Nack) {
        // While the keep-alive runs, a missing acknowledge usually means the
        // expander went into reset: the over-current latch removed DRV_EN.
        // Stop now and decide next tick, when the interrupt has sampled OC.
        guard_.setRequest(KeepAliveRequest::Off);
        pendingNack_ = onNack;
        relayClosed_ = false;
        weldBitOn_ = false;
        outShadow_ = {0xFF, 0xFF};
        cfgShadow_ = {0xFF, 0xFF};
        enter(Step::NackPending);
        return true;
    }
    bus_.recover();
    fail(HwCheck::BusError);
    return true;
}

bool OutputStage::write(uint8_t r, uint8_t v, HwCheck onError) { return !busFailed(bus_.write(r, v), onError); }

bool OutputStage::read(uint8_t r, uint8_t &v, HwCheck onError) { return !busFailed(bus_.read(r, v), onError); }

bool OutputStage::writeChecked(uint8_t r, uint8_t v, HwCheck onMismatch)
{
    uint8_t back = 0;
    if (!write(r, v, onMismatch) || !read(r, back, onMismatch)) {
        return false;
    }
    if (back != v) {
        fail(onMismatch);
        return false;
    }
    return true;
}

bool OutputStage::windowSince(MonoMs fromMs, CurrentReading &out) const
{
    if (!in_.current.valid || in_.current.startMs < fromMs) {
        return false;
    }
    out = in_.current;
    return true;
}

bool OutputStage::setValve(BitRef b, bool on)
{
    const uint8_t port = b.port & 1u;
    const auto m = static_cast<uint8_t>(1u << b.bit);
    const auto next = static_cast<uint8_t>(on ? (outShadow_[port] & ~m) : (outShadow_[port] | m));
    if (next == outShadow_[port]) {
        return true;
    }
    const uint8_t zones = zonesFitted_ == 0 ? kMaxZones : zonesFitted_;
    if (on) {
        // Never more than the allowed valve bits low after any write.
        const uint8_t other = outShadow_[port ^ 1u];
        const unsigned low =
            popcount8(static_cast<uint8_t>(~next & layout_.valveMask(port, zones))) +
            popcount8(static_cast<uint8_t>(~other & layout_.valveMask(static_cast<uint8_t>(port ^ 1u), zones)));
        if (low > cfg_.maxValveBitsLow) {
            fail(HwCheck::TooManyOutputs);
            return false;
        }
    }
    uint8_t backOut = 0;
    uint8_t backIn = 0;
    if (!write(outReg(port), next, HwCheck::RunReadback) || !read(outReg(port), backOut, HwCheck::RunReadback) ||
        !read(inReg(port), backIn, HwCheck::RunReadback)) {
        return false;
    }
    const uint8_t valves = layout_.valveMask(port, zones);
    if (backOut != next || (backIn & valves) != (next & valves)) {
        fail(HwCheck::RunReadback);
        return false;
    }
    outShadow_[port] = next;
    if (!on) {
        lastValveOffMs_ = clock_.monoMs();
    }
    return true;
}

// ---- IOutputHal --------------------------------------------------------------------------

void OutputStage::setZone(ZoneIndex zone, bool on)
{
    if (zone >= kMaxZones || (zonesFitted_ != 0 && zone >= zonesFitted_)) {
        return;
    }
    if (step_ != Step::Ready) {
        return; // nothing is driven outside a run: the expander is in reset or set to off
    }
    setValve(layout_.zone[zone], on);
}

void OutputStage::setMaster(bool on)
{
    if (step_ != Step::Ready) {
        return;
    }
    setValve(layout_.master, on);
}

void OutputStage::allOff()
{
    if (step_ != Step::Ready && step_ != Step::Weld && step_ != Step::Relay) {
        return;
    }
    const uint8_t zones = zonesFitted_ == 0 ? kMaxZones : zonesFitted_;
    for (uint8_t p = 0; p < 2; ++p) {
        const uint8_t valves = layout_.valveMask(p, zones);
        if ((outShadow_[p] & valves) == valves) {
            continue;
        }
        const auto next = static_cast<uint8_t>(outShadow_[p] | valves);
        if (!writeChecked(outReg(p), next, HwCheck::RunReadback)) {
            return;
        }
        outShadow_[p] = next;
        lastValveOffMs_ = clock_.monoMs();
    }
    weldBitOn_ = false;
}

// ---- the tick ------------------------------------------------------------------------------

void OutputStage::tick(const StageTickInput &in)
{
    in_ = in;
    const MonoMs now = clock_.monoMs();
    if (step_ == Step::Halted) {
        return;
    }
    if (guard_.tripped() || emergency_.load(std::memory_order_acquire)) {
        guard_.setRequest(KeepAliveRequest::Off);
        relayClosed_ = false;
        enter(Step::Halted);
        return;
    }
    KeepAliveReadbacks rb;
    const bool haveRb = guard_.readbacks(rb) && rb.samples > 0;

    // Over-current latch (19.8): it has already removed gate drive and
    // released the relay. Stop the keep-alive and clear it.
    if (haveRb && rb.overCurrentLatched &&
        (keepAliveActive() || step_ == Step::StopTest || step_ == Step::NackPending)) {
        onTrip();
        return;
    }
    if (step_ == Step::NackPending) {
        const HwCheck c = pendingNack_;
        pendingNack_ = HwCheck::None;
        fail(c);
        return;
    }
    // No 24 VAC: nothing runs and nothing is tested (step 0).
    if (!in.acPresent && step_ != Step::NoAc && step_ != Step::Fault && step_ != Step::TripClear) {
        abortNoAc();
        return;
    }
    if (!haveRb && step_ != Step::NoAc && step_ != Step::Idle && step_ != Step::Fault) {
        return; // the keep-alive interrupt has not run yet
    }

    switch (step_) {
    case Step::NoAc:
        if (in.acPresent) {
            if (fault_ != HwCheck::None) {
                enter(Step::Fault);
            } else if (!startupDone_) {
                begin(Proc::Startup);
            } else {
                enter(Step::Idle);
            }
        }
        break;
    case Step::Idle:
        if (!startupDone_) {
            begin(Proc::Startup);
        } else if (haveRb && rb.overCurrentLatched && rb.applied == KeepAliveRequest::Off &&
                   since(rb.nowMs, rb.appliedSinceMs) >= kTripClearMaxMs) {
            fail(HwCheck::LatchStuck); // 19.8: latch or detector A faulty
        } else if (wanted_) {
            begin(Proc::Run);
        } else if (in.sensorSupplyOn && (pollRequested_ || now - lastActiveEndMs_ >= cfg_.pollIntervalMs ||
                                         (pinHighDone_ && now - lastPinHighMs_ >= kPinHighMaxAgeMs))) {
            pollRequested_ = false;
            begin(Proc::Poll);
        }
        break;
    case Step::Precheck:
        doPrecheck(rb);
        break;
    case Step::IdleTest:
        doIdleTest(rb);
        break;
    case Step::PinHigh:
        doPinHigh(rb);
        break;
    case Step::PinHighRelease:
        doPinHighRelease(rb);
        break;
    case Step::Start:
        doStart(rb);
        break;
    case Step::Setup:
        doSetup();
        break;
    case Step::PollRead:
        doPollRead(rb);
        break;
    case Step::Weld:
        doWeld();
        break;
    case Step::Relay:
        doRelay();
        break;
    case Step::Ready:
        doReady(rb);
        break;
    case Step::EndValves:
        doEnd(rb);
        break;
    case Step::StopTest:
        doStopTest(rb);
        break;
    case Step::TripClear:
        if (rb.applied == KeepAliveRequest::Off) {
            const uint32_t low = since(rb.nowMs, rb.appliedSinceMs);
            if (low >= kTripHoldMs && !rb.overCurrentLatched) {
                lastActiveEndMs_ = now;
                enter(in.acPresent ? Step::Idle : Step::NoAc);
            } else if (low >= kTripClearMaxMs) {
                fail(HwCheck::LatchStuck);
            }
        }
        break;
    case Step::Fault:
        doFault();
        break;
    case Step::NackPending:
    case Step::Halted:
        break;
    }
    stepFirstTick_ = false;
}

// ---- steps ------------------------------------------------------------------------------------

void OutputStage::doPrecheck(const KeepAliveReadbacks &rb)
{
    // Step 0: 24 VAC is present (checked in tick), the keep-alive has been
    // low for 2 s, the latch is clear.
    guard_.setRequest(KeepAliveRequest::Off);
    if (rb.overCurrentLatched) {
        onTrip();
        return;
    }
    if (rb.applied != KeepAliveRequest::Off || since(rb.nowMs, rb.appliedSinceMs) < kKeepAliveLowBeforeStartMs) {
        return; // wait
    }
    if (proc_ == Proc::Run && !wanted_) {
        proc_ = Proc::None;
        enter(Step::Idle);
        return;
    }
    enter(Step::IdleTest);
    doIdleTest(rb);
}

void OutputStage::doIdleTest(const KeepAliveReadbacks &rb)
{
    // Step 1: both detectors off, expander in reset.
    if (rb.driveEnabled) {
        fail(HwCheck::IdleDriveEnabled);
        return;
    }
    if (!rb.relayOff) {
        fail(HwCheck::IdleRelayOn);
        return;
    }
    const BusStatus st = bus_.probe();
    if (st == BusStatus::Ok) {
        fail(HwCheck::IdleExpanderAnswers);
        return;
    }
    if (st != BusStatus::Nack) {
        bus_.recover();
        fail(HwCheck::BusError);
        return;
    }
    const MonoMs now = clock_.monoMs();
    const bool pinHighDue = !pinHighDone_ || now - lastPinHighMs_ >= kPinHighMaxAgeMs;
    if (proc_ == Proc::Startup || (proc_ == Proc::Run && cfg_.pinHighTestEveryRun) || pinHighDue) {
        pinHighThisProc_ = true;
        guard_.setRequest(KeepAliveRequest::HoldHigh);
        enter(Step::PinHigh);
    } else {
        guard_.setRequest(KeepAliveRequest::Run);
        enter(Step::Start);
    }
}

void OutputStage::doPinHigh(const KeepAliveReadbacks &rb)
{
    // Step 2: a steady high level must enable neither detector.
    if (rb.applied != KeepAliveRequest::HoldHigh || since(rb.nowMs, rb.appliedSinceMs) < kPinHighHoldMs) {
        return;
    }
    if (rb.driveEnabled) {
        fail(HwCheck::PinHighDriveEnabled); // C22 shorted
        return;
    }
    if (!rb.relayOff) {
        fail(HwCheck::PinHighRelayOn); // C51 shorted or a bleed resistor open
        return;
    }
    pinHighDone_ = true;
    lastPinHighMs_ = clock_.monoMs();
    guard_.setRequest(KeepAliveRequest::Off);
    enter(Step::PinHighRelease);
}

void OutputStage::doPinHighRelease(const KeepAliveReadbacks &rb)
{
    if (rb.applied != KeepAliveRequest::Off || since(rb.nowMs, rb.appliedSinceMs) < kPinHighReleaseMs) {
        return;
    }
    guard_.setRequest(KeepAliveRequest::Run);
    enter(Step::Start);
}

void OutputStage::doStart(const KeepAliveReadbacks &rb)
{
    // Step 3: within 150 ms of the first edge A_RB high, B_RB low, and the
    // expander answers. The interrupt timed the detectors to one period; the
    // acknowledge is polled every control tick.
    guard_.setRequest(KeepAliveRequest::Run);
    if (rb.applied != KeepAliveRequest::Run) {
        return;
    }
    const uint32_t elapsed = since(rb.nowMs, rb.appliedSinceMs);
    const bool aOn = rb.driveEnabled;
    const bool bOn = !rb.relayOff;
    if (aOn && bOn) {
        const uint32_t tA = since(rb.driveSinceMs, rb.appliedSinceMs);
        const uint32_t tB = since(rb.relaySinceMs, rb.appliedSinceMs);
        if (tA > kStartLimitMs) {
            fail(HwCheck::StartDriveTimeout);
            return;
        }
        if (tB > kStartLimitMs) {
            fail(HwCheck::StartRelayTimeout);
            return;
        }
        const BusStatus st = bus_.probe();
        if (st == BusStatus::Ok) {
            enableDriveMs_ = static_cast<uint16_t>(tA);
            enableRelayMs_ = static_cast<uint16_t>(tB);
            enter(Step::Setup);
            doSetup();
            return;
        }
        if (st != BusStatus::Nack) {
            bus_.recover();
            fail(HwCheck::BusError);
            return;
        }
    }
    if (elapsed > kStartLimitMs + cfg_.tickMs) {
        fail(!aOn ? HwCheck::StartDriveTimeout : !bOn ? HwCheck::StartRelayTimeout : HwCheck::StartNoAcknowledge);
    }
}

void OutputStage::doSetup()
{
    // 4a: every pin is still an input; the pull resistors show through.
    uint8_t in0 = 0;
    uint8_t in1 = 0;
    if (!read(reg::kInput0, in0, HwCheck::ExpanderInputs) || !read(reg::kInput1, in1, HwCheck::ExpanderInputs)) {
        return;
    }
    const uint8_t variantMask = ExpanderLayout::mask(layout_.variant8ZoneN, layout_.variant8ZoneN.port);
    const uint8_t variantIn = layout_.variant8ZoneN.port == 0 ? in0 : in1;
    const uint8_t fitted = (variantIn & variantMask) == 0 ? 8 : 4;
    const uint8_t sensMask = ExpanderLayout::mask(layout_.sensorFaultN, layout_.sensorFaultN.port);
    const uint8_t sensIn = layout_.sensorFaultN.port == 0 ? in0 : in1;
    sensorFault_ = (sensIn & sensMask) == 0;
    sensorKnown_ = true;
    for (uint8_t p = 0; p < 2; ++p) {
        const uint8_t v = p == 0 ? in0 : in1;
        const uint8_t checked = layout_.idleInputChecked(p);
        if ((v & checked) != (layout_.idleInputExpected(p) & checked)) {
            fail(HwCheck::ExpanderInputs);
            return;
        }
    }
    if (zonesFitted_ == 0) {
        zonesFitted_ = fitted; // the strap is read once, in the first self-test
    } else if (zonesFitted_ != fitted) {
        fail(HwCheck::ExpanderInputs); // a strap does not change
        return;
    }
    if (proc_ == Proc::Poll) {
        enter(Step::PollRead);
        return;
    }
    // 4b: outputs first, so that no pin drives a level before it is set.
    for (uint8_t p = 0; p < 2; ++p) {
        if (!writeChecked(outReg(p), layout_.outputsOff(p), HwCheck::ExpanderOutputs)) {
            return;
        }
        outShadow_[p] = layout_.outputsOff(p);
    }
    // 4c: then the direction.
    for (uint8_t p = 0; p < 2; ++p) {
        const uint8_t c = layout_.configRun(p, zonesFitted_);
        if (!writeChecked(cfgReg(p), c, HwCheck::ExpanderConfig)) {
            return;
        }
        cfgShadow_[p] = c;
    }
    uint8_t pol0 = 0xFF;
    uint8_t pol1 = 0xFF;
    if (!read(reg::kPolarity0, pol0, HwCheck::ExpanderConfig) ||
        !read(reg::kPolarity1, pol1, HwCheck::ExpanderConfig)) {
        return;
    }
    if (pol0 != 0 || pol1 != 0) {
        fail(HwCheck::ExpanderConfig);
        return;
    }
    if (proc_ == Proc::Startup) {
        // Start-up: no valve and no relay. Steps 8 and 9.
        endValvesMs_ = clock_.monoMs();
        enter(Step::EndValves);
        return;
    }
    if (proc_ == Proc::Run && !wanted_) {
        endValvesMs_ = clock_.monoMs();
        enter(Step::EndValves);
        return;
    }
    enter(Step::Weld);
    doWeld();
}

void OutputStage::doPollRead(const KeepAliveReadbacks &rb)
{
    // 19.6: input port 1 for SENS_FLT_N, then the stop test. No valve and no
    // relay bit is touched; everything is still an input.
    uint8_t v = 0;
    const BitRef s = layout_.sensorFaultN;
    if (!read(inReg(s.port), v, HwCheck::ExpanderInputs)) {
        return;
    }
    sensorFault_ = (v & (1u << s.bit)) == 0;
    sensorKnown_ = true;
    if (!toggledLongEnough(rb)) {
        return; // read again next tick
    }
    guard_.setRequest(KeepAliveRequest::Off);
    enter(Step::StopTest);
}

void OutputStage::doWeld()
{
    // Step 5: the run's first output on with the relay open must draw
    // nothing. A welded K1 contact, a bridged JP1 or a shorted Q1 shows here.
    const MonoMs now = clock_.monoMs();
    BitRef b{};
    bool have = false;
    if (firstBit_ < kMaxZones && (zonesFitted_ == 0 || firstBit_ < zonesFitted_)) {
        b = layout_.zone[firstBit_];
        have = true;
    } else if (firstBit_ == kMaxZones) {
        b = layout_.master;
        have = true;
    }
    if (stepFirstTick_ && !weldBitOn_) {
        if (!have || !in_.currentSenseAvailable) {
            // Nothing to test with: no known first output, or no current
            // readings (zones still run without them; firmware architecture
            // section 3, current sense unavailable).
            enter(Step::Relay);
            doRelay();
            return;
        }
        const auto m = static_cast<uint8_t>(1u << b.bit);
        const auto next = static_cast<uint8_t>(outShadow_[b.port & 1u] & ~m);
        if (!writeChecked(outReg(b.port & 1u), next, HwCheck::ExpanderOutputs)) {
            return;
        }
        outShadow_[b.port & 1u] = next;
        weldBitOn_ = true;
        weldStartMs_ = now;
        return;
    }
    CurrentReading r;
    const bool got = windowSince(weldStartMs_, r);
    const bool late = now - weldStartMs_ > kWindowWaitMs;
    if (!got && !late) {
        return;
    }
    // Bit off again before anything else.
    const auto m = static_cast<uint8_t>(1u << b.bit);
    const auto next = static_cast<uint8_t>(outShadow_[b.port & 1u] | m);
    if (!writeChecked(outReg(b.port & 1u), next, HwCheck::ExpanderOutputs)) {
        return;
    }
    outShadow_[b.port & 1u] = next;
    weldBitOn_ = false;
    lastValveOffMs_ = now;
    if (got && r.ma > in_.idleThresholdMa) {
        fail(HwCheck::RelayWelded);
        return;
    }
    enter(Step::Relay);
    doRelay();
}

void OutputStage::doRelay()
{
    // Step 6: close the relay, read it back, wait 20 ms, then one window with
    // every valve off must draw nothing (shorted triac, TVS, opto-triac or
    // snubber capacitor otherwise).
    const MonoMs now = clock_.monoMs();
    const uint8_t rp = layout_.relay.port & 1u;
    if (stepFirstTick_ && !relayClosed_) {
        const auto next = static_cast<uint8_t>(outShadow_[rp] | layout_.relayMask(rp));
        if (!writeChecked(outReg(rp), next, HwCheck::RelayNoReadback)) {
            return;
        }
        outShadow_[rp] = next;
        relayClosed_ = true;
        relayClosedMs_ = now;
        uint8_t in = 0;
        if (!read(inReg(rp), in, HwCheck::RelayNoReadback)) {
            return;
        }
        if ((in & layout_.relayMask(rp)) == 0) {
            fail(HwCheck::RelayNoReadback);
        }
        return;
    }
    if (now - relayClosedMs_ < kRelaySettleMs) {
        return;
    }
    if (in_.currentSenseAvailable) {
        CurrentReading r;
        const MonoMs from = relayClosedMs_ + kRelaySettleMs;
        if (!windowSince(from, r)) {
            if (now - from <= kWindowWaitMs) {
                return;
            }
        } else if (r.ma > in_.idleThresholdMa) {
            fail(HwCheck::OutputShorted);
            return;
        }
    }
    enter(Step::Ready);
}

void OutputStage::doReady(const KeepAliveReadbacks &rb)
{
    // Step 7, every control tick: scrub every register against the shadow
    // copy, valve inputs against valve outputs, and both detectors.
    if (!rb.driveEnabled) {
        fail(HwCheck::RunDriveLost); // OC is high (checked first in tick)
        return;
    }
    if (rb.relayOff) {
        fail(HwCheck::RunRelayLost);
        return;
    }
    std::array<uint8_t, 2> in{};
    std::array<uint8_t, 2> out{};
    std::array<uint8_t, 2> cfg{};
    for (uint8_t p = 0; p < 2; ++p) {
        if (!read(inReg(p), in[p], HwCheck::RunReadback) || !read(outReg(p), out[p], HwCheck::RunReadback) ||
            !read(cfgReg(p), cfg[p], HwCheck::RunReadback)) {
            return;
        }
    }
    for (uint8_t p = 0; p < 2; ++p) {
        const uint8_t driven = static_cast<uint8_t>(layout_.valveMask(p, zonesFitted_) | layout_.relayMask(p));
        if (out[p] != outShadow_[p] || cfg[p] != cfgShadow_[p] || (in[p] & driven) != (out[p] & driven)) {
            fail(HwCheck::RunReadback);
            return;
        }
    }
    const BitRef s = layout_.sensorFaultN;
    sensorFault_ = (in[s.port & 1u] & (1u << s.bit)) == 0;
    sensorKnown_ = true;

    // Rule 6: current with the relay closed and no valve on is a shorted
    // output device. Only windows that started after the last valve went
    // off and the relay settled count.
    const MonoMs now = clock_.monoMs();
    CurrentReading r;
    if (valveBitsLow() == 0 && in_.currentSenseAvailable &&
        windowSince(std::max(lastValveOffMs_ + kAfterOffSettleMs, relayClosedMs_ + kRelaySettleMs), r) &&
        r.ma > in_.idleThresholdMa) {
        fail(HwCheck::OutputShorted);
        return;
    }

    if (!wanted_ && valveBitsLow() == 0 && now - unwantedSinceMs_ >= cfg_.lingerMs) {
        endValvesMs_ = now;
        enter(Step::EndValves);
    }
}

void OutputStage::doEnd(const KeepAliveReadbacks &rb)
{
    // Step 8: every valve bit set, 40 ms, relay off, every pin an input.
    const MonoMs now = clock_.monoMs();
    if (!endWritesDone_) {
        if (stepFirstTick_) {
            const uint8_t zones = zonesFitted_ == 0 ? kMaxZones : zonesFitted_;
            for (uint8_t p = 0; p < 2; ++p) {
                const auto next = static_cast<uint8_t>(outShadow_[p] | layout_.valveMask(p, zones));
                if (next != outShadow_[p]) {
                    if (!writeChecked(outReg(p), next, HwCheck::EndNoAcknowledge)) {
                        return;
                    }
                    outShadow_[p] = next;
                    lastValveOffMs_ = now;
                }
            }
            endValvesMs_ = now;
            if (relayClosed_) {
                return; // wait 40 ms with the relay still closed
            }
        }
        if (relayClosed_ && now - endValvesMs_ < kRelayOpenDelayMs) {
            return;
        }
        const uint8_t rp = layout_.relay.port & 1u;
        const auto off = static_cast<uint8_t>(outShadow_[rp] & ~layout_.relayMask(rp));
        if (!write(outReg(rp), off, HwCheck::EndNoAcknowledge)) {
            return;
        }
        outShadow_[rp] = off;
        relayClosed_ = false;
        if (!write(reg::kConfig0, 0xFF, HwCheck::EndNoAcknowledge) ||
            !write(reg::kConfig1, 0xFF, HwCheck::EndNoAcknowledge)) {
            return;
        }
        cfgShadow_ = {0xFF, 0xFF};
        endWritesDone_ = true;
    }
    if (!toggledLongEnough(rb)) {
        return; // every pin is an input; keep toggling until the reservoirs are charged
    }
    guard_.setRequest(KeepAliveRequest::Off);
    enter(Step::StopTest);
}

void OutputStage::doStopTest(const KeepAliveReadbacks &rb)
{
    // Step 9: time both detectors from the stop, then the expander must be
    // in reset again.
    guard_.setRequest(KeepAliveRequest::Off);
    if (rb.applied != KeepAliveRequest::Off) {
        return;
    }
    const uint32_t elapsed = since(rb.nowMs, rb.appliedSinceMs);
    const bool aReleased = !rb.driveEnabled;
    const bool bReleased = rb.relayOff;
    if (!(aReleased && bReleased)) {
        if (elapsed >= kStopTestMaxMs) {
            fail(!aReleased ? HwCheck::StopDriveRelease : HwCheck::StopRelayRelease);
        }
        return;
    }
    const uint32_t tA = since(rb.driveSinceMs, rb.appliedSinceMs);
    const uint32_t tB = since(rb.relaySinceMs, rb.appliedSinceMs);
    releaseDriveMs_ = static_cast<uint16_t>(tA);
    releaseRelayMs_ = static_cast<uint16_t>(tB);
    if (tA < kDriveReleaseMinMs || tA > kDriveReleaseMaxMs) {
        fail(HwCheck::StopDriveRelease);
        return;
    }
    if (tB < kRelayReleaseMinMs || tB > kRelayReleaseMaxMs) {
        fail(HwCheck::StopRelayRelease);
        return;
    }
    const BusStatus st = bus_.probe();
    if (st == BusStatus::Ok) {
        fail(HwCheck::StopExpanderAnswers);
        return;
    }
    if (st != BusStatus::Nack) {
        bus_.recover();
        fail(HwCheck::BusError);
        return;
    }
    finishProcedure();
}

void OutputStage::doFault()
{
    // Keep-alive low. If the expander still answers (detector A stuck on),
    // every pin back to input, on every tick.
    guard_.setRequest(KeepAliveRequest::Off);
    const BusStatus st = bus_.probe();
    if (st == BusStatus::Ok) {
        (void)bus_.write(reg::kConfig0, 0xFF);
        (void)bus_.write(reg::kConfig1, 0xFF);
    } else if (st != BusStatus::Nack) {
        bus_.recover();
    }
}

} // namespace zf::hw
