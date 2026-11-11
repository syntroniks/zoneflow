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
    case StartError::Resting:
        return "resting";
    case StartError::MasterLockedOut:
        return "master_locked_out";
    }
    return "?";
}

uint32_t zeroCrossWaitUs(const ZeroCrossSample &s, int32_t offsetUs)
{
    if (!s.present || s.intervalUs < kZeroCrossMinIntervalUs || s.intervalUs > kZeroCrossMaxIntervalUs) {
        return 0;
    }
    const uint32_t age = s.nowUs - s.lastEdgeUs; // wraps correctly
    if (age >= s.intervalUs * kZeroCrossMaxAgeIntervals) {
        return 0;
    }
    // Phase since the last target instant, in [0, interval).
    const auto interval = static_cast<int64_t>(s.intervalUs);
    int64_t phase = (static_cast<int64_t>(age) - offsetUs) % interval;
    if (phase < 0) {
        phase += interval;
    }
    return phase == 0 ? 0 : static_cast<uint32_t>(interval - phase);
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
    masterPending_ = false;
    masterCloseArmed_ = false;
    anyClosed_ = false;
    anyOn_ = false;
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
    cfg_.staggerMs = std::max(cfg_.staggerMs, kMinStaggerMs);
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
    if (cfg_.masterEnabled && masterLockedOut_) {
        return {StartError::MasterLockedOut, 0};
    }
    if (z.state != ValveState::Off) {
        return {StartError::AlreadyActive, 0};
    }
    if (activeCount() >= cfg_.maxConcurrent) {
        return {StartError::TooManyActive, 0};
    }
    const uint32_t requested = std::min(durationSec, effectiveMaxRunSec(zone));
    if (requested == 0) {
        return {StartError::ZeroDuration, 0};
    }
    // Spec section 2 rule 4, per zone: earlier runs without a rest in
    // between use up the same maximum.
    const MonoMs budget = budgetMs(zone, now);
    if (budget < kMinRunMs) {
        return {StartError::Resting, 0};
    }
    const MonoMs durationMs = std::min<MonoMs>(static_cast<MonoMs>(requested) * 1000u, budget);
    const auto accepted = static_cast<uint32_t>((durationMs + 999u) / 1000u);

    MonoMs openAt = now;
    if (cfg_.masterEnabled) {
        if (!masterOn_) {
            masterPending_ = true; // opens first, when power is ready
        } else if (masterOnAtMs_ + cfg_.masterPreOpenMs > now) {
            openAt = masterOnAtMs_ + cfg_.masterPreOpenMs;
        }
        masterCloseArmed_ = false;
    }
    if (anyClosed_) {
        openAt = std::max<MonoMs>(openAt, lastCloseMs_ + cfg_.interZoneDelayMs);
    }

    z.state = ValveState::Waiting;
    z.durationMs = static_cast<uint32_t>(durationMs);
    z.openAtMs = openAt;
    z.lastStop = StopReason::None;
    updateDemand();
    tryOpen(now);
    if (z.state == ValveState::Waiting && listener_ != nullptr) {
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
    hal_.emergencyAllOff();
}

void ZoneDriver::tryOpen(MonoMs now)
{
    if (!hal_.powerReady() || inhibit_ || emergency_.load(std::memory_order_acquire)) {
        return;
    }
    if (masterPending_) {
        if (!staggerOk(now)) {
            return;
        }
        masterPending_ = false;
        setMaster(true, now);
        for (Zone &z : zones_) {
            if (z.state == ValveState::Waiting) {
                z.openAtMs = std::max<MonoMs>(z.openAtMs, now + cfg_.masterPreOpenMs);
            }
        }
    }
    for (std::size_t i = 0; i < cfg_.zoneCount; ++i) {
        Zone &z = zones_[i];
        if (z.state != ValveState::Waiting || now < z.openAtMs) {
            continue;
        }
        if (!staggerOk(now)) {
            return; // one output per stagger interval; the next tick tries again
        }
        openZone(static_cast<ZoneIndex>(i), now);
    }
}

uint8_t ZoneDriver::firstPendingBit() const
{
    if (masterPending_) {
        return kMasterBit;
    }
    uint8_t best = 0xFF;
    MonoMs bestAt = 0;
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        const Zone &z = zones_[i];
        if (z.state == ValveState::Waiting && (best == 0xFF || z.openAtMs < bestAt)) {
            best = static_cast<uint8_t>(i);
            bestAt = z.openAtMs;
        }
    }
    return best;
}

std::array<ZoneBudget, kMaxZones> ZoneDriver::exportBudget() const
{
    const MonoMs now = clock_.monoMs();
    std::array<ZoneBudget, kMaxZones> out{};
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        const Zone &z = zones_[i];
        MonoMs burst = z.burstMs;
        MonoMs closed = 0;
        if (z.state == ValveState::Open) {
            burst += now - z.openedAtMs;
        } else if (burst > 0) {
            closed = closedFor(z, now);
        }
        out[i].burstMs = static_cast<uint32_t>(std::min<MonoMs>(burst, 0xFFFFFFFFu));
        out[i].closedForMs = static_cast<uint32_t>(std::min<MonoMs>(closed, 0xFFFFFFFFu));
    }
    return out;
}

