// SPDX-License-Identifier: Apache-2.0
#include "zf/app/controller.hpp"

#include <algorithm>
#include <climits>

#include "zf/config/config_json.hpp"
#include "zf/fault_text.hpp"
#include "zf/time/time_zone.hpp"

namespace zf {
namespace {

// Zones the configuration asks for, limited to the outputs the board has
// (rev A: the variant strap, read in the start-up self-test; 0 = unknown).
uint8_t zoneCountFor(const Config &c, uint8_t boardZones)
{
    return boardZones == 0 ? c.zoneCount : std::min(c.zoneCount, boardZones);
}

ZoneDriverConfig driverConfigFrom(const Config &c, uint8_t boardZones)
{
    ZoneDriverConfig d;
    d.zoneCount = zoneCountFor(c, boardZones);
    d.maxConcurrent = c.driver.maxConcurrent;
    d.masterEnabled = c.master.enabled;
    d.masterPreOpenMs = c.master.preOpenMs;
    d.masterPostCloseMs = c.master.postCloseMs;
    d.interZoneDelayMs = c.driver.interZoneDelayMs;
    d.minRestSec = c.driver.minRestSec;
    d.zeroCrossSwitching = c.driver.zeroCrossSwitching;
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        d.zoneEnabled[i] = i < d.zoneCount && c.zones[i].enabled;
        d.maxRunSec[i] = c.zones[i].maxRunSec;
    }
    return d;
}

bool sameDriverConfig(const ZoneDriverConfig &a, const ZoneDriverConfig &b)
{
    return a.zoneCount == b.zoneCount && a.maxConcurrent == b.maxConcurrent && a.masterEnabled == b.masterEnabled &&
           a.masterPreOpenMs == b.masterPreOpenMs && a.masterPostCloseMs == b.masterPostCloseMs &&
           a.interZoneDelayMs == b.interZoneDelayMs && a.minRestSec == b.minRestSec &&
           a.zeroCrossSwitching == b.zeroCrossSwitching && a.zoneEnabled == b.zoneEnabled && a.maxRunSec == b.maxRunSec;
}

constexpr uint32_t faultBit(FaultCode c) { return 1u << static_cast<unsigned>(c); }

// Conditions clear by themselves when the cause goes away; everything else
// latches until ClearFaults.
constexpr uint32_t kSelfClearing = faultBit(FaultCode::AcLost) | faultBit(FaultCode::TimeInvalid) |
                                   faultBit(FaultCode::CurrentSenseUnavailable) | faultBit(FaultCode::SensorSupply) |
                                   faultBit(FaultCode::MainsSignal) | faultBit(FaultCode::NotCommissioned) |
                                   faultBit(FaultCode::SupplyLow) | faultBit(FaultCode::SupplyHigh);

// Output-stage faults: they clear when the stage accepts the acknowledgement.
constexpr uint32_t kStageFaults = faultBit(FaultCode::KeepAliveDetector) | faultBit(FaultCode::OutputExpander) |
                                  faultBit(FaultCode::RelayWelded) | faultBit(FaultCode::OutputShorted) |
                                  faultBit(FaultCode::TripLatchStuck) | faultBit(FaultCode::KeepAliveBridged) |
                                  faultBit(FaultCode::RelayGateBleed) | faultBit(FaultCode::RectifierFault) |
                                  faultBit(FaultCode::SupplyOverVoltage) | faultBit(FaultCode::LatchInputHeld);

constexpr int64_t kNoTrip = INT64_MIN;

} // namespace

Controller::Controller(const ControllerDeps &deps)
    : deps_(deps), driver_(deps.outputs, deps.outputEnable, deps.clock), scheduler_(deps.clock, driver_)
{
    driver_.setListener(this);
    scheduler_.setListener(this);
    for (auto &t : tripAtMs_) {
        t.fill(kNoTrip);
    }
}

void Controller::begin()
{
    config_ = deps_.config.get();
    configRevision_ = deps_.config.revision();
    driver_.init(driverConfigFrom(config_, boardZones_));
    // The run budget: RTC memory after a software restart; the copy in
    // flash after a power cut or a torn RTC record.
    if (!restoreRetained(deps_.clock.monoMs())) {
        restoreBudgetFromFlash();
    }

    if (deps_.runtimeStorage != nullptr) {
        SchedulerPersist persisted;
        uint32_t runtimeSeq = 0;
        runtimeLoad_ = loadRuntime(*deps_.runtimeStorage, persisted, runtimeSeq);
        if (runtimeLoad_ == RecordLoad::Certain || runtimeLoad_ == RecordLoad::Uncertain) {
            scheduler_.restore(persisted, runtimeLoad_ == RecordLoad::Uncertain);
        }
        BudgetRecord unused;
        uint32_t budgetSeq = 0;
        (void)loadBudget(*deps_.runtimeStorage, unused, budgetSeq);
        persist_.setSequences(runtimeSeq, budgetSeq);
    }
    applyConfig(config_);
    raiseFault(FaultCode::TimeInvalid, kNoZone);
    persistDirty_ = false;
}

void Controller::reloadConfig()
{
    const uint32_t rev = deps_.config.revision();
    if (rev == configRevision_) {
        return;
    }
    configRevision_ = rev;
    applyConfig(deps_.config.get());
}

