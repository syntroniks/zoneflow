// SPDX-License-Identifier: Apache-2.0
#include "zf/zone/zone_driver.hpp"

#include <algorithm>

namespace zf {

const char *toString(ValveState s)
{
    switch (s) {
    case ValveState::Off:
        return "off";
    case ValveState::Waiting:
        return "waiting";
    case ValveState::Open:
        return "open";
    }
    return "?";
}

const char *toString(StopReason r)
{
    switch (r) {
    case StopReason::None:
        return "none";
    case StopReason::Completed:
        return "completed";
    case StopReason::Requested:
        return "requested";
    case StopReason::MaxRunTime:
        return "max_run_time";
    case StopReason::Fault:
        return "fault";
    case StopReason::AcLost:
        return "ac_lost";
    case StopReason::Shutdown:
        return "shutdown";
    case StopReason::Maintenance:
        return "maintenance";
    }
    return "?";
}

const char *toString(StartError e)
{
    switch (e) {
    case StartError::Ok:
        return "ok";
    case StartError::InvalidZone:
        return "invalid_zone";
    case StartError::Disabled:
        return "disabled";
    case StartError::LockedOut:
        return "locked_out";
    case StartError::Inhibited:
        return "inhibited";
    case StartError::TooManyActive:
        return "too_many_active";
    case StartError::AlreadyActive:
        return "already_active";
    case StartError::ZeroDuration:
        return "zero_duration";
    }
    return "?";
}

ZoneDriver::ZoneDriver(IOutputHal &hal, OutputEnableGuard &oe, const IClock &clock) : hal_(hal), oe_(oe), clock_(clock)
{
}

void ZoneDriver::init(const ZoneDriverConfig &cfg)
{
    hal_.allOff();
    for (auto &z : zones_) {
        z = Zone{};
    }
    masterOn_ = false;
    masterCloseArmed_ = false;
    anyClosed_ = false;
    lastChangeMs_ = clock_.monoMs();
    applyConfig(cfg);
    updateDemand();
}

void ZoneDriver::reconfigure(const ZoneDriverConfig &cfg)
{
    stopAll(StopReason::Requested);
    applyConfig(cfg);
}

void ZoneDriver::applyConfig(const ZoneDriverConfig &cfg)
{
    cfg_ = cfg;
    cfg_.zoneCount = std::min<uint8_t>(cfg_.zoneCount, kMaxZones);
    cfg_.maxConcurrent = std::clamp<uint8_t>(cfg_.maxConcurrent, 1, kMaxConcurrentZonesLimit);
    for (std::size_t z = cfg_.zoneCount; z < kMaxZones; ++z) {
        cfg_.zoneEnabled[z] = false;
    }
}

uint32_t ZoneDriver::effectiveMaxRunSec(ZoneIndex zone) const
{
    if (zone >= kMaxZones) {
        return 0;
    }
    const uint32_t configured = cfg_.maxRunSec[zone];
    return (configured == 0 || configured > kHardMaxRunSec) ? kHardMaxRunSec : configured;
}

StartResult ZoneDriver::start(ZoneIndex zone, uint32_t durationSec)
{
    const MonoMs now = clock_.monoMs();
    if (zone >= cfg_.zoneCount) {
        return {StartError::InvalidZone, 0};
    }
    if (!cfg_.zoneEnabled[zone]) {
        return {StartError::Disabled, 0};
    }
    Zone &z = zones_[zone];
    if (z.lockedOut) {
        return {StartError::LockedOut, 0};
    }
    if (inhibit_ || emergency_.load(std::memory_order_acquire) || oe_.tripped()) {
        return {StartError::Inhibited, 0};
    }
    if (z.state != ValveState::Off) {
        return {StartError::AlreadyActive, 0};
    }
    if (activeCount() >= cfg_.maxConcurrent) {
        return {StartError::TooManyActive, 0};
    }
    const uint32_t accepted = std::min(durationSec, effectiveMaxRunSec(zone));
    if (accepted == 0) {
        return {StartError::ZeroDuration, 0};
    }

    MonoMs openAt = now;
    if (cfg_.masterEnabled) {
        if (!masterOn_) {
            setMaster(true, now);
            openAt = now + cfg_.masterPreOpenMs;
        }
        masterCloseArmed_ = false;
    }
    if (anyClosed_) {
        openAt = std::max<MonoMs>(openAt, lastCloseMs_ + cfg_.interZoneDelayMs);
    }

    z.state = ValveState::Waiting;
    z.durationMs = accepted * 1000u;
    z.openAtMs = openAt;
    z.lastStop = StopReason::None;
    updateDemand();
    if (openAt <= now) {
        openZone(zone, now);
    } else if (listener_ != nullptr) {
        listener_->onZoneChanged(zone, ValveState::Waiting, StopReason::None);
    }
    return {StartError::Ok, accepted};
}

void ZoneDriver::stop(ZoneIndex zone, StopReason reason)
{
    if (zone >= kMaxZones || zones_[zone].state == ValveState::Off) {
        return;
    }
    closeZone(zone, reason, clock_.monoMs());
}

void ZoneDriver::stopAll(StopReason reason) { forceAllOff(reason, clock_.monoMs()); }

void ZoneDriver::emergencyOff()
{
    emergency_.store(true, std::memory_order_release);
    oe_.trip();
    hal_.allOff();
}

void ZoneDriver::tick()
{
    const MonoMs now = clock_.monoMs();

    if (emergency_.load(std::memory_order_acquire)) {
        inhibit_ = true;
        if (anyOutputOn() || activeCount() > 0) {
            forceAllOff(StopReason::Shutdown, now);
        }
        hal_.allOff();
    }

    for (std::size_t i = 0; i < cfg_.zoneCount; ++i) {
        const auto zone = static_cast<ZoneIndex>(i);
        Zone &z = zones_[i];
        if (z.state == ValveState::Waiting && now >= z.openAtMs) {
            openZone(zone, now);
        } else if (z.state == ValveState::Open) {
            const MonoMs elapsed = now - z.openedAtMs;
            if (elapsed >= z.durationMs) {
                closeZone(zone, StopReason::Completed, now);
            } else if (elapsed >= static_cast<MonoMs>(effectiveMaxRunSec(zone)) * 1000u) {
                // Unreachable while durationMs is clamped; kept as a second,
                // independent check of spec section 2 rule 4.
                closeZone(zone, StopReason::MaxRunTime, now);
            }
        }
    }

    updateMaster(now);
    updateDemand();
    oe_.heartbeat(static_cast<uint32_t>(now));
}

void ZoneDriver::openZone(ZoneIndex zone, MonoMs now)
{
    Zone &z = zones_[zone];
    hal_.setZone(zone, true);
    z.state = ValveState::Open;
    z.openedAtMs = now;
    lastChangeMs_ = now;
    lastOpenedBit_ = zone;
    updateDemand();
    if (listener_ != nullptr) {
        listener_->onZoneChanged(zone, ValveState::Open, StopReason::None);
    }
}

void ZoneDriver::closeZone(ZoneIndex zone, StopReason reason, MonoMs now)
{
    Zone &z = zones_[zone];
    hal_.setZone(zone, false);
    if (z.state == ValveState::Open) {
        lastChangeMs_ = now;
        anyClosed_ = true;
        lastCloseMs_ = now;
    }
    z.state = ValveState::Off;
    z.lastStop = reason;
    if (listener_ != nullptr) {
        listener_->onZoneChanged(zone, ValveState::Off, reason);
    }
    updateMaster(now);
    updateDemand();
}

void ZoneDriver::setMaster(bool on, MonoMs now)
{
    hal_.setMaster(on);
    masterOn_ = on;
    lastChangeMs_ = now;
    if (on) {
        lastOpenedBit_ = kMasterBit;
    }
    updateDemand();
    if (listener_ != nullptr) {
        listener_->onMasterChanged(on);
    }
}

void ZoneDriver::updateMaster(MonoMs now)
{
    if (!masterOn_) {
        return;
    }
    if (activeCount() > 0) {
        masterCloseArmed_ = false;
        return;
    }
    if (!masterCloseArmed_) {
        masterCloseArmed_ = true;
        masterCloseAtMs_ = now + cfg_.masterPostCloseMs;
    }
    if (now >= masterCloseAtMs_) {
        masterCloseArmed_ = false;
        setMaster(false, now);
    }
}

void ZoneDriver::forceAllOff(StopReason reason, MonoMs now)
{
    hal_.allOff();
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        Zone &z = zones_[i];
        if (z.state == ValveState::Off) {
            continue;
        }
        if (z.state == ValveState::Open) {
            anyClosed_ = true;
            lastCloseMs_ = now;
        }
        z.state = ValveState::Off;
        z.lastStop = reason;
        if (listener_ != nullptr) {
            listener_->onZoneChanged(static_cast<ZoneIndex>(i), ValveState::Off, reason);
        }
    }
    masterCloseArmed_ = false;
    lastChangeMs_ = now;
    if (masterOn_) {
        masterOn_ = false;
        if (listener_ != nullptr) {
            listener_->onMasterChanged(false);
        }
    }
    updateDemand();
}

