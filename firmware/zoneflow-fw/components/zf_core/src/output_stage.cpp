// SPDX-License-Identifier: Apache-2.0
#include "zf/hw/output_stage.hpp"

#include <algorithm>
#include <string_view>

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
    case HwCheck::IdleDriveSupplyOn:
        return "idle_drive_supply_on";
    case HwCheck::IdleRelayGateDrain:
        return "idle_relay_gate_drain";
    case HwCheck::FloatPinHigh:
        return "float_pin_high";
    case HwCheck::FloatDriveEnabled:
        return "float_drive_enabled";
    case HwCheck::FloatRelayOn:
        return "float_relay_on";
    case HwCheck::WeldNoReadback:
        return "weld_no_readback";
    case HwCheck::GateBleed:
        return "gate_bleed";
    case HwCheck::StopDriveBand:
        return "stop_drive_band";
    case HwCheck::StopRelayBand:
        return "stop_relay_band";
    case HwCheck::KeepAlivePinRouted:
        return "keepalive_pin_routed";
    case HwCheck::RectifierFault:
        return "rectifier_fault";
    case HwCheck::SupplyOverVoltage:
        return "supply_over_voltage";
    case HwCheck::LatchInputHeld:
        return "latch_input_held";
    case HwCheck::Count:
        break;
    }
    return "?";
}

const char *toString(CurrentLoss l)
{
    switch (l) {
    case CurrentLoss::None:
        return "none";
    case CurrentLoss::Unavailable:
        return "unavailable";
    case CurrentLoss::NoWindow:
        return "no_window";
    case CurrentLoss::RestLevel:
        return "rest_level";
    }
    return "?";
}

const char *hwCheckStep(HwCheck c)
{
    switch (c) {
    case HwCheck::RectifierFault:
        return "0";
    case HwCheck::IdleDriveEnabled:
    case HwCheck::IdleRelayOn:
    case HwCheck::IdleExpanderAnswers:
    case HwCheck::IdleDriveSupplyOn:
    case HwCheck::IdleRelayGateDrain:
    case HwCheck::SupplyOverVoltage:
        return "1";
    case HwCheck::PinHighDriveEnabled:
    case HwCheck::PinHighRelayOn:
        return "2";
    case HwCheck::FloatPinHigh:
    case HwCheck::FloatDriveEnabled:
    case HwCheck::FloatRelayOn:
        return "2b";
    case HwCheck::StartDriveTimeout:
    case HwCheck::StartRelayTimeout:
    case HwCheck::StartNoAcknowledge:
    case HwCheck::LatchInputHeld:
        return "3";
    case HwCheck::ExpanderInputs:
        return "4a";
    case HwCheck::ExpanderOutputs:
        return "4b";
    case HwCheck::ExpanderConfig:
        return "4c";
    case HwCheck::RelayWelded:
    case HwCheck::WeldNoReadback:
        return "5";
    case HwCheck::GateBleed:
        return "5b";
    case HwCheck::RelayNoReadback:
    case HwCheck::OutputShorted:
        return "6";
    case HwCheck::RunReadback:
    case HwCheck::RunDriveLost:
    case HwCheck::RunRelayLost:
        return "7";
    case HwCheck::EndNoAcknowledge:
        return "8";
    case HwCheck::StopDriveRelease:
    case HwCheck::StopRelayRelease:
    case HwCheck::StopExpanderAnswers:
    case HwCheck::StopDriveBand:
    case HwCheck::StopRelayBand:
        return "9";
    case HwCheck::None:
    case HwCheck::BusError:
    case HwCheck::LatchStuck:
    case HwCheck::TooManyOutputs:
    case HwCheck::KeepAlivePinRouted:
    case HwCheck::Count:
        break;
    }
    return "";
}

