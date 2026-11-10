// SPDX-License-Identifier: Apache-2.0
#include "zf/config/config_json.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>

#include "cJSON.h"
#include "zf/time/civil.hpp"

namespace zf {
namespace {

struct JsonDeleter {
    void operator()(cJSON *p) const { cJSON_Delete(p); }
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

constexpr std::array<const char *, 7> kWeekdayNames = {"sun", "mon", "tue", "wed", "thu", "fri", "sat"};

// cJSON parses and frees recursively, one call per nesting level, and allows
// 1000 levels. On the ESP32-S3 parse_value takes 64 bytes of stack per level
// (-fstack-usage, ESP-IDF v5.5.6 toolchain), so a few hundred bytes of
// brackets would overflow a task stack. The config nests six levels deep.
constexpr int kMaxJsonDepth = 16;

// Counts bracket depth outside strings, before cJSON sees the text.
bool nestingWithinLimit(std::string_view json)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : json) {
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
            if (++depth > kMaxJsonDepth) {
                return false;
            }
        } else if (c == ']' || c == '}') {
            --depth;
        }
    }
    return true;
}

// Schema migrations, oldest first. Version 1 is the first schema, so the
// table is empty. Example of a future step:
//   bool v1ToV2(cJSON *root) { /* rename or reshape fields */ return true; }
//   {1, &v1ToV2},
constexpr std::array<ConfigMigration, 0> kMigrations{};

const char *contactName(ContactType c) { return c == ContactType::NormallyOpen ? "no" : "nc"; }

std::string join(const std::string &path, const char *key) { return path.empty() ? key : path + "." + key; }

std::string at(const std::string &path, std::size_t i) { return path + "[" + std::to_string(i) + "]"; }

void addWindow(cJSON *o, const CurrentWindow &w)
{
    cJSON_AddNumberToObject(o, "current_min_ma", w.minMa);
    cJSON_AddNumberToObject(o, "current_max_ma", w.maxMa);
}

void addContact(cJSON *o, const ContactInputConfig &c)
{
    cJSON_AddBoolToObject(o, "enabled", c.enabled);
    cJSON_AddStringToObject(o, "contact", contactName(c.contact));
    cJSON_AddNumberToObject(o, "debounce_ms", c.debounceMs);
}

void addDays(cJSON *parent, const DayRule &d)
{
    cJSON *o = cJSON_AddObjectToObject(parent, "days");
    if (o == nullptr) {
        return;
    }
    switch (d.type) {
    case DayRuleType::Weekdays: {
        cJSON_AddStringToObject(o, "type", "weekdays");
        cJSON *a = cJSON_AddArrayToObject(o, "weekdays");
        for (std::size_t i = 0; a != nullptr && i < kWeekdayNames.size(); ++i) {
            if ((d.weekdayMask & (1u << i)) != 0) {
                cJSON_AddItemToArray(a, cJSON_CreateString(kWeekdayNames[i]));
            }
        }
        break;
    }
    case DayRuleType::Interval:
        cJSON_AddStringToObject(o, "type", "interval");
        cJSON_AddNumberToObject(o, "every", d.intervalDays);
        cJSON_AddStringToObject(o, "anchor", formatIsoDate(d.intervalAnchorDay).c_str());
        break;
    case DayRuleType::OddDays:
        cJSON_AddStringToObject(o, "type", "odd");
        break;
    case DayRuleType::EvenDays:
        cJSON_AddStringToObject(o, "type", "even");
        break;
    }
}

std::string print(cJSON *root, bool pretty)
{
    char *s = pretty ? cJSON_Print(root) : cJSON_PrintUnformatted(root);
    if (s == nullptr) {
        return {};
    }
    std::string out(s);
    cJSON_free(s);
    return out;
}

// Reads typed values and records the first error with its JSON path.
// Absent fields leave the output untouched (it holds the default).
class Reader {
public:
    ValidationResult error;
    bool ok() const { return error.ok; }

    void fail(const std::string &path, const std::string &msg)
    {
        if (error.ok) {
            error = ValidationResult::bad(path, msg);
        }
    }

    const cJSON *object(const cJSON *parent, const char *key, const std::string &path)
    {
        const cJSON *o = cJSON_GetObjectItemCaseSensitive(parent, key);
        if (o != nullptr && !cJSON_IsObject(o)) {
            fail(join(path, key), "must be an object");
            return nullptr;
        }
        return o;
    }

