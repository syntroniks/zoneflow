// SPDX-License-Identifier: Apache-2.0
// REST API: access control, CSRF, request validation, and the mapping from
// API calls to core commands and configuration changes. Real ConfigStore,
// StateStore and TimeService; fake command sink and platform.
#include <gtest/gtest.h>

#include <memory>

#include "cJSON.h"
#include "fakes.hpp"
#include "web_fakes.hpp"
#include "zf/app/controller.hpp"
#include "zf_web/api.hpp"
#include "zf_web/command_watch.hpp"
#include "zf_web/json_map.hpp"

using namespace zf;
using namespace zf::web;
using namespace zf::test;

namespace {

struct Call {
    std::string method = "GET";
    std::string path{};
    std::string body{};
    std::string contentType{};
    std::string host = "192.168.1.30";
    std::string origin{};
    std::string cookie{};
    std::string authorization{};
    std::string csrf{};
    std::string peer = "192.168.1.50";
    std::size_t contentLength = 0;
    bool viaSetupAp = false;
};

class ApiTest : public ::testing::Test {
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
    std::string csrf;

    void SetUp() override
    {
        ASSERT_TRUE(config.update(Config::defaults()).ok);
        ASSERT_TRUE(time.setManual(clock.utc));
        AppServices services{bus, state, sink, config, time};
        platform.clock = &clock;
        api = std::make_unique<Api>(services, auth, platform, webStorage, clock);
        api->begin();
        publishState();
    }

    void publishState(void (*edit)(SystemState &) = nullptr)
    {
        SystemState s;
        s.utc = clock.utc;
        s.timeStatus = TimeStatus::Synced;
        s.zoneCount = 8;
        if (edit != nullptr) {
            edit(s);
        }
        state.publish(s);
    }

    ApiResponse call(const Call &c)
    {
        ApiRequest r;
        r.method = c.method;
        const std::size_t q = c.path.find('?');
        r.path = std::string_view(c.path).substr(0, q);
        r.query = q == std::string::npos ? std::string_view{} : std::string_view(c.path).substr(q + 1);
        r.body = c.body;
        r.contentLength = c.contentLength != 0 ? c.contentLength : c.body.size();
        r.contentType = c.contentType.empty() && !c.body.empty() ? std::string_view("application/json")
                                                                 : std::string_view(c.contentType);
        r.host = c.host;
        r.origin = c.origin;
        r.cookie = c.cookie;
        r.authorization = c.authorization;
        r.csrf = c.csrf;
        r.peer = c.peer;
        r.viaSetupAp = c.viaSetupAp;
        return api->handle(r);
    }

    ApiResponse get(const std::string &path) { return call({.path = path}); }

    // Admin call with a bearer token (no CSRF needed).
    ApiResponse admin(const std::string &method, const std::string &path, const std::string &body = "")
    {
        return call({.method = method, .path = path, .body = body, .authorization = "Bearer " + token});
    }

    void login()
    {
        const ApiResponse r = call({.method = "POST",
                                    .path = "/api/v1/auth/setup",
                                    .body = R"({"setup_code":"ABCDEFGHJK","password":"admin pass"})"});
        ASSERT_EQ(r.status, 201) << r.body;
        JsonPtr o = parseJson(r.body);
        token = cJSON_GetObjectItem(o.get(), "token")->valuestring;
        csrf = cJSON_GetObjectItem(o.get(), "csrf")->valuestring;
    }

    static std::string field(const ApiResponse &r, const char *key)
    {
        JsonPtr o = parseJson(r.body);
        const cJSON *e = cJSON_GetObjectItem(o.get(), "error");
        const cJSON *v = cJSON_GetObjectItem(e != nullptr ? e : o.get(), key);
        if (cJSON_IsString(v)) {
            return v->valuestring;
        }
        if (cJSON_IsNumber(v)) {
            return std::to_string(static_cast<long long>(v->valuedouble));
        }
        if (cJSON_IsBool(v)) {
            return cJSON_IsTrue(v) ? "true" : "false";
        }
        return v == nullptr ? "<missing>" : "<other>";
    }

    static std::string header(const ApiResponse &r, const std::string &name)
    {
        for (const auto &[k, v] : r.headers) {
            if (k == name) {
                return v;
            }
        }
        return {};
    }
};

} // namespace

// ---- routing and access ----------------------------------------------------------------

TEST_F(ApiTest, UnknownRouteAndWrongMethod)
{
    EXPECT_EQ(get("/api/v1/nope").status, 404);
    const ApiResponse r = call({.method = "DELETE", .path = "/api/v1/zones"});
    EXPECT_EQ(r.status, 405);
    EXPECT_EQ(header(r, "Allow"), "GET");
    const ApiResponse p = call({.method = "POST", .path = "/api/v1/programs/3"});
    EXPECT_EQ(p.status, 405);
    EXPECT_EQ(header(p, "Allow"), "GET, PUT, DELETE");
}

TEST_F(ApiTest, HostHeaderGuardsAgainstDnsRebinding)
{
    EXPECT_EQ(call({.path = "/api/v1/status", .host = "evil.example"}).status, 403);
    EXPECT_EQ(call({.path = "/api/v1/status", .host = "zoneflow-ab12.local"}).status, 200);
    EXPECT_EQ(call({.path = "/api/v1/status", .host = "ZoneFlow-AB12.local:80"}).status, 200);
    EXPECT_EQ(call({.path = "/api/v1/status", .host = "192.168.1.30:8080"}).status, 200);
    login();
    ASSERT_EQ(admin("PUT", "/api/v1/security", R"({"check_host":false})").status, 200);
    EXPECT_EQ(call({.path = "/api/v1/status", .host = "evil.example"}).status, 200);
}

TEST_F(ApiTest, WritesNeedAPasswordAndASession)
{
    ApiResponse r = call({.method = "POST", .path = "/api/v1/queue/stop"});
    EXPECT_EQ(r.status, 401);
    EXPECT_EQ(field(r, "code"), "password_not_set");
    login();
    r = call({.method = "POST", .path = "/api/v1/queue/stop"});
    EXPECT_EQ(r.status, 401);
    EXPECT_EQ(field(r, "code"), "unauthorized");
    EXPECT_EQ(sink.commands.size(), 0u);
    EXPECT_EQ(admin("POST", "/api/v1/queue/stop").status, 202);
    EXPECT_EQ(sink.count(CommandType::StopAll), 1);
}

TEST_F(ApiTest, ReadsOpenUnlessProtected)
{
    EXPECT_EQ(get("/api/v1/status").status, 200);
    login();
    ASSERT_EQ(admin("PUT", "/api/v1/security", R"({"protect_reads":true})").status, 200);
    EXPECT_EQ(get("/api/v1/status").status, 401);
    EXPECT_EQ(admin("GET", "/api/v1/status").status, 200);
    // Login and auth status stay public.
    EXPECT_EQ(get("/api/v1/auth").status, 200);
    // Settings persist.
    EXPECT_EQ(WebSettings::decode(webStorage.data.at(Api::kSettingsKey)).protectReads, true);
}

