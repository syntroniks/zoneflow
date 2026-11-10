// SPDX-License-Identifier: Apache-2.0
// Home Assistant device discovery: the document's shape is checked against
// the options Home Assistant documents (test/host/data/ha_mqtt_discovery.json,
// extracted from the Home Assistant 2026.11.1 MQTT documentation; see its
// "_source"), and the planner's removal of components that are no longer
// wanted.
#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "cJSON.h"
#include "fakes.hpp"
#include "zf_mqtt/mqtt_logic.hpp"

using namespace zf;
using namespace zf::mqtt;
using namespace zf::test;

namespace {

struct Json {
    cJSON *root = nullptr;
    explicit Json(const std::string &s) : root(cJSON_Parse(s.c_str())) {}
    ~Json() { cJSON_Delete(root); }
    Json(const Json &) = delete;
    Json &operator=(const Json &) = delete;
};

const Json &schema()
{
    static const Json doc([] {
        std::ifstream in(ZF_HA_SCHEMA_PATH);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }());
    return doc;
}

std::set<std::string> stringSet(const cJSON *array)
{
    std::set<std::string> out;
    const cJSON *i = nullptr;
    cJSON_ArrayForEach(i, array) { out.insert(i->valuestring); }
    return out;
}

Identity identity() { return {"a1b2c3d4e5f6", "a1:b2:c3:d4:e5:f6", "zoneflow-e5f6", "0.3.0", "zoneflow-rev-a"}; }

Config fullConfig()
{
    Config c = Config::defaults();
    c.deviceName.assign("Back garden");
    c.zoneCount = 8;
    c.zones[0].name.assign("Lawn");
    c.zones[5].enabled = false;
    c.rain.enabled = true;
    c.spare.enabled = true;
    c.flow.enabled = true;
    c.schedule.programs[0] = makeProgram(3, {360}, {{0, 600}});
    c.schedule.programs[0].name.assign("Morning");
    c.schedule.programs[1] = makeProgram(9, {1200}, {{1, 300}});
    c.schedule.programCount = 2;
    return c;
}

const Topics kTopics("zoneflow/a1b2c3d4e5f6");

// Checks one component against the documented options of its platform.
void checkComponent(const char *cid, const cJSON *c)
{
    SCOPED_TRACE(cid);
    const cJSON *platforms = cJSON_GetObjectItem(schema().root, "platforms");
    const cJSON *platformItem = cJSON_GetObjectItem(c, "platform");
    ASSERT_TRUE(cJSON_IsString(platformItem));
    const std::string platform = platformItem->valuestring;
    const cJSON *spec = cJSON_GetObjectItem(platforms, platform.c_str());
    ASSERT_NE(spec, nullptr) << "platform not in the fixture: " << platform;
    const std::set<std::string> options = stringSet(cJSON_GetObjectItem(spec, "options"));
    const std::set<std::string> shared = stringSet(cJSON_GetObjectItem(schema().root, "shared_options"));
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, c)
    {
        EXPECT_TRUE(options.count(item->string) == 1) << platform << " has no documented option " << item->string;
    }
    for (const std::string &req : stringSet(cJSON_GetObjectItem(spec, "required"))) {
        // Shared options at the root (availability_topic, qos) are inherited;
        // nothing required is taken from there.
        EXPECT_TRUE(cJSON_HasObjectItem(c, req.c_str())) << "missing required " << req;
        (void)shared;
    }
    // Every *_topic is a concrete topic without wildcards.
    cJSON_ArrayForEach(item, c)
    {
        const std::string key = item->string;
        if (key.size() > 6 && key.compare(key.size() - 6, 6, "_topic") == 0) {
            ASSERT_TRUE(cJSON_IsString(item));
            const std::string topic = item->valuestring;
            EXPECT_EQ(topic.find_first_of("+#"), std::string::npos) << key;
            EXPECT_EQ(topic.rfind(kTopics.root(), 0), 0u) << key << " outside the device's topic tree";
        }
    }
    // Device class and unit as documented for the platform.
    const cJSON *dc = cJSON_GetObjectItem(c, "device_class");
    if (dc != nullptr) {
        const cJSON *classes =
            cJSON_GetObjectItem(cJSON_GetObjectItem(schema().root, "device_classes"), platform.c_str());
        const cJSON *units = cJSON_GetObjectItem(classes, dc->valuestring);
        ASSERT_NE(units, nullptr) << "device_class " << dc->valuestring << " not documented for " << platform;
        const cJSON *unit = cJSON_GetObjectItem(c, "unit_of_measurement");
        if (cJSON_GetArraySize(units) > 0) {
            ASSERT_TRUE(cJSON_IsString(unit)) << "device class " << dc->valuestring << " needs a unit";
            EXPECT_EQ(stringSet(units).count(unit->valuestring), 1u) << unit->valuestring;
        }
    }
    // Sensor rule from the documentation: options only with device_class
    // enum, and not together with state_class or unit_of_measurement.
    if (cJSON_HasObjectItem(c, "options")) {
        EXPECT_STREQ(cJSON_GetObjectItem(c, "device_class")->valuestring, "enum");
        EXPECT_FALSE(cJSON_HasObjectItem(c, "state_class"));
        EXPECT_FALSE(cJSON_HasObjectItem(c, "unit_of_measurement"));
        EXPECT_GT(cJSON_GetArraySize(cJSON_GetObjectItem(c, "options")), 0);
    }
    // Valve rule: payload_open/payload_close only without reports_position.
    if (platform == "valve") {
        EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(c, "reports_position")));
    }
    const cJSON *category = cJSON_GetObjectItem(c, "entity_category");
    if (category != nullptr) {
        const std::string v = category->valuestring;
        EXPECT_TRUE(v == "config" || v == "diagnostic") << v;
    }
}

} // namespace