    const cJSON *array(const cJSON *parent, const char *key, const std::string &path, std::size_t maxSize)
    {
        const cJSON *a = cJSON_GetObjectItemCaseSensitive(parent, key);
        if (a == nullptr) {
            return nullptr;
        }
        if (!cJSON_IsArray(a)) {
            fail(join(path, key), "must be an array");
            return nullptr;
        }
        if (static_cast<std::size_t>(cJSON_GetArraySize(a)) > maxSize) {
            fail(join(path, key), "at most " + std::to_string(maxSize) + " items");
            return nullptr;
        }
        return a;
    }

    void boolean(const cJSON *parent, const char *key, bool &out, const std::string &path)
    {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(parent, key);
        if (v == nullptr) {
            return;
        }
        if (!cJSON_IsBool(v)) {
            fail(join(path, key), "must be true or false");
            return;
        }
        out = cJSON_IsTrue(v);
    }

    template <typename T> void integerItem(const cJSON *v, T &out, const std::string &path)
    {
        if (!cJSON_IsNumber(v)) {
            fail(path, "must be a number");
            return;
        }
        const double d = v->valuedouble;
        if (std::floor(d) != d) {
            fail(path, "must be a whole number");
            return;
        }
        if (d < static_cast<double>(std::numeric_limits<T>::min()) ||
            d > static_cast<double>(std::numeric_limits<T>::max())) {
            fail(path, "out of range");
            return;
        }
        out = static_cast<T>(d);
    }

    template <typename T> void integer(const cJSON *parent, const char *key, T &out, const std::string &path)
    {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(parent, key);
        if (v != nullptr) {
            integerItem(v, out, join(path, key));
        }
    }

    void number(const cJSON *parent, const char *key, float &out, const std::string &path)
    {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(parent, key);
        if (v == nullptr) {
            return;
        }
        if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble)) {
            fail(join(path, key), "must be a number");
            return;
        }
        out = static_cast<float>(v->valuedouble);
    }

    const char *stringValue(const cJSON *parent, const char *key, const std::string &path)
    {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(parent, key);
        if (v == nullptr) {
            return nullptr;
        }
        if (!cJSON_IsString(v) || v->valuestring == nullptr) {
            fail(join(path, key), "must be a string");
            return nullptr;
        }
        return v->valuestring;
    }

    template <std::size_t N>
    void string(const cJSON *parent, const char *key, FixedString<N> &out, const std::string &path)
    {
        const char *s = stringValue(parent, key, path);
        if (s == nullptr) {
            return;
        }
        const std::string_view sv(s);
        if (sv.size() > N) {
            fail(join(path, key), "too long (at most " + std::to_string(N) + " bytes)");
            return;
        }
        out.assign(sv);
    }

    void contact(const cJSON *parent, const char *key, ContactType &out, const std::string &path)
    {
        const char *s = stringValue(parent, key, path);
        if (s == nullptr) {
            return;
        }
        const std::string_view sv(s);
        if (sv == "no") {
            out = ContactType::NormallyOpen;
        } else if (sv == "nc") {
            out = ContactType::NormallyClosed;
        } else {
            fail(join(path, key), "must be \"no\" or \"nc\"");
        }
    }

    void window(const cJSON *o, CurrentWindow &w, const std::string &path)
    {
        integer(o, "current_min_ma", w.minMa, path);
        integer(o, "current_max_ma", w.maxMa, path);
    }

    void contactInput(const cJSON *o, ContactInputConfig &c, const std::string &path)
    {
        if (o == nullptr) {
            return;
        }
        boolean(o, "enabled", c.enabled, path);
        contact(o, "contact", c.contact, path);
        integer(o, "debounce_ms", c.debounceMs, path);
    }
};

