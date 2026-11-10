// SPDX-License-Identifier: Apache-2.0
#include "zf_mqtt/mqtt_logic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "cJSON.h"

namespace zf::mqtt {
namespace {

struct JsonDeleter {
    void operator()(cJSON *p) const { cJSON_Delete(p); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string print(cJSON *o)
{
    JsonPtr holder(o);
    if (o == nullptr) {
        return {};
    }
    char *s = cJSON_PrintUnformatted(o);
    if (s == nullptr) {
        return {};
    }
    std::string out(s);
    cJSON_free(s);
    return out;
}

bool faultActive(const SystemState &s, FaultCode c) { return (s.activeFaults & (1u << static_cast<unsigned>(c))) != 0; }

void addZoneNumber(cJSON *o, const char *key, ZoneIndex z)
{
    if (z == kNoZone) {
        cJSON_AddNullToObject(o, key);
    } else {
        cJSON_AddNumberToObject(o, key, z + 1);
    }
}

void addProgramId(cJSON *o, uint16_t id)
{
    if (id == 0) {
        cJSON_AddNullToObject(o, "program_id");
    } else {
        cJSON_AddNumberToObject(o, "program_id", id);
    }
}

bool headIsProgram(const QueueStatus &q, uint16_t id)
{
    return id != 0 && q.state != QueueState::Idle && q.shownCount > 0 && q.items[0].programId == id;
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

// ---- payload parsing -----------------------------------------------------------------

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

// A plain decimal number ("600", "24.0", "-1"). No exponent, no hex, no
// leading '+', at most 15 characters.
std::optional<double> plainNumber(std::string_view s)
{
    if (s.empty() || s.size() > 15) {
        return std::nullopt;
    }
    std::size_t i = 0;
    if (s[0] == '-') {
        i = 1;
    }
    bool digits = false;
    bool dot = false;
    for (; i < s.size(); ++i) {
        if (s[i] >= '0' && s[i] <= '9') {
            digits = true;
        } else if (s[i] == '.' && !dot) {
            dot = true;
        } else {
            return std::nullopt;
        }
    }
    if (!digits) {
        return std::nullopt;
    }
    char buf[16];
    std::copy(s.begin(), s.end(), buf);
    buf[s.size()] = '\0';
    return std::strtod(buf, nullptr);
}

// Whole number in [min, max] from a JSON number item.
bool wholeIn(double v, double min, double max, int64_t &out)
{
    if (!std::isfinite(v) || std::floor(v) != v || v < min || v > max) {
        return false;
    }
    out = static_cast<int64_t>(v);
    return true;
}

// Payload is either a bare number or a JSON object; a JSON object is parsed
// into `obj` (depth-limited: at most 2 levels are accepted).
enum class PayloadKind : uint8_t { Empty, Number, Object, Word, Bad };

struct Payload {
    PayloadKind kind = PayloadKind::Bad;
    double number = 0;
    std::string_view word;
    JsonPtr obj;
};

Payload classify(std::string_view raw)
{
    Payload p;
    const std::string_view s = trim(raw);
    if (s.empty()) {
        p.kind = PayloadKind::Empty;
        return p;
    }
    if (const auto n = plainNumber(s)) {
        p.kind = PayloadKind::Number;
        p.number = *n;
        return p;
    }
    if (s.front() == '{') {
        int depth = 0;
        for (const char c : s) {
            depth += (c == '{' || c == '[') ? 1 : 0;
            if (depth > 2) {
                return p; // Bad: nothing we accept nests that deep
            }
        }
        p.obj.reset(cJSON_ParseWithLength(s.data(), s.size()));
        p.kind = (p.obj && cJSON_IsObject(p.obj.get())) ? PayloadKind::Object : PayloadKind::Bad;
        return p;
    }
    for (const char c : s) {
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) {
            return p;
        }
    }
    p.kind = PayloadKind::Word;
    p.word = s;
    return p;
}

bool equalsNoCase(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char x = (a[i] >= 'a' && a[i] <= 'z') ? static_cast<char>(a[i] - 32) : a[i];
        const char y = (b[i] >= 'a' && b[i] <= 'z') ? static_cast<char>(b[i] - 32) : b[i];
        if (x != y) {
            return false;
        }
    }
    return true;
}

// Splits "a/b/c" into at most N levels; false if there are more.
template <std::size_t N> bool splitLevels(std::string_view s, std::array<std::string_view, N> &out, std::size_t &count)
{
    count = 0;
    while (true) {
        if (count == N) {
            return false;
        }
        const std::size_t slash = s.find('/');
        out[count++] = s.substr(0, slash);
        if (slash == std::string_view::npos) {
            return true;
        }
        s.remove_prefix(slash + 1);
    }
}

bool parseIndex(std::string_view s, uint32_t max, uint32_t &out)
{
    if (s.empty() || s.size() > 5 || (s.size() > 1 && s[0] == '0')) {
        return false;
    }
    uint32_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
        v = v * 10 + static_cast<uint32_t>(c - '0');
    }
    if (v == 0 || v > max) {
        return false;
    }
    out = v;
    return true;
}

ParsedCommand invalid(ParsedCommand p, std::string error)
{
    p.action = Action::None;
    p.outcome = Outcome::Invalid;
    p.error = std::move(error);
    return p;
}

ParsedCommand post(ParsedCommand p, const Command &c)
{
    p.action = Action::Post;
    p.outcome = Outcome::Queued;
    p.command = c;
    return p;
}

// Duration for a zone run: a number or {"duration_s": n}. 1 .. kHardMaxRunSec seconds, whole. The zone driver
// still clamps to the zone's max_run_s and its remaining budget.
bool runDuration(const Payload &pl, uint32_t &out, std::string &error)
{
    double v = 0;
    switch (pl.kind) {
    case PayloadKind::Number:
        v = pl.number;
        break;
    case PayloadKind::Object: {
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(pl.obj.get(), "duration_s");
        if (!cJSON_IsNumber(d)) {
            error = "duration_s_required";
            return false;
        }
        v = d->valuedouble;
        break;
    }
    default:
        error = "bad_payload";
        return false;
    }
    int64_t sec = 0;
    if (!wholeIn(v, 1, kHardMaxRunSec, sec)) {
        error = "duration_out_of_range";
        return false;
    }
    out = static_cast<uint32_t>(sec);
    return true;
}

const Program *findProgram(const Config &cfg, uint16_t id)
{
    for (std::size_t i = 0; i < cfg.schedule.programCount; ++i) {
        if (cfg.schedule.programs[i].id == id) {
            return &cfg.schedule.programs[i];
        }
    }
    return nullptr;
}

std::optional<bool> onOff(const Payload &pl)
{
    if (pl.kind == PayloadKind::Word) {
        if (equalsNoCase(pl.word, "ON") || equalsNoCase(pl.word, "TRUE")) {
            return true;
        }
        if (equalsNoCase(pl.word, "OFF") || equalsNoCase(pl.word, "FALSE")) {
            return false;
        }
    }
    if (pl.kind == PayloadKind::Number && (pl.number == 0 || pl.number == 1)) {
        return pl.number == 1;
    }
    return std::nullopt;
}

} // namespace

// ---- identity -----------------------------------------------------------------------------

std::string deviceIdFromMac(std::string_view mac)
{
    std::string out;
    for (const char c : mac) {
        if (c == ':' || c == '-') {
            continue;
        }
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) {
            return {};
        }
        out.push_back(c >= 'A' && c <= 'F' ? static_cast<char>(c + 32) : c);
    }
    return out.size() == 12 ? out : std::string{};
}