void Controller::applyConfig(const Config &cfg)
{
    const ZoneDriverConfig dc = driverConfigFrom(cfg, boardZones_);
    if (!sameDriverConfig(dc, driver_.config())) {
        driver_.reconfigure(dc);
    }
    config_ = cfg;

    const auto tz = TimeZone::parse(cfg.timezone.view());
    SchedulerConfig sc;
    sc.schedule = cfg.schedule;
    sc.zoneCount = dc.zoneCount;
    sc.zoneEnabled = dc.zoneEnabled;
    scheduler_.configure(sc, tz ? *tz : TimeZone::utc());

    CurrentMonitorConfig cm;
    cm.enabled = cfg.diag.currentEnabled;
    cm.settleMs = cfg.diag.settleMs;
    cm.idleMaxMa = cfg.diag.idleMaxMa;
    cm.shortCircuitMa = cfg.diag.shortCircuitMa;
    cm.confirmSamples = cfg.diag.confirmSamples;
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        cm.zone[i] = cfg.zones[i].current;
    }
    cm.master = cfg.master.current;
    current_.configure(cm);

    rain_.configure(cfg.rain);
    spare_.configure(cfg.spare);
    flow_.configure(cfg.flow);

    updateInhibit();
    publish(makeEvent(EventType::ConfigApplied));
    stateDirty_ = true;
}

void Controller::setTimeStatus(TimeStatus status)
{
    if (status == timeStatus_) {
        return;
    }
    timeStatus_ = status;
    scheduler_.setTimeValid(status != TimeStatus::Unset);
    if (status == TimeStatus::Unset) {
        raiseFault(FaultCode::TimeInvalid, kNoZone);
    } else {
        clearCondition(FaultCode::TimeInvalid);
    }
    Event e = makeEvent(EventType::TimeStatusChanged);
    e.time.status = status;
    publish(e);
    stateDirty_ = true;
}

bool Controller::handle(const Command &cmd)
{
    stateDirty_ = true;
    lastRunError_ = RunError::Ok;
    const bool accepted = apply(cmd);
    if (!accepted) {
        // Commands arrive through a queue; this event is how the poster
        // (REST, MQTT, display) learns the reason.
        Event e = makeEvent(EventType::CommandRejected);
        e.rejected = {cmd.type, cmd.zone, cmd.programId, lastRunError_};
        publish(e);
    }
    return accepted;
}

bool Controller::apply(const Command &cmd)
{
    switch (cmd.type) {
    case CommandType::RunZone:
        lastRunError_ = scheduler_.runZone(cmd.zone, cmd.durationSec);
        return lastRunError_ == RunError::Ok;
    case CommandType::RunProgram:
        lastRunError_ = scheduler_.runProgram(cmd.programId);
        return lastRunError_ == RunError::Ok;
    case CommandType::StopAll:
        scheduler_.stop();
        driver_.stopAll(StopReason::Requested);
        return true;
    case CommandType::Pause:
        scheduler_.pause();
        return true;
    case CommandType::Resume:
        scheduler_.resume();
        return true;
    case CommandType::Skip:
        scheduler_.skip();
        return true;
    case CommandType::SetRainDelay:
        scheduler_.setRainDelayUntil(cmd.until);
        return true;
    case CommandType::ClearFaults:
        clearFaults();
        return true;
    case CommandType::SetMaintenance: {
        if (maintenance_ == cmd.flag) {
            return true;
        }
        maintenance_ = cmd.flag;
        updateInhibit();
        Event e = makeEvent(EventType::MaintenanceChanged);
        e.flag.on = maintenance_;
        publish(e);
        return true;
    }
    case CommandType::ReloadConfig:
        reloadConfig();
        return true;
    case CommandType::AcknowledgeRepair:
        if (!cmd.flag) {
            return false;
        }
        acknowledgeHardware(true);
        return true;
    }
    return false;
}