void readDays(Reader &rd, const cJSON *o, DayRule &d, const std::string &path)
{
    const char *type = rd.stringValue(o, "type", path);
    if (type == nullptr) {
        if (rd.ok()) {
            rd.fail(join(path, "type"), "required");
        }
        return;
    }
    const std::string_view t(type);
    if (t == "weekdays") {
        d.type = DayRuleType::Weekdays;
        const cJSON *a = rd.array(o, "weekdays", path, 7);
        if (a == nullptr) {
            return; // absent: keep the default (every day)
        }
        uint8_t mask = 0;
        std::size_t i = 0;
        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, a)
        {
            bool known = false;
            if (cJSON_IsString(item) && item->valuestring != nullptr) {
                for (std::size_t w = 0; w < kWeekdayNames.size(); ++w) {
                    if (std::string_view(item->valuestring) == kWeekdayNames[w]) {
                        mask |= static_cast<uint8_t>(1u << w);
                        known = true;
                    }
                }
            }
            if (!known) {
                rd.fail(at(join(path, "weekdays"), i), "must be one of sun, mon, tue, wed, thu, fri, sat");
                return;
            }
            ++i;
        }
        d.weekdayMask = mask;
    } else if (t == "interval") {
        d.type = DayRuleType::Interval;
        rd.integer(o, "every", d.intervalDays, path);
        const char *anchor = rd.stringValue(o, "anchor", path);
        if (anchor != nullptr) {
            int64_t day = 0;
            if (!parseIsoDate(anchor, day)) {
                rd.fail(join(path, "anchor"), "must be a date YYYY-MM-DD");
                return;
            }
            d.intervalAnchorDay = static_cast<int32_t>(day);
        }
    } else if (t == "odd") {
        d.type = DayRuleType::OddDays;
    } else if (t == "even") {
        d.type = DayRuleType::EvenDays;
    } else {
        rd.fail(join(path, "type"), "must be weekdays, interval, odd or even");
    }
}

void readProgram(Reader &rd, const cJSON *o, Program &p, const std::string &path)
{
    rd.integer(o, "id", p.id, path);
    rd.string(o, "name", p.name, path);
    rd.boolean(o, "enabled", p.enabled, path);
    if (const cJSON *starts = rd.array(o, "starts", path, kMaxStartTimes)) {
        std::size_t i = 0;
        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, starts)
        {
            uint16_t minutes = 0;
            if (!cJSON_IsString(item) || item->valuestring == nullptr || !parseClockTime(item->valuestring, minutes)) {
                rd.fail(at(join(path, "starts"), i), "must be a time HH:MM");
                return;
            }
            p.startMinutes[i++] = minutes;
        }
        p.startCount = static_cast<uint8_t>(i);
    }
    if (const cJSON *days = rd.object(o, "days", path)) {
        readDays(rd, days, p.days, join(path, "days"));
    }
    if (const cJSON *durations = rd.array(o, "durations_s", path, kMaxZones)) {
        std::size_t i = 0;
        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, durations)
        {
            rd.integerItem(item, p.durationSec[i], at(join(path, "durations_s"), i));
            ++i;
        }
    }
    rd.boolean(o, "seasonal", p.useSeasonalAdjust, path);
    rd.boolean(o, "obey_sensor", p.obeySensorInhibit, path);
}

