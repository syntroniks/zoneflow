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
            --depth;
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
    return o;
}

cJSON *queueToJson(const QueueStatus &q, uint16_t pauseTimeoutMin)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", toString(q.state));
    cJSON_AddNumberToObject(o, "pause_timeout_min", pauseTimeoutMin);
    cJSON_AddNumberToObject(o, "length", q.length);
    cJSON_AddNumberToObject(o, "current_remaining_s", q.currentRemainingSec);
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
    cJSON_AddItemToObject(o, "faults", faultsToJson(s));
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
    case EventType::QueueChanged:
    case EventType::ConfigApplied:
    case EventType::PauseExpired:
    case EventType::Count:
        break;
    }
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
