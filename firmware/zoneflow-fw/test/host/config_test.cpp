// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "cJSON.h"
#include "fakes.hpp"
#include "zf/config/config_json.hpp"
#include "zf/config/config_store.hpp"

using namespace zf;
using namespace zf::test;

namespace {

Config sampleConfig()
{
    Config c = Config::defaults();
    c.deviceName.assign("garden");
    c.timezone.assign("CET-1CEST,M3.5.0,M10.5.0/3");
    c.zoneCount = 4;
    c.zones[1].name.assign("Beds");
    c.zones[1].maxRunSec = 900;
    c.zones[2].enabled = false;
    c.master.enabled = true;
    c.rain.enabled = true;
    c.spareFunction = SpareFunction::InhibitLikeRain;
    c.flow.enabled = true;
    c.flow.pulsesPerLitre = 5.5f;
    c.schedule.seasonalPercent = 120;
    Program p = makeProgram(3, {360, 1200}, {{0, 600}, {3, 120}});
    p.days.weekdayMask = 0b0101010;
    c.schedule.programs[0] = p;
    Program q = makeProgram(9, {300}, {{1, 60}});
    q.days.type = DayRuleType::Interval;
    q.days.intervalDays = 3;
    q.days.intervalAnchorDay = static_cast<int32_t>(civil::daysFromCivil(2026, 6, 1));
    q.useSeasonalAdjust = false;
    c.schedule.programs[1] = q;
    Program r = makeProgram(10, {0}, {{2, 1}});
    r.days.type = DayRuleType::EvenDays;
    r.enabled = false;
    c.schedule.programs[2] = r;
    c.schedule.programCount = 3;
    return c;
}

} // namespace

TEST(Config, DefaultsAreValid)
{
    const Config c = Config::defaults();
    EXPECT_TRUE(validate(c).ok) << validate(c).message;
    EXPECT_EQ(c.zones[0].name.view(), "Zone 1");
    EXPECT_EQ(c.driver.maxConcurrent, 2);
    EXPECT_FALSE(c.master.enabled);
}

TEST(Config, JsonRoundTrip)
{
    const Config c = sampleConfig();
    ASSERT_TRUE(validate(c).ok);
    const std::string json = configToJson(c, true);
    Config back;
    const auto res = configFromJson(json, back);
    ASSERT_TRUE(res.ok()) << res.validation.path << ": " << res.validation.message;
    EXPECT_EQ(res.origin, ConfigOrigin::Current);
    EXPECT_EQ(configToJson(back), configToJson(c));
    EXPECT_EQ(back.schedule.programs[0].days.weekdayMask, 0b0101010);
    EXPECT_EQ(back.schedule.programs[1].days.intervalAnchorDay, c.schedule.programs[1].days.intervalAnchorDay);
    EXPECT_EQ(back.schedule.programs[2].days.type, DayRuleType::EvenDays);
    EXPECT_FLOAT_EQ(back.flow.pulsesPerLitre, 5.5f);
}

TEST(Config, MissingFieldsTakeDefaults)
{
    Config c;
    const auto res = configFromJson(R"({"schema":1,"zone_count":4})", c);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(c.zoneCount, 4);
    EXPECT_EQ(c.zones[3].name.view(), "Zone 4");
    EXPECT_EQ(c.timezone.view(), "UTC0");
}

TEST(Config, UnknownFieldsAreIgnored)
{
    Config c;
    EXPECT_TRUE(configFromJson(R"({"schema":1,"future_thing":{"x":1}})", c).ok());
}

struct BadCase {
    const char *json;
    const char *path;
};

class ConfigRejects : public ::testing::TestWithParam<BadCase> {};

TEST_P(ConfigRejects, WithPath)
{
    Config c;
    const auto res = configFromJson(GetParam().json, c);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.validation.path, GetParam().path) << res.validation.message;
}

