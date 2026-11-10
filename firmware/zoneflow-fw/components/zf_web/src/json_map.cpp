// SPDX-License-Identifier: Apache-2.0
#include "zf_web/json_map.hpp"

#include <cstdlib>

#include "cJSON.h"

namespace zf::web {

void JsonDeleter::operator()(cJSON *p) const { cJSON_Delete(p); }

std::string printJson(const cJSON *json)
{
    if (json == nullptr) {
        return {};
    }
    char *text = cJSON_PrintUnformatted(json);
    if (text == nullptr) {
        return {};
    }
    std::string out(text);
    cJSON_free(text);
    return out;
}

bool jsonDepthOk(std::string_view text, int maxDepth)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > maxDepth) {
                return false;
            }
        } else if (c == ']' || c == '}') {
            // A closer with nothing open is not JSON. Letting the count go
            // negative would hide the openers after it (security review F7).
            if (--depth < 0) {
                return false;
            }
        }
    }
    return true;
}

JsonPtr parseJson(std::string_view text)
{
    if (!jsonDepthOk(text)) {
        return nullptr;
    }
    return JsonPtr(cJSON_ParseWithLength(text.data(), text.size()));
}

const char *sensorName(SensorId id)
{
    switch (id) {
    case SensorId::Rain:
        return "rain";
    case SensorId::Spare:
        return "spare";
    case SensorId::AcPresent:
        return "ac";
    }
    return "?";
}

namespace {

void addZoneNumber(cJSON *o, const char *key, ZoneIndex z)
{
    if (z == kNoZone) {
        cJSON_AddNullToObject(o, key);
    } else {
        cJSON_AddNumberToObject(o, key, z + 1);
    }
}

} // namespace

cJSON *zoneToJson(std::size_t index, const ZoneStatus &st, const ZoneConfig &cfg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", static_cast<double>(index + 1));
    cJSON_AddStringToObject(o, "name", cfg.name.c_str());
    cJSON_AddBoolToObject(o, "enabled", cfg.enabled);
    cJSON_AddNumberToObject(o, "max_run_s", cfg.maxRunSec);
    cJSON_AddNumberToObject(o, "default_run_s", cfg.defaultRunSec);
    cJSON_AddNumberToObject(o, "current_min_ma", cfg.current.minMa);
    cJSON_AddNumberToObject(o, "current_max_ma", cfg.current.maxMa);
    cJSON_AddStringToObject(o, "state", toString(st.state));
    cJSON_AddNumberToObject(o, "remaining_s", st.remainingSec);
    cJSON_AddStringToObject(o, "last_stop", toString(st.lastStop));
    cJSON_AddStringToObject(o, "fault", toString(st.fault));
    cJSON_AddBoolToObject(o, "locked_out", st.lockedOut);
    // Spec section 2 rule 4: after its maximum run time a zone rests.
    cJSON_AddBoolToObject(o, "resting", st.restRemainingSec > 0);
    cJSON_AddNumberToObject(o, "rest_remaining_s", st.restRemainingSec);
    if (st.learnedCurrentMa > 0) {
        cJSON_AddNumberToObject(o, "learned_current_ma", st.learnedCurrentMa);
    } else {
        cJSON_AddNullToObject(o, "learned_current_ma");
    }
    return o;
}

