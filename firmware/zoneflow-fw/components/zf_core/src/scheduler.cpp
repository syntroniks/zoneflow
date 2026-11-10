// SPDX-License-Identifier: Apache-2.0
#include "zf/sched/scheduler.hpp"

#include <algorithm>

#include "zf/time/civil.hpp"

namespace zf {

bool DayRule::matches(int64_t daySerial) const
{
    switch (type) {
    case DayRuleType::Weekdays:
        return (weekdayMask & (1u << civil::weekdayFromDays(daySerial))) != 0;
    case DayRuleType::Interval:
        return intervalDays > 0 && civil::floorMod(daySerial - intervalAnchorDay, intervalDays) == 0;
    case DayRuleType::OddDays:
        return civil::civilFromDays(daySerial).day % 2 == 1;
    case DayRuleType::EvenDays:
        return civil::civilFromDays(daySerial).day % 2 == 0;
    }
    return false;
}

const char *toString(RunSource s)
{
    switch (s) {
    case RunSource::Schedule:
        return "schedule";
    case RunSource::ManualProgram:
        return "manual_program";
    case RunSource::ManualZone:
        return "manual_zone";
    }
    return "?";
}

const char *toString(QueueState s)
{
    switch (s) {
    case QueueState::Idle:
        return "idle";
    case QueueState::Running:
        return "running";
    case QueueState::Paused:
        return "paused";
    }
    return "?";
}

const char *toString(SkipReason r)
{
    switch (r) {
    case SkipReason::RainDelay:
        return "rain_delay";
    case SkipReason::SensorInhibit:
        return "sensor_inhibit";
    case SkipReason::NoAc:
        return "no_ac";
    case SkipReason::Maintenance:
        return "maintenance";
    case SkipReason::AlreadyQueued:
        return "already_queued";
    case SkipReason::QueueFull:
        return "queue_full";
    case SkipReason::NothingToRun:
        return "nothing_to_run";
    case SkipReason::MasterLockedOut:
        return "master_locked_out";
    }
    return "?";
}

const char *toString(RunError e)
{
    switch (e) {
    case RunError::Ok:
        return "ok";
    case RunError::InvalidZone:
        return "invalid_zone";
    case RunError::UnknownProgram:
        return "unknown_program";
    case RunError::ZeroDuration:
        return "zero_duration";
    case RunError::QueueFull:
        return "queue_full";
    case RunError::AlreadyQueued:
        return "already_queued";
    case RunError::Inhibited:
        return "inhibited";
    case RunError::NothingToRun:
        return "nothing_to_run";
    case RunError::LockedOut:
        return "locked_out";
    case RunError::ZoneResting:
        return "zone_resting";
    case RunError::MasterLockedOut:
        return "master_locked_out";
    }
    return "?";
}

Scheduler::Scheduler(const IClock &clock, IZoneActuator &actuator) : clock_(clock), actuator_(actuator)
{
    keys_.fill(kNoKey);
}

int64_t Scheduler::keyIn(const TimeZone &tz, UtcSeconds t)
{
    const civil::LocalDateTime l = tz.toLocal(t);
    return makeKey(l.daySerial, static_cast<uint16_t>(l.secondOfDay / 60));
}

int64_t Scheduler::keyAt(UtcSeconds t) const { return keyIn(tz_, t); }

void Scheduler::configure(const SchedulerConfig &cfg, const TimeZone &tz)
{
    std::array<uint16_t, kMaxPrograms> oldIds{};
    std::array<int64_t, kMaxPrograms> oldKeys = keys_;
    const std::size_t oldCount = configured_ ? cfg_.schedule.programCount : 0;
    for (std::size_t i = 0; i < oldCount; ++i) {
        oldIds[i] = cfg_.schedule.programs[i].id;
    }

    // Keys are local (day, minute) values. When the time zone changes, move
    // each key to the same instant in the new zone; read in the new zone
    // unchanged, a key can name a later local time than the start it stands
    // for, and the next start would count as already run.
    if (configured_ && !(tz == tz_)) {
        for (std::size_t i = 0; i < oldCount; ++i) {
            if (oldKeys[i] != kNoKey) {
                const int64_t day = civil::floorDiv(oldKeys[i], 1440);
                const auto minute = static_cast<int32_t>(oldKeys[i] - day * 1440);
                oldKeys[i] = keyIn(tz, tz_.fromLocal(day, minute * 60));
            }
        }
    }

    cfg_ = cfg;
    tz_ = tz;
    auto &sched = cfg_.schedule;
    sched.programCount = std::min<uint8_t>(sched.programCount, kMaxPrograms);
    cfg_.zoneCount = std::min<uint8_t>(cfg_.zoneCount, kMaxZones);
    for (std::size_t i = 0; i < sched.programCount; ++i) {
        Program &p = sched.programs[i];
        p.startCount = std::min<uint8_t>(p.startCount, kMaxStartTimes);
        const auto first = p.startMinutes.begin();
        std::sort(first, first + p.startCount);
        // A start listed twice is one start, not one run plus one "missed".
        p.startCount = static_cast<uint8_t>(std::unique(first, first + p.startCount) - first);
    }

    const bool advance = configured_ && timeValid_;
    const int64_t nowKey = advance ? keyAt(clock_.nowUtc()) : kNoKey;
    for (std::size_t i = 0; i < kMaxPrograms; ++i) {
        int64_t key = kNoKey;
        if (i < sched.programCount) {
            const uint16_t id = sched.programs[i].id;
            bool found = false;
            for (std::size_t j = 0; j < oldCount; ++j) {
                if (oldIds[j] == id) {
                    key = oldKeys[j];
                    found = true;
                    break;
                }
            }
            if (!found && haveRestored_) {
                for (std::size_t j = 0; j < restored_.count; ++j) {
                    if (restored_.fired[j].programId == id) {
                        key = restored_.fired[j].key;
                    }
                }
            }
            if (advance && key < nowKey) {
                key = nowKey;
            }
        }
        keys_[i] = key;
    }

    removeIf([this](const RunEntry &e) {
        if (e.zone >= cfg_.zoneCount || !cfg_.zoneEnabled[e.zone]) {
            return true;
        }
        return e.source != RunSource::ManualZone && findProgram(e.programId) == nullptr;
    });

    const bool wasConfigured = configured_;
    configured_ = true;
    if (wasConfigured && listener_ != nullptr) {
        listener_->onPersistDirty();
    }
}

void Scheduler::restore(const SchedulerPersist &state)
{
    restored_ = state;
    restored_.count = std::min<uint8_t>(restored_.count, kMaxPrograms);
    haveRestored_ = true;
    rainDelayUntil_ = state.rainDelayUntil;
    rainDelayWasActive_ = rainDelayActive();
    if (configured_) {
        for (std::size_t i = 0; i < cfg_.schedule.programCount; ++i) {
            for (std::size_t j = 0; j < restored_.count; ++j) {
                if (restored_.fired[j].programId == cfg_.schedule.programs[i].id) {
                    keys_[i] = restored_.fired[j].key;
                }
            }
        }
    }
}

SchedulerPersist Scheduler::persist() const
{
    SchedulerPersist out{};
    out.rainDelayUntil = rainDelayUntil_;
    for (std::size_t i = 0; i < cfg_.schedule.programCount; ++i) {
        if (keys_[i] != kNoKey) {
            out.fired[out.count++] = {cfg_.schedule.programs[i].id, keys_[i]};
        }
    }
    return out;
}

void Scheduler::setTimeValid(bool valid)
{
    timeValid_ = valid;
    if (!valid) {
        cursorValid_ = false;
    }
}

void Scheduler::setInhibit(uint8_t flags)
{
    const uint8_t added = flags & static_cast<uint8_t>(~inhibit_);
    inhibit_ = flags;
    if ((added & (kInhibitNoAc | kInhibitMaintenance | kInhibitMasterLockout)) != 0) {
        stop();
    } else if ((added & kInhibitSensor) != 0) {
        removeIf([this](const RunEntry &e) {
            if (e.source != RunSource::Schedule) {
                return false;
            }
            const Program *p = findProgram(e.programId);
            return p != nullptr && p->obeySensorInhibit;
        });
    }
}

bool Scheduler::rainDelayActive() const
{
    return rainDelayUntil_ != 0 && timeValid_ && clock_.nowUtc() < rainDelayUntil_;
}

void Scheduler::setRainDelayUntil(UtcSeconds until)
{
    rainDelayUntil_ = until;
    rainDelayWasActive_ = rainDelayActive();
    if (rainDelayWasActive_) {
        removeIf([](const RunEntry &e) { return e.source == RunSource::Schedule; });
    }
    if (listener_ != nullptr) {
        listener_->onRainDelayChanged(until);
        listener_->onPersistDirty();
    }
}

const Program *Scheduler::findProgram(uint16_t programId) const
{
    for (std::size_t i = 0; i < cfg_.schedule.programCount; ++i) {
        if (cfg_.schedule.programs[i].id == programId) {
            return &cfg_.schedule.programs[i];
        }
    }
    return nullptr;
}

bool Scheduler::programQueued(uint16_t programId) const
{
    for (std::size_t i = 0; i < count_; ++i) {
        if (queue_[i].source != RunSource::ManualZone && queue_[i].programId == programId) {
            return true;
        }
    }
    return false;
}

uint32_t Scheduler::adjustedDurationSec(const Program &p, ZoneIndex zone) const
{
    if (zone >= kMaxZones) {
        return 0;
    }
    const uint32_t base = p.durationSec[zone];
    if (!p.useSeasonalAdjust) {
        return base;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(base) * cfg_.schedule.seasonalPercent + 50) / 100);
}

