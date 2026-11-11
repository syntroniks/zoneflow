// SPDX-License-Identifier: Apache-2.0
// Controller: owns the zone driver, scheduler, sensor and diagnostics logic,
// and is the single writer of SystemState and the single publisher on the
// EventBus. Pure C++; the device runs it from the control task, host tests
// run it directly.
//
// Threads: every method except emergencyOff() runs on the control task.
#pragma once

#include <array>
#include <cstdint>

#include "zf/app/persist.hpp"
#include "zf/app/retained.hpp"
#include "zf/config/config_store.hpp"
#include "zf/diag/current_monitor.hpp"
#include "zf/diag/current_sense.hpp"
#include "zf/diag/sensors.hpp"
#include "zf/diag/zone_fault_policy.hpp"
#include "zf/events/command.hpp"
#include "zf/events/event_bus.hpp"
#include "zf/events/state.hpp"
#include "zf/hal.hpp"
#include "zf/sched/scheduler.hpp"
#include "zf/time/clock.hpp"
#include "zf/zone/output_enable.hpp"
#include "zf/zone/zone_driver.hpp"

namespace zf {

struct ControllerDeps {
    const IClock &clock;
    IOutputHal &outputs;
    ISensorHal &sensors;
    OutputEnableGuard &outputEnable;
    EventBus &events;
    StateStore &state;
    ConfigStore &config;
    // Read once in begin(). Writes go through persistMailbox() and a writer
    // task, never from the control task. May be null (no persistence).
    IBlobStorage *runtimeStorage;
    // State kept over a software restart (run budgets, latched output-stage
    // fault, trips of the last day). May be null.
    IRetainedStore *retained = nullptr;
};

class Controller final : private IZoneDriverListener, private ISchedulerListener {
public:
    // Runtime state is written at most this often, except that a consumed
    // scheduled start is posted on the tick that consumed it.
    static constexpr uint32_t kPersistMinIntervalMs = 2000;
    // A snapshot is published at least this often.
    static constexpr uint32_t kSnapshotIntervalMs = 1000;
    // Hardware design 19.8: the output switched on within this long before a
    // trip is the faulted one; otherwise every output that was on is suspect.
    static constexpr uint32_t kTripBlameMs = 500;
    // ... and an output with this many trips within a day stays off until a
    // person clears the faults.
    static constexpr uint8_t kTripsPerDay = 3;
    static constexpr int64_t kDayMs = 24ll * 3600 * 1000;
    // Plausible mains frequency from the zero-cross signal (hardware design 19.9).
    static constexpr uint32_t kMinMainsMilliHz = 45000;
    static constexpr uint32_t kMaxMainsMilliHz = 66000;
    // A polling cycle for an unexplained flow change at most this often.
    static constexpr uint32_t kFlowPollMinIntervalMs = 60000;

    explicit Controller(const ControllerDeps &deps);

    // Outputs off, runtime state restored, configuration applied.
    void begin();

    // Applies the ConfigStore's current configuration if its revision moved.
    // tick() calls it too, so a stored configuration takes effect even when
    // its ReloadConfig command could not be queued.
    void reloadConfig();

    void setTimeStatus(TimeStatus status);

    // Returns false if the command was rejected. lastRunError() holds the
    // reason and a CommandRejected event carries it to other modules.
    bool handle(const Command &cmd);

    // One control step. Call every 20..100 ms.
    void tick();

    // Before begin(): the scale of the board's current front end.
    void configureCurrentSense(const CurrentSenseConfig &cfg) { currentSense_.configure(cfg); }

    // Optional, before begin(): a zero-cross input for driver.zero_cross_switching.
    void attachZeroCross(IZeroCrossHal *zc, int32_t offsetUs) { driver_.setZeroCross(zc, offsetUs); }

    // Any context: outputs off and OE tripped. For shutdown/OTA/panic paths.
    void emergencyOff() { driver_.emergencyOff(); }

    // Control task: report a fault found outside the controller (config
    // rejected at boot, storage failure). Latched until ClearFaults.
    void reportFault(FaultCode code) { raiseFault(code, kNoZone); }

    // Runtime state to store. The device drains it from a low-priority
    // writer task (persistWriteOnce); host tests call that directly.
    PersistMailbox &persistMailbox() { return persist_; }

    const ZoneDriver &driver() const { return driver_; }
    const Scheduler &scheduler() const { return scheduler_; }
    // How the two records with two copies were read at begin().
    RecordLoad runtimeLoad() const { return runtimeLoad_; }
    RecordLoad budgetLoad() const { return budgetLoad_; }
    uint32_t activeFaults() const { return activeFaults_; }
    bool maintenance() const { return maintenance_; }
    RunError lastRunError() const { return lastRunError_; }

private:
    // IZoneDriverListener
    void onZoneChanged(ZoneIndex zone, ValveState state, StopReason reason) override;
    void onMasterChanged(bool on) override;
    // ISchedulerListener
    void onProgramStarted(uint16_t programId, RunSource source, bool late) override;
    void onProgramSkipped(uint16_t programId, SkipReason reason) override;
    void onProgramMissed(uint16_t programId, UtcSeconds scheduledAt) override;
    void onQueueChanged() override;
    void onRainDelayChanged(UtcSeconds until) override;
    void onPersistDirty() override { persistDirty_ = true; }
    void onRunDropped(const RunEntry &entry, StartError reason) override;
    void onPauseExpired() override;

