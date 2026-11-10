// SPDX-License-Identifier: Apache-2.0
// The answers the firmware sends match the response schemas in
// docs/firmware/openapi.json: every `required` field is present and every
// field has the documented type, enum or const. The web UI's TypeScript
// types are generated from those schemas, so this keeps them truthful.
//
// The checker covers the subset of JSON Schema the document uses: $ref,
// allOf, oneOf (with a discriminator), type (one or a list), enum, const,
// properties, required and items.
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

#include "cJSON.h"
#include "fakes.hpp"
#include "web_fakes.hpp"
#include "zf/app/controller.hpp"
#include "zf_web/api.hpp"
#include "zf_web/json_map.hpp"

using namespace zf;
using namespace zf::test;
using namespace zf::web;

namespace {

struct Doc {
    cJSON *root = nullptr;
    Doc()
    {
        std::ifstream in(ZF_OPENAPI_PATH);
        std::stringstream ss;
        ss << in.rdbuf();
        root = cJSON_Parse(ss.str().c_str());
    }
    ~Doc() { cJSON_Delete(root); }
    Doc(const Doc &) = delete;
    Doc &operator=(const Doc &) = delete;
};

const Doc &doc()
{
    static const Doc d;
    return d;
}

const cJSON *resolve(const cJSON *schema)
{
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(schema, "$ref");
    while (cJSON_IsString(r)) {
        const std::string name = std::string(r->valuestring).substr(std::string("#/components/schemas/").size());
        schema = cJSON_GetObjectItemCaseSensitive(
            cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(doc().root, "components"), "schemas"),
            name.c_str());
        r = cJSON_GetObjectItemCaseSensitive(schema, "$ref");
    }
    return schema;
}

bool typeMatches(const char *t, const cJSON *v)
{
    const std::string_view s(t);
    if (s == "object") {
        return cJSON_IsObject(v);
    }
    if (s == "array") {
        return cJSON_IsArray(v);
    }
    if (s == "string") {
        return cJSON_IsString(v);
    }
    if (s == "boolean") {
        return cJSON_IsBool(v);
    }
    if (s == "null") {
        return cJSON_IsNull(v);
    }
    if (s == "number") {
        return cJSON_IsNumber(v);
    }
    if (s == "integer") {
        return cJSON_IsNumber(v) && std::floor(v->valuedouble) == v->valuedouble;
    }
    return false;
}

void check(const cJSON *schema, const cJSON *v, const std::string &path, std::vector<std::string> &errors)
{
    schema = resolve(schema);
    ASSERT_NE(schema, nullptr) << path;
    if (const cJSON *all = cJSON_GetObjectItemCaseSensitive(schema, "allOf")) {
        const cJSON *s = nullptr;
        cJSON_ArrayForEach(s, all) { check(s, v, path, errors); }
    }
    if (const cJSON *one = cJSON_GetObjectItemCaseSensitive(schema, "oneOf")) {
        int matches = 0;
        std::vector<std::string> firstErrors;
        const cJSON *s = nullptr;
        cJSON_ArrayForEach(s, one)
        {
            std::vector<std::string> e;
            check(s, v, path, e);
            if (e.empty()) {
                ++matches;
            } else if (firstErrors.empty() || e.size() < firstErrors.size()) {
                firstErrors = e;
            }
        }
        if (matches != 1) {
            errors.push_back(path + ": matches " + std::to_string(matches) + " of oneOf" +
                             (firstErrors.empty() ? "" : " (closest: " + firstErrors.front() + ")"));
        }
    }
    if (const cJSON *t = cJSON_GetObjectItemCaseSensitive(schema, "type")) {
        bool ok = false;
        if (cJSON_IsString(t)) {
            ok = typeMatches(t->valuestring, v);
        } else {
            const cJSON *x = nullptr;
            cJSON_ArrayForEach(x, t) { ok = ok || typeMatches(x->valuestring, v); }
        }
        if (!ok) {
            char *text = cJSON_PrintUnformatted(v);
            errors.push_back(path + ": wrong type: " + (text != nullptr ? text : "?"));
            cJSON_free(text);
            return;
        }
    }
    if (const cJSON *c = cJSON_GetObjectItemCaseSensitive(schema, "const")) {
        if (!cJSON_Compare(c, v, true)) {
            errors.push_back(path + ": not the const value");
        }
    }
    if (const cJSON *e = cJSON_GetObjectItemCaseSensitive(schema, "enum"); e != nullptr && !cJSON_IsNull(v)) {
        bool found = false;
        const cJSON *x = nullptr;
        cJSON_ArrayForEach(x, e) { found = found || cJSON_Compare(x, v, true); }
        if (!found) {
            errors.push_back(path + ": " + (cJSON_IsString(v) ? v->valuestring : std::string("value")) +
                             " not in enum");
        }
    }
    if (cJSON_IsObject(v)) {
        if (const cJSON *req = cJSON_GetObjectItemCaseSensitive(schema, "required")) {
            const cJSON *k = nullptr;
            cJSON_ArrayForEach(k, req)
            {
                if (!cJSON_HasObjectItem(v, k->valuestring)) {
                    errors.push_back(path + "." + k->valuestring + ": required but missing");
                }
            }
        }
        if (const cJSON *props = cJSON_GetObjectItemCaseSensitive(schema, "properties")) {
            const cJSON *p = nullptr;
            cJSON_ArrayForEach(p, props)
            {
                if (const cJSON *x = cJSON_GetObjectItemCaseSensitive(v, p->string)) {
                    check(p, x, path + "." + p->string, errors);
                }
            }
        }
    }
    if (cJSON_IsArray(v)) {
        if (const cJSON *items = cJSON_GetObjectItemCaseSensitive(schema, "items")) {
            int i = 0;
            const cJSON *x = nullptr;
            cJSON_ArrayForEach(x, v) { check(items, x, path + "[" + std::to_string(i++) + "]", errors); }
        }
    }
}