TEST_F(ApiTest, SetupSetsHardenedCookie)
{
    const ApiResponse bad = call({.method = "POST",
                                  .path = "/api/v1/auth/setup",
                                  .body = R"({"setup_code":"WRONGWRONG","password":"admin pass"})"});
    EXPECT_EQ(bad.status, 401);
    const ApiResponse r = call({.method = "POST",
                                .path = "/api/v1/auth/setup",
                                .body = R"({"setup_code":"ABCDEFGHJK","password":"admin pass"})"});
    ASSERT_EQ(r.status, 201);
    const std::string cookie = header(r, "Set-Cookie");
    EXPECT_EQ(cookie.rfind("zf_session=", 0), 0u);
    EXPECT_NE(cookie.find("HttpOnly"), std::string::npos);
    EXPECT_NE(cookie.find("SameSite=Strict"), std::string::npos);
    EXPECT_EQ(cookie.find("Secure"), std::string::npos); // plain HTTP
    EXPECT_EQ(header(r, "Cache-Control"), "no-store");
    // The setup code is never echoed.
    EXPECT_EQ(r.body.find("ABCDEFGHJK"), std::string::npos);
}

TEST_F(ApiTest, CookieWritesNeedCsrfHeader)
{
    login();
    const std::string cookie = "theme=dark; zf_session=" + token;
    ApiResponse r = call({.method = "POST", .path = "/api/v1/queue/pause", .cookie = cookie});
    EXPECT_EQ(r.status, 403);
    EXPECT_EQ(field(r, "code"), "csrf");
    r = call({.method = "POST", .path = "/api/v1/queue/pause", .cookie = cookie, .csrf = "0" + csrf.substr(1)});
    EXPECT_EQ(r.status, 403);
    r = call({.method = "POST", .path = "/api/v1/queue/pause", .cookie = cookie, .csrf = csrf});
    EXPECT_EQ(r.status, 202);
    // Reads with the cookie need no CSRF header.
    EXPECT_EQ(call({.path = "/api/v1/auth", .cookie = cookie}).status, 200);
    EXPECT_EQ(field(call({.path = "/api/v1/auth", .cookie = cookie}), "csrf"), csrf);
}

TEST_F(ApiTest, CrossOriginWritesRefused)
{
    login();
    ApiResponse r = call({.method = "POST",
                          .path = "/api/v1/queue/stop",
                          .origin = "http://evil.example",
                          .authorization = "Bearer " + token});
    EXPECT_EQ(r.status, 403);
    EXPECT_EQ(field(r, "code"), "bad_origin");
    r = call({.method = "POST",
              .path = "/api/v1/queue/stop",
              .origin = "http://192.168.1.30",
              .authorization = "Bearer " + token});
    EXPECT_EQ(r.status, 202);
    // Login from a foreign origin is refused too (login CSRF).
    r = call(
        {.method = "POST", .path = "/api/v1/auth/login", .body = R"({"password":"admin pass"})", .origin = "null"});
    EXPECT_EQ(r.status, 403);
}

TEST_F(ApiTest, ContentTypeAndSizeChecksRunBeforeParsing)
{
    login();
    ApiResponse r = call({.method = "PUT",
                          .path = "/api/v1/config",
                          .body = "{}",
                          .contentType = "text/plain",
                          .authorization = "Bearer " + token});
    EXPECT_EQ(r.status, 415);
    r = call({.method = "PUT",
              .path = "/api/v1/config",
              .body = "{}",
              .authorization = "Bearer " + token,
              .contentLength = 16385});
    EXPECT_EQ(r.status, 413);
    r = call({.method = "POST",
              .path = "/api/v1/zones/1/run",
              .body = std::string(600, ' '),
              .authorization = "Bearer " + token});
    EXPECT_EQ(r.status, 413);
    const std::string deep = std::string(17, '[') + std::string(17, ']');
    r = admin("PUT", "/api/v1/config", deep);
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(field(r, "code"), "invalid_json");
    r = admin("PUT", "/api/v1/config", "{not json");
    EXPECT_EQ(r.status, 400);
}

TEST_F(ApiTest, LoginRateLimitAnswers429)
{
    login();
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(call({.method = "POST", .path = "/api/v1/auth/login", .body = R"({"password":"wrong one"})"}).status,
                  401);
    }
    const ApiResponse r =
        call({.method = "POST", .path = "/api/v1/auth/login", .body = R"({"password":"admin pass"})"});
    EXPECT_EQ(r.status, 429);
    EXPECT_EQ(header(r, "Retry-After"), "1");
    // Another address may still log in.
    EXPECT_EQ(call({.method = "POST",
                    .path = "/api/v1/auth/login",
                    .body = R"({"password":"admin pass"})",
                    .peer = "192.168.1.60"})
                  .status,
              200);
}

TEST_F(ApiTest, LogoutClearsCookieAndSession)
{
    login();
    const ApiResponse r = admin("POST", "/api/v1/auth/logout");
    EXPECT_EQ(r.status, 200);
    EXPECT_NE(header(r, "Set-Cookie").find("Max-Age=0"), std::string::npos);
    EXPECT_EQ(admin("POST", "/api/v1/queue/stop").status, 401);
}

TEST_F(ApiTest, FullCommandQueueAnswers503)
{
    login();
    sink.full = true;
    const ApiResponse r = admin("POST", "/api/v1/queue/skip");
    EXPECT_EQ(r.status, 503);
    EXPECT_EQ(header(r, "Retry-After"), "1");
}

// ---- zones -----------------------------------------------------------------------------------

TEST_F(ApiTest, ZoneRunMapsOneBasedIdToCoreIndex)
{
    login();
    const ApiResponse r = admin("POST", "/api/v1/zones/3/run", R"({"duration_s":600})");
    ASSERT_EQ(r.status, 202) << r.body;
    ASSERT_EQ(sink.commands.size(), 1u);
    EXPECT_EQ(sink.commands[0].type, CommandType::RunZone);
    EXPECT_EQ(sink.commands[0].zone, 2);
    EXPECT_EQ(sink.commands[0].durationSec, 600u);
    EXPECT_EQ(field(r, "zone"), "3");
}