void Controller::tick()
{
    // A stored configuration is applied even if its ReloadConfig command was
    // lost: a writer that found the command queue full answered "stored,
    // reload not queued", and the controller ran the old configuration
    // (maximum run times, disabled zones) until the next restart.
    reloadConfig();
    const MonoMs now = deps_.clock.monoMs();
    const SensorSample s = deps_.sensors.read();
    sensorSupplyOn_ = s.sensorSupplyOn;
    mainsMilliHz_ = s.mainsMilliHz;

    // Valve current. Rev A delivers raw 100 ms windows (one new window every
    // other tick); fakes and simple boards give a reading every tick.
    bool freshCurrent = false;
    int32_t currentMa = -1;
    if (s.currentWindow.seq != 0) {
        if (s.currentWindow.seq != lastWindowSeq_) {
            lastWindowSeq_ = s.currentWindow.seq;
            // The true current is zero with the keep-alive stopped: the
            // expander is in reset and the relay open.
            const bool idle = stage_.phase == StagePhase::Idle || stage_.phase == StagePhase::NoAc;
            reading_ = currentSense_.process(s.currentWindow, idle, config_.diag.idleMaxMa);
            freshCurrent = true;
            currentMa = reading_.valid ? reading_.ma : -1;
            lastCurrentMa_ = currentMa;
        }
    } else {
        freshCurrent = true;
        currentMa = s.valveCurrentMa;
        lastCurrentMa_ = currentMa;
        reading_ = CurrentReading{};
        reading_.valid = currentMa >= 0;
        reading_.ma = currentMa;
        reading_.startMs = now;
        reading_.endMs = now;
    }

    // Output stage first (hardware design 19.7): it may withdraw power,
    // trip or fault before the driver switches anything this tick.
    StageTickInput in;
    in.acPresent = s.acPresent;
    in.current = reading_;
    in.currentSenseAvailable = s.currentSenseAvailable && config_.diag.currentEnabled;
    in.idleThresholdMa = config_.diag.idleMaxMa;
    in.sensorSupplyOn = s.sensorSupplyOn;
    in.keepAlivePinPlain = s.keepAlivePinPlain;
    in.dieTempKnown = s.dieTempKnown;
    in.dieTempC = s.dieTempC;
    deps_.outputs.tick(in);
    handleStage(now);
    if (!deps_.outputs.ownsKeepAlive()) {
        handleTrip(s.overCurrentTripped, now);
    }

    if (ac_.update(s.acPresent, now)) {
        Event e = makeEvent(EventType::SensorChanged);
        e.sensor = {SensorId::AcPresent, ac_.present()};
        publish(e);
        updateInhibit();
    }
    const bool acFault = (activeFaults_ & faultBit(FaultCode::AcLost)) != 0;
    if (ac_.present() && acFault) {
        clearCondition(FaultCode::AcLost);
    } else if (!ac_.present() && !acFault) {
        raiseFault(FaultCode::AcLost, kNoZone); // also covers "absent since boot"
    }
    // The zero-cross signal at a rate mains cannot have: bridged to the
    // keep-alive, or noise (hardware design 19.9).
    checkCondition(FaultCode::MainsSignal,
                   ac_.present() && s.mainsMilliHz != 0 &&
                       (s.mainsMilliHz < kMinMainsMilliHz || s.mainsMilliHz > kMaxMainsMilliHz));
    if (rain_.update(s.rainContactClosed, now)) {
        Event e = makeEvent(EventType::SensorChanged);
        e.sensor = {SensorId::Rain, rain_.active()};
        publish(e);
        updateInhibit();
    }
    if (spare_.update(s.spareContactClosed, now)) {
        Event e = makeEvent(EventType::SensorChanged);
        e.sensor = {SensorId::Spare, spare_.active()};
        publish(e);
        updateInhibit();
    }

    flow_.update(s.flowPulses, now);
    const bool leaking =
        config_.flow.enabled && driver_.outputMask() == 0 && flow_.rateLpm() > config_.flow.leakThresholdLpm;
    if (!leaking) {
        leakTiming_ = false;
    } else if (!leakTiming_) {
        leakTiming_ = true;
        leakSinceMs_ = now;
    } else if (now - leakSinceMs_ >= static_cast<MonoMs>(config_.flow.leakDetectSec) * 1000u &&
               (activeFaults_ & faultBit(FaultCode::FlowWithoutZones)) == 0) {
        raiseFault(FaultCode::FlowWithoutZones, kNoZone);
    }
    // Flow that starts with every valve shut, or stops while one is open, may
    // be the sensor supply failing: check it now (hardware design 19.6).
    const bool flowing = config_.flow.enabled && flow_.rateLpm() > 0.0f;
    if (flowing != flowing_) {
        flowing_ = flowing;
        const bool unexplained = flowing ? driver_.outputMask() == 0 : driver_.outputMask() != 0;
        if (unexplained && (!flowPollArmed_ || now - lastFlowPollMs_ >= kFlowPollMinIntervalMs)) {
            flowPollArmed_ = true;
            lastFlowPollMs_ = now;
            deps_.outputs.requestPoll();
        }
    }

    // No current sense: report it, keep watering (policy in
    // firmware-architecture.md, sensors and diagnostics). Short-circuit
    // protection then rests on the hardware trip and the output protection.
    // Hardware design rule 19: the output stage also counts the measurement
    // as lost when a step waited for a window in vain or a channel rests
    // outside its limits; it skips steps 5, 5b and 6 meanwhile, and this
    // condition says so for as long as it lasts.
    const bool stageLost = stage_.phase != StagePhase::None && stage_.currentLost != CurrentLoss::None;
    const bool senseLost = config_.diag.currentEnabled && (!s.currentSenseAvailable || stageLost);
    const bool senseFault = (activeFaults_ & faultBit(FaultCode::CurrentSenseUnavailable)) != 0;
    if (senseLost && !senseFault) {
        raiseFault(FaultCode::CurrentSenseUnavailable, kNoZone);
    } else if (!senseLost && senseFault) {
        clearCondition(FaultCode::CurrentSenseUnavailable);
    }

    // Without 24 VAC the current reading means nothing. While the output
    // stage tests itself with every valve off, its own checks (weld, shorted
    // output) judge the current, not the idle check here.
    const uint16_t mask = driver_.outputMask();
    const bool stageJudges = mask == 0 && stage_.phase != StagePhase::None && stage_.phase != StagePhase::Idle &&
                             stage_.phase != StagePhase::NoAc;
    if (ac_.present() && freshCurrent && !stageJudges) {
        handleCurrent(current_.evaluate(currentMa, mask, driver_.lastOpenedBit(), driver_.lastOutputChangeMs(), now));
    }
    handleRise(now);
    if (freshCurrent && s.currentWindow.seq != 0) {
        handleDc(reading_);
    }

    driver_.tick();
    scheduler_.tick();

    // A consumed scheduled start is stored at once: within the rate limit a
    // power cut lost it, and the start ran again after the restart (system
    // soak simulation, firmware-architecture.md section 22). Starts are a
    // few a day, so this adds no flash wear worth counting.
    if (persistDirty_ && (persistUrgent_ || now - lastPersistMs_ >= kPersistMinIntervalMs)) {
        savePersist(now);
    }
    if (persist_.takeFailure() && (activeFaults_ & faultBit(FaultCode::StorageError)) == 0) {
        raiseFault(FaultCode::StorageError, kNoZone);
    }
    saveRetained(now);
    postBudget(now);
    if (stateDirty_ || now - lastSnapshotMs_ >= kSnapshotIntervalMs) {
        publishSnapshot(now);
    }
}