INSTANTIATE_TEST_SUITE_P(
    Config, ConfigRejects,
    ::testing::Values(
        BadCase{"[]", ""}, BadCase{"not json", ""}, BadCase{R"({"zone_count":4})", "schema"},
        BadCase{R"({"schema":1,"zone_count":0})", "zone_count"},
        BadCase{R"({"schema":1,"zone_count":9})", "zone_count"},
        BadCase{R"({"schema":1,"zone_count":"8"})", "zone_count"},
        BadCase{R"({"schema":1,"zone_count":2.5})", "zone_count"},
        BadCase{R"({"schema":1,"timezone":"Mars/Base"})", "timezone"},
        BadCase{R"({"schema":1,"zones":[{"max_run_s":0}]})", "zones[0].max_run_s"},
        BadCase{R"({"schema":1,"zones":[{},{"max_run_s":14401}]})", "zones[1].max_run_s"},
        BadCase{R"({"schema":1,"zones":[{"current_min_ma":500,"current_max_ma":100}]})", "zones[0].current_min_ma"},
        BadCase{R"({"schema":1,"zones":[{"name":"this name is far too long for it"}]})", "zones[0].name"},
        BadCase{R"({"schema":1,"driver":{"max_concurrent":3}})", "driver.max_concurrent"},
        BadCase{R"({"schema":1,"driver":{"max_concurrent":-1}})", "driver.max_concurrent"},
        BadCase{R"({"schema":1,"sensors":{"rain":{"contact":"maybe"}}})", "sensors.rain.contact"},
        BadCase{R"({"schema":1,"sensors":{"flow":{"pulses_per_litre":0}}})", "sensors.flow.pulses_per_litre"},
        BadCase{R"({"schema":1,"schedule":{"seasonal_percent":300}})", "schedule.seasonal_percent"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":0}]}})", "schedule.programs[0].id"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":1},{"id":1}]}})", "schedule.programs[1].id"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":1,"starts":["25:00"]}]}})",
                "schedule.programs[0].starts[0]"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":1,"starts":["1","2","3","4","5"]}]}})",
                "schedule.programs[0].starts"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":1,"days":{"type":"weekdays","weekdays":["funday"]}}]}})",
                "schedule.programs[0].days.weekdays[0]"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":1,"days":{"type":"interval","every":0}}]}})",
                "schedule.programs[0].days.every"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":1,"days":{"type":"interval","anchor":"2026-02-30"}}]}})",
                "schedule.programs[0].days.anchor"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":1,"days":{"type":"monthly"}}]}})",
                "schedule.programs[0].days.type"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":1,"durations_s":[1,2,3,4,5,6,7,8,9]}]}})",
                "schedule.programs[0].durations_s"},
        BadCase{R"({"schema":1,"schedule":{"programs":[{"id":1,"durations_s":[20000]}]}})",
                "schedule.programs[0].durations_s"}),
    [](const ::testing::TestParamInfo<BadCase> &info) { return "case" + std::to_string(info.index); });

TEST(Config, ClockAndDateHelpers)
{
    uint16_t m = 0;
    EXPECT_TRUE(parseClockTime("06:05", m));
    EXPECT_EQ(m, 365);
    EXPECT_TRUE(parseClockTime("23:59", m));
    EXPECT_FALSE(parseClockTime("24:00", m));
    EXPECT_FALSE(parseClockTime("6:05", m));
    EXPECT_FALSE(parseClockTime("06:60", m));
    EXPECT_EQ(formatClockTime(365), "06:05");
    int64_t d = 0;
    EXPECT_TRUE(parseIsoDate("2028-02-29", d));
    EXPECT_EQ(formatIsoDate(d), "2028-02-29");
    EXPECT_FALSE(parseIsoDate("2026-02-29", d));
    EXPECT_FALSE(parseIsoDate("2026-13-01", d));
}

// ---- migration ------------------------------------------------------------

namespace {
bool renameZonesToValves(cJSON *root)
{
    cJSON *zones = cJSON_DetachItemFromObjectCaseSensitive(root, "valves");
    if (zones != nullptr) {
        cJSON_AddItemToObject(root, "zones", zones);
    }
    return true;
}
bool failingStep(cJSON *) { return false; }
} // namespace

TEST(ConfigMigration, AppliesStepsInOrder)
{
    cJSON *root = cJSON_Parse(R"({"schema":0,"valves":[{"name":"A"}]})");
    ASSERT_NE(root, nullptr);
    const ConfigMigration steps[] = {{0, &renameZonesToValves}};
    std::string err;
    ASSERT_TRUE(migrateConfigJson(root, 1, steps, 1, err)) << err;
    EXPECT_EQ(cJSON_GetObjectItem(root, "schema")->valueint, 1);
    EXPECT_NE(cJSON_GetObjectItem(root, "zones"), nullptr);
    cJSON_Delete(root);
}

TEST(ConfigMigration, MissingOrFailingStepIsAnError)
{
    cJSON *root = cJSON_Parse(R"({"schema":0})");
    std::string err;
    EXPECT_FALSE(migrateConfigJson(root, 2, nullptr, 0, err));
    EXPECT_NE(err.find("no migration from version 0"), std::string::npos);
    const ConfigMigration steps[] = {{0, &failingStep}};
    EXPECT_FALSE(migrateConfigJson(root, 1, steps, 1, err));
    cJSON_Delete(root);
}

TEST(ConfigMigration, NewerSchemaIsParsedBestEffort)
{
    Config c;
    const auto res = configFromJson(R"({"schema":99,"zone_count":6,"new_feature":true})", c);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(res.origin, ConfigOrigin::Newer);
    EXPECT_EQ(c.zoneCount, 6);
}

// ---- store ------------------------------------------------------------------

TEST(ConfigStore, LoadsDefaultsWhenEmpty)
{
    MemoryStorage mem;
    ConfigStore store(mem);
    EXPECT_EQ(store.load().status, ConfigStore::LoadStatus::Defaults);
    EXPECT_EQ(store.get().zoneCount, kMaxZones);
}

TEST(ConfigStore, UpdatePersistsAndBumpsRevision)
{
    MemoryStorage mem;
    ConfigStore store(mem);
    store.load();
    const uint32_t rev = store.revision();
    ASSERT_TRUE(store.update(sampleConfig()).ok);
    EXPECT_GT(store.revision(), rev);
    ConfigStore second(mem);
    EXPECT_EQ(second.load().status, ConfigStore::LoadStatus::Loaded);
    EXPECT_EQ(second.get().deviceName.view(), "garden");
}