TEST_F(ApiTest, ZoneRunValidation)
{
    login();
    EXPECT_EQ(admin("POST", "/api/v1/zones/0/run", R"({"duration_s":60})").status, 404);
    EXPECT_EQ(admin("POST", "/api/v1/zones/9/run", R"({"duration_s":60})").status, 404);
    EXPECT_EQ(admin("POST", "/api/v1/zones/x/run", R"({"duration_s":60})").status, 404);
    EXPECT_EQ(admin("POST", "/api/v1/zones/1/run", R"({"duration_s":0})").status, 400);
    EXPECT_EQ(admin("POST", "/api/v1/zones/1/run", R"({"duration_s":14401})").status, 400);
    EXPECT_EQ(admin("POST", "/api/v1/zones/1/run", R"({"duration_s":1.5})").status, 400);
    EXPECT_EQ(admin("POST", "/api/v1/zones/1/run", R"({"duration_s":"60"})").status, 400);
    // Clamped to the zone's maximum (3600 s by default) in the answer.
    const ApiResponse r = admin("POST", "/api/v1/zones/1/run", R"({"duration_s":14400})");
    EXPECT_EQ(r.status, 202);
    EXPECT_EQ(field(r, "duration_s"), "3600");

    Config c = config.get();
    c.zoneCount = 4;
    c.zones[1].enabled = false;
    ASSERT_TRUE(config.update(c).ok);
    EXPECT_EQ(admin("POST", "/api/v1/zones/5/run", R"({"duration_s":60})").status, 404);
    EXPECT_EQ(admin("POST", "/api/v1/zones/2/run", R"({"duration_s":60})").status, 409);
    publishState([](SystemState &s) { s.maintenance = true; });
    const ApiResponse m = admin("POST", "/api/v1/zones/1/run", R"({"duration_s":60})");
    EXPECT_EQ(m.status, 409);
    EXPECT_EQ(field(m, "code"), "inhibited");
}

TEST_F(ApiTest, ZonePatchRenamesAndReportsRelativePath)
{
    login();
    ApiResponse r = admin("PATCH", "/api/v1/zones/2", R"({"name":"Roses","max_run_s":900})");
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(field(r, "name"), "Roses");
    EXPECT_EQ(config.get().zones[1].name.view(), "Roses");
    EXPECT_EQ(config.get().zones[1].maxRunSec, 900u);
    EXPECT_EQ(sink.count(CommandType::ReloadConfig), 1);

    r = admin("PATCH", "/api/v1/zones/2", R"({"max_run_s":0})");
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(field(r, "path"), "max_run_s");
    r = admin("PATCH", "/api/v1/zones/2", R"({"zone_count":2})");
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(field(r, "path"), "zone_count");
    EXPECT_EQ(sink.count(CommandType::ReloadConfig), 1);
}

TEST_F(ApiTest, ZoneStopSkipsTheRunningHeadOnly)
{
    login();
    EXPECT_EQ(admin("POST", "/api/v1/zones/1/stop").status, 409);
    publishState([](SystemState &s) {
        s.zones[0].state = ValveState::Open;
        s.queue.state = QueueState::Running;
        s.queue.length = 2;
        s.queue.shownCount = 2;
        s.queue.items[0].zone = 0;
        s.queue.items[1].zone = 1;
    });
    EXPECT_EQ(admin("POST", "/api/v1/zones/2/stop").status, 409);
    EXPECT_EQ(admin("POST", "/api/v1/zones/1/stop").status, 202);
    EXPECT_EQ(sink.count(CommandType::Skip), 1);
}

TEST_F(ApiTest, ZonesListFollowsZoneCount)
{
    Config c = config.get();
    c.zoneCount = 4;
    ASSERT_TRUE(config.update(c).ok);
    JsonPtr a = parseJson(get("/api/v1/zones").body);
    ASSERT_TRUE(cJSON_IsArray(a.get()));
    EXPECT_EQ(cJSON_GetArraySize(a.get()), 4);
    EXPECT_EQ(cJSON_GetObjectItem(cJSON_GetArrayItem(a.get(), 0), "id")->valueint, 1);
    EXPECT_EQ(get("/api/v1/zones/4").status, 200);
    EXPECT_EQ(get("/api/v1/zones/5").status, 404);
}

// ---- programs -------------------------------------------------------------------------------------

TEST_F(ApiTest, ProgramCrud)
{
    login();
    ApiResponse r = admin("POST", "/api/v1/programs",
                          R"({"name":"Lawn","starts":["06:00"],"days":{"type":"weekdays","weekdays":["mon","thu"]},
                             "durations_s":[600,0,300]})");
    ASSERT_EQ(r.status, 201) << r.body;
    EXPECT_EQ(field(r, "id"), "1");
    EXPECT_EQ(header(r, "Location"), "/api/v1/programs/1");
    r = admin("POST", "/api/v1/programs", R"({"name":"Beds","starts":["07:30"]})");
    ASSERT_EQ(r.status, 201);
    EXPECT_EQ(field(r, "id"), "2");
    r = admin("POST", "/api/v1/programs", R"({"id":2,"name":"Dup"})");
    EXPECT_EQ(r.status, 409);
    r = admin("POST", "/api/v1/programs", R"({"id":40,"name":"Explicit"})");
    EXPECT_EQ(r.status, 201);
    EXPECT_EQ(config.get().schedule.programCount, 3);
    EXPECT_EQ(sink.count(CommandType::ReloadConfig), 3);

    JsonPtr list = parseJson(get("/api/v1/programs").body);
    EXPECT_EQ(cJSON_GetArraySize(list.get()), 3);

    r = admin("PUT", "/api/v1/programs/2", R"({"name":"Beds 2","starts":["08:00","20:00"],"enabled":false})");
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(field(r, "name"), "Beds 2");
    EXPECT_EQ(field(r, "enabled"), "false");
    EXPECT_EQ(admin("PUT", "/api/v1/programs/2", R"({"id":3})").status, 400);
    EXPECT_EQ(admin("PUT", "/api/v1/programs/9", R"({"name":"x"})").status, 404);

    // Validation path is relative to the program body.
    r = admin("PUT", "/api/v1/programs/2", R"({"starts":["25:00"]})");
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(field(r, "path"), "starts[0]");
    EXPECT_EQ(config.get().schedule.programs[1].name.view(), "Beds 2"); // unchanged

    EXPECT_EQ(admin("DELETE", "/api/v1/programs/1").status, 200);
    EXPECT_EQ(get("/api/v1/programs/1").status, 404);
    EXPECT_EQ(get("/api/v1/programs/2").status, 200);
    EXPECT_EQ(admin("DELETE", "/api/v1/programs/1").status, 404);
    EXPECT_EQ(config.get().schedule.programCount, 2);
}

TEST_F(ApiTest, ProgramLimitIsSixteen)
{
    login();
    for (int i = 0; i < 16; ++i) {
        ASSERT_EQ(admin("POST", "/api/v1/programs", R"({"name":"p"})").status, 201);
    }
    const ApiResponse r = admin("POST", "/api/v1/programs", R"({"name":"p"})");
    EXPECT_EQ(r.status, 409);
    EXPECT_EQ(field(r, "code"), "too_many_programs");
}