std::string rootTopic(const MqttConfig &cfg, std::string_view deviceId)
{
    if (!cfg.baseTopic.empty()) {
        return std::string(cfg.baseTopic.view());
    }
    return "zoneflow/" + std::string(deviceId);
}

std::string clientId(const MqttConfig &cfg, std::string_view deviceId)
{
    if (!cfg.clientId.empty()) {
        return std::string(cfg.clientId.view());
    }
    return "zoneflow-" + std::string(deviceId);
}

// ---- payloads ---------------------------------------------------------------------------

const char *haValveState(ValveState s)
{
    switch (s) {
    case ValveState::Off:
        return "closed";
    case ValveState::Waiting:
        return "opening";
    case ValveState::Open:
        return "open";
    }
    return "closed";
}

std::string devicePayload(const SystemState &s, const Config &cfg, const DeviceInfo &d)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", cfg.deviceName.c_str());
    cJSON_AddStringToObject(o, "firmware", d.firmware.c_str());
    cJSON_AddStringToObject(o, "hostname", d.hostname.c_str());
    cJSON_AddStringToObject(o, "ip", d.ip.c_str());
    if (d.wifiConnected) {
        cJSON_AddNumberToObject(o, "rssi", d.rssi);
    } else {
        cJSON_AddNullToObject(o, "rssi");
    }
    cJSON_AddNumberToObject(o, "uptime_s", static_cast<double>(s.monoMs / 1000));
    cJSON_AddStringToObject(o, "time_status", toString(s.timeStatus));
    cJSON_AddNumberToObject(o, "utc", static_cast<double>(s.utc));
    cJSON_AddBoolToObject(o, "maintenance", s.maintenance);
    cJSON *master = cJSON_AddObjectToObject(o, "master");
    cJSON_AddBoolToObject(master, "enabled", s.masterEnabled);
    cJSON_AddBoolToObject(master, "on", s.masterOn);
    cJSON_AddBoolToObject(master, "locked_out", s.masterLockedOut);
    cJSON_AddNumberToObject(master, "fault_count", s.masterFaultCount);
    cJSON *inhibit = cJSON_AddObjectToObject(o, "inhibit");
    cJSON_AddBoolToObject(inhibit, "sensor", (s.inhibitFlags & kInhibitSensor) != 0);
    cJSON_AddBoolToObject(inhibit, "no_ac", (s.inhibitFlags & kInhibitNoAc) != 0);
    cJSON_AddBoolToObject(inhibit, "maintenance", (s.inhibitFlags & kInhibitMaintenance) != 0);
    cJSON_AddBoolToObject(inhibit, "master_lockout", (s.inhibitFlags & kInhibitMasterLockout) != 0);
    cJSON_AddNumberToObject(o, "zone_count", cfg.zoneCount);
    cJSON_AddNumberToObject(o, "config_revision", s.configRevision);
    return print(o);
}

std::string zonePayload(std::size_t index, const ZoneStatus &z, const ZoneConfig &cfg, const QueueStatus &q)
{
    bool queued = false;
    for (std::size_t i = 0; i < q.shownCount && i < q.items.size(); ++i) {
        queued = queued || q.items[i].zone == index;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "zone", static_cast<double>(index + 1));
    cJSON_AddStringToObject(o, "name", cfg.name.c_str());
    cJSON_AddBoolToObject(o, "enabled", cfg.enabled);
    cJSON_AddStringToObject(o, "state", toString(z.state));
    cJSON_AddStringToObject(o, "valve", haValveState(z.state));
    cJSON_AddStringToObject(o, "switch", z.state == ValveState::Off ? "OFF" : "ON");
    cJSON_AddBoolToObject(o, "queued", queued);
    cJSON_AddNumberToObject(o, "remaining_s", z.remainingSec);
    cJSON_AddNumberToObject(o, "default_run_s", cfg.defaultRunSec);
    cJSON_AddNumberToObject(o, "default_run_min", std::round(cfg.defaultRunSec / 60.0 * 10.0) / 10.0);
    cJSON_AddNumberToObject(o, "max_run_s", cfg.maxRunSec);
    cJSON_AddStringToObject(o, "last_stop", toString(z.lastStop));
    cJSON_AddStringToObject(o, "fault", toString(z.fault));
    cJSON_AddBoolToObject(o, "fault_active", z.fault != FaultCode::None || z.lockedOut);
    cJSON_AddBoolToObject(o, "locked_out", z.lockedOut);
    cJSON_AddBoolToObject(o, "resting", z.restRemainingSec > 0);
    cJSON_AddNumberToObject(o, "rest_remaining_s", z.restRemainingSec);
    return print(o);
}

std::string programPayload(const Program &p, const QueueStatus &q)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", p.id);
    cJSON_AddStringToObject(o, "name", p.name.c_str());
    cJSON_AddBoolToObject(o, "enabled", p.enabled);
    cJSON_AddBoolToObject(o, "running", headIsProgram(q, p.id));
    return print(o);
}

std::string queuePayload(const QueueStatus &q, uint16_t pauseTimeoutMin)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", toString(q.state));
    cJSON_AddNumberToObject(o, "length", q.length);
    cJSON_AddNumberToObject(o, "current_remaining_s", q.currentRemainingSec);
    cJSON_AddNumberToObject(o, "pause_timeout_min", pauseTimeoutMin);
    cJSON *items = cJSON_AddArrayToObject(o, "items");
    for (std::size_t i = 0; items != nullptr && i < q.shownCount && i < q.items.size(); ++i) {
        const QueueItem &it = q.items[i];
        cJSON *e = cJSON_CreateObject();
        addZoneNumber(e, "zone", it.zone);
        cJSON_AddNumberToObject(e, "duration_s", it.durationSec);
        cJSON_AddStringToObject(e, "source", toString(it.source));
        addProgramId(e, it.programId);
        cJSON_AddItemToArray(items, e);
    }
    return print(o);
}