void ZoneDriver::updateDemand() { oe_.setDemand(masterOn_ || activeCount() > 0); }

void ZoneDriver::setLockout(ZoneIndex zone, bool locked)
{
    if (zone < kMaxZones) {
        zones_[zone].lockedOut = locked;
    }
}

bool ZoneDriver::lockedOut(ZoneIndex zone) const { return zone < kMaxZones && zones_[zone].lockedOut; }

void ZoneDriver::clearLockouts()
{
    for (auto &z : zones_) {
        z.lockedOut = false;
    }
}

ValveState ZoneDriver::state(ZoneIndex zone) const { return zone < kMaxZones ? zones_[zone].state : ValveState::Off; }

uint32_t ZoneDriver::remainingMs(ZoneIndex zone) const
{
    if (zone >= kMaxZones) {
        return 0;
    }
    const Zone &z = zones_[zone];
    switch (z.state) {
    case ValveState::Off:
        return 0;
    case ValveState::Waiting:
        return z.durationMs;
    case ValveState::Open: {
        const MonoMs elapsed = clock_.monoMs() - z.openedAtMs;
        return elapsed >= z.durationMs ? 0 : static_cast<uint32_t>(z.durationMs - elapsed);
    }
    }
    return 0;
}

StartError ZoneDriver::startBlocker(ZoneIndex zone) const
{
    if (zone < kMaxZones && zones_[zone].lockedOut) {
        return StartError::LockedOut;
    }
    return StartError::Ok;
}

StopReason ZoneDriver::lastStopReason(ZoneIndex zone) const
{
    return zone < kMaxZones ? zones_[zone].lastStop : StopReason::None;
}

uint8_t ZoneDriver::activeCount() const
{
    uint8_t n = 0;
    for (const auto &z : zones_) {
        n += z.state != ValveState::Off ? 1 : 0;
    }
    return n;
}

bool ZoneDriver::anyOutputOn() const
{
    if (masterOn_) {
        return true;
    }
    return std::any_of(zones_.begin(), zones_.end(), [](const Zone &z) { return z.state == ValveState::Open; });
}

uint16_t ZoneDriver::outputMask() const
{
    uint16_t mask = masterOn_ ? static_cast<uint16_t>(1u << kMasterBit) : 0;
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        if (zones_[i].state == ValveState::Open) {
            mask |= static_cast<uint16_t>(1u << i);
        }
    }
    return mask;
}

} // namespace zf