cJSON *queueToJson(const QueueStatus &q, uint16_t pauseTimeoutMin)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", toString(q.state));
    cJSON_AddNumberToObject(o, "pause_timeout_min", pauseTimeoutMin);
    cJSON_AddNumberToObject(o, "length", q.length);
    cJSON_AddNumberToObject(o, "current_remaining_s", q.currentRemainingSec);
    // A paused queue is stopped after pause_timeout_min, counted from the first pause.
    const bool paused = q.state == QueueState::Paused;
    cJSON_AddNumberToObject(o, "paused_s", paused ? q.pausedForSec : 0);
    if (paused) {
        const uint32_t limit = static_cast<uint32_t>(pauseTimeoutMin) * 60u;
        cJSON_AddNumberToObject(o, "stop_in_s", limit > q.pausedForSec ? limit - q.pausedForSec : 0);
    } else {
        cJSON_AddNullToObject(o, "stop_in_s");
    }
    cJSON *items = cJSON_AddArrayToObject(o, "items");
    for (std::size_t i = 0; items != nullptr && i < q.shownCount && i < q.items.size(); ++i) {
        const QueueItem &it = q.items[i];
        cJSON *e = cJSON_CreateObject();
        cJSON_AddNumberToObject(e, "id", it.id);
        addZoneNumber(e, "zone", it.zone);
        cJSON_AddNumberToObject(e, "duration_s", it.durationSec);
        cJSON_AddStringToObject(e, "source", toString(it.source));
        if (it.programId != 0) {
            cJSON_AddNumberToObject(e, "program_id", it.programId);
        } else {
            cJSON_AddNullToObject(e, "program_id");
        }
        cJSON_AddItemToArray(items, e);
    }
    return o;
}

cJSON *sensorsToJson(const SystemState &st)
{
    const SensorStatus &s = st.sensors;
    cJSON *o = cJSON_CreateObject();
    cJSON *rain = cJSON_AddObjectToObject(o, "rain");
    cJSON_AddBoolToObject(rain, "enabled", s.rainEnabled);
    cJSON_AddBoolToObject(rain, "active", s.rainActive);
    cJSON *spare = cJSON_AddObjectToObject(o, "spare");
    cJSON_AddBoolToObject(spare, "enabled", s.spareEnabled);
    cJSON_AddBoolToObject(spare, "active", s.spareActive);
    cJSON *flow = cJSON_AddObjectToObject(o, "flow");
    cJSON_AddBoolToObject(flow, "enabled", s.flowEnabled);
    cJSON_AddNumberToObject(flow, "rate_lpm", s.flowLpm);
    cJSON_AddNumberToObject(flow, "total_l", s.flowTotalLitres);
    cJSON_AddBoolToObject(o, "ac_present", s.acPresent);
    if (s.valveCurrentMa < 0) {
        cJSON_AddNullToObject(o, "valve_current_ma");
    } else {
        cJSON_AddNumberToObject(o, "valve_current_ma", s.valveCurrentMa);
    }
    const auto fault = [&](FaultCode c) { return (st.activeFaults & (1u << static_cast<unsigned>(c))) != 0; };
    // Zones still run without readings; the fault says diagnostics are blind.
    cJSON_AddBoolToObject(o, "current_sense_available", !fault(FaultCode::CurrentSenseUnavailable));
    // Hardware comparator removed gate drive; latched until faults are cleared.
    cJSON_AddBoolToObject(o, "over_current_tripped", fault(FaultCode::OverCurrentTrip));
    // The 5 V sensor supply reports over-current or over-temperature.
    cJSON_AddBoolToObject(o, "sensor_supply_fault", st.hardware.sensorSupplyFault);
    return o;
}