std::string sensorsPayload(const SystemState &st)
{
    const SensorStatus &s = st.sensors;
    cJSON *o = cJSON_CreateObject();
    cJSON *rain = cJSON_AddObjectToObject(o, "rain");
    cJSON_AddBoolToObject(rain, "enabled", s.rainEnabled);
    cJSON_AddBoolToObject(rain, "active", s.rainActive);
    cJSON *spare = cJSON_AddObjectToObject(o, "spare");
    cJSON_AddBoolToObject(spare, "enabled", s.spareEnabled);
    cJSON_AddBoolToObject(spare, "active", s.spareActive);
    cJSON_AddBoolToObject(o, "ac_present", s.acPresent);
    if (s.valveCurrentMa < 0) {
        cJSON_AddNullToObject(o, "valve_current_ma");
    } else {
        cJSON_AddNumberToObject(o, "valve_current_ma", s.valveCurrentMa);
    }
    cJSON_AddBoolToObject(o, "current_sense_available", !faultActive(st, FaultCode::CurrentSenseUnavailable));
    cJSON_AddBoolToObject(o, "over_current_tripped", faultActive(st, FaultCode::OverCurrentTrip));
    return print(o);
}

std::string flowPayload(const SystemState &st)
{
    const SensorStatus &s = st.sensors;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", s.flowEnabled);
    // Two and three decimals: 0.01 L/min and 1 mL are below what a pulse
    // flow meter resolves.
    cJSON_AddNumberToObject(o, "rate_lpm", std::round(static_cast<double>(s.flowLpm) * 100.0) / 100.0);
    cJSON_AddNumberToObject(o, "total_l", std::round(static_cast<double>(s.flowTotalLitres) * 1000.0) / 1000.0);
    return print(o);
}

std::string faultsPayload(const SystemState &s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *active = cJSON_AddArrayToObject(o, "active");
    bool any = false;
    for (unsigned i = 1; i < static_cast<unsigned>(FaultCode::Count); ++i) {
        if ((s.activeFaults & (1u << i)) != 0) {
            cJSON_AddItemToArray(active, cJSON_CreateString(toString(static_cast<FaultCode>(i))));
            any = true;
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
            any = true;
        }
    }
    cJSON_AddBoolToObject(o, "master_locked_out", s.masterLockedOut);
    any = any || s.masterLockedOut;
    cJSON_AddBoolToObject(o, "any", any);
    return print(o);
}

std::string rainDelayPayload(const SystemState &s)
{
    cJSON *o = cJSON_CreateObject();
    const bool active = s.rainDelayUntil != 0 && s.rainDelayUntil > s.utc;
    const int64_t remaining = active ? s.rainDelayUntil - s.utc : 0;
    cJSON_AddBoolToObject(o, "active", active);
    if (s.rainDelayUntil != 0) {
        cJSON_AddNumberToObject(o, "until_utc", static_cast<double>(s.rainDelayUntil));
    } else {
        cJSON_AddNullToObject(o, "until_utc");
    }
    cJSON_AddNumberToObject(o, "remaining_s", static_cast<double>(remaining));
    // Whole hours left, rounded up: what the Home Assistant number shows.
    cJSON_AddNumberToObject(o, "hours", static_cast<double>((remaining + 3599) / 3600));
    return print(o);
}

std::string updatePayload(std::string_view installed, const ota::OtaStatus &o)
{
    cJSON *j = cJSON_CreateObject();
    const std::string inst(installed);
    const bool running = o.phase != ota::OtaPhase::Idle && o.phase != ota::OtaPhase::Failed;
    cJSON_AddStringToObject(j, "installed_version", inst.c_str());
    // No release feed: the device only knows a newer version while one is
    // being installed (upload or pull through the REST API).
    const bool newer = running && !o.newVersion.empty();
    cJSON_AddStringToObject(j, "latest_version", newer ? o.newVersion.c_str() : inst.c_str());
    cJSON_AddStringToObject(j, "title", "ZoneFlow firmware");
    cJSON_AddStringToObject(j, "release_url", "https://github.com/syntroniks/zoneflow/releases");
    cJSON_AddBoolToObject(j, "in_progress", running);
    if (running && o.totalBytes > 0) {
        const uint32_t pct =
            static_cast<uint32_t>(std::min<uint64_t>(100, uint64_t{o.receivedBytes} * 100 / o.totalBytes));
        cJSON_AddNumberToObject(j, "update_percentage", pct);
    } else {
        cJSON_AddNullToObject(j, "update_percentage");
    }
    return print(j);
}

std::string eventPayload(const Event &e)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "type", toString(e.type));
    cJSON_AddNumberToObject(o, "utc", static_cast<double>(e.utc));
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
        cJSON_AddNumberToObject(o, "until_utc", static_cast<double>(e.rainDelay.until));
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
        addProgramId(o, e.rejected.programId);
        cJSON_AddStringToObject(o, "error", toString(e.rejected.error));
        break;
    case EventType::RunDropped:
        addZoneNumber(o, "zone", e.dropped.zone);
        addProgramId(o, e.dropped.programId);
        cJSON_AddStringToObject(o, "source", toString(e.dropped.source));
        cJSON_AddStringToObject(o, "reason", toString(e.dropped.reason));
        break;
    case EventType::QueueChanged:
    case EventType::ConfigApplied:
    case EventType::PauseExpired:
    case EventType::RunEnded: // the zone's state topic already covers it
    case EventType::Count:
        break;
    }
    return print(o);
}

// ---- change detection -----------------------------------------------------------------------