void readConfig(Reader &rd, const cJSON *root, Config &c)
{
    const std::string top;
    rd.string(root, "device_name", c.deviceName, top);
    rd.string(root, "timezone", c.timezone, top);
    rd.integer(root, "zone_count", c.zoneCount, top);

    if (const cJSON *zones = rd.array(root, "zones", top, kMaxZones)) {
        std::size_t i = 0;
        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, zones)
        {
            const std::string path = at("zones", i);
            if (!cJSON_IsObject(item)) {
                rd.fail(path, "must be an object");
                return;
            }
            ZoneConfig &z = c.zones[i];
            rd.string(item, "name", z.name, path);
            rd.boolean(item, "enabled", z.enabled, path);
            rd.integer(item, "max_run_s", z.maxRunSec, path);
            rd.window(item, z.current, path);
            ++i;
        }
    }

    if (const cJSON *m = rd.object(root, "master", top)) {
        rd.boolean(m, "enabled", c.master.enabled, "master");
        rd.integer(m, "pre_open_ms", c.master.preOpenMs, "master");
        rd.integer(m, "post_close_ms", c.master.postCloseMs, "master");
        rd.window(m, c.master.current, "master");
    }

    if (const cJSON *d = rd.object(root, "driver", top)) {
        rd.integer(d, "max_concurrent", c.driver.maxConcurrent, "driver");
        rd.integer(d, "inter_zone_delay_ms", c.driver.interZoneDelayMs, "driver");
        rd.integer(d, "min_rest_s", c.driver.minRestSec, "driver");
    }

    if (const cJSON *s = rd.object(root, "sensors", top)) {
        rd.contactInput(rd.object(s, "rain", "sensors"), c.rain, "sensors.rain");
        if (const cJSON *spare = rd.object(s, "spare", "sensors")) {
            rd.contactInput(spare, c.spare, "sensors.spare");
            if (const char *fn = rd.stringValue(spare, "function", "sensors.spare")) {
                const std::string_view f(fn);
                if (f == "none") {
                    c.spareFunction = SpareFunction::None;
                } else if (f == "inhibit") {
                    c.spareFunction = SpareFunction::InhibitLikeRain;
                } else {
                    rd.fail("sensors.spare.function", "must be \"none\" or \"inhibit\"");
                }
            }
        }
        if (const cJSON *f = rd.object(s, "flow", "sensors")) {
            rd.boolean(f, "enabled", c.flow.enabled, "sensors.flow");
            rd.number(f, "pulses_per_litre", c.flow.pulsesPerLitre, "sensors.flow");
            rd.number(f, "leak_threshold_lpm", c.flow.leakThresholdLpm, "sensors.flow");
            rd.integer(f, "leak_detect_s", c.flow.leakDetectSec, "sensors.flow");
        }
    }

    if (const cJSON *d = rd.object(root, "diagnostics", top)) {
        rd.boolean(d, "current_enabled", c.diag.currentEnabled, "diagnostics");
        rd.integer(d, "settle_ms", c.diag.settleMs, "diagnostics");
        rd.integer(d, "idle_max_ma", c.diag.idleMaxMa, "diagnostics");
        rd.integer(d, "short_circuit_ma", c.diag.shortCircuitMa, "diagnostics");
        rd.integer(d, "confirm_samples", c.diag.confirmSamples, "diagnostics");
    }

    if (const cJSON *s = rd.object(root, "schedule", top)) {
        rd.integer(s, "seasonal_percent", c.schedule.seasonalPercent, "schedule");
        rd.integer(s, "missed_start_grace_min", c.schedule.missedStartGraceMin, "schedule");
        if (const cJSON *programs = rd.array(s, "programs", "schedule", kMaxPrograms)) {
            std::size_t i = 0;
            const cJSON *item = nullptr;
            cJSON_ArrayForEach(item, programs)
            {
                const std::string path = at("schedule.programs", i);
                if (!cJSON_IsObject(item)) {
                    rd.fail(path, "must be an object");
                    return;
                }
                c.schedule.programs[i] = Program{};
                readProgram(rd, item, c.schedule.programs[i], path);
                if (!rd.ok()) {
                    return;
                }
                ++i;
            }
            c.schedule.programCount = static_cast<uint8_t>(i);
        }
    }
}

bool parseDigits(std::string_view s, std::size_t pos, std::size_t len, int &out)
{
    if (pos + len > s.size()) {
        return false;
    }
    int v = 0;
    for (std::size_t i = pos; i < pos + len; ++i) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        v = v * 10 + (s[i] - '0');
    }
    out = v;
    return true;
}

} // namespace

bool parseClockTime(std::string_view s, uint16_t &minutes)
{
    int h = 0;
    int m = 0;
    if (s.size() != 5 || s[2] != ':' || !parseDigits(s, 0, 2, h) || !parseDigits(s, 3, 2, m) || h > 23 || m > 59) {
        return false;
    }
    minutes = static_cast<uint16_t>(h * 60 + m);
    return true;
}

std::string formatClockTime(uint16_t minutes)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02u:%02u", static_cast<unsigned>((minutes / 60) % 24),
                  static_cast<unsigned>(minutes % 60));
    return buf;
}

bool parseIsoDate(std::string_view s, int64_t &daySerial)
{
    int y = 0;
    int m = 0;
    int d = 0;
    if (s.size() != 10 || s[4] != '-' || s[7] != '-' || !parseDigits(s, 0, 4, y) || !parseDigits(s, 5, 2, m) ||
        !parseDigits(s, 8, 2, d)) {
        return false;
    }
    if (m < 1 || m > 12 || d < 1 || static_cast<unsigned>(d) > civil::daysInMonth(y, static_cast<unsigned>(m))) {
        return false;
    }
    daySerial = civil::daysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
    return true;
}

std::string formatIsoDate(int64_t daySerial)
{
    const civil::Date d = civil::civilFromDays(daySerial);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%04d-%02u-%02u", d.year, d.month, d.day);
    return buf;
}