void ZoneDriver::restoreBudget(const std::array<ZoneBudget, kMaxZones> &budget)
{
    const MonoMs now = clock_.monoMs();
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        Zone &z = zones_[i];
        if (z.state != ValveState::Off) {
            continue;
        }
        z.burstMs = budget[i].burstMs;
        z.closedAtMs = now;
        z.restCreditMs = budget[i].closedForMs;
    }
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

    // Power withdrawn under open outputs (rev A: the output stage tripped
    // or failed; the controller normally reacts first).
    if (hal_.ownsKeepAlive() && anyOutputOn() && !hal_.powerReady()) {
        forceAllOff(StopReason::Fault, now);
    }
    updateDemand();
    tryOpen(now);

    for (std::size_t i = 0; i < cfg_.zoneCount; ++i) {
        const auto zone = static_cast<ZoneIndex>(i);
        Zone &z = zones_[i];
        if (z.state == ValveState::Open) {
            const MonoMs elapsed = now - z.openedAtMs;
            if (z.burstMs + elapsed >= maxRunMs(zone)) {
                // The zone's maximum is used up (spec section 2 rule 4). Also
                // a second, independent check of the clamp in start().
                closeZone(zone, StopReason::MaxRunTime, now);
            } else if (elapsed >= z.durationMs) {
                closeZone(zone, StopReason::Completed, now);
            }
        }
    }

    updateMaster(now);
    updateDemand();
    oe_.heartbeat(static_cast<uint32_t>(now));
}

void ZoneDriver::waitForZeroCross()
{
    if (zeroCross_ == nullptr || !cfg_.zeroCrossSwitching) {
        return;
    }
    const uint32_t wait = zeroCrossWaitUs(zeroCross_->sample(), zeroCrossOffsetUs_);
    if (wait > 0) {
        zeroCross_->delayUs(wait);
    }
}

void ZoneDriver::openZone(ZoneIndex zone, MonoMs now)
{
    Zone &z = zones_[zone];
    waitForZeroCross();
    hal_.setZone(zone, true);
    if (!hal_.powerReady()) {
        // The output stage failed this write (rev A: read-back, bus) and has
        // stopped the keep-alive. The zone never opened; do not report it
        // open until the controller catches up on the next tick.
        forceAllOff(StopReason::Fault, now);
        return;
    }
    z.state = ValveState::Open;
    z.openedAtMs = now;
    lastChangeMs_ = now;
    lastOpenedBit_ = zone;
    anyOn_ = true;
    lastOnMs_ = now;
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
        z.burstMs += now - z.openedAtMs;
        z.closedAtMs = now;
        z.restCreditMs = 0;
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
    if (on) {
        waitForZeroCross(); // turning off needs no alignment (triac)
    }
    hal_.setMaster(on);
    if (on && !hal_.powerReady()) {
        forceAllOff(StopReason::Fault, now); // as in openZone()
        return;
    }
    masterOn_ = on;
    lastChangeMs_ = now;
    if (on) {
        lastOpenedBit_ = kMasterBit;
        masterOnAtMs_ = now;
        anyOn_ = true;
        lastOnMs_ = now;
    }
    updateDemand();
    if (listener_ != nullptr) {
        listener_->onMasterChanged(on);
    }
}

void ZoneDriver::updateMaster(MonoMs now)
{
    if (masterPending_ && activeCount() == 0) {
        masterPending_ = false; // the zone that wanted it went away first
    }
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
            z.burstMs += now - z.openedAtMs;
            z.closedAtMs = now;
            z.restCreditMs = 0;
        }
        z.state = ValveState::Off;
        z.lastStop = reason;
        if (listener_ != nullptr) {
            listener_->onZoneChanged(static_cast<ZoneIndex>(i), ValveState::Off, reason);
        }
    }
    masterCloseArmed_ = false;
    masterPending_ = false;
    lastChangeMs_ = now;
    if (masterOn_) {
        masterOn_ = false;
        if (listener_ != nullptr) {
            listener_->onMasterChanged(false);
        }
    }
    updateDemand();
}

void ZoneDriver::updateDemand()
{
    const bool want = masterOn_ || masterPending_ || activeCount() > 0;
    hal_.requestPower(want, firstPendingBit());
    if (!hal_.ownsKeepAlive()) {
        oe_.setDemand(want);
    }
}

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
    masterLockedOut_ = false;
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
    if (zone >= kMaxZones) {
        return StartError::Ok;
    }
    if (zones_[zone].lockedOut) {
        return StartError::LockedOut;
    }
    if (cfg_.masterEnabled && masterLockedOut_) {
        return StartError::MasterLockedOut;
    }
    return restLeftMs(zone, clock_.monoMs()) > 0 ? StartError::Resting : StartError::Ok;
}

MonoMs ZoneDriver::budgetMs(ZoneIndex zone, MonoMs now)
{
    Zone &z = zones_[zone];
    if (z.state == ValveState::Off && z.burstMs > 0 &&
        closedFor(z, now) >= static_cast<MonoMs>(cfg_.minRestSec) * 1000u) {
        z.burstMs = 0;
        z.restCreditMs = 0;
    }
    const MonoMs max = maxRunMs(zone);
    return z.burstMs >= max ? 0 : max - z.burstMs;
}

MonoMs ZoneDriver::restLeftMs(ZoneIndex zone, MonoMs now) const
{
    const Zone &z = zones_[zone];
    if (z.state != ValveState::Off || z.burstMs + kMinRunMs <= maxRunMs(zone)) {
        return 0;
    }
    const MonoMs rest = static_cast<MonoMs>(cfg_.minRestSec) * 1000u;
    const MonoMs closed = closedFor(z, now);
    return closed >= rest ? 0 : rest - closed;
}

uint32_t ZoneDriver::restRemainingMs(ZoneIndex zone) const
{
    return zone < kMaxZones ? static_cast<uint32_t>(restLeftMs(zone, clock_.monoMs())) : 0;
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