void Controller::checkCondition(FaultCode code, bool present)
{
    const bool active = (activeFaults_ & faultBit(code)) != 0;
    if (present && !active) {
        raiseFault(code, kNoZone);
    } else if (!present && active) {
        clearCondition(code);
    }
}

void Controller::handleStage(MonoMs now)
{
    const OutputStageStatus st = deps_.outputs.status();
    const bool phaseChanged = st.phase != stage_.phase;
    stage_ = st;
    if (phaseChanged) {
        stateDirty_ = true;
    }
    if (st.phase == StagePhase::None) {
        return;
    }
    if (st.zonesFitted != 0 && st.zonesFitted != boardZones_) {
        boardZones_ = st.zonesFitted;
        applyConfig(config_); // limits the zone count to the outputs the board has
    }
    if (st.faultSeq != stageFaultSeq_) {
        stageFaultSeq_ = st.faultSeq;
        if (st.fault != HwCheck::None) {
            // The stage has already stopped the keep-alive. Bring the
            // firmware state in line and keep everything from starting.
            driver_.stopAll(StopReason::Fault);
            scheduler_.stop();
            raiseFault(faultCodeFor(st.fault), kNoZone, static_cast<uint8_t>(st.fault));
        }
    }
    const bool faulted = st.phase == StagePhase::Fault;
    if (faulted != hwFault_) {
        hwFault_ = faulted;
        updateInhibit();
    }
    if (st.tripSeq != stageTripSeq_) {
        stageTripSeq_ = st.tripSeq;
        onTrip(now);
    }
    if (st.sensorSupplyKnown) {
        checkCondition(FaultCode::SensorSupply, sensorSupplyOn_ && st.sensorSupplyFault);
    }
    // 19.7, the limits of step 9: a board with no stored release times
    // reports that it is not commissioned.
    checkCondition(FaultCode::NotCommissioned, !st.commissioned);
    // 19.10: step 0 waits while VIN_MON is outside 0.60 to 1.50 V.
    checkCondition(FaultCode::SupplyLow, st.supplyLow);
    checkCondition(FaultCode::SupplyHigh, st.supplyHigh);
}

void Controller::handleDc(const CurrentReading &r)
{
    // A DC component in the valve current: one triac polarity is not
    // conducting (hardware design 19.2). Confirmed over confirmSamples
    // windows, attributed like any current fault.
    const uint16_t mask = driver_.outputMask();
    if (!config_.diag.currentEnabled || !r.valid || !r.dcComponent || mask == 0 ||
        deps_.clock.monoMs() - driver_.lastOutputChangeMs() < config_.diag.settleMs) {
        dcWindows_ = 0;
        return;
    }
    if (++dcWindows_ < std::max<uint8_t>(config_.diag.confirmSamples, 1)) {
        return;
    }
    dcWindows_ = 0;
    const uint8_t bit = driver_.lastOpenedBit();
    if (bit < kMaxZones && (mask & (1u << bit)) != 0) {
        // zone_fault_policy.hpp: a half-dead output on the board, locked out.
        driver_.stop(bit, StopReason::Fault);
        driver_.setLockout(bit, kDcComponentPolicy.lockOut);
        zoneFault_[bit] = FaultCode::ZoneDcCurrent;
        raiseFault(FaultCode::ZoneDcCurrent, bit);
    } else {
        driver_.stopAll(StopReason::Fault);
        raiseFault(FaultCode::ZoneDcCurrent, kNoZone);
    }
}

void Controller::updateInhibit()
{
    const bool acOk = ac_.present();
    uint8_t flags = 0;
    if (rain_.active() || (config_.spareFunction == SpareFunction::InhibitLikeRain && spare_.active())) {
        flags |= kInhibitSensor;
    }
    if (!acOk) {
        flags |= kInhibitNoAc;
    }
    if (maintenance_) {
        flags |= kInhibitMaintenance;
    }
    if (config_.master.enabled && driver_.masterLockedOut()) {
        flags |= kInhibitMasterLockout;
    }
    if (hwFault_) {
        flags |= kInhibitHardware;
    }
    driver_.setInhibit(!acOk || maintenance_ || tripSeen_ || hwFault_);
    if (driver_.activeCount() > 0 || driver_.masterOn()) {
        if (maintenance_) {
            driver_.stopAll(StopReason::Maintenance);
        } else if (!acOk) {
            driver_.stopAll(StopReason::AcLost);
        }
    }
    scheduler_.setInhibit(flags);
    stateDirty_ = true;
}