bool hwCheckNeedsRepair(HwCheck c)
{
    // 19.7, the fault state: steps 1, 2, 2b, 5b and 9, the rectifier fault of
    // 19.10, the over-voltage of rule 18 (a step 1 check) and a held set
    // input of the latch (19.8).
    if (c == HwCheck::RectifierFault || c == HwCheck::LatchInputHeld) {
        return true;
    }
    const std::string_view step = hwCheckStep(c);
    return step == "1" || step == "2" || step == "2b" || step == "5b" || step == "9";
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

// Milliseconds from `earlier` to `later`, two interrupt times, never
// negative (an event before its reference gives 0). The interrupt's count is
// 32 bits and wraps after 49.7 days of uptime, so both are taken as ages at
// the interrupt's latest time `now`, modulo 2^32: exact for ages up to
// 49.7 days. A later wait, not a hang, when the reference is older still.
uint32_t since(uint32_t now, uint32_t later, uint32_t earlier)
{
    const uint32_t ageEarlier = now - earlier;
    const uint32_t ageLater = now - later;
    return ageEarlier > ageLater ? ageEarlier - ageLater : 0;
}

} // namespace

OutputStage::OutputStage(IExpanderBus &bus, OutputEnableGuard &guard, const IClock &clock, const ExpanderLayout &layout,
                         const OutputStageConfig &cfg, IDetectorReferenceStore *reference)
    : bus_(bus), guard_(guard), clock_(clock), layout_(layout), cfg_(cfg), refStore_(reference)
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
    skipNoted_ = false;
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
    case Step::GateBleed:
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
    bleedPhase_ = 0;
    outShadow_ = {0xFF, 0xFF};
    cfgShadow_ = {0xFF, 0xFF};
    proc_ = Proc::None;
    lastActiveEndMs_ = clock_.monoMs();
    enter(Step::Fault);
    // Every output off now, not a tick later (firmware architecture section
    // 22, gap 6). The keep-alive is already low, so the hardware follows
    // within the detectors' hold-up whatever this write does.
    releaseAll();
}

void OutputStage::releaseAll(bool recoverBus)
{
    // One pair write switches every valve and the relay off: port 1 (zones 5
    // to 8) follows port 0 (zones 1 to 4, the master, RLY_ON) by at most
    // kPairWriteSkewNs. Then every pin an input. Errors are ignored: the
    // keep-alive is stopped, so the expander goes into reset regardless.
    const BusStatus st = bus_.probe();
    if (st == BusStatus::Ok) {
        if (bus_.writePair(reg::kOutput0, layout_.outputsOff(0), layout_.outputsOff(1)) == BusStatus::Ok) {
            (void)bus_.writePair(reg::kConfig0, 0xFF, 0xFF);
        }
    } else if (st != BusStatus::Nack && recoverBus) {
        bus_.recover();
    }
}