TEST(MqttDiscovery, FixtureLoads)
{
    ASSERT_NE(schema().root, nullptr) << "cannot read " << ZF_HA_SCHEMA_PATH;
    EXPECT_NE(cJSON_GetObjectItem(cJSON_GetObjectItem(schema().root, "platforms"), "valve"), nullptr);
}

TEST(MqttDiscovery, DocumentMatchesTheDocumentedShape)
{
    ASSERT_NE(schema().root, nullptr);
    const std::string text = discoveryPayload(fullConfig(), identity(), kTopics, {});
    Json doc(text);
    ASSERT_NE(doc.root, nullptr) << "not JSON";

    for (const std::string &req : stringSet(cJSON_GetObjectItem(schema().root, "root_required"))) {
        EXPECT_TRUE(cJSON_HasObjectItem(doc.root, req.c_str())) << "root needs " << req;
    }
    const std::set<std::string> rootKeys = {"device", "origin", "components", "availability_topic", "qos"};
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, doc.root)
    {
        EXPECT_EQ(rootKeys.count(item->string), 1u) << "unexpected root key " << item->string;
    }
    const std::set<std::string> shared = stringSet(cJSON_GetObjectItem(schema().root, "shared_options"));
    EXPECT_EQ(shared.count("availability_topic"), 1u);
    EXPECT_EQ(shared.count("qos"), 1u);
    EXPECT_EQ(std::string(cJSON_GetObjectItem(doc.root, "availability_topic")->valuestring), kTopics.availability());

    const cJSON *dev = cJSON_GetObjectItem(doc.root, "device");
    const std::set<std::string> devOptions = stringSet(cJSON_GetObjectItem(schema().root, "device_options"));
    cJSON_ArrayForEach(item, dev)
    {
        EXPECT_EQ(devOptions.count(item->string), 1u) << "device has no documented option " << item->string;
    }
    ASSERT_TRUE(cJSON_IsArray(cJSON_GetObjectItem(dev, "identifiers")));
    EXPECT_STREQ(cJSON_GetArrayItem(cJSON_GetObjectItem(dev, "identifiers"), 0)->valuestring, "zoneflow_a1b2c3d4e5f6");
    EXPECT_STREQ(cJSON_GetObjectItem(dev, "name")->valuestring, "Back garden");
    EXPECT_STREQ(cJSON_GetObjectItem(dev, "configuration_url")->valuestring, "http://zoneflow-e5f6.local/");
    const cJSON *conn = cJSON_GetArrayItem(cJSON_GetObjectItem(dev, "connections"), 0);
    EXPECT_STREQ(cJSON_GetArrayItem(conn, 0)->valuestring, "mac");
    EXPECT_STREQ(cJSON_GetArrayItem(conn, 1)->valuestring, "a1:b2:c3:d4:e5:f6");

    const cJSON *origin = cJSON_GetObjectItem(doc.root, "origin");
    const std::set<std::string> originOptions = stringSet(cJSON_GetObjectItem(schema().root, "origin_options"));
    cJSON_ArrayForEach(item, origin) { EXPECT_EQ(originOptions.count(item->string), 1u) << item->string; }
    EXPECT_TRUE(cJSON_IsString(cJSON_GetObjectItem(origin, "name")));

    const cJSON *cmps = cJSON_GetObjectItem(doc.root, "components");
    ASSERT_TRUE(cJSON_IsObject(cmps));
    std::set<std::string> uniqueIds;
    std::size_t count = 0;
    cJSON_ArrayForEach(item, cmps)
    {
        ++count;
        // Home Assistant: component ids are in [a-zA-Z0-9_-].
        for (const char ch : std::string(item->string)) {
            EXPECT_TRUE(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-') << item->string;
        }
        checkComponent(item->string, item);
        const cJSON *uid = cJSON_GetObjectItem(item, "unique_id");
        ASSERT_TRUE(cJSON_IsString(uid));
        EXPECT_TRUE(uniqueIds.insert(uid->valuestring).second) << "duplicate unique_id " << uid->valuestring;
        EXPECT_EQ(std::string(uid->valuestring).rfind("zoneflow_a1b2c3d4e5f6_", 0), 0u);
    }
    // 7 enabled zones x 3, 2 programs x 2, 5 buttons, rain delay, rain, spare,
    // flow x 2, current, AC, problem, valve board, sensor supply, queue,
    // WiFi, firmware, update.
    EXPECT_EQ(count, 7u * 3 + 2 * 2 + 5 + 1 + 2 + 2 + 1 + 1 + 1 + 2 + 1 + 1 + 1 + 1);
    // Size check: published in one message (esp-mqtt fragments it on the
    // socket); this bounds the buffer the glue builds.
    EXPECT_LT(text.size(), 32u * 1024u) << "discovery document is " << text.size() << " bytes";
}