void markChanges(const SystemState &a, const SystemState &b, bool periodic, Dirty &d)
{
    if (periodic || a.timeStatus != b.timeStatus || a.maintenance != b.maintenance ||
        a.masterEnabled != b.masterEnabled || a.masterOn != b.masterOn || a.masterLockedOut != b.masterLockedOut ||
        a.masterFaultCount != b.masterFaultCount || a.inhibitFlags != b.inhibitFlags ||
        a.configRevision != b.configRevision || a.zoneCount != b.zoneCount) {
        d.device = true;
    }
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        const ZoneStatus &x = a.zones[i];
        const ZoneStatus &y = b.zones[i];
        const bool slow = x.state != y.state || x.lastStop != y.lastStop || x.fault != y.fault ||
                          x.enabled != y.enabled || x.lockedOut != y.lockedOut ||
                          (x.restRemainingSec > 0) != (y.restRemainingSec > 0);
        const bool counting = y.state != ValveState::Off || y.restRemainingSec > 0 ||
                              x.remainingSec != y.remainingSec || x.restRemainingSec != y.restRemainingSec;
        if (slow || (periodic && counting)) {
            d.zones = static_cast<uint16_t>(d.zones | (1u << i));
        }
    }
    bool queueSlow =
        a.queue.state != b.queue.state || a.queue.length != b.queue.length || a.queue.shownCount != b.queue.shownCount;
    for (std::size_t i = 0; !queueSlow && i < b.queue.shownCount && i < b.queue.items.size(); ++i) {
        queueSlow = a.queue.items[i].id != b.queue.items[i].id;
    }
    if (queueSlow || (periodic && b.queue.state != QueueState::Idle)) {
        d.queue = true;
    }
    if (queueSlow) {
        // Queued-zone flags and the running program follow the queue.
        d.zones = 0xFFFF;
        d.programs = true;
    }
    const SensorStatus &x = a.sensors;
    const SensorStatus &y = b.sensors;
    const uint32_t senseBits = (1u << static_cast<unsigned>(FaultCode::CurrentSenseUnavailable)) |
                               (1u << static_cast<unsigned>(FaultCode::OverCurrentTrip));
    if (x.rainEnabled != y.rainEnabled || x.rainActive != y.rainActive || x.spareEnabled != y.spareEnabled ||
        x.spareActive != y.spareActive || x.acPresent != y.acPresent ||
        (a.activeFaults & senseBits) != (b.activeFaults & senseBits) ||
        (x.valveCurrentMa < 0) != (y.valveCurrentMa < 0) || (periodic && x.valveCurrentMa != y.valveCurrentMa)) {
        d.sensors = true;
    }
    if (x.flowEnabled != y.flowEnabled ||
        (periodic && (x.flowLpm != y.flowLpm || x.flowTotalLitres != y.flowTotalLitres))) {
        d.flow = true;
    }
    bool faults = a.activeFaults != b.activeFaults || a.masterLockedOut != b.masterLockedOut;
    for (std::size_t i = 0; !faults && i < kMaxZones; ++i) {
        faults = a.zones[i].fault != b.zones[i].fault || a.zones[i].lockedOut != b.zones[i].lockedOut;
    }
    d.faults = d.faults || faults;
    const bool delayActive = b.rainDelayUntil != 0 && b.rainDelayUntil > b.utc;
    if (a.rainDelayUntil != b.rainDelayUntil || (periodic && delayActive) ||
        ((a.rainDelayUntil > a.utc) != delayActive)) {
        d.rainDelay = true;
    }
}

// ---- commands --------------------------------------------------------------------------------

const char *toString(Outcome o)
{
    switch (o) {
    case Outcome::Queued:
        return "queued";
    case Outcome::Applied:
        return "applied";
    case Outcome::Rejected:
        return "rejected";
    case Outcome::Invalid:
        return "invalid";
    case Outcome::IgnoredRetained:
        return "ignored_retained";
    case Outcome::RateLimited:
        return "rate_limited";
    case Outcome::Busy:
        return "busy";
    }
    return "?";
}