void OutputStage::onTrip()
{
    // 19.8: a trip with no valve bit low was not caused by load current. It
    // is not counted against an output; the keep-alive is held low for 1.5 s
    // and the sequence tries once more. A second one in a row means the set
    // input of the latch is held low: 3V3 above 4 V (U13 acting), U13 or its
    // divider, U6B, U6C or the chain R48 to R51.
    const bool load = valveBitsLow() > 0 || weldBitOn_;
    guard_.setRequest(KeepAliveRequest::Off);
    if (!load) {
        ++idleTrips_;
        if (++idleTripsInRow_ >= 2) {
            idleTripsInRow_ = 0;
            fail(HwCheck::LatchInputHeld);
            return;
        }
    } else {
        ++tripSeq_;
        idleTripsInRow_ = 0;
    }
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
    if (repairConfirmed && hwCheckNeedsRepair(fault_)) {
        // A repair of a layer may have changed the detectors' parts: learn
        // this board's release times again (19.7, the limits of step 9).
        forgetReference();
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
        s.phase = proc_ == Proc::Startup || proc_ == Proc::Commission ? StagePhase::StartupTest
                  : proc_ == Proc::Poll                               ? StagePhase::Polling
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
        s.phase = proc_ == Proc::Startup || proc_ == Proc::Commission ? StagePhase::StartupTest
                  : proc_ == Proc::Poll                               ? StagePhase::Polling
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
    // From the 100 ms before the pin floats to 100 ms after it is driven
    // again, so that the load is on for the whole 0.5 s (rule 17).
    s.floatTest = step_ == Step::PinHighRelease || step_ == Step::FloatLead || step_ == Step::Float ||
                  step_ == Step::FloatRelease;
    s.commissioned = reference_.valid();
    s.reference = reference_;
    s.commissionSamples = samples_;
    s.commissionWaitingForTemperature = !reference_.valid() && !dieTempForCommissioning();
    s.vinMonMv = vinMv_;
    s.supplyLow = vinMv_ >= kVinAbsentMv && vinMv_ < kVinMinMv;
    s.supplyHigh = vinMv_ > kVinMaxMv;
    s.currentLost = !in_.currentSenseAvailable ? CurrentLoss::Unavailable
                    : windowLost_               ? CurrentLoss::NoWindow
                    : restLost_                 ? CurrentLoss::RestLevel
                                                : CurrentLoss::None;
    s.checksSkipped = checksSkipped_;
    s.restLoMv = restLoMv_;
    s.restHiMv = restHiMv_;
    s.idleTrips = idleTrips_;
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
    if (step_ != Step::Ready && step_ != Step::Weld && step_ != Step::GateBleed && step_ != Step::Relay) {
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
    if (!refLoaded_) {
        loadReference();
    }
    // 19.10, rules 18 and 19: the supply monitor and the rest levels.
    if (!watchSupply(now)) {
        return;
    }
    // Rule 16 and contract 19.4: no peripheral may drive the keep-alive pin.
    if (!in.keepAlivePinPlain && step_ != Step::Fault) {
        fail(HwCheck::KeepAlivePinRouted);
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
                   since(rb.nowMs, rb.nowMs, rb.appliedSinceMs) >= kTripClearMaxMs) {
            fail(HwCheck::LatchStuck); // 19.8: latch or detector A faulty
        } else if (wanted_) {
            begin(Proc::Run);
        } else if (!reference_.valid() && now >= nextCommissionMs_) {
            // 19.7 items 1 and 2: commission while nothing is stored, only
            // while the die reads 25 to 50 C; an attempt is ten cycles at
            // most, and the next one follows 10 minutes later.
            if (!dieTempForCommissioning() || commissionCycles_ >= cfg_.maxCommissionCycles) {
                commissionCycles_ = 0;
                samples_ = 0;
                nextCommissionMs_ = now + cfg_.commissionRetryMs;
            } else {
                ++commissionCycles_;
                begin(Proc::Commission); // steps 0, 1, 3, 4a and 9 until five stop tests agree
            }
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
    case Step::FloatLead:
        doFloatLead(rb);
        break;
    case Step::Float:
        doFloat(rb);
        break;
    case Step::FloatRelease:
        doFloatRelease(rb);
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
    case Step::GateBleed:
        doGateBleed();
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
            const uint32_t low = since(rb.nowMs, rb.nowMs, rb.appliedSinceMs);
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
    // low for 2.5 s, the latch is clear. 2.5 s covers the relay detector's
    // slowest release, 2.19 s with the pin high (review A2 m10).
    guard_.setRequest(KeepAliveRequest::Off);
    if (!bootScrubDone_) {
        bootScrubDone_ = true;
        // After a restart with valves on, the expander can still hold its
        // outputs: the detectors take up to 0.83 s to let go, and with the
        // keep-alive net bridged to a net that toggles by itself (review A2
        // appendix A) they never do. Every pin back to input now rather
        // than after the 2.5 s below; step 1 then judges the board.
        releaseAll();
    }
    if (rb.overCurrentLatched) {
        onTrip();
        return;
    }
    if (rb.applied != KeepAliveRequest::Off ||
        since(rb.nowMs, rb.nowMs, rb.appliedSinceMs) < kKeepAliveLowBeforeStartMs) {
        return; // wait
    }
    if (proc_ == Proc::Run && !wanted_) {
        proc_ = Proc::None;
        enter(Step::Idle);
        return;
    }
    // 19.10: VIN_MON 0.60 to 1.50 V. Below, a brown-out leaves U1 off and
    // B_RB low, which step 1 would take for a relay fault; above, the supply
    // is beyond what the board allows. Wait in both cases; status() reports
    // supply low or supply high, which clear themselves.
    if (!supplyInRange()) {
        if (proc_ == Proc::Commission || proc_ == Proc::Poll) {
            proc_ = Proc::None; // tried again later
            enter(Step::Idle);
        }
        return;
    }
    if (proc_ == Proc::Run && !wanted_) {
        proc_ = Proc::None;
        enter(Step::Idle);
        return;
    }
    if (proc_ == Proc::Commission && wanted_) {
        proc_ = Proc::Run; // a run goes first; commissioning carries on afterwards
    }
    enter(Step::IdleTest);
    doIdleTest(rb);
}

bool OutputStage::restReadbackGateDrain() const
{
    // Review A2 4.6 question 7: with Q2 shorted gate to drain, Q2 sits at its
    // own threshold and GPIO7 sees 0.6 times that, 0.4 to 0.95 V, which a
    // digital read may take for high. The window must lie inside the 2.5 s
    // the keep-alive has been low.
    const CurrentReading &w = in_.current;
    if (!in_.currentSenseAvailable || w.relayReadbackMv < 0 || clock_.monoMs() - w.endMs > kRestWindowMaxAgeMs) {
        return false; // no analogue reading: the digital read alone decides
    }
    return w.relayReadbackMv >= kRelayGateDrainMinMv && w.relayReadbackMv <= kRelayGateDrainMaxMv;
}

void OutputStage::doIdleTest(const KeepAliveReadbacks &rb)
{
    // Step 1: both detectors off, the LED supply off, the expander in reset.
    // A_RB reads V_DRV since review A2 (section 8.6): with no acknowledge, a
    // high A_RB is Q3 conducting; with an acknowledge, detector A or the
    // latch is stuck on. An acknowledge with A_RB low is DRV_EN stuck high.
    const BusStatus st = bus_.probe();
    if (st != BusStatus::Ok && st != BusStatus::Nack) {
        bus_.recover();
        fail(HwCheck::BusError);
        return;
    }
    const bool answers = st == BusStatus::Ok;
    if (rb.driveEnabled) {
        fail(answers ? HwCheck::IdleDriveEnabled : HwCheck::IdleDriveSupplyOn);
        return;
    }
    if (restReadbackGateDrain()) {
        fail(HwCheck::IdleRelayGateDrain);
        return;
    }
    if (!rb.relayOff) {
        // Q2 on, or (rule 15) V_BUCK missing on USB power: B_RB is pulled up
        // from V_BUCK. GPIO7 reads 0 V either way; section 19 gives no
        // reading that tells the two apart.
        fail(HwCheck::IdleRelayOn);
        return;
    }
    if (answers) {
        fail(HwCheck::IdleExpanderAnswers);
        return;
    }
    // Rule 18: both channels resting above 0.90 V means 3V3 is above about
    // 3.6 V. (watchSupply() acts on the window as it arrives; this is the
    // check as step 1 states it.)
    if (restLoMv_ > kRestOverVoltageMv && restHiMv_ > kRestOverVoltageMv) {
        fail(HwCheck::SupplyOverVoltage);
        return;
    }
    const MonoMs now = clock_.monoMs();
    const bool pinHighDue = !pinHighDone_ || now - lastPinHighMs_ >= kPinHighMaxAgeMs;
    if (proc_ == Proc::Startup || (proc_ == Proc::Run && cfg_.pinHighTestEveryRun) || pinHighDue) {
        pinHighThisProc_ = true;
        guard_.setRequest(KeepAliveRequest::HoldHigh);
        enter(Step::PinHigh);
    } else if (proc_ == Proc::Run) {
        // Step 2b at start-up and before every run, however often step 2
        // runs (19.7, note to step 2): the load first, then the float.
        guard_.setRequest(KeepAliveRequest::Off);
        enter(Step::FloatLead);
    } else {
        guard_.setRequest(KeepAliveRequest::Run);
        enter(Step::Start);
    }
}

void OutputStage::doPinHigh(const KeepAliveReadbacks &rb)
{
    // Step 2: a steady high level must enable neither detector.
    if (rb.applied != KeepAliveRequest::HoldHigh || since(rb.nowMs, rb.nowMs, rb.appliedSinceMs) < kPinHighHoldMs) {
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
    // Step 2 ends with 100 ms low. The float test follows: status().floatTest
    // is already set, so the display, the backlight PWM and the PSRAM are
    // busy before the pin floats (rule 17).
    if (rb.applied != KeepAliveRequest::Off || since(rb.nowMs, rb.nowMs, rb.appliedSinceMs) < kPinHighReleaseMs) {
        return;
    }
    guard_.setRequest(KeepAliveRequest::Float);
    enter(Step::Float);
}

void OutputStage::doFloatLead(const KeepAliveReadbacks &rb)
{
    // Step 2b without step 2 before it: the pin stays low while the load
    // (status().floatTest) runs for 100 ms, then it floats.
    (void)rb;
    if (clock_.monoMs() - stepSinceMs_ < kFloatLoadLeadMs) {
        return;
    }
    guard_.setRequest(KeepAliveRequest::Float);
    enter(Step::Float);
}

void OutputStage::doFloat(const KeepAliveReadbacks &rb)
{
    // Step 2b: the pin an input with no pull for 0.5 s, read every 1 ms by
    // the interrupt. Nothing may drive the keep-alive net: R33 holds it low.
    if (rb.applied != KeepAliveRequest::Float) {
        return;
    }
    if (rb.floatHighReads > 0) {
        fail(HwCheck::FloatPinHigh); // another net lifts KA: stop at once, pin driven low
        return;
    }
    if (since(rb.nowMs, rb.nowMs, rb.appliedSinceMs) < kFloatTestMs) {
        return;
    }
    if (rb.floatReads < kFloatMinReads) {
        fail(HwCheck::FloatPinHigh); // the pin was not read as asked: the test did not run
        return;
    }
    if (rb.driveEnabled) {
        fail(HwCheck::FloatDriveEnabled);
        return;
    }
    if (!rb.relayOff) {
        fail(HwCheck::FloatRelayOn);
        return;
    }
    guard_.setRequest(KeepAliveRequest::Off);
    enter(Step::FloatRelease);
}

void OutputStage::doFloatRelease(const KeepAliveReadbacks &rb)
{
    if (rb.applied != KeepAliveRequest::Off || since(rb.nowMs, rb.nowMs, rb.appliedSinceMs) < kFloatReleaseMs) {
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
    const uint32_t elapsed = since(rb.nowMs, rb.nowMs, rb.appliedSinceMs);
    const bool aOn = rb.driveEnabled;
    const bool bOn = !rb.relayOff;
    if (aOn && bOn) {
        const uint32_t tA = since(rb.nowMs, rb.driveSinceMs, rb.appliedSinceMs);
        const uint32_t tB = since(rb.nowMs, rb.relaySinceMs, rb.appliedSinceMs);
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
    idleTripsInRow_ = 0; // steps 3 and 4a passed: the latch's set input is free
    if (proc_ == Proc::Poll || proc_ == Proc::Commission) {
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
    beginStopTest(rb);
}

bool OutputStage::firstOutput(BitRef &b) const
{
    if (firstBit_ < kMaxZones && (zonesFitted_ == 0 || firstBit_ < zonesFitted_)) {
        b = layout_.zone[firstBit_];
        return true;
    }
    if (firstBit_ == kMaxZones) {
        b = layout_.master;
        return true;
    }
    return false;
}

void OutputStage::doWeld()
{
    // Step 5: the run's first output on with the relay open must draw
    // nothing. A welded K1 contact, a bridged JP1 or a shorted Q1 shows here.
    const MonoMs now = clock_.monoMs();
    BitRef b{};
    const bool have = firstOutput(b);
    const uint8_t port = b.port & 1u;
    const auto m = static_cast<uint8_t>(1u << b.bit);
    if (stepFirstTick_ && !weldBitOn_) {
        if (!have || !currentUsable()) {
            // Nothing to test with: no known first output, or no current
            // readings. Rule 19: zones still run without them, and the
            // condition is reported for as long as it lasts (status()
            // currentLost, counted in checksSkipped). Step 5b needs the same.
            if (!currentUsable()) {
                noteSkippedChecks();
            }
            enter(Step::Relay);
            doRelay();
            return;
        }
        const auto next = static_cast<uint8_t>(outShadow_[port] & ~m);
        if (!writeChecked(outReg(port), next, HwCheck::ExpanderOutputs)) {
            return;
        }
        outShadow_[port] = next;
        weldBitOn_ = true;
        weldStartMs_ = now;
        // The pin itself must follow: a valve pin that does not read low
        // while driven low is an open or shorted pin of the expander, its
        // pull-up array or the opto LED path (review A2 appendix A).
        uint8_t in = 0;
        if (!read(inReg(port), in, HwCheck::WeldNoReadback)) {
            return;
        }
        if ((in & m) != 0) {
            fail(HwCheck::WeldNoReadback);
        }
        return;
    }
    CurrentReading r;
    const bool got = windowSince(weldStartMs_, r);
    const bool late = now - weldStartMs_ > kWindowWaitMs;
    if (!got && !late) {
        return;
    }
    // Bit off again before anything else.
    const auto next = static_cast<uint8_t>(outShadow_[port] | m);
    if (!writeChecked(outReg(port), next, HwCheck::ExpanderOutputs)) {
        return;
    }
    outShadow_[port] = next;
    weldBitOn_ = false;
    lastValveOffMs_ = now;
    if (got && r.ma > in_.idleThresholdMa) {
        fail(HwCheck::RelayWelded);
        return;
    }
    if (!got) {
        // No window in 0.5 s: the current measurement is lost (rule 19).
        // Step 5 is not passed, it is skipped, and that is reported.
        windowLost_ = true;
        noteSkippedChecks();
        enter(Step::Relay);
        doRelay();
        return;
    }
    bleedPhase_ = 0;
    enter(Step::GateBleed);
    doGateBleed();
}

void OutputStage::doGateBleed()
{
    // Step 5b: RLY_ON high for 20 ms, then its pin an input without driving
    // it low first. With R12 in place the gate of Q1 discharges and K1 opens
    // within its 10 ms; with R12 open Q1 keeps its charge, and the output of
    // step 5 then draws current through the relay.
    const MonoMs now = clock_.monoMs();
    BitRef b{};
    (void)firstOutput(b); // step 5 ran, so there is one
    const uint8_t port = b.port & 1u;
    const auto m = static_cast<uint8_t>(1u << b.bit);
    const uint8_t rp = layout_.relay.port & 1u;
    const uint8_t rm = layout_.relayMask(rp);
    switch (bleedPhase_) {
    case 0: {
        const auto next = static_cast<uint8_t>(outShadow_[rp] | rm);
        if (!writeChecked(outReg(rp), next, HwCheck::RelayNoReadback)) {
            return;
        }
        outShadow_[rp] = next;
        relayClosed_ = true;
        bleedSinceMs_ = now;
        bleedPhase_ = 1;
        return;
    }
    case 1: {
        if (now - bleedSinceMs_ < kGateBleedHoldMs) {
            return;
        }
        // Configuration 0 = 0xE0 on rev A: RLY_ON an input, its output bit
        // still set.
        const auto cfg = static_cast<uint8_t>(cfgShadow_[rp] | rm);
        if (!writeChecked(cfgReg(rp), cfg, HwCheck::ExpanderConfig)) {
            return;
        }
        cfgShadow_[rp] = cfg;
        relayClosed_ = false;
        bleedSinceMs_ = now;
        bleedPhase_ = 2;
        return;
    }
    case 2: {
        if (now - bleedSinceMs_ < kGateBleedWaitMs) {
            return;
        }
        const auto next = static_cast<uint8_t>(outShadow_[port] & ~m);
        if (!writeChecked(outReg(port), next, HwCheck::ExpanderOutputs)) {
            return;
        }
        outShadow_[port] = next;
        weldBitOn_ = true;
        bleedSinceMs_ = now;
        bleedPhase_ = 3;
        return;
    }
    default:
        break;
    }
    CurrentReading r;
    const bool got = windowSince(bleedSinceMs_, r);
    if (!got && now - bleedSinceMs_ <= kWindowWaitMs) {
        return;
    }
    // Output off, RLY_ON cleared, then RLY_ON an output again (low).
    const auto off = static_cast<uint8_t>(outShadow_[port] | m);
    if (!writeChecked(outReg(port), off, HwCheck::ExpanderOutputs)) {
        return;
    }
    outShadow_[port] = off;
    weldBitOn_ = false;
    lastValveOffMs_ = now;
    const auto relayOff = static_cast<uint8_t>(outShadow_[rp] & ~rm);
    if (!writeChecked(outReg(rp), relayOff, HwCheck::ExpanderOutputs)) {
        return;
    }
    outShadow_[rp] = relayOff;
    const auto cfg = static_cast<uint8_t>(cfgShadow_[rp] & ~rm);
    if (!writeChecked(cfgReg(rp), cfg, HwCheck::ExpanderConfig)) {
        return;
    }
    cfgShadow_[rp] = cfg;
    bleedPhase_ = 0;
    if (got && r.ma > in_.idleThresholdMa) {
        fail(HwCheck::GateBleed);
        return;
    }
    if (!got) {
        windowLost_ = true; // rule 19, as in step 5
        noteSkippedChecks();
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
    if (currentUsable()) {
        CurrentReading r;
        const MonoMs from = relayClosedMs_ + kRelaySettleMs;
        if (!windowSince(from, r)) {
            if (now - from <= kWindowWaitMs) {
                return;
            }
            windowLost_ = true; // rule 19: skipped, and reported
            noteSkippedChecks();
        } else if (r.ma > in_.idleThresholdMa) {
            fail(HwCheck::OutputShorted);
            return;
        }
    } else {
        noteSkippedChecks();
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
    if (valveBitsLow() == 0 && currentUsable() &&
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
    beginStopTest(rb);
}

void OutputStage::beginStopTest(const KeepAliveReadbacks &rb)
{
    // The only way into step 9. Every caller has waited for
    // toggledLongEnough(): 1 s of toggling, above the 0.3 s of 19.7.
    toggledBeforeStopMs_ = rb.applied == KeepAliveRequest::Run ? since(rb.nowMs, rb.nowMs, rb.appliedSinceMs) : 0;
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
    const uint32_t elapsed = since(rb.nowMs, rb.nowMs, rb.appliedSinceMs);
    const bool aReleased = !rb.driveEnabled;
    const bool bReleased = rb.relayOff;
    if (!(aReleased && bReleased)) {
        if (elapsed >= kStopTestMaxMs) {
            fail(!aReleased ? HwCheck::StopDriveRelease : HwCheck::StopRelayRelease);
        }
        return;
    }
    const uint32_t tA = since(rb.nowMs, rb.driveSinceMs, rb.appliedSinceMs);
    const uint32_t tB = since(rb.nowMs, rb.relaySinceMs, rb.appliedSinceMs);
    releaseDriveMs_ = static_cast<uint16_t>(tA);
    releaseRelayMs_ = static_cast<uint16_t>(tB);
    // Absolute limits (review A2): wider than every tolerance corner.
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
    if (!judgeAgainstReference(tA, tB)) {
        return;
    }
    finishProcedure();
}

bool OutputStage::judgeAgainstReference(uint32_t tA, uint32_t tB)
{
    if (!reference_.valid()) {
        // Not commissioned: the absolute limits only (19.7, item 3). Start-up
        // and commissioning cycles teach the board its own times.
        if (proc_ == Proc::Startup || proc_ == Proc::Commission) {
            learn(tA, tB);
        }
        return true;
    }
    // The band (19.7, item 2): an open R30 or R91 doubles tA, an open R14 or
    // R90 doubles tB.
    const auto inBand = [](uint32_t t, uint16_t ref) {
        return t * 100u >= ref * kBandLowPercent && t * 100u <= ref * kBandHighPercent;
    };
    if (!inBand(tA, reference_.driveMs)) {
        fail(HwCheck::StopDriveBand);
        return false;
    }
    if (!inBand(tB, reference_.relayMs)) {
        fail(HwCheck::StopRelayBand);
        return false;
    }
    return true;
}

void OutputStage::learn(uint32_t tA, uint32_t tB)
{
    // 19.7 item 2: a sample only while the die reads 25 to 50 C (the board
    // then 10 to 40 C, the range the simulation covers). The five are in a
    // row: one taken outside the window starts the count again.
    if (!dieTempForCommissioning()) {
        samples_ = 0;
        return;
    }
    const bool plausible =
        tA >= kLearnDriveMinMs && tA <= kLearnDriveMaxMs && tB >= kLearnRelayMinMs && tB <= kLearnRelayMaxMs;
    if (!plausible) {
        samples_ = 0; // not what a healthy board gives near room temperature: start again
        return;
    }
    sampleDrive_[samples_] = static_cast<uint16_t>(tA);
    sampleRelay_[samples_] = static_cast<uint16_t>(tB);
    if (++samples_ < kCommissionSamples) {
        return;
    }
    samples_ = 0;
    auto a = sampleDrive_;
    auto b = sampleRelay_;
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    const auto agree = [](const std::array<uint16_t, kCommissionSamples> &v) {
        return static_cast<uint32_t>(v.back()) * 100u <= static_cast<uint32_t>(v.front()) * kLearnMaxSpreadPercent;
    };
    if (!agree(a) || !agree(b)) {
        return; // one board at one temperature gives the same time five times
    }
    DetectorReference r;
    r.driveMs = a[kCommissionSamples / 2];
    r.relayMs = b[kCommissionSamples / 2];
    reference_ = r;
    if (refStore_ != nullptr) {
        (void)refStore_->save(r); // kept in RAM for this boot even if the write failed
    }
}

void OutputStage::loadReference()
{
    refLoaded_ = true;
    DetectorReference r;
    if (refStore_ != nullptr && refStore_->load(r) && r.valid() && r.driveMs >= kLearnDriveMinMs &&
        r.driveMs <= kLearnDriveMaxMs && r.relayMs >= kLearnRelayMinMs && r.relayMs <= kLearnRelayMaxMs) {
        reference_ = r;
    }
}

void OutputStage::forgetReference()
{
    reference_ = DetectorReference{};
    samples_ = 0;
    commissionCycles_ = 0;
    refLoaded_ = true;
    if (refStore_ != nullptr) {
        (void)refStore_->clear();
    }
}

void OutputStage::doFault()
{
    // Keep-alive low. If the expander still answers (detector A stuck on),
    // every output off and every pin back to input, on every tick.
    guard_.setRequest(KeepAliveRequest::Off);
    releaseAll(true);
}

bool OutputStage::dieTempForCommissioning() const
{
    return in_.dieTempKnown && in_.dieTempC >= kCommissionTempMinC && in_.dieTempC <= kCommissionTempMaxC;
}

bool OutputStage::supplyInRange() const
{
    // No VIN_MON reading (the ADC path failed): rule 19 reports the lost
    // measurement and step 0 goes on without the supply check, as zones
    // still run without current readings.
    return vinMv_ < 0 || (vinMv_ >= kVinMinMv && vinMv_ <= kVinMaxMv);
}

void OutputStage::noteSkippedChecks()
{
    if (!skipNoted_ && proc_ == Proc::Run) {
        skipNoted_ = true;
        ++checksSkipped_;
    }
}

bool OutputStage::restWindow(const CurrentReading &w) const
{
    // A window taken wholly with the keep-alive stopped: the amplifiers rest
    // at VB, 0.2385 times 3V3 (19.10).
    if (w.meanLoMv < 0 || w.meanHiMv < 0 || w.startMs < lastActiveEndMs_) {
        return false;
    }
    return step_ == Step::Idle || step_ == Step::NoAc || step_ == Step::Precheck || step_ == Step::IdleTest;
}

bool OutputStage::watchSupply(MonoMs now)
{
    const CurrentReading &w = in_.current;
    if (w.seq != 0 && w.seq != lastWindowSeq_) {
        lastWindowSeq_ = w.seq;
        windowLost_ = false; // a window arrived
        vinMv_ = w.vinMonMv;
        if (restWindow(w)) {
            restLoMv_ = w.meanLoMv;
            restHiMv_ = w.meanHiMv;
            if (restLoMv_ > kRestOverVoltageMv && restHiMv_ > kRestOverVoltageMv) {
                // Rule 18: 3V3 above about 3.6 V. U13 removes drive by
                // hardware above 4.0 V; firmware stops the keep-alive and
                // refuses runs until a person confirms the repair.
                if (fault_ == HwCheck::None) {
                    fail(HwCheck::SupplyOverVoltage);
                    return false;
                }
            }
            restLost_ = restLoMv_ < kRestLoMinMv || restLoMv_ > kRestLoMaxMv || restHiMv_ < kRestHiMinMv ||
                        restHiMv_ > kRestHiMaxMv;
        }
    }
    // 19.10: no rectified supply while mains makes edges on AC_ZC for 2 s
    // or more is D2, R1 or C1. Without edges it is USB power alone.
    if (in_.acPresent) {
        if (!acSteady_) {
            acSteady_ = true;
            acSinceMs_ = now;
        }
    } else {
        acSteady_ = false;
    }
    const bool absent = vinMv_ >= 0 && vinMv_ < kVinAbsentMv;
    if (absent && !vinAbsent_) {
        vinAbsentSinceMs_ = now;
    }
    vinAbsent_ = absent;
    if (absent && acSteady_ && now - std::max(acSinceMs_, vinAbsentSinceMs_) >= kRectifierFaultMs &&
        fault_ == HwCheck::None) {
        fail(HwCheck::RectifierFault);
        return false;
    }
    return true;
}

} // namespace zf::hw