TEST(MqttDiscovery, ZoneEntitiesAndTheirTopics)
{
    Json doc(discoveryPayload(fullConfig(), identity(), kTopics, {}));
    const cJSON *cmps = cJSON_GetObjectItem(doc.root, "components");
    const cJSON *valve = cJSON_GetObjectItem(cmps, "zone_1_valve");
    ASSERT_NE(valve, nullptr);
    EXPECT_STREQ(cJSON_GetObjectItem(valve, "platform")->valuestring, "valve");
    EXPECT_STREQ(cJSON_GetObjectItem(valve, "name")->valuestring, "Lawn");
    EXPECT_EQ(std::string(cJSON_GetObjectItem(valve, "command_topic")->valuestring), kTopics.command("zone/1/set"));
    EXPECT_EQ(std::string(cJSON_GetObjectItem(valve, "state_topic")->valuestring), kTopics.zone(1));
    EXPECT_EQ(cJSON_GetObjectItem(cmps, "zone_6_valve"), nullptr) << "zone 6 is disabled";
    EXPECT_EQ(cJSON_GetObjectItem(cmps, "zone_1_switch"), nullptr);

    const cJSON *num = cJSON_GetObjectItem(cmps, "zone_1_run_time");
    ASSERT_NE(num, nullptr);
    EXPECT_EQ(cJSON_GetObjectItem(num, "max")->valueint, 240);
    EXPECT_EQ(std::string(cJSON_GetObjectItem(num, "command_topic")->valuestring),
              kTopics.command("zone/1/default_run_s"));

    Config sw = fullConfig();
    sw.mqtt.zoneEntity = HaZoneEntity::Switch;
    Json doc2(discoveryPayload(sw, identity(), kTopics, {}));
    const cJSON *cmps2 = cJSON_GetObjectItem(doc2.root, "components");
    const cJSON *s = cJSON_GetObjectItem(cmps2, "zone_1_switch");
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(cJSON_GetObjectItem(s, "platform")->valuestring, "switch");
    EXPECT_EQ(cJSON_GetObjectItem(cmps2, "zone_1_valve"), nullptr);
}

TEST(MqttDiscovery, OptionalSensorsFollowTheConfig)
{
    Config c = fullConfig();
    c.rain.enabled = false;
    c.flow.enabled = false;
    Json doc(discoveryPayload(c, identity(), kTopics, {}));
    const cJSON *cmps = cJSON_GetObjectItem(doc.root, "components");
    EXPECT_EQ(cJSON_GetObjectItem(cmps, "rain"), nullptr);
    EXPECT_EQ(cJSON_GetObjectItem(cmps, "flow_rate"), nullptr);
    EXPECT_NE(cJSON_GetObjectItem(cmps, "spare"), nullptr);
}