RunError Scheduler::runZone(ZoneIndex zone, uint32_t durationSec)
{
    if ((inhibit_ & (kInhibitNoAc | kInhibitMaintenance)) != 0) {
        return RunError::Inhibited;
    }
    if ((inhibit_ & kInhibitMasterLockout) != 0) {
        return RunError::MasterLockedOut;
    }
    if (zone >= cfg_.zoneCount || !cfg_.zoneEnabled[zone]) {
        return RunError::InvalidZone;
    }
    if (durationSec == 0) {
        return RunError::ZeroDuration;
    }
    switch (actuator_.startBlocker(zone)) {
    case StartError::LockedOut:
        return RunError::LockedOut;
    case StartError::Resting:
        return RunError::ZoneResting;
    default:
        break;
    }
    // The driver clamps again to the zone's limit; this keeps the
    // millisecond value below from wrapping.
    durationSec = std::min(durationSec, kHardMaxRunSec);
    RunEntry e;
    e.zone = zone;
    e.durationSec = durationSec;
    e.remainingMs = durationSec * 1000u;
    e.source = RunSource::ManualZone;
    if (!push(e)) {
        return RunError::QueueFull;
    }
    notifyQueue();
    processQueue();
    return RunError::Ok;
}

RunError Scheduler::runProgram(uint16_t programId)
{
    if ((inhibit_ & (kInhibitNoAc | kInhibitMaintenance)) != 0) {
        return RunError::Inhibited;
    }
    if ((inhibit_ & kInhibitMasterLockout) != 0) {
        return RunError::MasterLockedOut;
    }
    const Program *p = findProgram(programId);
    if (p == nullptr) {
        return RunError::UnknownProgram;
    }
    if (programQueued(programId)) {
        return RunError::AlreadyQueued;
    }
    const RunError r = enqueueProgram(*p, RunSource::ManualProgram);
    if (r == RunError::Ok) {
        if (listener_ != nullptr) {
            listener_->onProgramStarted(programId, RunSource::ManualProgram, false);
        }
        processQueue();
    }
    return r;
}

