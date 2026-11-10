// SPDX-License-Identifier: Apache-2.0
// Scheduler engine: turns programs into a run queue and feeds the queue to
// the zone driver one entry at a time. Pure C++, no ESP-IDF.
//
// Time handling (details in docs/design/firmware-architecture.md):
//  - Start times are local wall-clock minutes, evaluated with TimeZone.
//    A start in a DST gap runs at the end of the gap; a start in a repeated
//    hour runs once, at the first occurrence.
//  - Every second the scheduler looks at the window (last evaluation, now].
//    A start in the window runs if it is no older than the missed-start grace
//    period; older starts are reported as missed. Only the newest start of a
//    program runs; earlier ones in the same window count as missed.
//  - Each program remembers the last start it consumed as a (local day,
//    minute) key, so a start never runs twice, even if the clock is set back
//    or the device reboots. The keys are persisted.
//  - A program with no stored key (never ran, or runtime state lost) gets
//    no catch-up: its key starts at the current minute.
//  - No scheduled start runs while the wall clock is not valid.
//
// Threads: control task only.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zf/sched/program.hpp"
#include "zf/time/clock.hpp"
#include "zf/time/time_zone.hpp"
#include "zf/zone/zone_types.hpp"

namespace zf {

enum class RunSource : uint8_t { Schedule, ManualProgram, ManualZone };

enum class QueueState : uint8_t { Idle, Running, Paused };

enum class SkipReason : uint8_t {
    RainDelay,
    SensorInhibit,
    NoAc,
    Maintenance,
    AlreadyQueued,
    QueueFull,
    NothingToRun,
};

enum class RunError : uint8_t {
    Ok,
    InvalidZone,
    UnknownProgram,
    ZeroDuration,
    QueueFull,
    AlreadyQueued,
    Inhibited,
    NothingToRun,
    LockedOut,   // zone locked out by a latched fault (RunZone)
    ZoneResting, // zone reached its maximum run time and is resting (RunZone)
};

// Inhibit flags (bitmask).
enum InhibitFlag : uint8_t {
    kInhibitSensor = 1u << 0,      // rain/spare sensor active: scheduled runs only
    kInhibitNoAc = 1u << 1,        // no 24 VAC: everything
    kInhibitMaintenance = 1u << 2, // OTA or service mode: everything
};

const char *toString(RunSource s);
const char *toString(QueueState s);
const char *toString(SkipReason r);
const char *toString(RunError e);

struct RunEntry {
    uint32_t id = 0;
    ZoneIndex zone = kNoZone;
    uint32_t durationSec = 0; // as queued (after seasonal adjustment)
    uint32_t remainingMs = 0; // updated on pause
    RunSource source = RunSource::ManualZone;
    uint16_t programId = 0; // 0 for manual zone runs
};

struct SchedulerConfig {
    ScheduleConfig schedule;
    uint8_t zoneCount = kMaxZones;
    std::array<bool, kMaxZones> zoneEnabled{};
};

// Persisted across reboots.
struct SchedulerPersist {
    struct Fired {
        uint16_t programId = 0;
        int64_t key = 0; // localDay * 1440 + minute of the last consumed start
    };
    UtcSeconds rainDelayUntil = 0;
    uint8_t count = 0;
    std::array<Fired, kMaxPrograms> fired{};
};

class ISchedulerListener {
public:
    virtual ~ISchedulerListener() = default;
    virtual void onProgramStarted(uint16_t programId, RunSource source, bool late) = 0;
    virtual void onProgramSkipped(uint16_t programId, SkipReason reason) = 0;
    virtual void onProgramMissed(uint16_t programId, UtcSeconds scheduledAt) = 0;
    virtual void onQueueChanged() = 0;
    virtual void onRainDelayChanged(UtcSeconds until) = 0;
    // SchedulerPersist changed and should be saved.
    virtual void onPersistDirty() = 0;
    // A queued entry reached the head of the queue and its zone refused to
    // start (locked out, resting, disabled, inhibited); the entry is dropped.
    virtual void onRunDropped(const RunEntry &entry, StartError reason)
    {
        (void)entry;
        (void)reason;
    }
};

class Scheduler {
public:
    // A start older than this is reported missed, never run, whatever the
    // grace setting. Also bounds the look-back after a forward time jump.
    static constexpr int64_t kLookBackSec = 24 * 3600;
    // Clock set back by more than this: the stored keys are treated as
    // wrong (clock was in the future) and clamped to now.
    static constexpr int64_t kLargeBackJumpSec = 24 * 3600;
    // A start that runs this much after its scheduled time is reported late.
    static constexpr int64_t kLateThresholdSec = 60;
    // Delay between a start time and its evaluation that still counts as
    // on time, whatever the grace setting (control loop jitter, flash stalls).
    static constexpr int64_t kOnTimeSlackSec = 10;
    static constexpr int64_t kNoKey = INT64_MIN;

