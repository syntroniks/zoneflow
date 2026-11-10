// SPDX-License-Identifier: Apache-2.0
#include "zf/app/controller.hpp"

#include "zf/config/config_json.hpp"
#include "zf/time/time_zone.hpp"

namespace zf {
namespace {

ZoneDriverConfig driverConfigFrom(const Config &c)
{
    ZoneDriverConfig d;
    d.zoneCount = c.zoneCount;
    d.maxConcurrent = c.driver.maxConcurrent;
    d.masterEnabled = c.master.enabled;
    d.masterPreOpenMs = c.master.preOpenMs;
    d.masterPostCloseMs = c.master.postCloseMs;
    d.interZoneDelayMs = c.driver.interZoneDelayMs;
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        d.zoneEnabled[i] = i < c.zoneCount && c.zones[i].enabled;
        d.maxRunSec[i] = c.zones[i].maxRunSec;
    }
    return d;
}

bool sameDriverConfig(const ZoneDriverConfig &a, const ZoneDriverConfig &b)
{
    return a.zoneCount == b.zoneCount && a.maxConcurrent == b.maxConcurrent && a.masterEnabled == b.masterEnabled &&
           a.masterPreOpenMs == b.masterPreOpenMs && a.masterPostCloseMs == b.masterPostCloseMs &&
           a.interZoneDelayMs == b.interZoneDelayMs && a.zoneEnabled == b.zoneEnabled && a.maxRunSec == b.maxRunSec;
}

constexpr uint32_t faultBit(FaultCode c) { return 1u << static_cast<unsigned>(c); }

// Conditions clear by themselves when the cause goes away; everything else
// latches until ClearFaults.
constexpr uint32_t kSelfClearing = faultBit(FaultCode::AcLost) | faultBit(FaultCode::TimeInvalid);

} // namespace

Controller::Controller(const ControllerDeps &deps)
    : deps_(deps), driver_(deps.outputs, deps.outputEnable, deps.clock), scheduler_(deps.clock, driver_)
{
    driver_.setListener(this);
    scheduler_.setListener(this);
}

void Controller::begin()
{
    config_ = deps_.config.get();
    configRevision_ = deps_.config.revision();
    driver_.init(driverConfigFrom(config_));

    if (deps_.runtimeStorage != nullptr) {
        std::string json;
        SchedulerPersist persisted;
        if (deps_.runtimeStorage->read(ConfigStore::kRuntimeKey, json) && persistFromJson(json, persisted)) {
            scheduler_.restore(persisted);
        }
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
    const ZoneDriverConfig dc = driverConfigFrom(cfg);
    if (!sameDriverConfig(dc, driver_.config())) {
        driver_.reconfigure(dc);
    }
    config_ = cfg;

    const auto tz = TimeZone::parse(cfg.timezone.view());
    SchedulerConfig sc;
    sc.schedule = cfg.schedule;
    sc.zoneCount = cfg.zoneCount;
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
    }
    return false;
}

void Controller::tick()
{
    const MonoMs now = deps_.clock.monoMs();
    const SensorSample s = deps_.sensors.read();
    lastCurrentMa_ = s.valveCurrentMa;

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

    // Without 24 VAC the current reading means nothing.
    if (ac_.present()) {
        handleCurrent(current_.evaluate(s.valveCurrentMa, driver_.outputMask(), driver_.lastOpenedBit(),
                                        driver_.lastOutputChangeMs(), now));
    }

    driver_.tick();
    scheduler_.tick();

    if (persistDirty_ && now - lastPersistMs_ >= kPersistMinIntervalMs) {
        savePersist(now);
    }
    if (stateDirty_ || now - lastSnapshotMs_ >= kSnapshotIntervalMs) {
        publishSnapshot(now);
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
    driver_.setInhibit(!acOk || maintenance_);
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
        return;
    }
    case CurrentFault::OpenCircuit:
        if (zone != kNoZone) {
            driver_.stop(zone, StopReason::Fault);
            zoneFault_[zone] = FaultCode::ZoneOpenCircuit;
        } else {
            driver_.stopAll(StopReason::Fault);
            scheduler_.stop();
        }
        raiseFault(FaultCode::ZoneOpenCircuit, zone);
        return;
    case CurrentFault::CurrentWhileIdle:
        // Firmware already has every output off; report so a human checks
        // the output stage.
        raiseFault(FaultCode::CurrentWhileIdle, kNoZone);
        return;
    }
}

void Controller::raiseFault(FaultCode code, ZoneIndex zone)
{
    activeFaults_ |= faultBit(code);
    Event e = makeEvent(EventType::FaultRaised);
    e.fault = {code, zone};
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
    e.fault = {code, kNoZone};
    publish(e);
    stateDirty_ = true;
}

void Controller::clearFaults()
{
    driver_.clearLockouts();
    zoneFault_.fill(FaultCode::None);
    const uint32_t latched = activeFaults_ & ~kSelfClearing;
    activeFaults_ &= kSelfClearing;
    for (unsigned c = 1; c < static_cast<unsigned>(FaultCode::Count); ++c) {
        if ((latched & (1u << c)) != 0) {
            Event e = makeEvent(EventType::FaultCleared);
            e.fault = {static_cast<FaultCode>(c), kNoZone};
            publish(e);
        }
    }
    stateDirty_ = true;
}

void Controller::savePersist(MonoMs now)
{
    lastPersistMs_ = now;
    persistDirty_ = false;
    if (deps_.runtimeStorage == nullptr) {
        return;
    }
    const std::string json = persistToJson(scheduler_.persist());
    if (json.empty() || !deps_.runtimeStorage->write(ConfigStore::kRuntimeKey, json)) {
        if ((activeFaults_ & faultBit(FaultCode::StorageError)) == 0) {
            raiseFault(FaultCode::StorageError, kNoZone);
        }
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
    st.zoneCount = config_.zoneCount;
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        const auto z = static_cast<ZoneIndex>(i);
        ZoneStatus &zs = st.zones[i];
        zs.state = driver_.state(z);
        zs.remainingSec = (driver_.remainingMs(z) + 999u) / 1000u;
        zs.lastStop = driver_.lastStopReason(z);
        zs.fault = zoneFault_[i];
        zs.enabled = driver_.config().zoneEnabled[i];
        zs.lockedOut = driver_.lockedOut(z);
    }
    st.masterEnabled = config_.master.enabled;
    st.masterOn = driver_.masterOn();

    st.queue.state = scheduler_.queueState();
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
    stateDirty_ = true;
}

void Controller::onMasterChanged(bool on)
{
    Event e = makeEvent(EventType::MasterChanged);
    e.master.on = on;
    publish(e);
    stateDirty_ = true;
}

void Controller::onProgramStarted(uint16_t programId, RunSource source, bool late)
{
    Event e = makeEvent(EventType::ProgramStarted);
    e.program = ProgramData{programId, source, SkipReason::NothingToRun, late, 0};
    publish(e);
}

void Controller::onProgramSkipped(uint16_t programId, SkipReason reason)
{
    Event e = makeEvent(EventType::ProgramSkipped);
    e.program = ProgramData{programId, RunSource::Schedule, reason, false, 0};
    publish(e);
}

void Controller::onProgramMissed(uint16_t programId, UtcSeconds scheduledAt)
{
    Event e = makeEvent(EventType::ProgramMissed);
    e.program = ProgramData{programId, RunSource::Schedule, SkipReason::NothingToRun, false, scheduledAt};
    publish(e);
}

void Controller::onQueueChanged()
{
    publish(makeEvent(EventType::QueueChanged));
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