RunError Scheduler::enqueueProgram(const Program &p, RunSource source)
{
    std::size_t needed = 0;
    for (std::size_t z = 0; z < cfg_.zoneCount; ++z) {
        if (cfg_.zoneEnabled[z] && adjustedDurationSec(p, static_cast<ZoneIndex>(z)) > 0) {
            ++needed;
        }
    }
    if (needed == 0) {
        return RunError::NothingToRun;
    }
    if (count_ + needed > kRunQueueCapacity) {
        return RunError::QueueFull;
    }
    for (std::size_t z = 0; z < cfg_.zoneCount; ++z) {
        const auto zone = static_cast<ZoneIndex>(z);
        const uint32_t d = adjustedDurationSec(p, zone);
        if (!cfg_.zoneEnabled[z] || d == 0) {
            continue;
        }
        RunEntry e;
        e.zone = zone;
        e.durationSec = d;
        e.remainingMs = d * 1000u;
        e.source = source;
        e.programId = p.id;
        push(e);
    }
    notifyQueue();
    return RunError::Ok;
}

bool Scheduler::push(const RunEntry &e)
{
    if (count_ >= kRunQueueCapacity) {
        return false;
    }
    queue_[count_] = e;
    queue_[count_].id = nextId_++;
    ++count_;
    return true;
}

void Scheduler::popFront()
{
    if (count_ == 0) {
        return;
    }
    std::move(queue_.begin() + 1, queue_.begin() + count_, queue_.begin());
    --count_;
}

