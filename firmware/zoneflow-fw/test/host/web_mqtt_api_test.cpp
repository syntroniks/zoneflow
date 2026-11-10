// SPDX-License-Identifier: Apache-2.0
// REST routes for MQTT: GET/PUT /api/v1/mqtt. The password and CA
// certificate are write-only and never part of the configuration document;
// connection fields go through the core's validation.
#include <gtest/gtest.h>

#include <memory>

#include "cJSON.h"
#include "fakes.hpp"
#include "web_fakes.hpp"
#include "zf_web/api.hpp"
#include "zf_web/json_map.hpp"

using namespace zf;
using namespace zf::web;
using namespace zf::test;

namespace {

constexpr const char *kPem = "-----BEGIN CERTIFICATE-----\nMIIBszCCAVmgAwIBAgIU\n-----END CERTIFICATE-----\n";

class MqttApiTest : public ::testing::Test {
protected:
    MemoryStorage configStorage;
    MemoryStorage webStorage;
    ConfigStore config{configStorage};
    StateStore state;
    EventBus bus;
    RecordingSink sink;
    FakeSystemTime systemTime;
    TimeService time{systemTime, nullptr};
    FakeClock clock;
    SeqRandom rng;
    AuthService auth{webStorage, rng, 2};
    FakePlatform platform;
    std::unique_ptr<Api> api;
    std::string token;

    void SetUp() override
    {
        ASSERT_TRUE(config.update(Config::defaults()).ok);
        AppServices services{bus, state, sink, config, time};
        platform.clock = &clock;
        api = std::make_unique<Api>(services, auth, platform, webStorage, clock);
        api->begin();
        ApiRequest r;
        const std::string body = R"({"setup_code":"ABCDEFGHJK","password":"admin pass"})";
        r.method = "POST";
        r.path = "/api/v1/auth/setup";
        r.body = body;
        r.contentLength = body.size();
        r.contentType = "application/json";
        r.host = "192.168.1.30";
        r.peer = "192.168.1.50";
        const ApiResponse s = api->handle(r);
        ASSERT_EQ(s.status, 201) << s.body;
        JsonPtr o = parseJson(s.body);
        token = std::string("Bearer ") + cJSON_GetObjectItem(o.get(), "token")->valuestring;
    }

    ApiResponse call(const std::string &method, const std::string &body = "", bool authed = true)
    {
        ApiRequest r;
        r.method = method;
        r.path = "/api/v1/mqtt";
        r.body = body;
        r.contentLength = body.size();
        r.contentType = body.empty() ? "" : "application/json";
        r.host = "192.168.1.30";
        r.peer = "192.168.1.50";
        if (authed) {
            r.authorization = token;
        }
        return api->handle(r);
    }
};

std::string field(const std::string &body, const char *key)
{
    JsonPtr o = parseJson(body);
    const cJSON *e = cJSON_GetObjectItem(o.get(), "error");
    const cJSON *v = cJSON_GetObjectItem(e != nullptr ? e : o.get(), key);
    if (cJSON_IsString(v)) {
        return v->valuestring;
    }
    if (cJSON_IsNumber(v)) {
        return std::to_string(v->valueint);
    }
    if (cJSON_IsBool(v)) {
        return cJSON_IsTrue(v) ? "true" : "false";
    }
    return v == nullptr ? "<missing>" : "<other>";
}

} // namespace

