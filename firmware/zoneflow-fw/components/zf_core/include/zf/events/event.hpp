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
    MasterLockedOut,         // repeated current faults on the master output; latched
    CurrentSenseUnavailable, // no valve current readings; zones still run; clears itself
    OverCurrentTrip,         // hardware over-current comparator tripped; latched
    // Rev A output stage (hardware design 19.7, 19.8). Latched until a person
    // acknowledges; nothing runs meanwhile. FaultData::detail is the HwCheck.
    KeepAliveDetector, // a keep-alive detector failed its test (steps 1, 2, 3, 7, 9)
    OutputExpander,    // the I/O expander did not answer or read back as written
    RelayWelded,       // current with the valve-common relay open (step 5)
    OutputShorted,     // current with the relay closed and no valve on (step 6, rule 6)
    TripLatchStuck,    // the over-current latch did not clear (19.8)
    SensorSupply,      // the 5 V sensor supply reports over-current or over-temperature; clears itself
    ZoneDcCurrent,     // a zone's current has a DC component: one triac polarity not conducting; latched
    MainsSignal,       // the zero-cross signal is not at mains frequency (bridged to the keep-alive?); clears itself
    // Added after hardware review A2.
    KeepAliveBridged, // another net drives the keep-alive net (step 2b float test, rule 16); latched
    RelayGateBleed,   // the relay driver Q1 keeps its gate charge when released: R12 open (step 5b); latched
    NotCommissioned,  // no stored detector release times yet: step 9 uses its absolute limits only; clears itself
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
    RunEnded,        // a zone that was open closed; data: run (for the history log)
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
    uint8_t detail; // HwCheck for the output-stage faults, else 0
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

// One opening of one zone, from open to close. A pause and resume gives two
// runs. Current and volume are measured while the run was open.
struct RunEndedData {
    ZoneIndex zone;
    RunSource source;
    StopReason reason;
    uint16_t programId;  // 0 for a manual zone run
    uint16_t plannedSec; // what was left to run when it opened
    uint16_t actualSec;  // how long it was open
    uint16_t currentMa;  // mean valve current of this zone alone; 0 = not measured
    int32_t centilitres; // flow-meter volume while open; -1 = no flow meter
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
        RunEndedData run;
    };
};
static_assert(std::is_trivially_copyable_v<Event>, "events are copied with memcpy into RTOS queues");

} // namespace zf
