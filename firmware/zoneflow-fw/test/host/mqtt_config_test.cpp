// SPDX-License-Identifier: Apache-2.0
// Configuration schema 2: the "mqtt" object, zones[].default_run_s and the
// migration from schema 1.
#include <gtest/gtest.h>

#include "cJSON.h"
#include "fakes.hpp"
#include "zf/config/config_json.hpp"
#include "zf/config/config_store.hpp"

using namespace zf;
using namespace zf::test;

TEST(MqttConfig, DefaultsAreOffAndSensible)
{
    const Config c = Config::defaults();
    EXPECT_EQ(kConfigSchemaVersion, 2);
    EXPECT_FALSE(c.mqtt.enabled);
    EXPECT_EQ(c.mqtt.port, 1883);
    EXPECT_FALSE(c.mqtt.tls);
    EXPECT_EQ(c.mqtt.ca, MqttCaSource::Bundle);
    EXPECT_TRUE(c.mqtt.discovery);
    EXPECT_EQ(c.mqtt.discoveryPrefix.view(), "homeassistant");
    EXPECT_EQ(c.mqtt.zoneEntity, HaZoneEntity::Valve);
    EXPECT_TRUE(c.mqtt.baseTopic.empty());
    EXPECT_EQ(c.zones[0].defaultRunSec, 600u);
    EXPECT_TRUE(validate(c).ok);
}

TEST(MqttConfig, RoundTripsThroughJson)
{
    Config c = Config::defaults();
    c.mqtt.enabled = true;
    c.mqtt.host.assign("broker.lan");
    c.mqtt.port = 8883;
    c.mqtt.username.assign("zoneflow");
    c.mqtt.tls = true;
    c.mqtt.ca = MqttCaSource::Custom;
    c.mqtt.clientId.assign("garden-ctl");
    c.mqtt.baseTopic.assign("garden/irrigation");
    c.mqtt.discovery = false;
    c.mqtt.discoveryPrefix.assign("ha");
    c.mqtt.zoneEntity = HaZoneEntity::Switch;
    c.zones[3].defaultRunSec = 1200;
    const std::string json = configToJson(c);
    EXPECT_EQ(json.find("password"), std::string::npos) << "the password never goes into the config document";
    Config back;
    const auto res = configFromJson(json, back);
    ASSERT_TRUE(res.ok()) << res.validation.path << ": " << res.validation.message;
    EXPECT_EQ(res.origin, ConfigOrigin::Current);
    EXPECT_TRUE(back.mqtt.enabled);
    EXPECT_EQ(back.mqtt.host.view(), "broker.lan");
    EXPECT_EQ(back.mqtt.port, 8883);
    EXPECT_EQ(back.mqtt.username.view(), "zoneflow");
    EXPECT_TRUE(back.mqtt.tls);
    EXPECT_EQ(back.mqtt.ca, MqttCaSource::Custom);
    EXPECT_EQ(back.mqtt.clientId.view(), "garden-ctl");
    EXPECT_EQ(back.mqtt.baseTopic.view(), "garden/irrigation");
    EXPECT_FALSE(back.mqtt.discovery);
    EXPECT_EQ(back.mqtt.discoveryPrefix.view(), "ha");
    EXPECT_EQ(back.mqtt.zoneEntity, HaZoneEntity::Switch);
    EXPECT_EQ(back.zones[3].defaultRunSec, 1200u);
}

struct MqttBad {
    const char *json;
    const char *path;
};

class MqttConfigRejects : public ::testing::TestWithParam<MqttBad> {};

TEST_P(MqttConfigRejects, WithPath)
{
    Config c;
    const auto res = configFromJson(GetParam().json, c);
    EXPECT_FALSE(res.ok()) << GetParam().json;
    EXPECT_EQ(res.validation.path, GetParam().path) << res.validation.message;
}