TEST(ConfigStore, InvalidUpdateChangesNothing)
{
    MemoryStorage mem;
    ConfigStore store(mem);
    store.load();
    Config bad = Config::defaults();
    bad.driver.maxConcurrent = 5;
    const uint32_t rev = store.revision();
    const auto v = store.update(bad);
    EXPECT_FALSE(v.ok);
    EXPECT_EQ(v.path, "driver.max_concurrent");
    EXPECT_EQ(store.revision(), rev);
    EXPECT_EQ(mem.writes, 0);
}

TEST(ConfigStore, StorageFailureChangesNothing)
{
    MemoryStorage mem;
    ConfigStore store(mem);
    store.load();
    mem.failWrites = true;
    EXPECT_FALSE(store.update(sampleConfig()).ok);
    EXPECT_EQ(store.get().deviceName.view(), "zoneflow");
}

TEST(ConfigStore, CorruptStoredConfigFallsBackToDefaultsAndKeepsCopy)
{
    MemoryStorage mem;
    mem.data["config"] = R"({"schema":1,"zone_count":42})";
    ConfigStore store(mem);
    const auto r = store.load();
    EXPECT_EQ(r.status, ConfigStore::LoadStatus::InvalidDefaults);
    EXPECT_EQ(r.validation.path, "zone_count");
    EXPECT_EQ(store.get().zoneCount, kMaxZones);
    EXPECT_EQ(mem.data["config"], R"({"schema":1,"zone_count":42})");
}

TEST(ConfigStore, NewerSchemaIsNotWrittenBack)
{
    MemoryStorage mem;
    mem.data["config"] = R"({"schema":7,"zone_count":4})";
    ConfigStore store(mem);
    EXPECT_EQ(store.load().status, ConfigStore::LoadStatus::NewerSchema);
    EXPECT_EQ(mem.writes, 0);
    EXPECT_EQ(store.get().zoneCount, 4);
}

TEST(ConfigStore, UpdateFromJsonAndFactoryReset)
{
    MemoryStorage mem;
    ConfigStore store(mem);
    store.load();
    ASSERT_TRUE(store.updateFromJson(R"({"schema":1,"device_name":"shed"})").ok);
    EXPECT_EQ(store.get().deviceName.view(), "shed");
    EXPECT_TRUE(store.factoryReset());
    EXPECT_EQ(store.get().deviceName.view(), "zoneflow");
    EXPECT_EQ(mem.data.count("config"), 0u);
}

TEST(RuntimePersist, RoundTrip)
{
    SchedulerPersist p;
    p.rainDelayUntil = 1780000000;
    p.count = 2;
    p.fired[0] = {3, 29'000'000};
    p.fired[1] = {9, 29'000'123};
    SchedulerPersist back;
    ASSERT_TRUE(persistFromJson(persistToJson(p), back));
    EXPECT_EQ(back.rainDelayUntil, p.rainDelayUntil);
    ASSERT_EQ(back.count, 2);
    EXPECT_EQ(back.fired[1].programId, 9);
    EXPECT_EQ(back.fired[1].key, 29'000'123);
    EXPECT_FALSE(persistFromJson("garbage", back));
}

// cJSON recurses once per nesting level (64 bytes of stack per level on the
// ESP32-S3, measured with -fstack-usage) and allows 1000 levels. A short
// hostile document must be rejected before it reaches cJSON.
TEST(Config, DeeplyNestedJsonIsRejected)
{
    std::string json = R"({"schema":1,"x":)";
    json += std::string(200, '[');
    json += std::string(200, ']');
    json += "}";
    Config out;
    const ConfigParseResult r = configFromJson(json, out);
    EXPECT_FALSE(r.ok());

    SchedulerPersist p;
    EXPECT_FALSE(persistFromJson(R"({"fired":)" + std::string(200, '[') + std::string(200, ']') + "}", p));

    // Brackets inside strings do not count, and real documents still parse.
    Config c = Config::defaults();
    c.deviceName.assign("[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[");
    Config back;
    EXPECT_TRUE(configFromJson(configToJson(c), back).ok());
    EXPECT_EQ(back.deviceName.view(), c.deviceName.view());
}

// ---- phase 2 hardening ---------------------------------------------------------

// A document from newer firmware sent through the API must not be written back
// as an older schema: the fields this firmware does not know would be lost.
TEST(ConfigStore, NewerSchemaThroughApiIsRejected)
{
    MemoryStorage mem;
    ConfigStore store(mem);
    store.load();
    const uint32_t rev = store.revision();
    const ValidationResult v = store.updateFromJson(R"({"schema":2,"device_name":"shed","new_feature":true})");
    EXPECT_FALSE(v.ok);
    EXPECT_EQ(v.path, "schema");
    EXPECT_NE(v.message.find("newer"), std::string::npos) << v.message;
    EXPECT_EQ(mem.writes, 0);
    EXPECT_EQ(store.revision(), rev);
    EXPECT_EQ(store.get().deviceName.view(), "zoneflow");
}