cJSON *hardwareToJson(const HardwareStatus &h)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "phase", toString(h.phase));
    if (h.zonesFitted == 0) {
        cJSON_AddNullToObject(o, "zones_fitted");
    } else {
        cJSON_AddNumberToObject(o, "zones_fitted", h.zonesFitted);
    }
    cJSON_AddBoolToObject(o, "relay_closed", h.relayClosed);
    cJSON_AddBoolToObject(o, "sensor_supply_on", h.sensorSupplyOn);
    cJSON_AddBoolToObject(o, "sensor_supply_fault", h.sensorSupplyFault);
    if (h.check == HwCheck::None) {
        cJSON_AddNullToObject(o, "check");
        cJSON_AddNullToObject(o, "step");
    } else {
        cJSON_AddStringToObject(o, "check", toString(h.check));
        cJSON_AddNumberToObject(o, "step", h.step);
    }
    cJSON_AddBoolToObject(o, "repair_needed", h.repairNeeded);
    cJSON_AddNumberToObject(o, "self_tests_passed", h.selfTestsPassed);
    cJSON *det = cJSON_AddObjectToObject(o, "detectors");
    cJSON_AddNumberToObject(det, "enable_drive_ms", h.enableDriveMs);
    cJSON_AddNumberToObject(det, "enable_relay_ms", h.enableRelayMs);
    cJSON_AddNumberToObject(det, "release_drive_ms", h.releaseDriveMs);
    cJSON_AddNumberToObject(det, "release_relay_ms", h.releaseRelayMs);
    if (h.mainsMilliHz == 0) {
        cJSON_AddNullToObject(o, "mains_hz");
    } else {
        cJSON_AddNumberToObject(o, "mains_hz", static_cast<double>(h.mainsMilliHz) / 1000.0);
    }
    cJSON *cur = cJSON_AddObjectToObject(o, "current");
    cJSON_AddStringToObject(cur, "channel",
                            h.currentChannel == CurrentChannel::High  ? "high"
                            : h.currentChannel == CurrentChannel::Low ? "low"
                                                                      : "none");
    cJSON_AddNumberToObject(cur, "noise_floor_ma", h.noiseFloorMa);
    cJSON_AddNumberToObject(cur, "dc_ma", h.currentDcMa);
    if (h.lastTripCurrentMa < 0) {
        cJSON_AddNullToObject(cur, "last_trip_ma");
    } else {
        cJSON_AddNumberToObject(cur, "last_trip_ma", h.lastTripCurrentMa);
    }
    return o;
}

cJSON *faultsToJson(const SystemState &s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *active = cJSON_AddArrayToObject(o, "active");
    for (unsigned i = 1; i < static_cast<unsigned>(FaultCode::Count); ++i) {
        if ((s.activeFaults & (1u << i)) != 0) {
            cJSON_AddItemToArray(active, cJSON_CreateString(toString(static_cast<FaultCode>(i))));
        }
    }
    cJSON *zones = cJSON_AddArrayToObject(o, "zones");
    for (std::size_t z = 0; z < s.zoneCount && z < s.zones.size(); ++z) {
        if (s.zones[z].fault != FaultCode::None || s.zones[z].lockedOut) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "zone", static_cast<double>(z + 1));
            cJSON_AddStringToObject(e, "fault", toString(s.zones[z].fault));
            cJSON_AddBoolToObject(e, "locked_out", s.zones[z].lockedOut);
            cJSON_AddItemToArray(zones, e);
        }
    }
    // The output stage's failed check (hardware design 19.7), while latched.
    if (s.hardware.check == HwCheck::None) {
        cJSON_AddNullToObject(o, "hardware");
    } else {
        cJSON *h = cJSON_AddObjectToObject(o, "hardware");
        cJSON_AddStringToObject(h, "check", toString(s.hardware.check));
        cJSON_AddNumberToObject(h, "step", s.hardware.step);
        cJSON_AddBoolToObject(h, "repair_needed", s.hardware.repairNeeded);
    }
    return o;
}

cJSON *rainDelayToJson(const SystemState &s)
{
    cJSON *o = cJSON_CreateObject();
    const bool active = s.rainDelayUntil != 0 && s.rainDelayUntil > s.utc;
    cJSON_AddBoolToObject(o, "active", active);
    if (s.rainDelayUntil != 0) {
        cJSON_AddNumberToObject(o, "until_utc", static_cast<double>(s.rainDelayUntil));
    } else {
        cJSON_AddNullToObject(o, "until_utc");
    }
    cJSON_AddNumberToObject(o, "remaining_s", active ? static_cast<double>(s.rainDelayUntil - s.utc) : 0.0);
    return o;
}