INSTANTIATE_TEST_SUITE_P(
    Mqtt, MqttConfigRejects,
    ::testing::Values(MqttBad{R"({"schema":2,"mqtt":{"enabled":true}})", "mqtt.host"},
                      MqttBad{R"({"schema":2,"mqtt":{"host":"mqtt://broker"}})", "mqtt.host"},
                      MqttBad{R"({"schema":2,"mqtt":{"host":"bro ker"}})", "mqtt.host"},
                      MqttBad{R"({"schema":2,"mqtt":{"host":"b","port":0}})", "mqtt.port"},
                      MqttBad{R"({"schema":2,"mqtt":{"host":"b","port":65536}})", "mqtt.port"},
                      MqttBad{R"({"schema":2,"mqtt":{"username":"a\u0001b"}})", "mqtt.username"},
                      MqttBad{R"({"schema":2,"mqtt":{"client_id":"two words"}})", "mqtt.client_id"},
                      MqttBad{R"({"schema":2,"mqtt":{"base_topic":"a/+/b"}})", "mqtt.base_topic"},
                      MqttBad{R"({"schema":2,"mqtt":{"base_topic":"a/#"}})", "mqtt.base_topic"},
                      MqttBad{R"({"schema":2,"mqtt":{"base_topic":"/a"}})", "mqtt.base_topic"},
                      MqttBad{R"({"schema":2,"mqtt":{"base_topic":"a/"}})", "mqtt.base_topic"},
                      MqttBad{R"({"schema":2,"mqtt":{"base_topic":"a//b"}})", "mqtt.base_topic"},
                      MqttBad{R"({"schema":2,"mqtt":{"discovery_prefix":""}})", "mqtt.discovery_prefix"},
                      MqttBad{R"({"schema":2,"mqtt":{"ca":"system"}})", "mqtt.ca"},
                      MqttBad{R"({"schema":2,"mqtt":{"ha_zone_entity":"light"}})", "mqtt.ha_zone_entity"},
                      MqttBad{R"({"schema":2,"mqtt":{"tls":"yes"}})", "mqtt.tls"},
                      MqttBad{R"({"schema":2,"zones":[{"default_run_s":0}]})", "zones[0].default_run_s"},
                      MqttBad{R"({"schema":2,"zones":[{},{"default_run_s":14401}]})", "zones[1].default_run_s"}));

TEST(MqttConfig, TopicPrefixRule)
{
    EXPECT_TRUE(validTopicPrefix("zoneflow"));
    EXPECT_TRUE(validTopicPrefix("home/garden/zoneflow-1"));
    EXPECT_FALSE(validTopicPrefix(""));
    EXPECT_FALSE(validTopicPrefix(std::string(64, 'a')));
    EXPECT_FALSE(validTopicPrefix("a\tb"));
}

// ---- migration 1 -> 2 ---------------------------------------------------------------------

TEST(MqttConfig, SchemaOneDocumentMigrates)
{
    Config c;
    const auto res = configFromJson(R"({"schema":1,"device_name":"shed","zone_count":2,
        "zones":[{"name":"Lawn","max_run_s":900},{"name":"Beds"}]})",
                                    c);
    ASSERT_TRUE(res.ok()) << res.validation.message;
    EXPECT_EQ(res.origin, ConfigOrigin::Migrated);
    EXPECT_EQ(res.storedVersion, 1);
    EXPECT_EQ(c.deviceName.view(), "shed");
    EXPECT_EQ(c.zones[0].maxRunSec, 900u);
    EXPECT_EQ(c.zones[0].defaultRunSec, 600u);
    EXPECT_FALSE(c.mqtt.enabled);
}

TEST(MqttConfig, MigrationKeepsValuesAlreadyPresent)
{
    cJSON *root = cJSON_Parse(R"({"schema":1,"zones":[{"name":"A"},{"name":"B","default_run_s":60}]})");
    ASSERT_NE(root, nullptr);
    Config parsed;
    char *text = cJSON_PrintUnformatted(root);
    const auto res = configFromJson(text, parsed);
    cJSON_free(text);
    cJSON_Delete(root);
    ASSERT_TRUE(res.ok());
    EXPECT_EQ(parsed.zones[0].defaultRunSec, 600u);
    EXPECT_EQ(parsed.zones[1].defaultRunSec, 60u) << "a value already present is kept";
}

TEST(MqttConfig, StoredSchemaOneIsWrittenBackAsSchemaTwo)
{
    MemoryStorage mem;
    mem.data[ConfigStore::kConfigKey] = R"({"schema":1,"device_name":"barn","zones":[{"name":"Z1"}]})";
    ConfigStore store(mem);
    const auto loaded = store.load();
    EXPECT_EQ(loaded.status, ConfigStore::LoadStatus::Migrated);
    EXPECT_EQ(store.get().deviceName.view(), "barn");
    const std::string &stored = mem.data[ConfigStore::kConfigKey];
    cJSON *doc = cJSON_Parse(stored.c_str());
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(cJSON_GetObjectItem(doc, "schema")->valueint, 2);
    const cJSON *mqtt = cJSON_GetObjectItem(doc, "mqtt");
    ASSERT_TRUE(cJSON_IsObject(mqtt));
    EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(mqtt, "enabled")));
    EXPECT_EQ(cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(doc, "zones"), 0), "default_run_s")->valueint,
              600);
    cJSON_Delete(doc);
    ConfigStore again(mem);
    EXPECT_EQ(again.load().status, ConfigStore::LoadStatus::Loaded);
}

TEST(MqttConfig, SchemaOneThroughTheApiPathIsAccepted)
{
    // A backup made by schema-1 firmware restores on this firmware.
    MemoryStorage mem;
    ConfigStore store(mem);
    store.load();
    const ValidationResult v = store.updateFromJson(R"({"schema":1,"device_name":"old-backup"})");
    EXPECT_TRUE(v.ok) << v.path << ": " << v.message;
    EXPECT_EQ(store.get().deviceName.view(), "old-backup");
}