TEST_F(ApiTest, ProgramRunPostsCommand)
{
    login();
    EXPECT_EQ(admin("POST", "/api/v1/programs/5/run").status, 404);
    ASSERT_EQ(admin("POST", "/api/v1/programs", R"({"id":5,"name":"p","durations_s":[60]})").status, 201);
    EXPECT_EQ(admin("POST", "/api/v1/programs/5/run").status, 202);
    ASSERT_EQ(sink.count(CommandType::RunProgram), 1);
    EXPECT_EQ(sink.commands.back().programId, 5);
}

// ---- queue, rain delay, faults, sensors ------------------------------------------------------------

TEST_F(ApiTest, QueueAndFaultCommands)
{
    login();
    EXPECT_EQ(admin("POST", "/api/v1/queue/pause").status, 202);
    EXPECT_EQ(admin("POST", "/api/v1/queue/resume").status, 202);
    EXPECT_EQ(admin("POST", "/api/v1/queue/skip").status, 202);
    EXPECT_EQ(admin("POST", "/api/v1/queue/stop", "{}").status, 202);
    EXPECT_EQ(admin("POST", "/api/v1/faults/clear").status, 202);
    ASSERT_EQ(sink.commands.size(), 5u);
    EXPECT_EQ(sink.commands[0].type, CommandType::Pause);
    EXPECT_EQ(sink.commands[1].type, CommandType::Resume);
    EXPECT_EQ(sink.commands[2].type, CommandType::Skip);
    EXPECT_EQ(sink.commands[3].type, CommandType::StopAll);
    EXPECT_EQ(sink.commands[4].type, CommandType::ClearFaults);
}

TEST_F(ApiTest, RainDelay)
{
    login();
    ApiResponse r = admin("PUT", "/api/v1/rain-delay", R"({"hours":24})");
    ASSERT_EQ(r.status, 202) << r.body;
    EXPECT_EQ(sink.commands.back().type, CommandType::SetRainDelay);
    EXPECT_EQ(sink.commands.back().until, clock.utc + 24 * 3600);
    r = admin("PUT", "/api/v1/rain-delay", R"({"until_utc":)" + std::to_string(clock.utc + 600) + "}");
    EXPECT_EQ(r.status, 202);
    EXPECT_EQ(sink.commands.back().until, clock.utc + 600);
    EXPECT_EQ(admin("PUT", "/api/v1/rain-delay", R"({"until_utc":)" + std::to_string(clock.utc - 1) + "}").status, 400);
    EXPECT_EQ(admin("PUT", "/api/v1/rain-delay", R"({"hours":721})").status, 400);
    EXPECT_EQ(admin("PUT", "/api/v1/rain-delay", R"({"hours":1,"until_utc":1})").status, 400);
    EXPECT_EQ(admin("DELETE", "/api/v1/rain-delay").status, 202);
    EXPECT_EQ(sink.commands.back().until, 0);

    publishState([](SystemState &s) { s.rainDelayUntil = s.utc + 100; });
    JsonPtr o = parseJson(get("/api/v1/rain-delay").body);
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(o.get(), "active")));
    EXPECT_EQ(cJSON_GetObjectItem(o.get(), "remaining_s")->valueint, 100);
}

TEST_F(ApiTest, RainDelayNeedsAValidClock)
{
    login();
    TimeService unset(systemTime, nullptr);
    AppServices services{bus, state, sink, config, unset};
    Api other(services, auth, platform, webStorage, clock);
    other.begin();
    ApiRequest req;
    req.method = "PUT";
    req.path = "/api/v1/rain-delay";
    req.body = R"({"hours":2})";
    req.contentType = "application/json";
    const std::string bearer = "Bearer " + token;
    req.authorization = bearer;
    const ApiResponse r = other.handle(req);
    EXPECT_EQ(r.status, 409);
}

TEST_F(ApiTest, FaultsAndSensorsReflectState)
{
    publishState([](SystemState &s) {
        s.activeFaults =
            (1u << static_cast<unsigned>(FaultCode::AcLost)) | (1u << static_cast<unsigned>(FaultCode::ShortCircuit));
        s.zones[2].fault = FaultCode::ShortCircuit;
        s.zones[2].lockedOut = true;
        s.sensors.rainEnabled = true;
        s.sensors.rainActive = true;
        s.sensors.valveCurrentMa = 230;
    });
    JsonPtr f = parseJson(get("/api/v1/faults").body);
    const cJSON *active = cJSON_GetObjectItem(f.get(), "active");
    ASSERT_EQ(cJSON_GetArraySize(active), 2);
    EXPECT_STREQ(cJSON_GetArrayItem(active, 0)->valuestring, "short_circuit");
    EXPECT_STREQ(cJSON_GetArrayItem(active, 1)->valuestring, "ac_lost");
    const cJSON *zones = cJSON_GetObjectItem(f.get(), "zones");
    ASSERT_EQ(cJSON_GetArraySize(zones), 1);
    EXPECT_EQ(cJSON_GetObjectItem(cJSON_GetArrayItem(zones, 0), "zone")->valueint, 3);

    JsonPtr s = parseJson(get("/api/v1/sensors").body);
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(s.get(), "rain"), "active")));
    EXPECT_EQ(cJSON_GetObjectItem(s.get(), "valve_current_ma")->valueint, 230);
}

// ---- config, backup, time ------------------------------------------------------------------------------

TEST_F(ApiTest, ConfigPutValidatesAndReloads)
{
    login();
    JsonPtr doc = parseJson(get("/api/v1/config").body);
    cJSON_ReplaceItemInObject(doc.get(), "device_name", cJSON_CreateString("garden"));
    ApiResponse r = admin("PUT", "/api/v1/config", printJson(doc.get()));
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(config.get().deviceName.view(), "garden");
    EXPECT_EQ(sink.count(CommandType::ReloadConfig), 1);

    r = admin("PUT", "/api/v1/config", R"({"schema":1,"zones":[{"max_run_s":0}]})");
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(field(r, "code"), "invalid_config");
    EXPECT_EQ(field(r, "path"), "zones[0].max_run_s");
    EXPECT_EQ(config.get().deviceName.view(), "garden");

    configStorage.failWrites = true;
    r = admin("PUT", "/api/v1/config", printJson(doc.get()));
    EXPECT_EQ(r.status, 500);
}

TEST_F(ApiTest, BackupRestoreRoundTripWithoutSecrets)
{
    login();
    Config c = config.get();
    c.deviceName.assign("backyard");
    ASSERT_TRUE(config.update(c).ok);
    const ApiResponse b = get("/api/v1/config/backup");
    ASSERT_EQ(b.status, 200);
    EXPECT_NE(header(b, "Content-Disposition").find("zoneflow-ab12-backup.json"), std::string::npos);
    EXPECT_EQ(b.body.find("ABCDEFGHJK"), std::string::npos); // setup code
    EXPECT_EQ(b.body.find("pbkdf2"), std::string::npos);     // password hash
    EXPECT_EQ(b.body.find(token), std::string::npos);        // session
    EXPECT_EQ(field(b, "format"), "zoneflow-backup");

    c.deviceName.assign("changed");
    ASSERT_TRUE(config.update(c).ok);
    ApiResponse r = admin("POST", "/api/v1/config/restore", b.body);
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(config.get().deviceName.view(), "backyard");

    // A bare configuration document is accepted too.
    c.deviceName.assign("bare");
    r = admin("POST", "/api/v1/config/restore", configToJson(c));
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(config.get().deviceName.view(), "bare");
    EXPECT_EQ(admin("POST", "/api/v1/config/restore", R"({"format":"other","config":{}})").status, 400);
}