TEST_F(MqttApiTest, GetShowsSettingsAndStatusButNoSecrets)
{
    platform.mqttPassword = "hunter22";
    platform.mqtt.state = mqtt::ConnState::Connected;
    platform.mqtt.broker.assign("mqtt://192.168.1.10:1883");
    platform.mqtt.connectedSinceMs = clock.monoMs();
    clock.advanceMs(5000);
    const ApiResponse r = call("GET", "", false); // reads are open by default
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(field(r.body, "enabled"), "false");
    EXPECT_EQ(field(r.body, "port"), "1883");
    EXPECT_EQ(field(r.body, "discovery_prefix"), "homeassistant");
    EXPECT_EQ(field(r.body, "password_set"), "true");
    EXPECT_EQ(field(r.body, "ca_cert_set"), "false");
    EXPECT_EQ(r.body.find("hunter22"), std::string::npos);
    EXPECT_EQ(field(r.body, "password"), "<missing>");
    JsonPtr o = parseJson(r.body);
    const cJSON *st = cJSON_GetObjectItem(o.get(), "status");
    EXPECT_STREQ(cJSON_GetObjectItem(st, "state")->valuestring, "connected");
    EXPECT_EQ(cJSON_GetObjectItem(st, "connected_for_s")->valueint, 5);
}

TEST_F(MqttApiTest, PutNeedsAdmin) { EXPECT_EQ(call("PUT", R"({"enabled":true})", false).status, 401); }

TEST_F(MqttApiTest, PutStoresSettingsAndWriteOnlyPassword)
{
    const ApiResponse r = call("PUT", R"({"enabled":true,"host":"broker.lan","port":8883,"tls":true,
        "username":"zf","password":"s3cret pass","ha_zone_entity":"switch"})");
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(field(r.body, "password_set"), "true");
    EXPECT_EQ(r.body.find("s3cret"), std::string::npos);
    const Config c = config.get();
    EXPECT_TRUE(c.mqtt.enabled);
    EXPECT_EQ(c.mqtt.host.view(), "broker.lan");
    EXPECT_EQ(c.mqtt.port, 8883);
    EXPECT_TRUE(c.mqtt.tls);
    EXPECT_EQ(c.mqtt.zoneEntity, HaZoneEntity::Switch);
    EXPECT_EQ(platform.mqttPassword, "s3cret pass");
    EXPECT_EQ(sink.count(CommandType::ReloadConfig), 1);
    // Not in the configuration document, the backup or GET /config.
    EXPECT_EQ(config.toJson().find("s3cret"), std::string::npos);
    EXPECT_EQ(configStorage.data[ConfigStore::kConfigKey].find("s3cret"), std::string::npos);
}

TEST_F(MqttApiTest, PasswordOnlyDoesNotRewriteTheConfig)
{
    const int writes = configStorage.writes;
    const ApiResponse r = call("PUT", R"({"password":"another"})");
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(configStorage.writes, writes);
    EXPECT_EQ(platform.mqttPassword, "another");
    EXPECT_EQ(call("PUT", R"({"password":null})").status, 200);
    EXPECT_EQ(platform.mqttPassword, "");
}

TEST_F(MqttApiTest, CustomCaCertificate)
{
    std::string body = std::string(R"({"tls":true,"ca":"custom","ca_cert":")");
    for (const char *p = kPem; *p != '\0'; ++p) {
        body += *p == '\n' ? std::string("\\n") : std::string(1, *p);
    }
    body += "\"}";
    const ApiResponse r = call("PUT", body);
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(field(r.body, "ca_cert_set"), "true");
    EXPECT_EQ(platform.mqttCa, kPem);
    EXPECT_EQ(config.get().mqtt.ca, MqttCaSource::Custom);
    EXPECT_EQ(r.body.find("BEGIN CERTIFICATE"), std::string::npos) << "the certificate is not echoed";

    const ApiResponse bad = call("PUT", R"({"ca_cert":"not a certificate"})");
    EXPECT_EQ(bad.status, 400);
    EXPECT_EQ(field(bad.body, "path"), "ca_cert");
}