    bool apply(const Command &cmd);
    void applyConfig(const Config &cfg);
    void updateInhibit();
    void handleCurrent(const CurrentVerdict &v);
    void handleRise(MonoMs now);
    void handleTrip(bool tripped, MonoMs now);
    void onTrip(MonoMs now);
    bool recordTrip(uint8_t bit, MonoMs now); // true: limit reached
    void handleStage(MonoMs now);
    void handleDc(const CurrentReading &r);
    void checkCondition(FaultCode code, bool present);
    void acknowledgeHardware(bool repaired);
    void saveRetained(MonoMs now);
    bool restoreRetained(MonoMs now); // false: no intact record
    void restoreBudgetFromFlash();
    void postBudget(MonoMs now);
    void raiseFault(FaultCode code, ZoneIndex zone, uint8_t detail);
    void recordMasterFault();
    void raiseFault(FaultCode code, ZoneIndex zone);
    void clearCondition(FaultCode code);
    void clearFaults();
    void savePersist(MonoMs now);
    void publishSnapshot(MonoMs now);
    Event makeEvent(EventType type) const;
    void publish(const Event &e);

    ControllerDeps deps_;
    ZoneDriver driver_;
    Scheduler scheduler_;
    ContactSensor rain_;
    ContactSensor spare_;
    AcMonitor ac_;
    FlowMeter flow_;
    CurrentMonitor current_;

    Config config_{};
    uint32_t configRevision_ = 0;
    TimeStatus timeStatus_ = TimeStatus::Unset;
    bool maintenance_ = false;
    uint32_t activeFaults_ = 0;
    std::array<FaultCode, kMaxZones> zoneFault_{};
    int32_t lastCurrentMa_ = -1;
    MonoMs leakSinceMs_ = 0;
    bool leakTiming_ = false;
    RunError lastRunError_ = RunError::Ok;
    // Master lockout: consecutive current faults attributed to the master.
    // A master session (on to off) of at least settle time without such a
    // fault resets the count.
    uint8_t masterFaults_ = 0;
    bool masterFaultInSession_ = false;
    bool masterOpenCircuit_ = false; // the master drew no current: ZoneOpenCircuit stays until cleared
    // Current rose with no output switched: an output device, or the zone?
    ShortedOutputCheck riseCheck_;
    MonoMs masterOnSinceMs_ = 0;
    // Hardware over-current latch.
    bool tripSeen_ = false;
    // Trips per output (zones, then the master): times of the last few,
    // INT64_MIN = none. Signed so that trips from before a restart fit.
    std::array<std::array<int64_t, kTripsKept>, kMaxZones + 1> tripAtMs_{};
    int32_t lastTripCurrentMa_ = -1;

    // Current measurement from raw windows (rev A).
    CurrentSense currentSense_;
    CurrentReading reading_{};
    uint32_t lastWindowSeq_ = 0;
    uint8_t dcWindows_ = 0;

    // Output stage.
    uint32_t stageFaultSeq_ = 0;
    uint32_t stageTripSeq_ = 0;
    uint8_t boardZones_ = 0; // 0 = not known
    bool hwFault_ = false;   // the stage is in its fault state
    OutputStageStatus stage_{};
    uint32_t mainsMilliHz_ = 0;
    bool sensorSupplyOn_ = false;
    bool flowing_ = false;
    MonoMs lastFlowPollMs_ = 0;
    bool flowPollArmed_ = false;

    // The run each zone has open, for RunEnded (history).
    struct OpenRun {
        bool open = false;
        MonoMs openedMs = 0;
        uint16_t plannedSec = 0;
        RunSource source = RunSource::ManualZone;
        uint16_t programId = 0;
        uint64_t startPulses = 0;
    };
    std::array<OpenRun, kMaxZones> runs_{};
    void trackRun(ZoneIndex zone, ValveState state, StopReason reason);

    PersistMailbox persist_;
    // The run budget in flash (zf/app/budget.hpp).
    BudgetPoster budgetPoster_;
    RecordLoad runtimeLoad_ = RecordLoad::None;
    RecordLoad budgetLoad_ = RecordLoad::None;
    bool persistDirty_ = false;
    bool persistUrgent_ = false; // a scheduled start was consumed: store it now
    MonoMs lastPersistMs_ = 0;
    bool stateDirty_ = true;
    MonoMs lastSnapshotMs_ = 0;
};

} // namespace zf