TEST_F(ApiTest, TimeGetAndPut)
{
    login();
    JsonPtr t = parseJson(get("/api/v1/time").body);
    EXPECT_STREQ(cJSON_GetObjectItem(t.get(), "status")->valuestring, "synced");
    EXPECT_STREQ(cJSON_GetObjectItem(t.get(), "local")->valuestring, "2026-06-01T00:00:00");

    ApiResponse r = admin("PUT", "/api/v1/time", R"({"timezone":"CET-1CEST,M3.5.0,M10.5.0/3"})");
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(field(r, "local"), "2026-06-01T02:00:00");
    EXPECT_EQ(field(r, "utc_offset_s"), "7200");
    EXPECT_EQ(config.get().timezone.view(), "CET-1CEST,M3.5.0,M10.5.0/3");
    EXPECT_EQ(admin("PUT", "/api/v1/time", R"({"timezone":"not a zone"})").status, 400);
    EXPECT_EQ(admin("PUT", "/api/v1/time", R"({"utc":1000})").status, 400);
    r = admin("PUT", "/api/v1/time", R"({"utc":1800000000})");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(systemTime.t, 1800000000);
}

// ---- network, provisioning, system, firmware -------------------------------------------------------------

TEST_F(ApiTest, NetworkStatusNeverShowsTheSetupCode)
{
    const ApiResponse r = get("/api/v1/network");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(field(r, "hostname"), "zoneflow-ab12");
    EXPECT_EQ(r.body.find("ABCDEFGHJK"), std::string::npos);
    EXPECT_EQ(get("/api/v1/info").body.find("ABCDEFGHJK"), std::string::npos);
    EXPECT_EQ(get("/api/v1/provision").body.find("ABCDEFGHJK"), std::string::npos);
}

TEST_F(ApiTest, ReprovisionAndRebootAreDeferredActions)
{
    login();
    ApiResponse r = admin("POST", "/api/v1/network/reprovision", R"({"transport":"softap"})");
    EXPECT_EQ(r.status, 202);
    EXPECT_EQ(r.action, PostAction::Reprovision);
    EXPECT_EQ(r.transport, net::ProvTransport::SoftAp);
    r = admin("POST", "/api/v1/network/reprovision");
    EXPECT_EQ(r.transport, net::ProvTransport::Ble);
    EXPECT_EQ(admin("POST", "/api/v1/network/reprovision", R"({"transport":"usb"})").status, 400);
    r = admin("POST", "/api/v1/system/reboot");
    EXPECT_EQ(r.action, PostAction::Reboot);
    EXPECT_EQ(admin("POST", "/api/v1/system/factory-reset", "{}").status, 400);
    r = admin("POST", "/api/v1/system/factory-reset", R"({"confirm":"factory-reset"})");
    EXPECT_EQ(r.status, 202);
    EXPECT_EQ(r.action, PostAction::FactoryReset);
}

TEST_F(ApiTest, ProvisioningRoutesOnlyDuringSoftAp)
{
    ApiResponse r =
        call({.method = "POST", .path = "/api/v1/provision/wifi", .body = R"({"ssid":"home","password":"secret123"})"});
    EXPECT_EQ(r.status, 409);
    EXPECT_EQ(get("/api/v1/provision/networks").status, 409);

    platform.net.provisioning = net::ProvTransport::SoftAp;
    platform.net.state = net::NetState::Provisioning;
    JsonPtr nets = parseJson(call({.path = "/api/v1/provision/networks", .viaSetupAp = true}).body);
    EXPECT_EQ(cJSON_GetArraySize(nets.get()), 2);
    EXPECT_EQ(call({.method = "POST",
                    .path = "/api/v1/provision/wifi",
                    .body = R"({"ssid":"home","password":"short"})",
                    .viaSetupAp = true})
                  .status,
              400);
    r = call({.method = "POST",
              .path = "/api/v1/provision/wifi",
              .body = R"({"ssid":"home","password":"secret123","admin_password":"admin pass"})",
              .viaSetupAp = true});
    ASSERT_EQ(r.status, 202) << r.body;
    ASSERT_EQ(platform.provisioned.size(), 1u);
    EXPECT_EQ(platform.provisioned[0].first, "home");
    EXPECT_TRUE(auth.passwordSet());
    // An open network needs no passphrase.
    EXPECT_EQ(
        call({.method = "POST", .path = "/api/v1/provision/wifi", .body = R"({"ssid":"cafe"})", .viaSetupAp = true})
            .status,
        202);
}

// Security review F1. SoftAP provisioning does not end when the station
// connects: the provisioning manager stops about 30 s later, and until then
// the HTTP server answers on the home network too. A LAN device must not
// reach the provisioning routes in that window: it could set the admin
// password (when the owner left that field empty) or start WiFi scans.
TEST_F(ApiTest, ProvisioningRoutesOnlyFromTheSetupAccessPoint)
{
    platform.net.provisioning = net::ProvTransport::SoftAp;
    platform.net.state = net::NetState::Provisioning;
    ApiResponse r = call({.method = "POST",
                          .path = "/api/v1/provision/wifi",
                          .body = R"({"ssid":"evil","password":"secret123","admin_password":"attacker pw"})",
                          .peer = "192.168.1.66"});
    EXPECT_EQ(r.status, 403) << r.body;
    EXPECT_EQ(field(r, "code"), "setup_ap_only");
    EXPECT_FALSE(auth.passwordSet());
    EXPECT_TRUE(platform.provisioned.empty());
    EXPECT_EQ(call({.path = "/api/v1/provision/networks", .peer = "192.168.1.66"}).status, 403);

    r = call({.method = "POST",
              .path = "/api/v1/provision/wifi",
              .body = R"({"ssid":"home","password":"secret123","admin_password":"owner pass"})",
              .peer = "192.168.4.2",
              .viaSetupAp = true});
    EXPECT_EQ(r.status, 202) << r.body;
    EXPECT_TRUE(auth.passwordSet());
}