TEST_F(MqttApiTest, ValidationErrorsComeFromTheCoreWithRelativePaths)
{
    const int writes = configStorage.writes;
    ApiResponse r = call("PUT", R"({"enabled":true,"host":""})");
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(field(r.body, "code"), "invalid_config");
    EXPECT_EQ(field(r.body, "path"), "host");
    r = call("PUT", R"({"base_topic":"a/#"})");
    EXPECT_EQ(field(r.body, "path"), "base_topic");
    r = call("PUT", R"({"port":70000})");
    EXPECT_EQ(field(r.body, "path"), "port");
    r = call("PUT", R"({"colour":"blue"})");
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(field(r.body, "path"), "colour");
    r = call("PUT", "{\"password\":\"" + std::string(129, 'x') + "\"}");
    EXPECT_EQ(field(r.body, "path"), "password");
    // A rejected request changes nothing, secrets included.
    r = call("PUT", R"({"password":"kept out","host":"bad host"})");
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(platform.mqttSecretWrites, 0);
    EXPECT_EQ(configStorage.writes, writes);
}

TEST_F(MqttApiTest, SecretStorageFailureIsReported)
{
    platform.mqttStoreFails = true;
    const ApiResponse r = call("PUT", R"({"password":"x"})");
    EXPECT_EQ(r.status, 500);
    EXPECT_EQ(field(r.body, "code"), "storage_error");
}

TEST_F(MqttApiTest, ZonePatchAcceptsDefaultRunTime)
{
    ApiRequest r;
    const std::string body = R"({"default_run_s":900})";
    r.method = "PATCH";
    r.path = "/api/v1/zones/2";
    r.body = body;
    r.contentLength = body.size();
    r.contentType = "application/json";
    r.host = "192.168.1.30";
    r.authorization = token;
    const ApiResponse p = api->handle(r);
    ASSERT_EQ(p.status, 200) << p.body;
    EXPECT_EQ(field(p.body, "default_run_s"), "900");
    EXPECT_EQ(config.get().zones[1].defaultRunSec, 900u);
}

// The MQTT routes go through Api::admit() like every other route
// (security review, pull request #23).
TEST_F(MqttApiTest, ReadsFollowProtectReads)
{
    EXPECT_EQ(call("GET", "", false).status, 200);
    ApiRequest r;
    const std::string body = R"({"protect_reads":true})";
    r.method = "PUT";
    r.path = "/api/v1/security";
    r.body = body;
    r.contentLength = body.size();
    r.contentType = "application/json";
    r.host = "192.168.1.30";
    r.authorization = token;
    ASSERT_EQ(api->handle(r).status, 200);
    EXPECT_EQ(call("GET", "", false).status, 401);
    EXPECT_EQ(call("GET").status, 200);
}

TEST_F(MqttApiTest, WritesNeedCsrfOriginAndHost)
{
    const std::string raw = token.substr(std::string("Bearer ").size());
    const auto put = [&](std::string cookie, std::string csrf, std::string origin, std::string host) {
        ApiRequest r;
        const std::string body = R"({"password":"x"})";
        r.method = "PUT";
        r.path = "/api/v1/mqtt";
        r.body = body;
        r.contentLength = body.size();
        r.contentType = "application/json";
        r.host = host;
        r.origin = origin;
        r.cookie = cookie;
        r.csrf = csrf;
        return api->handle(r);
    };
    const std::string cookie = "zf_session=" + raw;
    EXPECT_EQ(put(cookie, "", "", "192.168.1.30").status, 403) << "cookie session without the CSRF header";
    EXPECT_EQ(put("", "", "http://evil.example", "192.168.1.30").status, 403) << "cross-origin write";
    EXPECT_EQ(put("", "", "", "evil.example").status, 403) << "DNS rebinding guard";
    EXPECT_EQ(platform.mqttSecretWrites, 0);
}

TEST_F(MqttApiTest, BodyLimitAndContentType)
{
    std::string big = "{\"ca_cert\":\"" + std::string(9000, 'A') + "\"}";
    EXPECT_EQ(call("PUT", big).status, 413);
    ApiRequest r;
    const std::string body = "password=x";
    r.method = "PUT";
    r.path = "/api/v1/mqtt";
    r.body = body;
    r.contentLength = body.size();
    r.contentType = "application/x-www-form-urlencoded";
    r.host = "192.168.1.30";
    r.authorization = token;
    EXPECT_EQ(api->handle(r).status, 415);
}