TEST(MqttDiscovery, FlowSensorClasses)
{
    Json doc(discoveryPayload(fullConfig(), identity(), kTopics, {}));
    const cJSON *cmps = cJSON_GetObjectItem(doc.root, "components");
    const cJSON *rate = cJSON_GetObjectItem(cmps, "flow_rate");
    EXPECT_STREQ(cJSON_GetObjectItem(rate, "state_class")->valuestring, "measurement");
    const cJSON *total = cJSON_GetObjectItem(cmps, "flow_total");
    EXPECT_STREQ(cJSON_GetObjectItem(total, "device_class")->valuestring, "water");
    EXPECT_STREQ(cJSON_GetObjectItem(total, "state_class")->valuestring, "total_increasing");
    const cJSON *update = cJSON_GetObjectItem(cmps, "firmware_update");
    EXPECT_EQ(cJSON_GetObjectItem(update, "command_topic"), nullptr) << "no install from Home Assistant";
}

TEST(MqttDiscovery, PlannerRemovesComponentsOnceThenOmitsThem)
{
    DiscoveryPlanner planner;
    Config c = fullConfig();
    c.zoneCount = 4;
    c.spare.enabled = false;

    // First plan after boot: removal entries for everything not wanted
    // (zones 5..8, the switch variants, spare), then the clean document.
    auto first = planner.plan(c, identity(), kTopics);
    ASSERT_EQ(first.size(), 2u);
    {
        Json withRemovals(first[0]);
        const cJSON *cmps = cJSON_GetObjectItem(withRemovals.root, "components");
        const cJSON *gone = cJSON_GetObjectItem(cmps, "zone_6_valve");
        ASSERT_NE(gone, nullptr);
        EXPECT_EQ(cJSON_GetArraySize(gone), 1) << "removal entry carries only the platform";
        EXPECT_STREQ(cJSON_GetObjectItem(gone, "platform")->valuestring, "valve");
        EXPECT_NE(cJSON_GetObjectItem(cmps, "zone_1_switch"), nullptr);
        EXPECT_NE(cJSON_GetObjectItem(cmps, "spare"), nullptr);
        EXPECT_GT(cJSON_GetArraySize(cJSON_GetObjectItem(cmps, "zone_1_valve")), 1);
        Json clean(first[1]);
        EXPECT_EQ(cJSON_GetObjectItem(cJSON_GetObjectItem(clean.root, "components"), "zone_6_valve"), nullptr);
    }
    // Republish (reconnect, Home Assistant birth): nothing new to remove.
    EXPECT_EQ(planner.plan(c, identity(), kTopics).size(), 1u);

    // A zone gets disabled and a program deleted: removed once.
    c.zones[1].enabled = false;
    c.schedule.programCount = 1;
    auto second = planner.plan(c, identity(), kTopics);
    ASSERT_EQ(second.size(), 2u);
    Json r(second[0]);
    const cJSON *cmps = cJSON_GetObjectItem(r.root, "components");
    EXPECT_STREQ(cJSON_GetObjectItem(cJSON_GetObjectItem(cmps, "zone_2_valve"), "platform")->valuestring, "valve");
    EXPECT_STREQ(cJSON_GetObjectItem(cJSON_GetObjectItem(cmps, "program_9_run"), "platform")->valuestring, "button");
    EXPECT_STREQ(cJSON_GetObjectItem(cJSON_GetObjectItem(cmps, "program_9_enabled"), "platform")->valuestring,
                 "switch");
    EXPECT_EQ(cJSON_GetObjectItem(cmps, "zone_6_valve"), nullptr) << "already removed";
    EXPECT_EQ(planner.plan(c, identity(), kTopics).size(), 1u);

    // Enabled again, then disabled again: removed again.
    c.zones[1].enabled = true;
    EXPECT_EQ(planner.plan(c, identity(), kTopics).size(), 1u);
    c.zones[1].enabled = false;
    EXPECT_EQ(planner.plan(c, identity(), kTopics).size(), 2u);
}

TEST(MqttDiscovery, SwitchingEntityTypeRemovesTheOldOne)
{
    DiscoveryPlanner planner;
    Config c = fullConfig();
    planner.plan(c, identity(), kTopics);
    c.mqtt.zoneEntity = HaZoneEntity::Switch;
    auto out = planner.plan(c, identity(), kTopics);
    ASSERT_EQ(out.size(), 2u);
    Json r(out[0]);
    const cJSON *v = cJSON_GetObjectItem(cJSON_GetObjectItem(r.root, "components"), "zone_1_valve");
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(cJSON_GetArraySize(v), 1);
}