TEST_F(ApiTest, FirmwarePull)
{
    login();
    EXPECT_EQ(admin("POST", "/api/v1/firmware/pull", R"({"url":"http://example.com/fw.bin"})").status, 400);
    ApiResponse r = admin("POST", "/api/v1/firmware/pull",
                          R"({"url":"https://github.com/syntroniks/zoneflow/releases/download/v1/zoneflow-fw.bin"})");
    EXPECT_EQ(r.status, 202);
    EXPECT_FALSE(platform.pulledForce);
    platform.pullResult = ota::OtaError::ZonesActive;
    r = admin("POST", "/api/v1/firmware/pull", R"({"url":"https://x/y.bin","force":false})");
    EXPECT_EQ(r.status, 409);
    EXPECT_EQ(field(r, "code"), "zones_active");
    platform.pullResult = ota::OtaError::None;
    EXPECT_EQ(admin("POST", "/api/v1/firmware/pull", R"({"url":"https://x/y.bin","force":true})").status, 202);
    EXPECT_TRUE(platform.pulledForce);
}

TEST_F(ApiTest, StreamedRoutesAreAdmittedButNotHandledHere)
{
    login();
    ApiRequest req;
    req.method = "POST";
    req.path = "/api/v1/firmware/upload";
    req.contentType = "application/octet-stream";
    req.contentLength = 1500000;
    const std::string bearer = "Bearer " + token;
    req.authorization = bearer;
    const Admission a = api->admit(req);
    ASSERT_TRUE(a.ok);
    EXPECT_EQ(a.match.route->id, RouteId::FirmwareUpload);
    req.contentLength = 0x400001;
    EXPECT_EQ(api->admit(req).error.status, 413);
    req.contentLength = 100;
    req.contentType = "application/json";
    EXPECT_EQ(api->admit(req).error.status, 415);
    req.authorization = {};
    req.contentType = "application/octet-stream";
    EXPECT_EQ(api->admit(req).error.status, 401);
}

TEST_F(ApiTest, StatusCombinesCoreNetworkAndOta)
{
    publishState([](SystemState &s) {
        s.zones[0].state = ValveState::Open;
        s.zones[0].remainingSec = 42;
        s.queue.state = QueueState::Running;
    });
    JsonPtr o = parseJson(get("/api/v1/status").body);
    ASSERT_TRUE(o);
    const cJSON *zones = cJSON_GetObjectItem(o.get(), "zones");
    ASSERT_EQ(cJSON_GetArraySize(zones), 8);
    EXPECT_STREQ(cJSON_GetObjectItem(cJSON_GetArrayItem(zones, 0), "state")->valuestring, "open");
    EXPECT_EQ(cJSON_GetObjectItem(cJSON_GetArrayItem(zones, 0), "remaining_s")->valueint, 42);
    EXPECT_STREQ(cJSON_GetObjectItem(cJSON_GetObjectItem(o.get(), "queue"), "state")->valuestring, "running");
    EXPECT_STREQ(cJSON_GetObjectItem(cJSON_GetObjectItem(o.get(), "network"), "state")->valuestring, "connected");
    EXPECT_STREQ(cJSON_GetObjectItem(cJSON_GetObjectItem(o.get(), "ota"), "phase")->valuestring, "idle");
}

TEST_F(ApiTest, SecuritySettings)
{
    login();
    EXPECT_EQ(admin("PUT", "/api/v1/security", R"({"bogus":true})").status, 400);
    EXPECT_EQ(admin("PUT", "/api/v1/security", R"({"https":"yes"})").status, 400);
    const ApiResponse r = admin("PUT", "/api/v1/security", R"({"https":true})");
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(field(r, "restart_required"), "true");
    EXPECT_TRUE(api->settings().https);
}

TEST_F(ApiTest, PasswordChangeKeepsCallerSignedIn)
{
    login();
    const ApiResponse r =
        admin("POST", "/api/v1/auth/password", R"({"current_password":"admin pass","new_password":"better pass"})");
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(admin("POST", "/api/v1/queue/stop").status, 401); // old token gone
    JsonPtr o = parseJson(r.body);
    token = cJSON_GetObjectItem(o.get(), "token")->valuestring;
    EXPECT_EQ(admin("POST", "/api/v1/queue/stop").status, 202);
}

// ---- event stream payloads --------------------------------------------------------------------------

TEST(EventJson, ZoneChangedUsesOneBasedZone)
{
    Event e{};
    e.type = EventType::ZoneChanged;
    e.utc = 1800000000;
    e.zone = {4, ValveState::Open, StopReason::None};
    JsonPtr o(eventToJson(e));
    EXPECT_STREQ(cJSON_GetObjectItem(o.get(), "type")->valuestring, "zone_changed");
    EXPECT_EQ(cJSON_GetObjectItem(o.get(), "zone")->valueint, 5);
    EXPECT_STREQ(cJSON_GetObjectItem(o.get(), "state")->valuestring, "open");
}

TEST(EventJson, FaultWithoutZoneIsNull)
{
    Event e{};
    e.type = EventType::FaultRaised;
    e.fault = {FaultCode::AcLost, kNoZone};
    JsonPtr o(eventToJson(e));
    EXPECT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(o.get(), "zone")));
    EXPECT_STREQ(cJSON_GetObjectItem(o.get(), "code")->valuestring, "ac_lost");
}

TEST(EventJson, EveryEventTypeHasAName)
{
    for (unsigned t = 0; t < static_cast<unsigned>(EventType::Count); ++t) {
        Event e{};
        e.type = static_cast<EventType>(t);
        JsonPtr o(eventToJson(e));
        EXPECT_STRNE(cJSON_GetObjectItem(o.get(), "type")->valuestring, "?");
    }
}

TEST(Sse, MessageFraming)
{
    EXPECT_EQ(sseMessage("state", 7, R"({"a":1})"), "id: 7\nevent: state\ndata: {\"a\":1}\n\n");
}

// ---- command outcome (CommandRejected) -----------------------------------------------------

namespace {

Event rejection(CommandType type, ZoneIndex zone, uint16_t programId, RunError error)
{
    Event e{};
    e.type = EventType::CommandRejected;
    e.rejected = {type, zone, programId, error};
    return e;
}

} // namespace

TEST(CommandWatch, MatchesOnlyTheArmedCommand)
{
    EventBus bus;
    CommandWatch w;
    const EventBus::Handle h = w.attach(bus);
    bus.publish(rejection(CommandType::RunZone, 1, 0, RunError::LockedOut));
    EXPECT_FALSE(w.rejected().has_value()); // nothing armed
    w.arm(Command::runZone(1, 60));
    bus.publish(rejection(CommandType::RunZone, 2, 0, RunError::LockedOut));
    bus.publish(rejection(CommandType::RunProgram, 1, 0, RunError::AlreadyQueued));
    EXPECT_FALSE(w.rejected().has_value());
    bus.publish(rejection(CommandType::RunZone, 1, 0, RunError::ZoneResting));
    ASSERT_TRUE(w.rejected().has_value());
    EXPECT_EQ(*w.rejected(), RunError::ZoneResting);
    w.disarm();
    w.arm(Command::runProgram(7));
    EXPECT_FALSE(w.rejected().has_value()); // arm() clears the last verdict
    bus.publish(rejection(CommandType::RunProgram, kNoZone, 7, RunError::MasterLockedOut));
    EXPECT_EQ(*w.rejected(), RunError::MasterLockedOut);
    bus.unsubscribe(h);
}

TEST_F(ApiTest, RunRejectedByControllerAnswers409)
{
    login();
    platform.onSleep = [&] { bus.publish(rejection(CommandType::RunZone, 1, 0, RunError::LockedOut)); };
    const ApiResponse r = admin("POST", "/api/v1/zones/2/run", R"({"duration_s":60})");
    EXPECT_EQ(r.status, 409);
    EXPECT_EQ(field(r, "code"), "locked_out");
    EXPECT_LE(platform.sleptMs, Api::kConfirmPollMs);
}

TEST_F(ApiTest, RunConfirmedOnceTheControlLoopPublishes)
{
    login();
    platform.onSleep = [&] { publishState(); };
    const ApiResponse r = admin("POST", "/api/v1/zones/2/run", R"({"duration_s":60})");
    ASSERT_EQ(r.status, 202) << r.body;
    EXPECT_EQ(field(r, "confirmed"), "true");
    EXPECT_LT(platform.sleptMs, Api::kConfirmWaitMs);
}

TEST_F(ApiTest, RunUnconfirmedWhenTheControlLoopIsSilent)
{
    login();
    platform.onSleep = [&] { bus.publish(rejection(CommandType::RunZone, 5, 0, RunError::LockedOut)); }; // other zone
    const ApiResponse r = admin("POST", "/api/v1/zones/2/run", R"({"duration_s":60})");
    ASSERT_EQ(r.status, 202);
    EXPECT_EQ(field(r, "confirmed"), "false");
    EXPECT_EQ(platform.sleptMs, Api::kConfirmWaitMs);
}

TEST_F(ApiTest, ProgramRunRejectionMapsEveryRunError)
{
    login();
    ASSERT_EQ(admin("POST", "/api/v1/programs", R"({"id":3,"name":"p","durations_s":[60]})").status, 201);
    const std::pair<RunError, int> cases[] = {
        {RunError::AlreadyQueued, 409}, {RunError::QueueFull, 409},      {RunError::Inhibited, 409},
        {RunError::NothingToRun, 409},  {RunError::UnknownProgram, 404}, {RunError::MasterLockedOut, 409},
        {RunError::LockedOut, 409},     {RunError::ZoneResting, 409},    {RunError::InvalidZone, 404},
        {RunError::ZeroDuration, 400},
    };
    for (const auto &[err, status] : cases) {
        platform.onSleep = [&, err = err] { bus.publish(rejection(CommandType::RunProgram, kNoZone, 3, err)); };
        const ApiResponse r = admin("POST", "/api/v1/programs/3/run");
        EXPECT_EQ(r.status, status) << toString(err);
        EXPECT_EQ(field(r, "code"), toString(err));
    }
}

TEST_F(ApiTest, KnownBlockersAnsweredFromTheSnapshot)
{
    login();
    publishState([](SystemState &s) { s.zones[1].restRemainingSec = 1200; });
    ApiResponse r = admin("POST", "/api/v1/zones/2/run", R"({"duration_s":60})");
    EXPECT_EQ(r.status, 409);
    EXPECT_EQ(field(r, "code"), "zone_resting");
    EXPECT_EQ(header(r, "Retry-After"), "1200");
    publishState([](SystemState &s) { s.zones[1].lockedOut = true; });
    EXPECT_EQ(field(admin("POST", "/api/v1/zones/2/run", R"({"duration_s":60})"), "code"), "locked_out");
    publishState([](SystemState &s) {
        s.masterEnabled = true;
        s.masterLockedOut = true;
    });
    EXPECT_EQ(field(admin("POST", "/api/v1/zones/2/run", R"({"duration_s":60})"), "code"), "master_locked_out");
    ASSERT_EQ(admin("POST", "/api/v1/programs", R"({"id":3,"name":"p","durations_s":[60]})").status, 201);
    EXPECT_EQ(field(admin("POST", "/api/v1/programs/3/run"), "code"), "master_locked_out");
    EXPECT_EQ(sink.count(CommandType::RunZone) + sink.count(CommandType::RunProgram), 0);
}

// End to end with the real controller: the API posts into the real command
// queue, and "sleeping" runs the control loop.
TEST(ApiWithController, RejectionAndAcceptanceComeFromTheControlTask)
{
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    MemoryStorage storage;
    MemoryStorage webStorage;
    ConfigStore config{storage};
    CommandQueue queue;
    FakeSystemTime systemTime;
    TimeService time{systemTime, nullptr};
    SeqRandom rng;
    AuthService auth{webStorage, rng, 2};
    FakePlatform platform;
    platform.clock = &clock;

    Config c = Config::defaults();
    c.master.enabled = false;
    c.driver.interZoneDelayMs = 0;
    c.schedule.programCount = 1;
    c.schedule.programs[0] = makeProgram(4, {}, {{0, 600}, {1, 300}});
    ASSERT_TRUE(config.update(c).ok);
    ASSERT_TRUE(time.setManual(clock.utc));

    Controller ctrl(ControllerDeps{clock, out, sensors, oe, bus, state, config, &storage});
    ctrl.begin();
    ctrl.setTimeStatus(TimeStatus::Synced);
    for (int i = 0; i < 25; ++i) { // AC debounce
        clock.advanceMs(50);
        ctrl.tick();
    }
    // Every 50 ms of "sleep" is one control period: drain, then tick.
    uint32_t sinceTick = 0;
    platform.onSleep = [&] {
        sinceTick += Api::kConfirmPollMs;
        if (sinceTick < 50) {
            return;
        }
        sinceTick = 0;
        Command cmd;
        while (queue.pop(cmd)) {
            ctrl.handle(cmd);
        }
        ctrl.tick();
    };

    AppServices services{bus, state, queue, config, time};
    Api api(services, auth, platform, webStorage, clock);
    api.begin();
    ASSERT_EQ(auth.setInitialPassword("admin pass"), AuthError::Ok);
    const std::string bearer = "Bearer " + auth.login(1, "admin pass", 0).session.token;

    ApiRequest req;
    req.method = "POST";
    req.path = "/api/v1/programs/4/run";
    req.authorization = bearer;
    ApiResponse r = api.handle(req);
    ASSERT_EQ(r.status, 202) << r.body;
    JsonPtr o = parseJson(r.body);
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(o.get(), "confirmed")));

    // The same program again: the scheduler refuses it.
    r = api.handle(req);
    EXPECT_EQ(r.status, 409) << r.body;
    o = parseJson(r.body);
    EXPECT_STREQ(cJSON_GetObjectItem(cJSON_GetObjectItem(o.get(), "error"), "code")->valuestring, "already_queued");
}

// ---- new core state in the API -----------------------------------------------------------------

TEST_F(ApiTest, StatusShowsRestLockoutTripAndPauseTimeout)
{
    publishState([](SystemState &s) {
        s.zones[0].restRemainingSec = 900;
        s.masterEnabled = true;
        s.masterLockedOut = true;
        s.masterFaultCount = 3;
        s.inhibitFlags = kInhibitMasterLockout;
        s.activeFaults = (1u << static_cast<unsigned>(FaultCode::MasterLockedOut)) |
                         (1u << static_cast<unsigned>(FaultCode::CurrentSenseUnavailable)) |
                         (1u << static_cast<unsigned>(FaultCode::OverCurrentTrip));
    });
    JsonPtr o = parseJson(get("/api/v1/status").body);
    const cJSON *z = cJSON_GetArrayItem(cJSON_GetObjectItem(o.get(), "zones"), 0);
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(z, "resting")));
    EXPECT_EQ(cJSON_GetObjectItem(z, "rest_remaining_s")->valueint, 900);
    const cJSON *m = cJSON_GetObjectItem(o.get(), "master");
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(m, "locked_out")));
    EXPECT_EQ(cJSON_GetObjectItem(m, "fault_count")->valueint, 3);
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(cJSON_GetObjectItem(o.get(), "inhibit"), "master_lockout")));
    const cJSON *sensorsJson = cJSON_GetObjectItem(o.get(), "sensors");
    EXPECT_TRUE(cJSON_IsFalse(cJSON_GetObjectItem(sensorsJson, "current_sense_available")));
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(sensorsJson, "over_current_tripped")));
    EXPECT_EQ(cJSON_GetObjectItem(cJSON_GetObjectItem(o.get(), "queue"), "pause_timeout_min")->valueint, 120);
    const cJSON *active = cJSON_GetObjectItem(cJSON_GetObjectItem(o.get(), "faults"), "active");
    std::vector<std::string> names;
    const cJSON *it = nullptr;
    cJSON_ArrayForEach(it, active) { names.emplace_back(it->valuestring); }
    EXPECT_EQ(names, (std::vector<std::string>{"master_locked_out", "current_sense_unavailable", "over_current_trip"}));
    // Same fields on the narrower routes.
    JsonPtr sensorsRoute = parseJson(get("/api/v1/sensors").body);
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(sensorsRoute.get(), "over_current_tripped")));
    JsonPtr zone = parseJson(get("/api/v1/zones/1").body);
    EXPECT_EQ(cJSON_GetObjectItem(zone.get(), "rest_remaining_s")->valueint, 900);
}