cJSON *stateToJson(const SystemState &s, const Config &cfg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "sequence", s.sequence);
    cJSON_AddNumberToObject(o, "utc", static_cast<double>(s.utc));
    cJSON_AddStringToObject(o, "time_status", toString(s.timeStatus));
    cJSON_AddNumberToObject(o, "uptime_s", static_cast<double>(s.monoMs / 1000));
    cJSON *zones = cJSON_AddArrayToObject(o, "zones");
    for (std::size_t i = 0; i < s.zoneCount && i < s.zones.size(); ++i) {
        cJSON_AddItemToArray(zones, zoneToJson(i, s.zones[i], cfg.zones[i]));
    }
    cJSON *master = cJSON_AddObjectToObject(o, "master");
    cJSON_AddBoolToObject(master, "enabled", s.masterEnabled);
    cJSON_AddBoolToObject(master, "on", s.masterOn);
    cJSON_AddBoolToObject(master, "locked_out", s.masterLockedOut);
    cJSON_AddNumberToObject(master, "fault_count", s.masterFaultCount);
    cJSON_AddItemToObject(o, "queue", queueToJson(s.queue, cfg.schedule.pauseTimeoutMin));
    cJSON_AddItemToObject(o, "sensors", sensorsToJson(s));
    cJSON_AddItemToObject(o, "rain_delay", rainDelayToJson(s));
    cJSON *inhibit = cJSON_AddObjectToObject(o, "inhibit");
    cJSON_AddBoolToObject(inhibit, "sensor", (s.inhibitFlags & kInhibitSensor) != 0);
    cJSON_AddBoolToObject(inhibit, "no_ac", (s.inhibitFlags & kInhibitNoAc) != 0);
    cJSON_AddBoolToObject(inhibit, "maintenance", (s.inhibitFlags & kInhibitMaintenance) != 0);
    cJSON_AddBoolToObject(inhibit, "master_lockout", (s.inhibitFlags & kInhibitMasterLockout) != 0);
    cJSON_AddBoolToObject(inhibit, "hardware", (s.inhibitFlags & kInhibitHardware) != 0);
    cJSON_AddItemToObject(o, "faults", faultsToJson(s));
    cJSON_AddItemToObject(o, "hardware", hardwareToJson(s.hardware));
    cJSON_AddBoolToObject(o, "maintenance", s.maintenance);
    cJSON_AddNumberToObject(o, "config_revision", s.configRevision);
    return o;
}