const cJSON *responseSchema(const std::string &route, const char *method = "get", const char *status = "200")
{
    const cJSON *op = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(doc().root, "paths"), route.c_str()), method);
    const cJSON *resp = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(op, "responses"), status);
    return cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(resp, "content"), "application/json"),
        "schema");
}

// The API with a real controller that has run, faulted and paused, a
// configuration with programs, and a history log with every record type.
class ApiSchemaTest : public ::testing::Test {
protected:
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    MemoryStorage configStorage;
    MemoryStorage webStorage;
    MemoryStorage historyStorage;
    ConfigStore config{configStorage};
    RecordingSink sink;
    FakeSystemTime systemTime;
    TimeService time{systemTime, nullptr};
    SeqRandom rng;
    AuthService auth{webStorage, rng, 2};
    FakePlatform platform;
    HistoryLog history{historyStorage};
    std::unique_ptr<Controller> ctrl;
    std::unique_ptr<Api> api;
    std::string token;

    void SetUp() override
    {
        Config c = Config::defaults();
        c.zoneCount = 4;
        c.flow.enabled = true;
        c.master.enabled = true;
        c.zones[0].name.assign("Lawn");
        Program p;
        p.id = 3;
        p.name.assign("Evening");
        p.startCount = 1;
        p.startMinutes[0] = 1200;
        p.durationSec[0] = 120;
        p.durationSec[1] = 60;
        c.schedule.programs[0] = p;
        Program q = p;
        q.id = 4;
        q.days.type = DayRuleType::Interval;
        q.days.intervalDays = 3;
        c.schedule.programs[1] = q;
        c.schedule.programCount = 2;
        ASSERT_TRUE(config.update(c).ok);
        ASSERT_TRUE(time.setManual(clock.utc));

        ctrl =
            std::make_unique<Controller>(ControllerDeps{clock, out, sensors, oe, bus, state, config, &configStorage});
        ctrl->begin();
        ctrl->setTimeStatus(TimeStatus::Synced);
        history.load();
        bus.subscribe(
            [](const Event &e, void *ctx) {
                if (const auto r = historyFromEvent(e)) {
                    static_cast<HistoryLog *>(ctx)->append(*r, e.monoMs);
                }
            },
            &history);
        sensors.sample.valveCurrentMa = 300;
        run(1000);
        ASSERT_TRUE(ctrl->handle(Command::runZone(0, 30)));
        run(40000); // a run that completes (and teaches zone 1 its current)
        ASSERT_TRUE(ctrl->handle(Command::runProgram(3)));
        run(5000);
        ASSERT_TRUE(ctrl->handle(Command::simple(CommandType::Pause))); // a paused queue
        ASSERT_TRUE(ctrl->handle(Command::rainDelay(clock.utc + 3600)));
        run(1000);
        HistoryRecord r;
        r.utc = clock.utc;
        r.kind = HistoryKind::Boot;
        history.append(r, 0);
        r.kind = HistoryKind::Ota;
        r.a = static_cast<uint8_t>(HistoryOtaStage::Failed);
        r.b = 1;
        r.c = static_cast<uint16_t>(ota::OtaError::BadImage);
        history.append(r, 0);
        r = {};
        r.kind = HistoryKind::ProgramMissed;
        r.programId = 3;
        history.append(r, 0);
        r.kind = HistoryKind::RunDropped;
        r.zone = 1;
        r.b = static_cast<uint8_t>(StartError::LockedOut);
        history.append(r, 0);
        r = {};
        r.kind = HistoryKind::ProgramSkipped;
        r.programId = 3;
        history.append(r, 0);

        AppServices services{bus, state, sink, config, time};
        platform.clock = &clock;
        platform.historyLog = &history;
        api = std::make_unique<Api>(services, auth, platform, webStorage, clock);
        api->begin();
    }