TEST_F(ApiTest, NewConfigFieldsRoundTripAndValidate)
{
    login();
    JsonPtr doc = parseJson(get("/api/v1/config").body);
    cJSON *driver = cJSON_GetObjectItem(doc.get(), "driver");
    cJSON_ReplaceItemInObject(driver, "min_rest_s", cJSON_CreateNumber(600));
    cJSON_ReplaceItemInObject(driver, "zero_cross_switching", cJSON_CreateTrue());
    cJSON_ReplaceItemInObject(cJSON_GetObjectItem(doc.get(), "master"), "fault_lockout_count", cJSON_CreateNumber(5));
    cJSON_ReplaceItemInObject(cJSON_GetObjectItem(doc.get(), "schedule"), "pause_timeout_min", cJSON_CreateNumber(30));
    ASSERT_EQ(admin("PUT", "/api/v1/config", printJson(doc.get())).status, 200);
    const Config c = config.get();
    EXPECT_EQ(c.driver.minRestSec, 600u);
    EXPECT_TRUE(c.driver.zeroCrossSwitching);
    EXPECT_EQ(c.master.faultLockoutCount, 5);
    EXPECT_EQ(c.schedule.pauseTimeoutMin, 30);
    EXPECT_EQ(field(get("/api/v1/queue"), "pause_timeout_min"), "30");

    cJSON_ReplaceItemInObject(driver, "min_rest_s", cJSON_CreateNumber(10));
    const ApiResponse r = admin("PUT", "/api/v1/config", printJson(doc.get()));
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(field(r, "path"), "driver.min_rest_s");
}