ParsedCommand parseCommand(const Topics &topics, std::string_view topic, std::string_view payload, bool retained,
                           const Config &cfg, UtcSeconds now, bool timeValid)
{
    ParsedCommand p;
    const std::string prefix = topics.root() + "/cmd/";
    if (topic.size() <= prefix.size() || topic.substr(0, prefix.size()) != prefix) {
        p.name = "unknown";
        return invalid(std::move(p), "unknown_command");
    }
    std::array<std::string_view, 4> lv{};
    std::size_t n = 0;
    const bool fits = splitLevels(topic.substr(prefix.size()), lv, n);
    p.name = fits ? std::string(lv[0]) : "unknown";
    if (lv[0] == "zone" && n >= 3) {
        p.name = "zone_" + std::string(lv[2]);
    } else if (lv[0] == "program" && n >= 3) {
        p.name = "program_" + std::string(lv[2]);
    }
    // Rule: a retained message is never a command. A retained "run" left on
    // the broker would otherwise start watering at every reconnect.
    if (retained) {
        p.outcome = Outcome::IgnoredRetained;
        p.error = "retained";
        return p;
    }
    if (payload.size() > kMaxCommandPayload) {
        return invalid(std::move(p), "payload_too_large");
    }
    if (!fits) {
        return invalid(std::move(p), "unknown_command");
    }
    const Payload pl = classify(payload);
    if (pl.kind == PayloadKind::Bad) {
        return invalid(std::move(p), "bad_payload");
    }
    // Rule: an empty message is never a command. Clearing a retained message
    // (publishing an empty retained payload) delivers an empty, non-retained
    // message to every current subscriber; it must not start anything.
    if (pl.kind == PayloadKind::Empty) {
        return invalid(std::move(p), "empty_payload");
    }

    if (lv[0] == "zone" && n == 3) {
        uint32_t number = 0;
        if (!parseIndex(lv[1], cfg.zoneCount, number)) {
            return invalid(std::move(p), "no_such_zone");
        }
        const ZoneIndex z = static_cast<ZoneIndex>(number - 1);
        p.zone = z;
        const ZoneConfig &zc = cfg.zones[z];
        const std::string_view verb = lv[2];
        if (verb == "run" || verb == "set") {
            bool stop = false;
            if (verb == "set") {
                if (pl.kind != PayloadKind::Word) {
                    return invalid(std::move(p), "bad_payload");
                }
                if (equalsNoCase(pl.word, "OPEN") || equalsNoCase(pl.word, "ON")) {
                    stop = false;
                } else if (equalsNoCase(pl.word, "CLOSE") || equalsNoCase(pl.word, "OFF") ||
                           equalsNoCase(pl.word, "STOP")) {
                    stop = true;
                } else {
                    return invalid(std::move(p), "bad_payload");
                }
            }
            if (stop) {
                p.name = "zone_stop";
                p.action = Action::StopZone;
                p.outcome = Outcome::Queued;
                return p;
            }
            p.name = "zone_run";
            if (!zc.enabled) {
                return invalid(std::move(p), "zone_disabled");
            }
            uint32_t sec = 0;
            std::string err;
            if (verb == "set") {
                sec = zc.defaultRunSec;
            } else if (!runDuration(pl, sec, err)) {
                return invalid(std::move(p), err);
            }
            return post(std::move(p), Command::runZone(z, sec));
        }
        if (verb == "stop") {
            p.action = Action::StopZone;
            p.outcome = Outcome::Queued;
            return p;
        }
        if (verb == "default_run_s") {
            int64_t sec = 0;
            if (pl.kind != PayloadKind::Number || !wholeIn(std::round(pl.number), 1, kHardMaxRunSec, sec)) {
                return invalid(std::move(p), "duration_out_of_range");
            }
            p.action = Action::SetRunTime;
            p.outcome = Outcome::Applied;
            p.value = static_cast<uint32_t>(sec);
            return p;
        }
        return invalid(std::move(p), "unknown_command");
    }

    if (lv[0] == "program" && n == 3) {
        uint32_t id = 0;
        if (!parseIndex(lv[1], 65535, id) || findProgram(cfg, static_cast<uint16_t>(id)) == nullptr) {
            return invalid(std::move(p), "no_such_program");
        }
        p.programId = static_cast<uint16_t>(id);
        if (lv[2] == "run") {
            return post(std::move(p), Command::runProgram(p.programId));
        }
        if (lv[2] == "enabled") {
            const auto on = onOff(pl);
            if (!on) {
                return invalid(std::move(p), "bad_payload");
            }
            p.action = Action::SetProgramEnabled;
            p.outcome = Outcome::Applied;
            p.flag = *on;
            return p;
        }
        return invalid(std::move(p), "unknown_command");
    }

    if (n == 1) {
        struct Simple {
            std::string_view name;
            CommandType type;
        };
        static constexpr Simple kSimple[] = {{"stop_all", CommandType::StopAll},
                                             {"pause", CommandType::Pause},
                                             {"resume", CommandType::Resume},
                                             {"skip", CommandType::Skip},
                                             {"clear_faults", CommandType::ClearFaults}};
        for (const Simple &s : kSimple) {
            if (lv[0] == s.name) {
                return post(std::move(p), Command::simple(s.type));
            }
        }
        if (lv[0] == "rain_delay") {
            int64_t value = 0;
            UtcSeconds until = 0;
            bool hours = true;
            if (pl.kind == PayloadKind::Number) {
                value = 0;
                if (!wholeIn(std::round(pl.number), 0, kMaxRainDelayHours, value)) {
                    return invalid(std::move(p), "hours_out_of_range");
                }
            } else if (pl.kind == PayloadKind::Object) {
                const cJSON *h = cJSON_GetObjectItemCaseSensitive(pl.obj.get(), "hours");
                const cJSON *u = cJSON_GetObjectItemCaseSensitive(pl.obj.get(), "until_utc");
                if ((h == nullptr) == (u == nullptr)) {
                    return invalid(std::move(p), "give_hours_or_until_utc");
                }
                if (h != nullptr) {
                    if (!cJSON_IsNumber(h) || !wholeIn(h->valuedouble, 0, kMaxRainDelayHours, value)) {
                        return invalid(std::move(p), "hours_out_of_range");
                    }
                } else {
                    hours = false;
                    if (!cJSON_IsNumber(u) || !wholeIn(u->valuedouble, 0, 4102444800.0, value)) {
                        return invalid(std::move(p), "until_out_of_range");
                    }
                }
            } else {
                return invalid(std::move(p), "bad_payload");
            }
            if (value == 0) {
                return post(std::move(p), Command::rainDelay(0));
            }
            if (!timeValid) {
                return invalid(std::move(p), "time_not_set");
            }
            if (hours) {
                until = now + value * 3600;
            } else {
                until = value;
                if (until <= now || until > now + int64_t{kMaxRainDelayHours} * 3600) {
                    return invalid(std::move(p), "until_out_of_range");
                }
            }
            return post(std::move(p), Command::rainDelay(until));
        }
    }
    return invalid(std::move(p), "unknown_command");
}

std::string resolveStopZone(ZoneIndex zone, const SystemState &s, Command &out)
{
    if (zone >= kMaxZones) {
        return "no_such_zone";
    }
    const bool head = s.queue.state != QueueState::Idle && s.queue.shownCount > 0 && s.queue.items[0].zone == zone;
    if (head) {
        out = Command::simple(CommandType::Skip);
        return {};
    }
    if (s.zones[zone].state == ValveState::Off) {
        return "not_running";
    }
    return "not_head";
}

std::string resultPayload(std::string_view command, Outcome outcome, std::string_view error, ZoneIndex zone,
                          uint16_t programId)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "command", std::string(command).c_str());
    cJSON_AddStringToObject(o, "status", toString(outcome));
    if (error.empty()) {
        cJSON_AddNullToObject(o, "error");
    } else {
        cJSON_AddStringToObject(o, "error", std::string(error).c_str());
    }
    if (zone != kNoZone) {
        cJSON_AddNumberToObject(o, "zone", zone + 1);
    }
    if (programId != 0) {
        cJSON_AddNumberToObject(o, "program_id", programId);
    }
    return print(o);
}

std::string rejectionPayload(const CommandRejectedData &r)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "command", toString(r.command));
    cJSON_AddStringToObject(o, "status", toString(Outcome::Rejected));
    cJSON_AddStringToObject(o, "error", toString(r.error));
    if (r.zone != kNoZone && r.command == CommandType::RunZone) {
        cJSON_AddNumberToObject(o, "zone", r.zone + 1);
    }
    if (r.programId != 0 && r.command == CommandType::RunProgram) {
        cJSON_AddNumberToObject(o, "program_id", r.programId);
    }
    return print(o);
}

// ---- pending configuration edits ---------------------------------------------------------------

void PendingEdits::setRunTime(ZoneIndex z, uint32_t sec)
{
    if (z < kMaxZones) {
        runMask_ = static_cast<uint16_t>(runMask_ | (1u << z));
        runSec_[z] = sec;
    }
}

void PendingEdits::setProgramEnabled(uint16_t id, bool on)
{
    for (std::size_t i = 0; i < programCount_; ++i) {
        if (programs_[i].id == id) {
            programs_[i].on = on;
            return;
        }
    }
    if (programCount_ < programs_.size()) {
        programs_[programCount_++] = {id, on};
    }
}

bool PendingEdits::applyTo(Config &cfg)
{
    bool changed = false;
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        if ((runMask_ & (1u << z)) != 0 && cfg.zones[z].defaultRunSec != runSec_[z]) {
            cfg.zones[z].defaultRunSec = runSec_[z];
            changed = true;
        }
    }
    for (std::size_t i = 0; i < programCount_; ++i) {
        for (std::size_t k = 0; k < cfg.schedule.programCount; ++k) {
            Program &p = cfg.schedule.programs[k];
            if (p.id == programs_[i].id && p.enabled != programs_[i].on) {
                p.enabled = programs_[i].on;
                changed = true;
            }
        }
    }
    runMask_ = 0;
    programCount_ = 0;
    return changed;
}

