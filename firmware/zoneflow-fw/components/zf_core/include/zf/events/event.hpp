// SPDX-License-Identifier: Apache-2.0
// Events published by the controller. Events are small, trivially copyable
// values so a subscriber can copy them into a FreeRTOS queue.
#pragma once

#include <cstdint>
#include <type_traits>

#include "zf/events/command.hpp"
#include "zf/sched/scheduler.hpp"
#include "zf/types.hpp"
#include "zf/zone/zone_types.hpp"

namespace zf {

enum class FaultCode : uint8_t {
    None = 0,
    ZoneOpenCircuit,
    ZoneOverCurrent,
    ShortCircuit,
    CurrentWhileIdle,
    AcLost,
    FlowWithoutZones,
    ConfigInvalid,
    TimeInvalid,
    StorageError,
    MasterLockedOut, // repeated current faults on the master output; latched
    Count,
};
static_assert(static_cast<unsigned>(FaultCode::Count) <= 32, "fault bitmask is 32 bits");

const char *toString(FaultCode f);

enum class SensorId : uint8_t { Rain, Spare, AcPresent };

enum class TimeStatus : uint8_t {
    Unset,   // no valid wall clock; schedules do not run
    FromRtc, // set from the RTC at boot
    Synced,  // set from SNTP (or by the user) since boot
};

const char *toString(TimeStatus s);

enum class EventType : uint8_t {
    ZoneChanged,
    MasterChanged,
    QueueChanged,
    ProgramStarted,
    ProgramSkipped,
    ProgramMissed,
    SensorChanged,
    FaultRaised,
    FaultCleared,
    RainDelayChanged,
    ConfigApplied,
    TimeStatusChanged,
    MaintenanceChanged,
    CommandRejected, // Controller::handle() refused a command; data: rejected
    RunDropped,      // a queued run could not start and was dropped; data: dropped
    PauseExpired,    // the queue stayed paused for schedule.pause_timeout_min and was stopped
    Count,
};
static_assert(static_cast<unsigned>(EventType::Count) <= 32, "event mask is 32 bits");

const char *toString(EventType t);

using EventMask = uint32_t;
inline constexpr EventMask kAllEvents = 0xFFFFFFFFu;
constexpr EventMask eventBit(EventType t) { return 1u << static_cast<unsigned>(t); }

struct ZoneChangedData {
    ZoneIndex zone;
    ValveState state;
    StopReason reason;
};
struct MasterChangedData {
    bool on;
};
struct ProgramData {
    uint16_t programId;
    RunSource source;
    SkipReason skipReason;  // ProgramSkipped only
    bool late;              // ProgramStarted only
    UtcSeconds scheduledAt; // ProgramMissed only
};
struct SensorData {
    SensorId sensor;
    bool active;
};
struct FaultData {
    FaultCode code;
    ZoneIndex zone; // kNoZone when not zone-specific
};
struct RainDelayData {
    UtcSeconds until; // 0 = none
};
struct TimeStatusData {
    TimeStatus status;
};
struct FlagData {
    bool on;
};
struct CommandRejectedData {
    CommandType command;
    ZoneIndex zone;     // as in the command (RunZone)
    uint16_t programId; // as in the command (RunProgram)
    RunError error;
};
struct RunDroppedData {
    ZoneIndex zone;
    uint16_t programId; // 0 for a manual zone run
    RunSource source;
    StartError reason;
};

struct Event {
    EventType type;
    MonoMs monoMs;
    UtcSeconds utc;
    union {
        ZoneChangedData zone;
        MasterChangedData master;
        ProgramData program;
        SensorData sensor;
        FaultData fault;
        RainDelayData rainDelay;
        TimeStatusData time;
        FlagData flag;
        CommandRejectedData rejected;
        RunDroppedData dropped;
    };
};
static_assert(std::is_trivially_copyable_v<Event>, "events are copied with memcpy into RTOS queues");

} // namespace zf