TEST_F(ApiTest, RestoreOfANewerSchemaIsRefusedWithTheCorePath)
{
    login();
    Config c = config.get();
    c.deviceName.assign("before");
    ASSERT_TRUE(config.update(c).ok);
    JsonPtr doc = parseJson(get("/api/v1/config/backup").body);
    cJSON *cfg = cJSON_GetObjectItem(doc.get(), "config");
    cJSON_ReplaceItemInObject(cfg, "schema", cJSON_CreateNumber(kConfigSchemaVersion + 1));
    cJSON_ReplaceItemInObject(cfg, "device_name", cJSON_CreateString("after"));
    const ApiResponse r = admin("POST", "/api/v1/config/restore", printJson(doc.get()));
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(field(r, "code"), "unsupported_schema");
    EXPECT_EQ(field(r, "path"), "schema");
    EXPECT_NE(field(r, "message").find("newer than this firmware"), std::string::npos);
    EXPECT_EQ(config.get().deviceName.view(), "before");
}

TEST(EventJson, NewEventTypesCarryTheirData)
{
    Event e{};
    e.type = EventType::CommandRejected;
    e.rejected = {CommandType::RunZone, 2, 0, RunError::ZoneResting};
    JsonPtr o(eventToJson(e));
    EXPECT_STREQ(cJSON_GetObjectItem(o.get(), "type")->valuestring, "command_rejected");
    EXPECT_STREQ(cJSON_GetObjectItem(o.get(), "command")->valuestring, "run_zone");
    EXPECT_EQ(cJSON_GetObjectItem(o.get(), "zone")->valueint, 3);
    EXPECT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(o.get(), "program_id")));
    EXPECT_STREQ(cJSON_GetObjectItem(o.get(), "error")->valuestring, "zone_resting");

    Event d{};
    d.type = EventType::RunDropped;
    d.dropped = {0, 9, RunSource::Schedule, StartError::Resting};
    JsonPtr p(eventToJson(d));
    EXPECT_STREQ(cJSON_GetObjectItem(p.get(), "type")->valuestring, "run_dropped");
    EXPECT_EQ(cJSON_GetObjectItem(p.get(), "zone")->valueint, 1);
    EXPECT_EQ(cJSON_GetObjectItem(p.get(), "program_id")->valueint, 9);
    EXPECT_STREQ(cJSON_GetObjectItem(p.get(), "source")->valuestring, "schedule");
    EXPECT_STREQ(cJSON_GetObjectItem(p.get(), "reason")->valuestring, "resting");

    Event x{};
    x.type = EventType::PauseExpired;
    JsonPtr q(eventToJson(x));
    EXPECT_STREQ(cJSON_GetObjectItem(q.get(), "type")->valuestring, "pause_expired");
}