std::string configToJson(const Config &c, bool pretty)
{
    JsonPtr root(cJSON_CreateObject());
    cJSON *r = root.get();
    if (r == nullptr) {
        return {};
    }
    cJSON_AddNumberToObject(r, "schema", kConfigSchemaVersion);
    cJSON_AddStringToObject(r, "device_name", c.deviceName.c_str());
    cJSON_AddStringToObject(r, "timezone", c.timezone.c_str());
    cJSON_AddNumberToObject(r, "zone_count", c.zoneCount);

    if (cJSON *zones = cJSON_AddArrayToObject(r, "zones")) {
        for (const ZoneConfig &z : c.zones) {
            cJSON *o = cJSON_CreateObject();
            if (o == nullptr) {
                return {};
            }
            cJSON_AddStringToObject(o, "name", z.name.c_str());
            cJSON_AddBoolToObject(o, "enabled", z.enabled);
            cJSON_AddNumberToObject(o, "max_run_s", z.maxRunSec);
            addWindow(o, z.current);
            cJSON_AddItemToArray(zones, o);
        }
    }

    if (cJSON *m = cJSON_AddObjectToObject(r, "master")) {
        cJSON_AddBoolToObject(m, "enabled", c.master.enabled);
        cJSON_AddNumberToObject(m, "pre_open_ms", c.master.preOpenMs);
        cJSON_AddNumberToObject(m, "post_close_ms", c.master.postCloseMs);
        addWindow(m, c.master.current);
    }

    if (cJSON *d = cJSON_AddObjectToObject(r, "driver")) {
        cJSON_AddNumberToObject(d, "max_concurrent", c.driver.maxConcurrent);
        cJSON_AddNumberToObject(d, "inter_zone_delay_ms", c.driver.interZoneDelayMs);
        cJSON_AddNumberToObject(d, "min_rest_s", c.driver.minRestSec);
    }

    if (cJSON *s = cJSON_AddObjectToObject(r, "sensors")) {
        if (cJSON *rain = cJSON_AddObjectToObject(s, "rain")) {
            addContact(rain, c.rain);
        }
        if (cJSON *spare = cJSON_AddObjectToObject(s, "spare")) {
            addContact(spare, c.spare);
            cJSON_AddStringToObject(spare, "function",
                                    c.spareFunction == SpareFunction::InhibitLikeRain ? "inhibit" : "none");
        }
        if (cJSON *f = cJSON_AddObjectToObject(s, "flow")) {
            cJSON_AddBoolToObject(f, "enabled", c.flow.enabled);
            cJSON_AddNumberToObject(f, "pulses_per_litre", c.flow.pulsesPerLitre);
            cJSON_AddNumberToObject(f, "leak_threshold_lpm", c.flow.leakThresholdLpm);
            cJSON_AddNumberToObject(f, "leak_detect_s", c.flow.leakDetectSec);
        }
    }

    if (cJSON *d = cJSON_AddObjectToObject(r, "diagnostics")) {
        cJSON_AddBoolToObject(d, "current_enabled", c.diag.currentEnabled);
        cJSON_AddNumberToObject(d, "settle_ms", c.diag.settleMs);
        cJSON_AddNumberToObject(d, "idle_max_ma", c.diag.idleMaxMa);
        cJSON_AddNumberToObject(d, "short_circuit_ma", c.diag.shortCircuitMa);
        cJSON_AddNumberToObject(d, "confirm_samples", c.diag.confirmSamples);
    }

    if (cJSON *s = cJSON_AddObjectToObject(r, "schedule")) {
        cJSON_AddNumberToObject(s, "seasonal_percent", c.schedule.seasonalPercent);
        cJSON_AddNumberToObject(s, "missed_start_grace_min", c.schedule.missedStartGraceMin);
        cJSON *programs = cJSON_AddArrayToObject(s, "programs");
        for (std::size_t i = 0; programs != nullptr && i < c.schedule.programCount; ++i) {
            const Program &p = c.schedule.programs[i];
            cJSON *o = cJSON_CreateObject();
            if (o == nullptr) {
                return {};
            }
            cJSON_AddNumberToObject(o, "id", p.id);
            cJSON_AddStringToObject(o, "name", p.name.c_str());
            cJSON_AddBoolToObject(o, "enabled", p.enabled);
            if (cJSON *starts = cJSON_AddArrayToObject(o, "starts")) {
                for (std::size_t k = 0; k < p.startCount; ++k) {
                    cJSON_AddItemToArray(starts, cJSON_CreateString(formatClockTime(p.startMinutes[k]).c_str()));
                }
            }
            addDays(o, p.days);
            if (cJSON *durations = cJSON_AddArrayToObject(o, "durations_s")) {
                for (uint32_t d : p.durationSec) {
                    cJSON_AddItemToArray(durations, cJSON_CreateNumber(d));
                }
            }
            cJSON_AddBoolToObject(o, "seasonal", p.useSeasonalAdjust);
            cJSON_AddBoolToObject(o, "obey_sensor", p.obeySensorInhibit);
            cJSON_AddItemToArray(programs, o);
        }
    }
    return print(r, pretty);
}