cJSON *eventToJson(const Event &e)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", toString(e.type));
    cJSON_AddNumberToObject(o, "utc", static_cast<double>(e.utc));
    cJSON_AddNumberToObject(o, "mono_ms", static_cast<double>(e.monoMs));
    switch (e.type) {
    case EventType::ZoneChanged:
        addZoneNumber(o, "zone", e.zone.zone);
        cJSON_AddStringToObject(o, "state", toString(e.zone.state));
        cJSON_AddStringToObject(o, "reason", toString(e.zone.reason));
        break;
    case EventType::MasterChanged:
        cJSON_AddBoolToObject(o, "on", e.master.on);
        break;
    case EventType::ProgramStarted:
        cJSON_AddNumberToObject(o, "program_id", e.program.programId);
        cJSON_AddStringToObject(o, "source", toString(e.program.source));
        cJSON_AddBoolToObject(o, "late", e.program.late);
        break;
    case EventType::ProgramSkipped:
        cJSON_AddNumberToObject(o, "program_id", e.program.programId);
        cJSON_AddStringToObject(o, "reason", toString(e.program.skipReason));
        break;
    case EventType::ProgramMissed:
        cJSON_AddNumberToObject(o, "program_id", e.program.programId);
        cJSON_AddNumberToObject(o, "scheduled_utc", static_cast<double>(e.program.scheduledAt));
        break;
    case EventType::SensorChanged:
        cJSON_AddStringToObject(o, "sensor", sensorName(e.sensor.sensor));
        cJSON_AddBoolToObject(o, "active", e.sensor.active);
        break;
    case EventType::FaultRaised:
    case EventType::FaultCleared:
        cJSON_AddStringToObject(o, "code", toString(e.fault.code));
        addZoneNumber(o, "zone", e.fault.zone);
        if (e.type == EventType::FaultRaised && e.fault.detail != 0 &&
            e.fault.detail < static_cast<uint8_t>(HwCheck::Count)) {
            const auto check = static_cast<HwCheck>(e.fault.detail);
            cJSON_AddStringToObject(o, "check", toString(check));
            cJSON_AddNumberToObject(o, "step", hwCheckStep(check));
        }
        break;
    case EventType::RainDelayChanged:
        if (e.rainDelay.until == 0) {
            cJSON_AddNullToObject(o, "until_utc");
        } else {
            cJSON_AddNumberToObject(o, "until_utc", static_cast<double>(e.rainDelay.until));
        }
        break;
    case EventType::TimeStatusChanged:
        cJSON_AddStringToObject(o, "status", toString(e.time.status));
        break;
    case EventType::MaintenanceChanged:
        cJSON_AddBoolToObject(o, "on", e.flag.on);
        break;
    case EventType::CommandRejected:
        cJSON_AddStringToObject(o, "command", toString(e.rejected.command));
        addZoneNumber(o, "zone", e.rejected.zone);
        if (e.rejected.programId != 0) {
            cJSON_AddNumberToObject(o, "program_id", e.rejected.programId);
        } else {
            cJSON_AddNullToObject(o, "program_id");
        }
        cJSON_AddStringToObject(o, "error", toString(e.rejected.error));
        break;
    case EventType::RunDropped:
        addZoneNumber(o, "zone", e.dropped.zone);
        if (e.dropped.programId != 0) {
            cJSON_AddNumberToObject(o, "program_id", e.dropped.programId);
        } else {
            cJSON_AddNullToObject(o, "program_id");
        }
        cJSON_AddStringToObject(o, "source", toString(e.dropped.source));
        cJSON_AddStringToObject(o, "reason", toString(e.dropped.reason));
        break;
    case EventType::RunEnded:
        addZoneNumber(o, "zone", e.run.zone);
        if (e.run.programId != 0) {
            cJSON_AddNumberToObject(o, "program_id", e.run.programId);
        } else {
            cJSON_AddNullToObject(o, "program_id");
        }
        cJSON_AddStringToObject(o, "source", toString(e.run.source));
        cJSON_AddStringToObject(o, "end", toString(e.run.reason));
        cJSON_AddNumberToObject(o, "planned_s", e.run.plannedSec);
        cJSON_AddNumberToObject(o, "actual_s", e.run.actualSec);
        if (e.run.currentMa > 0) {
            cJSON_AddNumberToObject(o, "current_ma", e.run.currentMa);
        } else {
            cJSON_AddNullToObject(o, "current_ma");
        }
        if (e.run.centilitres >= 0) {
            cJSON_AddNumberToObject(o, "litres", e.run.centilitres / 100.0);
        } else {
            cJSON_AddNullToObject(o, "litres");
        }
        break;
    case EventType::QueueChanged:
    case EventType::ConfigApplied:
    case EventType::PauseExpired:
    case EventType::Count:
        break;
    }
    return o;
}

namespace {

void addProgram(cJSON *o, uint16_t id)
{
    if (id != 0) {
        cJSON_AddNumberToObject(o, "program_id", id);
    } else {
        cJSON_AddNullToObject(o, "program_id");
    }
}

void addHistoryZone(cJSON *o, uint8_t zone)
{
    if (zone < kMaxZones) {
        cJSON_AddNumberToObject(o, "zone", zone + 1);
    } else {
        cJSON_AddNullToObject(o, "zone");
    }
}

template <typename E> const char *enumName(uint8_t v, E count)
{
    return v < static_cast<uint8_t>(count) ? toString(static_cast<E>(v)) : "unknown";
}

} // namespace