void Controller::handleCurrent(const CurrentVerdict &v)
{
    const ZoneIndex zone = v.outputBit < kMaxZones ? v.outputBit : kNoZone;
    switch (v.fault) {
    case CurrentFault::None:
        return;
    case CurrentFault::ShortCircuit:
    case CurrentFault::OverCurrent: {
        static_assert(zoneFaultPolicy(CurrentFault::OverCurrent).lockOut, "zone_fault_policy.hpp");
        if (v.outputBit == ZoneDriver::kMasterBit) {
            masterFaultInSession_ = true; // before stopAll: closing the master must not count as clean
        }
        // Everything off first; bookkeeping after.
        driver_.stopAll(StopReason::Fault);
        scheduler_.stop();
        const FaultCode code =
            v.fault == CurrentFault::ShortCircuit ? FaultCode::ShortCircuit : FaultCode::ZoneOverCurrent;
        if (zone != kNoZone) {
            driver_.setLockout(zone, true);
            zoneFault_[zone] = code;
        }
        raiseFault(code, zone);
        if (v.outputBit == ZoneDriver::kMasterBit) {
            recordMasterFault();
        }
        return;
    }
    case CurrentFault::OpenCircuit:
        // zone_fault_policy.hpp: stopped and reported, not locked out; the
        // next run inside the zone's window clears the report.
        static_assert(!zoneFaultPolicy(CurrentFault::OpenCircuit).lockOut, "zone_fault_policy.hpp");
        if (zone != kNoZone) {
            driver_.stop(zone, StopReason::Fault);
            zoneFault_[zone] = FaultCode::ZoneOpenCircuit;
        } else {
            driver_.stopAll(StopReason::Fault);
            scheduler_.stop();
            masterOpenCircuit_ = true;
        }
        raiseFault(FaultCode::ZoneOpenCircuit, zone);
        return;
    case CurrentFault::CurrentRose:
        // zone_fault_policy.hpp: every output off at once. The output stage
        // checks rule 6 with every valve off within one window; handleRise()
        // blames the zone if it finds nothing.
        driver_.stopAll(StopReason::Fault);
        scheduler_.stop();
        riseCheck_.begin(v.outputBit, deps_.clock.monoMs());
        stateDirty_ = true;
        return;
    case CurrentFault::CurrentWhileIdle:
        // Firmware already has every output off; report so a human checks
        // the output stage.
        raiseFault(FaultCode::CurrentWhileIdle, kNoZone);
        return;
    }
}

void Controller::handleRise(MonoMs now)
{
    if (!riseCheck_.active()) {
        return;
    }
    if ((activeFaults_ & (faultBit(FaultCode::OutputShorted) | faultBit(FaultCode::CurrentWhileIdle))) != 0) {
        riseCheck_.idleCurrentFound(); // a failed-short output device: the stage opened the relay
        return;
    }
    const uint8_t bit = riseCheck_.poll(now);
    if (bit != 0xFF) {
        CurrentVerdict v;
        v.fault = CurrentFault::OverCurrent; // the extra current went through the zone itself
        v.outputBit = bit;
        handleCurrent(v);
    }
}

void Controller::handleTrip(bool tripped, MonoMs now)
{
    // Boards without a sequenced output stage: the latch is a level on the
    // sensor sample, cleared by the board. Starts wait until it is clear.
    if (tripped == tripSeen_) {
        return;
    }
    tripSeen_ = tripped;
    if (tripped) {
        onTrip(now);
    }
    updateInhibit();
}

void Controller::onTrip(MonoMs now)
{
    // Hardware design 19.8. The comparator has already removed gate drive
    // and released the relay. The output switched on within 500 ms is the
    // faulted one; without one, every output that was on is suspect. Each
    // trip counts against the outputs it marks; three in a day and the
    // output stays off until a person clears the faults. The rest of the
    // queue carries on once the latch is clear (the stage clears it by
    // holding the keep-alive low and runs the self-test again).
    lastTripCurrentMa_ = lastCurrentMa_;
    const uint16_t mask = driver_.outputMask();
    const uint8_t last = driver_.lastOpenedBit();
    uint16_t faulted = 0;
    if (last <= ZoneDriver::kMasterBit && (mask & (1u << last)) != 0 &&
        now - driver_.openedAtMs(last) <= kTripBlameMs) {
        faulted = static_cast<uint16_t>(1u << last);
    }
    const uint16_t marked = faulted != 0 ? faulted : mask;
    const bool master = (faulted & (1u << ZoneDriver::kMasterBit)) != 0;
    if (master) {
        masterFaultInSession_ = true; // closing the master is not a clean session
    }
    driver_.stopAll(StopReason::Fault);
    for (uint8_t z = 0; z < kMaxZones; ++z) {
        if ((marked & (1u << z)) == 0) {
            continue;
        }
        if (recordTrip(z, now)) {
            driver_.setLockout(z, true);
        }
        if ((faulted & (1u << z)) != 0 || driver_.lockedOut(z)) {
            zoneFault_[z] = FaultCode::OverCurrentTrip;
            raiseFault(FaultCode::OverCurrentTrip, z);
        }
    }
    const bool masterLimit = (marked & (1u << ZoneDriver::kMasterBit)) != 0 && recordTrip(ZoneDriver::kMasterBit, now);
    if (master || faulted == 0) {
        raiseFault(FaultCode::OverCurrentTrip, kNoZone);
    }
    if (master || masterLimit) {
        recordMasterFault();
    }
    updateInhibit();
}

bool Controller::recordTrip(uint8_t bit, MonoMs now)
{
    auto &times = tripAtMs_[bit];
    const auto t = static_cast<int64_t>(now);
    // Replace the oldest.
    std::size_t oldest = 0;
    for (std::size_t i = 1; i < times.size(); ++i) {
        if (times[i] < times[oldest]) {
            oldest = i;
        }
    }
    times[oldest] = t;
    std::size_t today = 0;
    for (int64_t at : times) {
        today += at != kNoTrip && t - at < kDayMs ? 1u : 0u;
    }
    return today >= kTripsPerDay;
}