    void run(MonoMs ms)
    {
        for (MonoMs t = 0; t < ms; t += 50) {
            sensors.sample.flowPulses += out.anyOn() ? 20 : 0;
            clock.advanceMs(50);
            ctrl->tick();
        }
    }

    ApiResponse get(const std::string &path, const std::string &authorization = "")
    {
        ApiRequest r;
        r.method = "GET";
        const std::size_t q = path.find('?');
        r.path = std::string_view(path).substr(0, q);
        r.query = q == std::string::npos ? std::string_view{} : std::string_view(path).substr(q + 1);
        r.host = "192.168.1.30";
        r.peer = "192.168.1.50";
        r.authorization = authorization;
        return api->handle(r);
    }

    void expectMatches(const std::string &route, const std::string &path, const std::string &authorization = "")
    {
        const ApiResponse r = get(path, authorization);
        ASSERT_EQ(r.status, 200) << path << ": " << r.body;
        const cJSON *schema = responseSchema(route);
        ASSERT_NE(schema, nullptr) << route << " has no 200 JSON schema";
        JsonPtr body = parseJson(r.body);
        ASSERT_TRUE(body) << path;
        std::vector<std::string> errors;
        check(schema, body.get(), path, errors);
        EXPECT_TRUE(errors.empty()) << path << ":\n  " << [&] {
            std::string s;
            for (const std::string &e : errors) {
                s += e + "\n  ";
            }
            return s;
        }();
    }
};

} // namespace

TEST_F(ApiSchemaTest, EveryReadAnswerMatchesItsDocumentedSchema)
{
    ASSERT_EQ(state.snapshot().queue.state, QueueState::Paused) << "fixture: a paused queue";
    for (const char *route : {"/auth", "/security", "/info", "/status", "/zones", "/programs", "/queue", "/rain-delay",
                              "/sensors", "/faults", "/config", "/config/backup", "/time", "/network", "/firmware",
                              "/mqtt", "/provision", "/history"}) {
        SCOPED_TRACE(route);
        expectMatches(route, std::string("/api/v1") + route);
    }
    expectMatches("/zones/{zone}", "/api/v1/zones/1");
    expectMatches("/programs/{id}", "/api/v1/programs/4");
    expectMatches("/history", "/api/v1/history?limit=100");
}

TEST_F(ApiSchemaTest, TheEventStreamStateMessageIsAStatus)
{
    JsonPtr body(stateToJson(state.snapshot(), config.get()));
    std::vector<std::string> errors;
    // The stream merges network and OTA state into the snapshot (web.cpp).
    cJSON_AddItemToObject(body.get(), "network", netStatusToJson(platform.network()));
    cJSON_AddItemToObject(body.get(), "ota", otaStatusToJson(platform.otaStatus()));
    check(responseSchema("/status"), body.get(), "state", errors);
    EXPECT_TRUE(errors.empty()) << (errors.empty() ? "" : errors.front());
}

TEST_F(ApiSchemaTest, TheCheckerCatchesAMissingRequiredField)
{
    JsonPtr body = parseJson(get("/api/v1/queue").body);
    cJSON_DeleteItemFromObject(body.get(), "paused_s");
    std::vector<std::string> errors;
    check(responseSchema("/queue"), body.get(), "queue", errors);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("paused_s"), std::string::npos);

    JsonPtr h = parseJson(get("/api/v1/history").body);
    cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItem(h.get(), "records"), 0);
    cJSON_ReplaceItemInObject(first, "type", cJSON_CreateString("no_such_type"));
    errors.clear();
    check(responseSchema("/history"), h.get(), "history", errors);
    EXPECT_FALSE(errors.empty()) << "unknown record type";
}