    Scheduler(const IClock &clock, IZoneActuator &actuator);

    void setListener(ISchedulerListener *listener) { listener_ = listener; }

    // Applies configuration. Queued entries are kept unless their zone or
    // program no longer exists. After the first call, every program's key is
    // advanced to now so an edit never triggers a past start. A new time zone
    // moves each key to the same instant in the new zone, so a change of
    // zone neither skips nor repeats a start.
    void configure(const SchedulerConfig &cfg, const TimeZone &tz);
    void restore(const SchedulerPersist &state);
    SchedulerPersist persist() const;

    void setTimeValid(bool valid);
    bool timeValid() const { return timeValid_; }
    void setInhibit(uint8_t flags);
    uint8_t inhibit() const { return inhibit_; }

    // 0 clears. Activating a delay removes queued scheduled runs.
    void setRainDelayUntil(UtcSeconds until);
    UtcSeconds rainDelayUntil() const { return rainDelayUntil_; }
    bool rainDelayActive() const;

    // Manual runs ignore day rules, rain delay and sensor inhibit, but not
    // missing 24 VAC or maintenance. Manual program runs apply the seasonal
    // adjustment like scheduled runs.
    RunError runZone(ZoneIndex zone, uint32_t durationSec);
    RunError runProgram(uint16_t programId);

    void pause();
    void resume();
    void stop();
    void skip();

    // Call every control tick. Starts are evaluated once per UTC second.
    void tick();

    QueueState queueState() const { return state_; }
    std::size_t queueSize() const { return count_; }
    const RunEntry *entry(std::size_t i) const { return i < count_ ? &queue_[i] : nullptr; }
    bool programQueued(uint16_t programId) const;
    const Program *findProgram(uint16_t programId) const;
    uint32_t adjustedDurationSec(const Program &p, ZoneIndex zone) const;

    static int64_t makeKey(int64_t localDay, uint16_t minute) { return localDay * 1440 + minute; }

private:
    void evaluate(UtcSeconds now);
    void evaluateProgram(std::size_t index, UtcSeconds from, UtcSeconds now);
    void fireScheduled(const Program &p, bool late);
    RunError enqueueProgram(const Program &p, RunSource source);
    bool push(const RunEntry &e);
    void popFront();
    template <typename Pred> void removeIf(Pred pred);
    void processQueue();
    void stopFront();
    int64_t keyAt(UtcSeconds t) const;
    static int64_t keyIn(const TimeZone &tz, UtcSeconds t);
    void notifyQueue();

    const IClock &clock_;
    IZoneActuator &actuator_;
    ISchedulerListener *listener_ = nullptr;
    SchedulerConfig cfg_{};
    TimeZone tz_ = TimeZone::utc();
    bool configured_ = false;

    std::array<int64_t, kMaxPrograms> keys_{}; // parallel to cfg_.schedule.programs
    SchedulerPersist restored_{};
    bool haveRestored_ = false;

    bool timeValid_ = false;
    bool cursorValid_ = false;
    UtcSeconds cursor_ = 0;
    UtcSeconds lastEvalSecond_ = 0;
    uint8_t inhibit_ = 0;
    UtcSeconds rainDelayUntil_ = 0;
    bool rainDelayWasActive_ = false;

    std::array<RunEntry, kRunQueueCapacity> queue_{};
    std::size_t count_ = 0;
    uint32_t nextId_ = 1;
    QueueState state_ = QueueState::Idle;
};

} // namespace zf