// ---- rate limiter ---------------------------------------------------------------------------------

bool RateLimiter::allow(MonoMs now)
{
    if (!started_) {
        started_ = true;
        last_ = now;
    }
    if (perSecond_ > 0 && now > last_) {
        const MonoMs interval = 1000 / perSecond_;
        const MonoMs earned = (now - last_) / interval;
        if (earned > 0) {
            tokens_ = static_cast<uint32_t>(std::min<MonoMs>(burst_, tokens_ + earned));
            last_ += earned * interval;
        }
    }
    if (tokens_ == 0) {
        return false;
    }
    --tokens_;
    return true;
}

// ---- discovery --------------------------------------------------------------------------------------

namespace {

constexpr const char *kValve = "valve";
constexpr const char *kSwitch = "switch";
constexpr const char *kNumber = "number";
constexpr const char *kBinarySensor = "binary_sensor";
constexpr const char *kSensor = "sensor";
constexpr const char *kButton = "button";
constexpr const char *kUpdate = "update";

std::string zoneId(std::size_t n, const char *what) { return "zone_" + std::to_string(n) + "_" + what; }
std::string programId(uint16_t id, const char *what) { return "program_" + std::to_string(id) + "_" + what; }

bool zoneActive(const Config &cfg, std::size_t i) { return i < cfg.zoneCount && cfg.zones[i].enabled; }

class Builder {
public:
    Builder(const Identity &id, const Topics &t, cJSON *cmps) : id_(id), t_(t), cmps_(cmps) {}

    cJSON *add(const std::string &cid, const char *platform, const std::string &name)
    {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "platform", platform);
        cJSON_AddStringToObject(c, "unique_id", ("zoneflow_" + id_.deviceId + "_" + cid).c_str());
        cJSON_AddStringToObject(c, "name", name.c_str());
        cJSON_AddItemToObject(cmps_, cid.c_str(), c);
        return c;
    }

    static void str(cJSON *c, const char *k, const std::string &v) { cJSON_AddStringToObject(c, k, v.c_str()); }
    static void str(cJSON *c, const char *k, const char *v) { cJSON_AddStringToObject(c, k, v); }

    const Topics &t() const { return t_; }

private:
    const Identity &id_;
    const Topics &t_;
    cJSON *cmps_;
};

void addZone(Builder &b, const Config &cfg, std::size_t i)
{
    const std::size_t n = i + 1;
    const std::string name(cfg.zones[i].name.view());
    const std::string state = b.t().zone(n);
    const std::string set = b.t().command("zone/" + std::to_string(n) + "/set");
    if (cfg.mqtt.zoneEntity == HaZoneEntity::Valve) {
        cJSON *v = b.add(zoneId(n, "valve"), kValve, name);
        Builder::str(v, "device_class", "water");
        Builder::str(v, "state_topic", state);
        Builder::str(v, "value_template", "{{ value_json.valve }}");
        Builder::str(v, "command_topic", set);
        Builder::str(v, "payload_open", "OPEN");
        Builder::str(v, "payload_close", "CLOSE");
        Builder::str(v, "payload_stop", "STOP");
        cJSON_AddBoolToObject(v, "reports_position", false);
        Builder::str(v, "json_attributes_topic", state);
    } else {
        cJSON *s = b.add(zoneId(n, "switch"), kSwitch, name);
        Builder::str(s, "state_topic", state);
        Builder::str(s, "value_template", "{{ value_json.switch }}");
        Builder::str(s, "command_topic", set);
        Builder::str(s, "payload_on", "ON");
        Builder::str(s, "payload_off", "OFF");
        Builder::str(s, "icon", "mdi:sprinkler-variant");
        Builder::str(s, "json_attributes_topic", state);
    }
    cJSON *num = b.add(zoneId(n, "run_time"), kNumber, name + " run time");
    Builder::str(num, "state_topic", state);
    Builder::str(num, "value_template", "{{ value_json.default_run_min }}");
    Builder::str(num, "command_topic", b.t().command("zone/" + std::to_string(n) + "/default_run_s"));
    // Shown in minutes, sent in seconds.
    Builder::str(num, "command_template", "{{ (value * 60) | round(0) | int }}");
    cJSON_AddNumberToObject(num, "min", 1);
    cJSON_AddNumberToObject(num, "max", kHardMaxRunSec / 60);
    cJSON_AddNumberToObject(num, "step", 1);
    Builder::str(num, "unit_of_measurement", "min");
    Builder::str(num, "device_class", "duration");
    Builder::str(num, "mode", "box");
    Builder::str(num, "entity_category", "config");
    cJSON *f = b.add(zoneId(n, "fault"), kBinarySensor, name + " fault");
    Builder::str(f, "device_class", "problem");
    Builder::str(f, "state_topic", state);
    Builder::str(f, "value_template", "{{ 'ON' if value_json.fault_active else 'OFF' }}");
    Builder::str(f, "json_attributes_topic", state);
    Builder::str(f, "json_attributes_template",
                 "{{ {'fault': value_json.fault, 'locked_out': value_json.locked_out, 'resting': value_json.resting, "
                 "'rest_remaining_s': value_json.rest_remaining_s} | tojson }}");
    Builder::str(f, "entity_category", "diagnostic");
}

void addProgram(Builder &b, const Program &p)
{
    const std::string name(p.name.view().empty() ? "Program " + std::to_string(p.id) : std::string(p.name.view()));
    cJSON *run = b.add(programId(p.id, "run"), kButton, "Run " + name);
    Builder::str(run, "command_topic", b.t().command("program/" + std::to_string(p.id) + "/run"));
    Builder::str(run, "payload_press", "RUN");
    Builder::str(run, "icon", "mdi:play");
    cJSON *en = b.add(programId(p.id, "enabled"), kSwitch, name + " enabled");
    Builder::str(en, "state_topic", b.t().program(p.id));
    Builder::str(en, "value_template", "{{ 'ON' if value_json.enabled else 'OFF' }}");
    Builder::str(en, "command_topic", b.t().command("program/" + std::to_string(p.id) + "/enabled"));
    Builder::str(en, "payload_on", "ON");
    Builder::str(en, "payload_off", "OFF");
    Builder::str(en, "json_attributes_topic", b.t().program(p.id));
    Builder::str(en, "entity_category", "config");
}