bool migrateConfigJson(cJSON *root, int targetVersion, const ConfigMigration *steps, std::size_t stepCount,
                       std::string &error)
{
    cJSON *schema = cJSON_GetObjectItemCaseSensitive(root, "schema");
    if (!cJSON_IsNumber(schema)) {
        error = "schema: missing";
        return false;
    }
    int version = schema->valueint;
    while (version < targetVersion) {
        const ConfigMigration *step = nullptr;
        for (std::size_t i = 0; i < stepCount; ++i) {
            if (steps[i].fromVersion == version) {
                step = &steps[i];
                break;
            }
        }
        if (step == nullptr) {
            error = "schema: no migration from version " + std::to_string(version);
            return false;
        }
        if (!step->apply(root)) {
            error = "schema: migration from version " + std::to_string(version) + " failed";
            return false;
        }
        ++version;
        cJSON_SetNumberValue(schema, version);
    }
    return true;
}

ConfigParseResult configFromJson(std::string_view json, Config &out)
{
    ConfigParseResult res;
    if (!nestingWithinLimit(json)) {
        res.validation = ValidationResult::bad("", "nested more than " + std::to_string(kMaxJsonDepth) + " levels");
        return res;
    }
    JsonPtr root(cJSON_ParseWithLength(json.data(), json.size()));
    if (!root || !cJSON_IsObject(root.get())) {
        res.validation = ValidationResult::bad("", "not a JSON object");
        return res;
    }
    const cJSON *schema = cJSON_GetObjectItemCaseSensitive(root.get(), "schema");
    if (!cJSON_IsNumber(schema) || std::floor(schema->valuedouble) != schema->valuedouble || schema->valuedouble < 1 ||
        schema->valuedouble > 10000) {
        res.validation = ValidationResult::bad("schema", "missing or not a schema version");
        return res;
    }
    res.storedVersion = schema->valueint;
    if (res.storedVersion > kConfigSchemaVersion) {
        res.origin = ConfigOrigin::Newer;
    } else if (res.storedVersion < kConfigSchemaVersion) {
        std::string err;
        if (!migrateConfigJson(root.get(), kConfigSchemaVersion, kMigrations.data(), kMigrations.size(), err)) {
            res.validation = ValidationResult::bad("schema", err);
            return res;
        }
        res.origin = ConfigOrigin::Migrated;
    }

    Config cfg = Config::defaults();
    Reader rd;
    readConfig(rd, root.get(), cfg);
    if (!rd.ok()) {
        res.validation = rd.error;
        return res;
    }
    res.validation = validate(cfg);
    if (res.validation.ok) {
        cfg.schemaVersion = kConfigSchemaVersion;
        out = cfg;
    }
    return res;
}

std::string persistToJson(const SchedulerPersist &state)
{
    JsonPtr root(cJSON_CreateObject());
    if (!root) {
        return {};
    }
    cJSON_AddNumberToObject(root.get(), "rain_delay_until", static_cast<double>(state.rainDelayUntil));
    if (cJSON *fired = cJSON_AddArrayToObject(root.get(), "fired")) {
        for (std::size_t i = 0; i < state.count && i < state.fired.size(); ++i) {
            cJSON *o = cJSON_CreateObject();
            if (o == nullptr) {
                return {};
            }
            cJSON_AddNumberToObject(o, "id", state.fired[i].programId);
            cJSON_AddNumberToObject(o, "key", static_cast<double>(state.fired[i].key));
            cJSON_AddItemToArray(fired, o);
        }
    }
    return print(root.get(), false);
}

bool persistFromJson(std::string_view json, SchedulerPersist &out)
{
    if (!nestingWithinLimit(json)) {
        return false;
    }
    JsonPtr root(cJSON_ParseWithLength(json.data(), json.size()));
    if (!root || !cJSON_IsObject(root.get())) {
        return false;
    }
    SchedulerPersist p{};
    Reader rd;
    rd.integer(root.get(), "rain_delay_until", p.rainDelayUntil, "");
    if (const cJSON *fired = rd.array(root.get(), "fired", "", kMaxPrograms)) {
        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, fired)
        {
            SchedulerPersist::Fired f{};
            rd.integer(item, "id", f.programId, "fired");
            rd.integer(item, "key", f.key, "fired");
            p.fired[p.count++] = f;
        }
    }
    if (!rd.ok()) {
        return false;
    }
    out = p;
    return true;
}

} // namespace zf