void Controller::recordMasterFault()
{
    masterFaultInSession_ = true;
    if (masterFaults_ < 0xFF) {
        ++masterFaults_;
    }
    if (masterFaults_ >= config_.master.faultLockoutCount && !driver_.masterLockedOut()) {
        // Like a zone lockout: no program retries into a shorted master
        // circuit. Every output is already off.
        driver_.setMasterLockout(true);
        raiseFault(FaultCode::MasterLockedOut, kNoZone);
        updateInhibit();
    }
    stateDirty_ = true;
}

void Controller::raiseFault(FaultCode code, ZoneIndex zone) { raiseFault(code, zone, 0); }

void Controller::raiseFault(FaultCode code, ZoneIndex zone, uint8_t detail)
{
    activeFaults_ |= faultBit(code);
    Event e = makeEvent(EventType::FaultRaised);
    e.fault = {code, zone, detail};
    publish(e);
    stateDirty_ = true;
}

void Controller::clearCondition(FaultCode code)
{
    if ((activeFaults_ & faultBit(code)) == 0) {
        return;
    }
    activeFaults_ &= ~faultBit(code);
    Event e = makeEvent(EventType::FaultCleared);
    e.fault = {code, kNoZone, 0};
    publish(e);
    stateDirty_ = true;
}

void Controller::clearFaults()
{
    driver_.clearLockouts();
    current_.rearm(); // a current fault that is still present is raised again
    zoneFault_.fill(FaultCode::None);
    masterFaults_ = 0;
    masterOpenCircuit_ = false;
    for (auto &t : tripAtMs_) {
        t.fill(kNoTrip); // a cleared output gets its three trips again
    }
    // One press acknowledges an output-stage fault, except a keep-alive
    // detector fault, which needs the repair confirmed (AcknowledgeRepair).
    const bool stageCleared = deps_.outputs.acknowledgeFault(false);
    const uint32_t keep = kSelfClearing | (stageCleared ? 0u : (activeFaults_ & kStageFaults));
    const uint32_t latched = activeFaults_ & ~keep;
    activeFaults_ &= keep;
    for (unsigned c = 1; c < static_cast<unsigned>(FaultCode::Count); ++c) {
        if ((latched & (1u << c)) != 0) {
            Event e = makeEvent(EventType::FaultCleared);
            e.fault = {static_cast<FaultCode>(c), kNoZone, 0};
            publish(e);
        }
    }
    handleStage(deps_.clock.monoMs());
    updateInhibit();
    stateDirty_ = true;
}

void Controller::acknowledgeHardware(bool repaired)
{
    if (!deps_.outputs.acknowledgeFault(repaired)) {
        return;
    }
    const uint32_t cleared = activeFaults_ & kStageFaults;
    activeFaults_ &= ~kStageFaults;
    for (unsigned c = 1; c < static_cast<unsigned>(FaultCode::Count); ++c) {
        if ((cleared & (1u << c)) != 0) {
            Event e = makeEvent(EventType::FaultCleared);
            e.fault = {static_cast<FaultCode>(c), kNoZone, 0};
            publish(e);
        }
    }
    handleStage(deps_.clock.monoMs());
    updateInhibit();
    stateDirty_ = true;
}

void Controller::saveRetained(MonoMs now)
{
    if (deps_.retained == nullptr) {
        return;
    }
    RetainedRecord r;
    const auto budget = driver_.exportBudget();
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        r.burstMs[i] = budget[i].burstMs;
        r.closedForMs[i] = budget[i].closedForMs;
    }
    r.hwFault = static_cast<uint8_t>(stage_.fault);
    const auto t = static_cast<int64_t>(now);
    for (std::size_t b = 0; b <= kMaxZones; ++b) {
        for (std::size_t k = 0; k < kTripsKept; ++k) {
            const int64_t at = tripAtMs_[b][k];
            r.tripAgeSec[b][k] = at == kNoTrip || t - at >= kDayMs ? 0u : static_cast<uint32_t>((t - at) / 1000 + 1);
        }
    }
    seal(r);
    deps_.retained->save(r);
}

void Controller::restoreBudgetFromFlash()
{
    if (deps_.runtimeStorage == nullptr) {
        return;
    }
    BudgetRecord rec;
    uint32_t seq = 0;
    budgetLoad_ = loadBudget(*deps_.runtimeStorage, rec, seq);
    std::array<uint32_t, kMaxZones> maxRunMs{};
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        maxRunMs[z] = driver_.effectiveMaxRunSec(static_cast<ZoneIndex>(z)) * 1000u;
    }
    driver_.restoreBudget(budgetAfterPowerCut(budgetLoad_, rec, maxRunMs));
}

void Controller::postBudget(MonoMs now)
{
    BudgetRecord r;
    if (deps_.runtimeStorage != nullptr && budgetPoster_.due(driver_, now, r)) {
        persist_.postBudget(r);
    }
}