void addButton(Builder &b, const char *cid, const char *name, const char *icon)
{
    cJSON *c = b.add(cid, kButton, name);
    Builder::str(c, "command_topic", b.t().command(cid));
    Builder::str(c, "payload_press", "PRESS");
    Builder::str(c, "icon", icon);
}

void addDeviceEntities(Builder &b, const Config &cfg, const Identity &id)
{
    const Topics &t = b.t();
    addButton(b, "stop_all", "Stop all", "mdi:stop-circle");
    addButton(b, "pause", "Pause", "mdi:pause");
    addButton(b, "resume", "Resume", "mdi:play-pause");
    addButton(b, "skip", "Skip", "mdi:skip-next");
    addButton(b, "clear_faults", "Clear faults", "mdi:alert-remove");

    cJSON *rd = b.add("rain_delay", kNumber, "Rain delay");
    Builder::str(rd, "state_topic", t.rainDelay());
    Builder::str(rd, "value_template", "{{ value_json.hours }}");
    Builder::str(rd, "command_topic", t.command("rain_delay"));
    cJSON_AddNumberToObject(rd, "min", 0);
    cJSON_AddNumberToObject(rd, "max", kMaxRainDelayHours);
    cJSON_AddNumberToObject(rd, "step", 1);
    Builder::str(rd, "unit_of_measurement", "h");
    Builder::str(rd, "device_class", "duration");
    Builder::str(rd, "mode", "box");
    Builder::str(rd, "icon", "mdi:weather-rainy");
    Builder::str(rd, "json_attributes_topic", t.rainDelay());

    if (cfg.rain.enabled) {
        cJSON *r = b.add("rain", kBinarySensor, "Rain sensor");
        Builder::str(r, "device_class", "moisture");
        Builder::str(r, "state_topic", t.sensors());
        Builder::str(r, "value_template", "{{ 'ON' if value_json.rain.active else 'OFF' }}");
    }
    if (cfg.spare.enabled) {
        cJSON *s = b.add("spare", kBinarySensor, "Spare input");
        Builder::str(s, "state_topic", t.sensors());
        Builder::str(s, "value_template", "{{ 'ON' if value_json.spare.active else 'OFF' }}");
    }
    if (cfg.flow.enabled) {
        cJSON *fr = b.add("flow_rate", kSensor, "Flow rate");
        Builder::str(fr, "device_class", "volume_flow_rate");
        Builder::str(fr, "unit_of_measurement", "L/min");
        Builder::str(fr, "state_class", "measurement");
        Builder::str(fr, "state_topic", t.flow());
        Builder::str(fr, "value_template", "{{ value_json.rate_lpm }}");
        cJSON_AddNumberToObject(fr, "suggested_display_precision", 2);
        cJSON *ft = b.add("flow_total", kSensor, "Water used");
        Builder::str(ft, "device_class", "water");
        Builder::str(ft, "unit_of_measurement", "L");
        // Counts from boot; total_increasing lets Home Assistant treat the
        // drop to 0 after a restart as a new cycle, not negative usage.
        Builder::str(ft, "state_class", "total_increasing");
        Builder::str(ft, "state_topic", t.flow());
        Builder::str(ft, "value_template", "{{ value_json.total_l }}");
        cJSON_AddNumberToObject(ft, "suggested_display_precision", 1);
    }
    cJSON *cur = b.add("valve_current", kSensor, "Valve current");
    Builder::str(cur, "device_class", "current");
    Builder::str(cur, "unit_of_measurement", "mA");
    Builder::str(cur, "state_class", "measurement");
    Builder::str(cur, "state_topic", t.sensors());
    // null (no reading) renders as "None", which Home Assistant shows as unknown.
    Builder::str(cur, "value_template", "{{ value_json.valve_current_ma }}");
    Builder::str(cur, "entity_category", "diagnostic");

    cJSON *ac = b.add("ac_power", kBinarySensor, "24 VAC supply");
    Builder::str(ac, "device_class", "power");
    Builder::str(ac, "state_topic", t.sensors());
    Builder::str(ac, "value_template", "{{ 'ON' if value_json.ac_present else 'OFF' }}");
    Builder::str(ac, "entity_category", "diagnostic");

    cJSON *pr = b.add("problem", kBinarySensor, "Fault");
    Builder::str(pr, "device_class", "problem");
    Builder::str(pr, "state_topic", t.faults());
    Builder::str(pr, "value_template", "{{ 'ON' if value_json.any else 'OFF' }}");
    Builder::str(pr, "json_attributes_topic", t.faults());

    cJSON *q = b.add("queue", kSensor, "Queue");
    Builder::str(q, "device_class", "enum");
    cJSON *opts = cJSON_AddArrayToObject(q, "options");
    for (const QueueState s : {QueueState::Idle, QueueState::Running, QueueState::Paused}) {
        cJSON_AddItemToArray(opts, cJSON_CreateString(toString(s)));
    }
    Builder::str(q, "state_topic", t.queue());
    Builder::str(q, "value_template", "{{ value_json.state }}");
    Builder::str(q, "json_attributes_topic", t.queue());

    cJSON *w = b.add("wifi_signal", kSensor, "WiFi signal");
    Builder::str(w, "device_class", "signal_strength");
    Builder::str(w, "unit_of_measurement", "dBm");
    Builder::str(w, "state_class", "measurement");
    Builder::str(w, "state_topic", t.device());
    Builder::str(w, "value_template", "{{ value_json.rssi }}");
    Builder::str(w, "entity_category", "diagnostic");

    cJSON *fw = b.add("firmware", kSensor, "Firmware version");
    Builder::str(fw, "state_topic", t.device());
    Builder::str(fw, "value_template", "{{ value_json.firmware }}");
    Builder::str(fw, "icon", "mdi:chip");
    Builder::str(fw, "entity_category", "diagnostic");

    // Read-only: no command_topic, so Home Assistant offers no install
    // button. Installing stays behind the web UI's admin login.
    cJSON *up = b.add("firmware_update", kUpdate, "Firmware");
    Builder::str(up, "device_class", "firmware");
    Builder::str(up, "state_topic", t.update());
    Builder::str(up, "entity_category", "diagnostic");
    (void)id;
}

} // namespace