cJSON *historyRecordToJson(uint64_t id, const HistoryRecord &r)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", static_cast<double>(id));
    cJSON_AddNumberToObject(o, "utc", static_cast<double>(r.utc));
    // Recorded while the clock was not set (before 2025-01-01): the time is wrong.
    cJSON_AddBoolToObject(o, "time_valid", r.utc >= 1735689600);
    cJSON_AddStringToObject(o, "type", toString(r.kind));
    switch (r.kind) {
    case HistoryKind::Run: {
        addHistoryZone(o, r.zone);
        addProgram(o, r.programId);
        cJSON_AddStringToObject(
            o, "source",
            r.a <= static_cast<uint8_t>(RunSource::ManualZone) ? toString(static_cast<RunSource>(r.a)) : "unknown");
        cJSON_AddStringToObject(
            o, "end",
            r.b <= static_cast<uint8_t>(StopReason::Maintenance) ? toString(static_cast<StopReason>(r.b)) : "unknown");
        cJSON_AddNumberToObject(o, "planned_s", r.c);
        cJSON_AddNumberToObject(o, "actual_s", r.d);
        const uint16_t ma = static_cast<uint16_t>(r.e & 0xFFFFu);
        const uint16_t dl = static_cast<uint16_t>(r.e >> 16);
        if (ma > 0) {
            cJSON_AddNumberToObject(o, "current_ma", ma);
        } else {
            cJSON_AddNullToObject(o, "current_ma");
        }
        if (dl != kHistoryNoVolume) {
            cJSON_AddNumberToObject(o, "litres", dl / 10.0);
        } else {
            cJSON_AddNullToObject(o, "litres");
        }
        break;
    }
    case HistoryKind::ProgramStarted:
        addProgram(o, r.programId);
        cJSON_AddStringToObject(
            o, "source",
            r.a <= static_cast<uint8_t>(RunSource::ManualZone) ? toString(static_cast<RunSource>(r.a)) : "unknown");
        cJSON_AddBoolToObject(o, "late", r.b != 0);
        break;
    case HistoryKind::ProgramSkipped:
        addProgram(o, r.programId);
        cJSON_AddStringToObject(o, "reason",
                                r.a <= static_cast<uint8_t>(SkipReason::HardwareFault)
                                    ? toString(static_cast<SkipReason>(r.a))
                                    : "unknown");
        break;
    case HistoryKind::ProgramMissed:
        addProgram(o, r.programId);
        cJSON_AddNumberToObject(o, "scheduled_utc", r.d);
        break;
    case HistoryKind::RunDropped:
        addHistoryZone(o, r.zone);
        addProgram(o, r.programId);
        cJSON_AddStringToObject(
            o, "source",
            r.a <= static_cast<uint8_t>(RunSource::ManualZone) ? toString(static_cast<RunSource>(r.a)) : "unknown");
        cJSON_AddStringToObject(o, "reason", toString(static_cast<StartError>(r.b)));
        break;
    case HistoryKind::FaultRaised:
    case HistoryKind::FaultCleared:
        addHistoryZone(o, r.zone);
        cJSON_AddStringToObject(o, "code", enumName(r.a, FaultCode::Count));
        break;
    case HistoryKind::Boot:
        cJSON_AddStringToObject(o, "reset_reason",
                                r.a <= static_cast<uint8_t>(HistoryResetReason::DeepSleep)
                                    ? toString(static_cast<HistoryResetReason>(r.a))
                                    : "unknown");
        break;
    case HistoryKind::TimeSet:
        cJSON_AddStringToObject(o, "status",
                                r.a <= static_cast<uint8_t>(TimeStatus::Synced) ? toString(static_cast<TimeStatus>(r.a))
                                                                                : "unknown");
        break;
    case HistoryKind::ConfigChanged:
        break;
    case HistoryKind::RainDelay:
        if (r.d != 0) {
            cJSON_AddNumberToObject(o, "until_utc", r.d);
        } else {
            cJSON_AddNullToObject(o, "until_utc");
        }
        break;
    case HistoryKind::Sensor:
        cJSON_AddStringToObject(
            o, "sensor",
            r.a <= static_cast<uint8_t>(SensorId::AcPresent) ? sensorName(static_cast<SensorId>(r.a)) : "unknown");
        cJSON_AddBoolToObject(o, "active", r.b != 0);
        break;
    case HistoryKind::Maintenance:
        cJSON_AddBoolToObject(o, "on", r.b != 0);
        break;
    case HistoryKind::PauseExpired:
        break;
    case HistoryKind::Ota: {
        static constexpr const char *kSources[] = {"none", "upload", "pull"};
        cJSON_AddStringToObject(o, "stage",
                                r.a <= static_cast<uint8_t>(HistoryOtaStage::Installed)
                                    ? toString(static_cast<HistoryOtaStage>(r.a))
                                    : "unknown");
        cJSON_AddStringToObject(o, "source", r.b < 3 ? kSources[r.b] : "none");
        if (r.c != 0) {
            cJSON_AddStringToObject(o, "error", toString(static_cast<ota::OtaError>(r.c)));
        } else {
            cJSON_AddNullToObject(o, "error");
        }
        break;
    }
    case HistoryKind::None:
    case HistoryKind::Count:
        break;
    }
    return o;
}