template <typename Pred> void Scheduler::removeIf(Pred pred)
{
    bool changed = false;
    std::size_t out = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        if (pred(queue_[i])) {
            if (i == 0 && state_ == QueueState::Running) {
                actuator_.stop(queue_[0].zone, StopReason::Requested);
                state_ = QueueState::Idle;
            }
            changed = true;
            continue;
        }
        queue_[out++] = queue_[i];
    }
    count_ = out;
    if (count_ == 0 && state_ == QueueState::Running) {
        state_ = QueueState::Idle;
    }
    if (changed) {
        notifyQueue();
    }
}

void Scheduler::stopFront()
{
    if (state_ == QueueState::Running && count_ > 0) {
        actuator_.stop(queue_[0].zone, StopReason::Requested);
    }
}

void Scheduler::pause()
{
    if (state_ == QueueState::Running && count_ > 0) {
        RunEntry &front = queue_[0];
        front.remainingMs = actuator_.remainingMs(front.zone);
        actuator_.stop(front.zone, StopReason::Requested);
        if (front.remainingMs == 0) {
            popFront();
        }
    }
    state_ = QueueState::Paused;
    notifyQueue();
}

void Scheduler::resume()
{
    if (state_ != QueueState::Paused) {
        return;
    }
    state_ = QueueState::Idle;
    notifyQueue();
    processQueue();
}

void Scheduler::stop()
{
    stopFront();
    const bool changed = count_ > 0 || state_ != QueueState::Idle;
    count_ = 0;
    state_ = QueueState::Idle;
    if (changed) {
        notifyQueue();
    }
}

void Scheduler::skip()
{
    if (count_ == 0) {
        return;
    }
    stopFront();
    popFront();
    if (state_ == QueueState::Running) {
        state_ = QueueState::Idle;
    }
    notifyQueue();
    processQueue();
}

void Scheduler::processQueue()
{
    // Bounded: each pass either returns or removes one entry.
    for (std::size_t guard = 0; guard <= kRunQueueCapacity; ++guard) {
        if (state_ == QueueState::Running) {
            if (count_ > 0 && actuator_.state(queue_[0].zone) != ValveState::Off) {
                return;
            }
            popFront();
            state_ = QueueState::Idle;
            notifyQueue();
        }
        if (state_ == QueueState::Paused || count_ == 0) {
            return;
        }
        RunEntry &front = queue_[0];
        const uint32_t sec = (front.remainingMs + 999u) / 1000u;
        const StartResult r = actuator_.start(front.zone, sec);
        if (r.ok()) {
            state_ = QueueState::Running;
            notifyQueue();
            return;
        }
        if (r.error == StartError::TooManyActive || r.error == StartError::AlreadyActive) {
            return; // retry on the next tick
        }
        // The zone cannot run (locked out, resting, disabled, inhibited).
        if (listener_ != nullptr) {
            listener_->onRunDropped(front, r.error);
        }
        popFront();
        notifyQueue();
    }
}

void Scheduler::tick()
{
    const UtcSeconds now = clock_.nowUtc();
    // Retire finished entries first so a program that just ended does not
    // count as still queued when its next start is evaluated.
    processQueue();
    if (timeValid_ && now != lastEvalSecond_) {
        lastEvalSecond_ = now;
        evaluate(now);
    }
    const bool active = rainDelayActive();
    if (rainDelayWasActive_ && !active && timeValid_) {
        rainDelayUntil_ = 0;
        if (listener_ != nullptr) {
            listener_->onRainDelayChanged(0);
            listener_->onPersistDirty();
        }
    }
    rainDelayWasActive_ = active;
    processQueue();
}

void Scheduler::evaluate(UtcSeconds now)
{
    if (!configured_) {
        return;
    }
    if (!cursorValid_) {
        // First valid time since boot (or since the clock became valid):
        // look back so starts missed during a power cut are found.
        cursor_ = now - kLookBackSec;
        cursorValid_ = true;
        // A key more than kLargeBackJumpSec ahead of the clock was stored
        // while the clock was wrong. The clamp below only sees a jump within
        // one boot, so check the restored keys here.
        const int64_t k = keyAt(now);
        bool clamped = false;
        for (std::size_t i = 0; i < cfg_.schedule.programCount; ++i) {
            if (keys_[i] > k + kLargeBackJumpSec / 60) {
                keys_[i] = k;
                clamped = true;
            }
        }
        if (clamped && listener_ != nullptr) {
            listener_->onPersistDirty();
        }
    } else if (now < cursor_) {
        if (cursor_ - now > kLargeBackJumpSec) {
            const int64_t k = keyAt(now);
            for (std::size_t i = 0; i < cfg_.schedule.programCount; ++i) {
                keys_[i] = std::min(keys_[i], k);
            }
            if (listener_ != nullptr) {
                listener_->onPersistDirty();
            }
        }
        cursor_ = now;
        return;
    }
    const UtcSeconds from = std::max(cursor_, now - kLookBackSec);
    cursor_ = now;
    if (from >= now) {
        return;
    }
    for (std::size_t i = 0; i < cfg_.schedule.programCount; ++i) {
        if (cfg_.schedule.programs[i].enabled) {
            evaluateProgram(i, from, now);
        }
    }
}

