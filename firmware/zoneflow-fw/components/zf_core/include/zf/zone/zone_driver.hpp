// SPDX-License-Identifier: Apache-2.0
// Zone driver: the only code that switches valve outputs. Implements the
// firmware half of system spec section 2.
//
// Guarantees:
//  - All outputs off at init(), on reconfigure() and on every fault path.
//  - A zone never stays open longer than min(zone max run time,
//    kHardMaxRunSec), whatever the caller asked for. The limit is per zone,
//    not per run: on-time adds up over back-to-back runs until the zone has
//    been closed for minRestSec. A zone that reached the limit refuses to
//    start (StartError::Resting) until it has been closed for minRestSec.
//  - At most maxConcurrent zones are open or waiting (ceiling
//    kMaxConcurrentZonesLimit). The master output is extra.
//  - Master valve opens masterPreOpenMs before the first zone and closes
//    masterPostCloseMs after the last zone.
//  - A zone opens no sooner than interZoneDelayMs after the previous close.
//
// Threads: every method except emergencyOff() belongs to the control task.
// emergencyOff() may run in any context (shutdown handler, other task).
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "zf/hal.hpp"
#include "zf/time/clock.hpp"
#include "zf/zone/output_enable.hpp"
#include "zf/zone/zone_types.hpp"

namespace zf {

struct ZoneDriverConfig {
    uint8_t zoneCount = kMaxZones;
    uint8_t maxConcurrent = kMaxConcurrentZonesLimit;
    bool masterEnabled = false;
    uint32_t masterPreOpenMs = 0;
    uint32_t masterPostCloseMs = 0;
    uint32_t interZoneDelayMs = 0;
    // A zone that has been open for its maximum run time, counting every run
    // with less than this much closed time in between, stays closed for this
    // long (spec section 2 rule 4).
    uint32_t minRestSec = 1800;
    std::array<bool, kMaxZones> zoneEnabled{};
    std::array<uint32_t, kMaxZones> maxRunSec{};
};

class IZoneDriverListener {
public:
    virtual ~IZoneDriverListener() = default;
    // Called on the control task after the outputs changed. Must not call
    // back into the driver.
    virtual void onZoneChanged(ZoneIndex zone, ValveState state, StopReason reason) = 0;
    virtual void onMasterChanged(bool on) = 0;
};

class ZoneDriver final : public IZoneActuator {
public:
    ZoneDriver(IOutputHal &hal, OutputEnableGuard &oe, const IClock &clock);

    void setListener(IZoneDriverListener *listener) { listener_ = listener; }

    // Forces all outputs off, then applies the configuration.
    void init(const ZoneDriverConfig &cfg);
    // Stops everything (StopReason::Requested), then applies the configuration.
    void reconfigure(const ZoneDriverConfig &cfg);

    StartResult start(ZoneIndex zone, uint32_t durationSec) override;
    void stop(ZoneIndex zone, StopReason reason) override;
    ValveState state(ZoneIndex zone) const override;
    uint32_t remainingMs(ZoneIndex zone) const override;
    StartError startBlocker(ZoneIndex zone) const override;
    // Time left before a zone that reached its maximum run time may open
    // again; 0 when it is not resting.
    uint32_t restRemainingMs(ZoneIndex zone) const;

    // Turns every output off at once, master included, skipping the master
    // post-close delay.
    void stopAll(StopReason reason);

    // Any context: outputs off and OE tripped, without touching driver
    // state or calling the listener. The next tick() on the control task
    // reconciles the state.
    void emergencyOff();

    // Advances timers. Call every control tick (tens of milliseconds).
    void tick();

    // Blocks new starts (maintenance, OTA, no 24 VAC). Does not stop
    // running zones; callers decide that.
    void setInhibit(bool inhibit) { inhibit_ = inhibit; }
    bool inhibited() const { return inhibit_; }

    // A locked-out zone refuses to start until cleared (latched fault).
    void setLockout(ZoneIndex zone, bool locked);
    bool lockedOut(ZoneIndex zone) const;
    void clearLockouts(); // zones and master

    // A locked-out master refuses to open, so with the master enabled no zone
    // starts (StartError::MasterLockedOut).
    void setMasterLockout(bool locked) { masterLockedOut_ = locked; }
    bool masterLockedOut() const { return masterLockedOut_; }

    bool masterOn() const { return masterOn_; }
    uint8_t activeCount() const;
    bool anyOutputOn() const;
    uint32_t effectiveMaxRunSec(ZoneIndex zone) const;
    StopReason lastStopReason(ZoneIndex zone) const;
    const ZoneDriverConfig &config() const { return cfg_; }

    // For diagnostics: bit z = zone z open, bit kMaxZones = master on.
    uint16_t outputMask() const;
    MonoMs lastOutputChangeMs() const { return lastChangeMs_; }
    // Bit index (as in outputMask) of the output most recently switched on.
    uint8_t lastOpenedBit() const { return lastOpenedBit_; }
    // When the output (bit as in outputMask) last switched on.
    MonoMs openedAtMs(uint8_t bit) const
    {
        return bit == kMasterBit ? masterOnAtMs_ : (bit < kMaxZones ? zones_[bit].openedAtMs : 0);
    }

    static constexpr uint8_t kMasterBit = kMaxZones;

private:
    struct Zone {
        ValveState state = ValveState::Off;
        uint32_t durationMs = 0;
        MonoMs openAtMs = 0;
        MonoMs openedAtMs = 0;
        StopReason lastStop = StopReason::None;
        bool lockedOut = false;
        // On-time since the zone last had minRestSec of closed time.
        MonoMs burstMs = 0;
        MonoMs closedAtMs = 0;
    };

    // Smallest run worth starting; a smaller remainder counts as exhausted.
    static constexpr MonoMs kMinRunMs = 1000;

    MonoMs maxRunMs(ZoneIndex zone) const { return static_cast<MonoMs>(effectiveMaxRunSec(zone)) * 1000u; }
    // Clears the on-time of a zone that has rested; returns the run time
    // still allowed now.
    MonoMs budgetMs(ZoneIndex zone, MonoMs now);
    MonoMs restLeftMs(ZoneIndex zone, MonoMs now) const;

    void applyConfig(const ZoneDriverConfig &cfg);
    void openZone(ZoneIndex z, MonoMs now);
    void closeZone(ZoneIndex z, StopReason reason, MonoMs now);
    void setMaster(bool on, MonoMs now);
    void updateMaster(MonoMs now);
    void updateDemand();
    void forceAllOff(StopReason reason, MonoMs now);

    IOutputHal &hal_;
    OutputEnableGuard &oe_;
    const IClock &clock_;
    IZoneDriverListener *listener_ = nullptr;
    ZoneDriverConfig cfg_{};
    std::array<Zone, kMaxZones> zones_{};
    bool masterOn_ = false;
    MonoMs masterOnAtMs_ = 0;
    bool masterCloseArmed_ = false;
    MonoMs masterCloseAtMs_ = 0;
    bool anyClosed_ = false;
    MonoMs lastCloseMs_ = 0;
    MonoMs lastChangeMs_ = 0;
    uint8_t lastOpenedBit_ = 0xFF;
    bool inhibit_ = false;
    bool masterLockedOut_ = false;
    std::atomic<bool> emergency_{false};
};

} // namespace zf