cJSON *historyPageToJson(const HistoryPage &page, std::size_t capacity)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *a = cJSON_AddArrayToObject(o, "records");
    for (const HistoryEntry &e : page.entries) {
        cJSON_AddItemToArray(a, historyRecordToJson(e.id, e.record));
    }
    if (page.more && !page.entries.empty()) {
        cJSON_AddNumberToObject(o, "next_before", static_cast<double>(page.entries.back().id));
    } else {
        cJSON_AddNullToObject(o, "next_before");
    }
    if (page.newestId > 0) {
        cJSON_AddNumberToObject(o, "newest_id", static_cast<double>(page.newestId));
        cJSON_AddNumberToObject(o, "oldest_id", static_cast<double>(page.oldestId));
    } else {
        cJSON_AddNullToObject(o, "newest_id");
        cJSON_AddNullToObject(o, "oldest_id");
    }
    cJSON_AddNumberToObject(o, "capacity", static_cast<double>(capacity));
    cJSON_AddNumberToObject(o, "unsaved", static_cast<double>(page.unflushed));
    return o;
}

cJSON *netStatusToJson(const net::NetStatus &n)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", net::toString(n.state));
    cJSON_AddBoolToObject(o, "provisioned", n.provisioned);
    cJSON_AddStringToObject(o, "provisioning", net::toString(n.provisioning));
    cJSON_AddStringToObject(o, "hostname", n.hostname.c_str());
    cJSON_AddStringToObject(o, "mac", n.mac.c_str());
    cJSON_AddStringToObject(o, "ssid", n.ssid.c_str());
    cJSON_AddStringToObject(o, "bssid", n.bssid.c_str());
    cJSON_AddNumberToObject(o, "channel", n.channel);
    cJSON_AddNumberToObject(o, "rssi", n.rssi);
    cJSON_AddStringToObject(o, "ip", n.ip.c_str());
    cJSON_AddStringToObject(o, "netmask", n.netmask.c_str());
    cJSON_AddStringToObject(o, "gateway", n.gateway.c_str());
    cJSON_AddNumberToObject(o, "reconnect_attempt", n.reconnectAttempt);
    cJSON_AddNumberToObject(o, "retry_in_ms", n.retryInMs);
    cJSON_AddNumberToObject(o, "last_disconnect_reason", n.lastDisconnectReason);
    cJSON_AddNumberToObject(o, "disconnects", n.disconnects);
    cJSON_AddBoolToObject(o, "mdns", n.mdnsRunning);
    return o;
}

cJSON *otaStatusToJson(const ota::OtaStatus &s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "phase", ota::toString(s.phase));
    cJSON_AddStringToObject(o, "error", ota::toString(s.error));
    cJSON_AddStringToObject(o, "source", ota::toString(s.source));
    cJSON_AddBoolToObject(o, "forced", s.forced);
    cJSON_AddNumberToObject(o, "received_bytes", s.receivedBytes);
    cJSON_AddNumberToObject(o, "total_bytes", s.totalBytes);
    cJSON_AddStringToObject(o, "new_version", s.newVersion.c_str());
    return o;
}

std::string sseMessage(std::string_view event, uint32_t id, std::string_view json)
{
    std::string m;
    m.reserve(json.size() + event.size() + 32);
    m += "id: ";
    m += std::to_string(id);
    m += "\nevent: ";
    m += event;
    m += "\ndata: ";
    m += json;
    m += "\n\n";
    return m;
}

} // namespace zf::web