void Scheduler::evaluateProgram(std::size_t index, UtcSeconds from, UtcSeconds now)
{
    const Program &p = cfg_.schedule.programs[index];
    if (p.startCount == 0) {
        return;
    }
    if (keys_[index] == kNoKey) {
        // Never ran as far as we know (no persisted state): no catch-up and
        // no missed reports, only starts from the current minute on.
        keys_[index] = keyAt(now) - 1;
        if (listener_ != nullptr) {
            listener_->onPersistDirty();
        }
    }
    struct Candidate {
        UtcSeconds at;
        int64_t key;
    };
    // The window spans at most kLookBackSec plus DST slack: four local days.
    std::array<Candidate, 4 * kMaxStartTimes> found{};
    std::size_t n = 0;
    const int64_t firstDay = tz_.localDay(from) - 1;
    const int64_t lastDay = tz_.localDay(now);
    for (int64_t day = firstDay; day <= lastDay && n < found.size(); ++day) {
        if (!p.days.matches(day)) {
            continue;
        }
        for (std::size_t s = 0; s < p.startCount && n < found.size(); ++s) {
            const uint16_t minute = p.startMinutes[s];
            const int64_t key = makeKey(day, minute);
            if (key <= keys_[index]) {
                continue;
            }
            const UtcSeconds at = tz_.fromLocal(day, static_cast<int32_t>(minute) * 60);
            if (at <= from || at > now) {
                continue;
            }
            found[n++] = Candidate{at, key};
        }
    }
    if (n == 0) {
        return;
    }
    // Keys and instants are both ascending; the last candidate is the newest.
    const Candidate newest = found[n - 1];
    const int64_t grace =
        std::max<int64_t>(static_cast<int64_t>(cfg_.schedule.missedStartGraceMin) * 60, kOnTimeSlackSec);
    const bool runNewest = now - newest.at <= grace;
    keys_[index] = newest.key;
    if (listener_ != nullptr) {
        for (std::size_t i = 0; i + 1 < n; ++i) {
            listener_->onProgramMissed(p.id, found[i].at);
        }
        if (!runNewest) {
            listener_->onProgramMissed(p.id, newest.at);
        }
        listener_->onPersistDirty();
    }
    if (runNewest) {
        fireScheduled(p, now - newest.at > kLateThresholdSec);
    }
}

void Scheduler::fireScheduled(const Program &p, bool late)
{
    auto skipped = [this, &p](SkipReason r) {
        if (listener_ != nullptr) {
            listener_->onProgramSkipped(p.id, r);
        }
    };
    if ((inhibit_ & kInhibitMaintenance) != 0) {
        skipped(SkipReason::Maintenance);
    } else if ((inhibit_ & kInhibitNoAc) != 0) {
        skipped(SkipReason::NoAc);
    } else if ((inhibit_ & kInhibitMasterLockout) != 0) {
        skipped(SkipReason::MasterLockedOut);
    } else if (rainDelayActive()) {
        skipped(SkipReason::RainDelay);
    } else if ((inhibit_ & kInhibitSensor) != 0 && p.obeySensorInhibit) {
        skipped(SkipReason::SensorInhibit);
    } else if (programQueued(p.id)) {
        skipped(SkipReason::AlreadyQueued);
    } else {
        const RunError r = enqueueProgram(p, RunSource::Schedule);
        if (r == RunError::Ok) {
            if (listener_ != nullptr) {
                listener_->onProgramStarted(p.id, RunSource::Schedule, late);
            }
        } else if (r == RunError::QueueFull) {
            skipped(SkipReason::QueueFull);
        } else {
            skipped(SkipReason::NothingToRun);
        }
    }
}

void Scheduler::notifyQueue()
{
    if (listener_ != nullptr) {
        listener_->onQueueChanged();
    }
}

} // namespace zf