bool Controller::restoreRetained(MonoMs now)
{
    RetainedRecord r;
    if (deps_.retained == nullptr || !deps_.retained->load(r) || !intact(r)) {
        return false;
    }
    std::array<ZoneBudget, kMaxZones> budget{};
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        budget[i] = {r.burstMs[i], r.closedForMs[i]};
    }
    driver_.restoreBudget(budget);
    if (r.hwFault != 0 && r.hwFault < static_cast<uint8_t>(HwCheck::Count)) {
        deps_.outputs.restoreFault(static_cast<HwCheck>(r.hwFault));
    }
    const auto t = static_cast<int64_t>(now);
    for (std::size_t b = 0; b <= kMaxZones; ++b) {
        for (std::size_t k = 0; k < kTripsKept; ++k) {
            const uint32_t age = r.tripAgeSec[b][k];
            tripAtMs_[b][k] = age == 0 ? kNoTrip : t - static_cast<int64_t>(age - 1) * 1000;
        }
    }
    return true;
}

void Controller::savePersist(MonoMs now)
{
    // A plain-data copy for the writer task: no JSON, no heap, no flash here.
    lastPersistMs_ = now;
    persistDirty_ = false;
    persistUrgent_ = false;
    if (deps_.runtimeStorage != nullptr) {
        persist_.post(scheduler_.persist());
    }
}

void Controller::publishSnapshot(MonoMs now)
{
    stateDirty_ = false;
    lastSnapshotMs_ = now;
    SystemState st;
    st.monoMs = now;
    st.utc = deps_.clock.nowUtc();
    st.timeStatus = timeStatus_;
    st.zoneCount = driver_.config().zoneCount; // limited to the outputs the board has
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        const auto z = static_cast<ZoneIndex>(i);
        ZoneStatus &zs = st.zones[i];
        zs.state = driver_.state(z);
        zs.remainingSec = (driver_.remainingMs(z) + 999u) / 1000u;
        zs.lastStop = driver_.lastStopReason(z);
        zs.fault = zoneFault_[i];
        zs.enabled = driver_.config().zoneEnabled[i];
        zs.lockedOut = driver_.lockedOut(z);
        zs.restRemainingSec = (driver_.restRemainingMs(z) + 999u) / 1000u;
        zs.learnedCurrentMa = current_.observedMa(static_cast<uint8_t>(i));
    }
    st.masterEnabled = config_.master.enabled;
    st.masterOn = driver_.masterOn();
    st.masterLockedOut = driver_.masterLockedOut();
    st.masterFaultCount = masterFaults_;

    st.queue.state = scheduler_.queueState();
    st.queue.pausedForSec = scheduler_.pausedForMs() / 1000u;
    st.queue.length = static_cast<uint8_t>(scheduler_.queueSize());
    for (std::size_t i = 0; i < scheduler_.queueSize() && i < QueueStatus::kShown; ++i) {
        const RunEntry *e = scheduler_.entry(i);
        st.queue.items[i] = QueueItem{e->id, e->zone, e->durationSec, e->source, e->programId};
        st.queue.shownCount = static_cast<uint8_t>(i + 1);
    }
    if (st.queue.state == QueueState::Running && scheduler_.queueSize() > 0) {
        st.queue.currentRemainingSec = st.zones[scheduler_.entry(0)->zone].remainingSec;
    }

    st.sensors.rainEnabled = config_.rain.enabled;
    st.sensors.rainActive = rain_.active();
    st.sensors.spareEnabled = config_.spare.enabled;
    st.sensors.spareActive = spare_.active();
    st.sensors.acPresent = ac_.present();
    st.sensors.flowEnabled = config_.flow.enabled;
    st.sensors.flowLpm = flow_.rateLpm();
    st.sensors.flowTotalLitres = flow_.totalLitres();
    st.sensors.valveCurrentMa = lastCurrentMa_;

    st.rainDelayUntil = scheduler_.rainDelayActive() ? scheduler_.rainDelayUntil() : 0;
    st.inhibitFlags = scheduler_.inhibit();
    st.activeFaults = activeFaults_;
    st.maintenance = maintenance_;
    st.configRevision = configRevision_;

    HardwareStatus &hw = st.hardware;
    hw.phase = stage_.phase;
    hw.zonesFitted = stage_.zonesFitted;
    hw.relayClosed = stage_.relayClosed;
    hw.sensorSupplyOn = sensorSupplyOn_;
    hw.sensorSupplyFault = stage_.sensorSupplyKnown && stage_.sensorSupplyFault;
    hw.check = stage_.fault;
    hw.step = hwCheckStep(stage_.fault);
    hw.repairNeeded = hwCheckNeedsRepair(stage_.fault);
    hw.selfTestsPassed = stage_.selfTestsPassed;
    hw.enableDriveMs = stage_.enableDriveMs;
    hw.enableRelayMs = stage_.enableRelayMs;
    hw.releaseDriveMs = stage_.releaseDriveMs;
    hw.releaseRelayMs = stage_.releaseRelayMs;
    hw.commissioned = stage_.commissioned;
    hw.referenceDriveMs = stage_.reference.driveMs;
    hw.referenceRelayMs = stage_.reference.relayMs;
    hw.vinMonMv = stage_.vinMonMv;
    hw.restLoMv = stage_.restLoMv;
    hw.restHiMv = stage_.restHiMv;
    hw.currentLost = stage_.phase == StagePhase::None ? CurrentLoss::None : stage_.currentLost;
    hw.checksSkipped = stage_.checksSkipped;
    hw.idleTrips = stage_.idleTrips;
    hw.commissionWaitingForTemperature = stage_.commissionWaitingForTemperature;
    hw.mainsMilliHz = mainsMilliHz_;
    hw.noiseFloorMa = currentSense_.noiseFloorMa();
    hw.currentChannel = reading_.channel;
    hw.currentDcMa = reading_.dcMa;
    hw.lastTripCurrentMa = lastTripCurrentMa_;
    deps_.state.publish(st);
}