std::vector<DiscoveryPlanner::Component> DiscoveryPlanner::wanted(const Config &cfg)
{
    std::vector<Component> out;
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        if (!zoneActive(cfg, i)) {
            continue;
        }
        const std::size_t n = i + 1;
        if (cfg.mqtt.zoneEntity == HaZoneEntity::Valve) {
            out.push_back({zoneId(n, "valve"), kValve});
        } else {
            out.push_back({zoneId(n, "switch"), kSwitch});
        }
        out.push_back({zoneId(n, "run_time"), kNumber});
        out.push_back({zoneId(n, "fault"), kBinarySensor});
    }
    for (std::size_t i = 0; i < cfg.schedule.programCount; ++i) {
        const uint16_t id = cfg.schedule.programs[i].id;
        out.push_back({programId(id, "run"), kButton});
        out.push_back({programId(id, "enabled"), kSwitch});
    }
    if (cfg.rain.enabled) {
        out.push_back({"rain", kBinarySensor});
    }
    if (cfg.spare.enabled) {
        out.push_back({"spare", kBinarySensor});
    }
    if (cfg.flow.enabled) {
        out.push_back({"flow_rate", kSensor});
        out.push_back({"flow_total", kSensor});
    }
    return out;
}

std::string discoveryPayload(const Config &cfg, const Identity &id, const Topics &t,
                             const std::vector<DiscoveryPlanner::Component> &removals)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON *ids = cJSON_AddArrayToObject(dev, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString(("zoneflow_" + id.deviceId).c_str()));
    if (!id.mac.empty()) {
        cJSON *conns = cJSON_AddArrayToObject(dev, "connections");
        cJSON *mac = cJSON_CreateArray();
        cJSON_AddItemToArray(mac, cJSON_CreateString("mac"));
        cJSON_AddItemToArray(mac, cJSON_CreateString(id.mac.c_str()));
        cJSON_AddItemToArray(conns, mac);
    }
    cJSON_AddStringToObject(dev, "name", cfg.deviceName.c_str());
    cJSON_AddStringToObject(dev, "manufacturer", "ZoneFlow open hardware");
    cJSON_AddStringToObject(dev, "model", "ZoneFlow irrigation controller");
    cJSON_AddStringToObject(dev, "model_id", id.board.c_str());
    cJSON_AddStringToObject(dev, "sw_version", id.firmware.c_str());
    cJSON_AddStringToObject(dev, "serial_number", id.deviceId.c_str());
    if (!id.hostname.empty()) {
        cJSON_AddStringToObject(dev, "configuration_url", ("http://" + id.hostname + ".local/").c_str());
    }
    cJSON *origin = cJSON_AddObjectToObject(root, "origin");
    cJSON_AddStringToObject(origin, "name", "zoneflow-fw");
    cJSON_AddStringToObject(origin, "sw_version", id.firmware.c_str());
    cJSON_AddStringToObject(origin, "support_url", std::string(kSupportUrl).c_str());
    // Shared options, inherited by every component.
    cJSON_AddStringToObject(root, "availability_topic", t.availability().c_str());
    cJSON_AddNumberToObject(root, "qos", 1);

    cJSON *cmps = cJSON_AddObjectToObject(root, "components");
    Builder b(id, t, cmps);
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        if (zoneActive(cfg, i)) {
            addZone(b, cfg, i);
        }
    }
    for (std::size_t i = 0; i < cfg.schedule.programCount; ++i) {
        addProgram(b, cfg.schedule.programs[i]);
    }
    addDeviceEntities(b, cfg, id);
    for (const DiscoveryPlanner::Component &r : removals) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "platform", r.platform);
        cJSON_AddItemToObject(cmps, r.id.c_str(), c);
    }
    return print(root);
}

std::vector<std::string> DiscoveryPlanner::plan(const Config &cfg, const Identity &id, const Topics &t)
{
    const std::vector<Component> want = wanted(cfg);
    const auto contains = [](const std::vector<Component> &v, const Component &c) {
        return std::any_of(v.begin(), v.end(), [&](const Component &x) {
            return x.id == c.id && std::string_view(x.platform) == c.platform;
        });
    };
    // Every component this firmware could have announced for a zone, the
    // optional sensors, and programs announced since boot.
    std::vector<Component> possible;
    for (std::size_t n = 1; n <= kMaxZones; ++n) {
        possible.push_back({zoneId(n, "valve"), kValve});
        possible.push_back({zoneId(n, "switch"), kSwitch});
        possible.push_back({zoneId(n, "run_time"), kNumber});
        possible.push_back({zoneId(n, "fault"), kBinarySensor});
    }
    possible.push_back({"rain", kBinarySensor});
    possible.push_back({"spare", kBinarySensor});
    possible.push_back({"flow_rate", kSensor});
    possible.push_back({"flow_total", kSensor});
    for (const uint16_t pid : programsSeen_) {
        possible.push_back({programId(pid, "run"), kButton});
        possible.push_back({programId(pid, "enabled"), kSwitch});
    }

    std::vector<Component> removals;
    for (const Component &c : possible) {
        if (!contains(want, c) && !contains(removedOrAbsent_, c)) {
            removals.push_back(c);
        }
    }
    // A wanted component is no longer removed; if it goes away again, it
    // needs a new removal entry.
    removedOrAbsent_.erase(std::remove_if(removedOrAbsent_.begin(), removedOrAbsent_.end(),
                                          [&](const Component &c) { return contains(want, c); }),
                           removedOrAbsent_.end());
    for (const Component &c : removals) {
        removedOrAbsent_.push_back(c);
    }
    for (std::size_t i = 0; i < cfg.schedule.programCount; ++i) {
        const uint16_t pid = cfg.schedule.programs[i].id;
        if (std::find(programsSeen_.begin(), programsSeen_.end(), pid) == programsSeen_.end()) {
            programsSeen_.push_back(pid);
        }
    }

    std::vector<std::string> out;
    if (!removals.empty()) {
        out.push_back(discoveryPayload(cfg, id, t, removals));
    }
    out.push_back(discoveryPayload(cfg, id, t, {}));
    return out;
}

// ---- connection --------------------------------------------------------------------------------

const char *toString(ConnState s)
{
    switch (s) {
    case ConnState::Disabled:
        return "disabled";
    case ConnState::WaitingNetwork:
        return "waiting_network";
    case ConnState::Connecting:
        return "connecting";
    case ConnState::Connected:
        return "connected";
    case ConnState::WaitingRetry:
        return "waiting_retry";
    }
    return "?";
}

void ConnectionPolicy::onConnected()
{
    backoff_.reset();
    waiting_ = false;
}

void ConnectionPolicy::onDisconnected(MonoMs now, uint32_t random)
{
    retryAt_ = now + backoff_.next(random);
    waiting_ = true;
}

uint32_t birthDelayMs(uint32_t random) { return 1000 + random % 4001; }

} // namespace zf::mqtt