Event Controller::makeEvent(EventType type) const
{
    Event e{};
    e.type = type;
    e.monoMs = deps_.clock.monoMs();
    e.utc = deps_.clock.nowUtc();
    return e;
}

void Controller::publish(const Event &e) { deps_.events.publish(e); }

void Controller::onZoneChanged(ZoneIndex zone, ValveState state, StopReason reason)
{
    if (state == ValveState::Open && config_.flow.enabled) {
        flow_.markSession();
    }
    Event e = makeEvent(EventType::ZoneChanged);
    e.zone = {zone, state, reason};
    publish(e);
    trackRun(zone, state, reason);
    stateDirty_ = true;
}

void Controller::trackRun(ZoneIndex zone, ValveState state, StopReason reason)
{
    if (zone >= kMaxZones) {
        return;
    }
    OpenRun &r = runs_[zone];
    const MonoMs now = deps_.clock.monoMs();
    if (state == ValveState::Open && !r.open) {
        r = OpenRun{};
        r.open = true;
        r.openedMs = now;
        r.plannedSec = static_cast<uint16_t>(std::min<uint32_t>((driver_.remainingMs(zone) + 999u) / 1000u, 0xFFFFu));
        // The queue feeds the driver one entry at a time: the entry for this
        // zone nearest the head is the one that opened it.
        for (std::size_t i = 0; i < scheduler_.queueSize(); ++i) {
            const RunEntry *q = scheduler_.entry(i);
            if (q->zone == zone) {
                r.source = q->source;
                r.programId = q->programId;
                break;
            }
        }
        r.startPulses = flow_.totalPulses();
        current_.beginRun(static_cast<uint8_t>(zone));
        return;
    }
    if (state != ValveState::Off || !r.open) {
        return;
    }
    r.open = false;
    Event e = makeEvent(EventType::RunEnded);
    RunEndedData &d = e.run;
    d.zone = zone;
    d.source = r.source;
    d.reason = reason;
    d.programId = r.programId;
    d.plannedSec = r.plannedSec;
    d.actualSec = static_cast<uint16_t>(std::min<MonoMs>((now - r.openedMs + 500u) / 1000u, 0xFFFFu));
    d.currentMa = current_.runMeanMa(static_cast<uint8_t>(zone));
    // zone_fault_policy.hpp: an open circuit clears itself once the zone has
    // run with its current inside its window (the wire was fixed).
    if (zoneFault_[zone] == FaultCode::ZoneOpenCircuit && d.currentMa > 0 &&
        zoneFaultPolicy(CurrentFault::OpenCircuit).clearsOnGoodRun) {
        zoneFault_[zone] = FaultCode::None;
        const bool another = std::any_of(zoneFault_.begin(), zoneFault_.end(),
                                         [](FaultCode c) { return c == FaultCode::ZoneOpenCircuit; });
        if (!another && !masterOpenCircuit_) {
            clearCondition(FaultCode::ZoneOpenCircuit);
        }
        stateDirty_ = true;
    }
    // Two zones open together share the volume measured meanwhile.
    d.centilitres =
        config_.flow.enabled ? static_cast<int32_t>(std::min(flow_.litresSince(r.startPulses) * 100.0f, 2.0e9f)) : -1;
    publish(e);
}

void Controller::onMasterChanged(bool on)
{
    const MonoMs now = deps_.clock.monoMs();
    if (on) {
        masterOnSinceMs_ = now;
        masterFaultInSession_ = false;
    } else if (!masterFaultInSession_ && now - masterOnSinceMs_ >= config_.diag.settleMs) {
        masterFaults_ = 0; // ran cleanly
    }
    Event e = makeEvent(EventType::MasterChanged);
    e.master.on = on;
    publish(e);
    stateDirty_ = true;
}

void Controller::onProgramStarted(uint16_t programId, RunSource source, bool late)
{
    persistUrgent_ = persistUrgent_ || source == RunSource::Schedule;
    Event e = makeEvent(EventType::ProgramStarted);
    e.program = ProgramData{programId, source, SkipReason::NothingToRun, late, 0};
    publish(e);
}

void Controller::onProgramSkipped(uint16_t programId, SkipReason reason)
{
    persistUrgent_ = true;
    Event e = makeEvent(EventType::ProgramSkipped);
    e.program = ProgramData{programId, RunSource::Schedule, reason, false, 0};
    publish(e);
}

void Controller::onProgramMissed(uint16_t programId, UtcSeconds scheduledAt)
{
    persistUrgent_ = true;
    Event e = makeEvent(EventType::ProgramMissed);
    e.program = ProgramData{programId, RunSource::Schedule, SkipReason::NothingToRun, false, scheduledAt};
    publish(e);
}

void Controller::onQueueChanged()
{
    publish(makeEvent(EventType::QueueChanged));
    stateDirty_ = true;
}

void Controller::onRunDropped(const RunEntry &entry, StartError reason)
{
    Event e = makeEvent(EventType::RunDropped);
    e.dropped = {entry.zone, entry.programId, entry.source, reason};
    publish(e);
}

void Controller::onPauseExpired()
{
    publish(makeEvent(EventType::PauseExpired));
    stateDirty_ = true;
}

void Controller::onRainDelayChanged(UtcSeconds until)
{
    Event e = makeEvent(EventType::RainDelayChanged);
    e.rainDelay.until = until;
    publish(e);
    stateDirty_ = true;
}

} // namespace zf
