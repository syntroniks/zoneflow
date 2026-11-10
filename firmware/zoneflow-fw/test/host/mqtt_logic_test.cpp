// SPDX-License-Identifier: Apache-2.0
// zf_mqtt logic: topics, state payloads, command parsing and validation,
// retained-command rejection, rate limiting, reconnect backoff and the
// coalesced configuration edits.
#include <gtest/gtest.h>

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
    const cJSON *operator[](const char *k) const { return cJSON_GetObjectItemCaseSensitive(root, k); }
};

std::string str(const cJSON *o) { return cJSON_IsString(o) ? o->valuestring : std::string("<not a string>"); }

Config testConfig()
{
    Config c = Config::defaults();
    c.zoneCount = 4;
    c.zones[2].enabled = false;
    c.zones[1].defaultRunSec = 300;
    c.zones[0].name.assign("Lawn");
    c.schedule.programs[0] = makeProgram(7, {360}, {{0, 600}});
    c.schedule.programs[0].name.assign("Morning");
    c.schedule.programCount = 1;
    c.mqtt.enabled = true;
    c.mqtt.host.assign("broker");
    return c;
}

constexpr UtcSeconds kNow = 1'790'000'000;

struct Fixture {
    Config cfg = testConfig();
    Topics t{"zoneflow/a1b2c3d4e5f6"};
    ParsedCommand cmd(const std::string &suffix, std::string_view payload, bool retained = false,
                      bool timeValid = true) const
    {
        return parseCommand(t, t.command(suffix), payload, retained, cfg, kNow, timeValid);
    }
};

} // namespace

// ---- identity and topics -----------------------------------------------------------------------

TEST(MqttTopics, DeviceIdAndRoot)
{
    EXPECT_EQ(deviceIdFromMac("A1:B2:c3:d4:e5:F6"), "a1b2c3d4e5f6");
    EXPECT_EQ(deviceIdFromMac("a1b2c3d4e5f6"), "a1b2c3d4e5f6");
    EXPECT_EQ(deviceIdFromMac("a1:b2:c3"), "");
    EXPECT_EQ(deviceIdFromMac("zz:b2:c3:d4:e5:f6"), "");

    MqttConfig m;
    EXPECT_EQ(rootTopic(m, "a1b2c3d4e5f6"), "zoneflow/a1b2c3d4e5f6");
    EXPECT_EQ(clientId(m, "a1b2c3d4e5f6"), "zoneflow-a1b2c3d4e5f6");
    m.baseTopic.assign("garden/ctl");
    m.clientId.assign("garden");
    EXPECT_EQ(rootTopic(m, "a1b2c3d4e5f6"), "garden/ctl");
    EXPECT_EQ(clientId(m, "a1b2c3d4e5f6"), "garden");
}

TEST(MqttTopics, StableTree)
{
    const Topics t("zoneflow/a1b2c3d4e5f6");
    EXPECT_EQ(t.availability(), "zoneflow/a1b2c3d4e5f6/status");
    EXPECT_EQ(t.device(), "zoneflow/a1b2c3d4e5f6/device");
    EXPECT_EQ(t.zone(3), "zoneflow/a1b2c3d4e5f6/zone/3");
    EXPECT_EQ(t.program(12), "zoneflow/a1b2c3d4e5f6/program/12");
    EXPECT_EQ(t.queue(), "zoneflow/a1b2c3d4e5f6/queue");
    EXPECT_EQ(t.sensors(), "zoneflow/a1b2c3d4e5f6/sensors");
    EXPECT_EQ(t.flow(), "zoneflow/a1b2c3d4e5f6/flow");
    EXPECT_EQ(t.faults(), "zoneflow/a1b2c3d4e5f6/faults");
    EXPECT_EQ(t.rainDelay(), "zoneflow/a1b2c3d4e5f6/rain_delay");
    EXPECT_EQ(t.update(), "zoneflow/a1b2c3d4e5f6/update");
    EXPECT_EQ(t.event(), "zoneflow/a1b2c3d4e5f6/event");
    EXPECT_EQ(t.result(), "zoneflow/a1b2c3d4e5f6/result");
    EXPECT_EQ(t.commandFilter(), "zoneflow/a1b2c3d4e5f6/cmd/#");
    EXPECT_EQ(Topics::discovery("homeassistant", "a1b2c3d4e5f6"), "homeassistant/device/zoneflow_a1b2c3d4e5f6/config");
    EXPECT_EQ(Topics::haStatus("homeassistant"), "homeassistant/status");
}

// ---- payloads ----------------------------------------------------------------------------------

TEST(MqttPayload, Zone)
{
    ZoneConfig zc;
    zc.name.assign("Lawn");
    zc.defaultRunSec = 90;
    ZoneStatus z;
    z.state = ValveState::Waiting;
    z.remainingSec = 80;
    z.fault = FaultCode::ZoneOpenCircuit;
    QueueStatus q;
    q.state = QueueState::Running;
    q.shownCount = 1;
    q.items[0].zone = 0;
    Json j(zonePayload(0, z, zc, q));
    ASSERT_NE(j.root, nullptr);
    EXPECT_EQ(j["zone"]->valueint, 1);
    EXPECT_EQ(str(j["name"]), "Lawn");
    EXPECT_EQ(str(j["state"]), "waiting");
    EXPECT_EQ(str(j["valve"]), "opening");
    EXPECT_EQ(str(j["switch"]), "ON");
    EXPECT_TRUE(cJSON_IsTrue(j["queued"]));
    EXPECT_EQ(j["remaining_s"]->valueint, 80);
    EXPECT_EQ(j["default_run_s"]->valueint, 90);
    EXPECT_DOUBLE_EQ(j["default_run_min"]->valuedouble, 1.5);
    EXPECT_EQ(str(j["fault"]), "zone_open_circuit");
    EXPECT_TRUE(cJSON_IsTrue(j["fault_active"]));
    EXPECT_STREQ(haValveState(ValveState::Off), "closed");
    EXPECT_STREQ(haValveState(ValveState::Open), "open");
}

TEST(MqttPayload, SensorsFlowFaultsRainDelay)
{
    SystemState s;
    s.zoneCount = 2;
    s.utc = kNow;
    s.sensors.rainEnabled = true;
    s.sensors.rainActive = true;
    s.sensors.flowEnabled = true;
    s.sensors.flowLpm = 12.3456f;
    s.sensors.flowTotalLitres = 100.0f;
    s.sensors.valveCurrentMa = -1;
    s.activeFaults = 1u << static_cast<unsigned>(FaultCode::CurrentSenseUnavailable);
    s.zones[1].lockedOut = true;
    s.zones[1].fault = FaultCode::ShortCircuit;
    s.rainDelayUntil = kNow + 3601;

    Json sens(sensorsPayload(s));
    EXPECT_TRUE(cJSON_IsTrue(cJSON_GetObjectItem(sens["rain"], "active")));
    EXPECT_TRUE(cJSON_IsNull(sens["valve_current_ma"])) << "no reading is null, shown as unknown";
    EXPECT_TRUE(cJSON_IsFalse(sens["current_sense_available"]));

    Json flow(flowPayload(s));
    EXPECT_DOUBLE_EQ(flow["rate_lpm"]->valuedouble, 12.35);
    EXPECT_DOUBLE_EQ(flow["total_l"]->valuedouble, 100.0);

    Json faults(faultsPayload(s));
    EXPECT_TRUE(cJSON_IsTrue(faults["any"]));
    EXPECT_EQ(cJSON_GetArraySize(faults["active"]), 1);
    EXPECT_EQ(cJSON_GetObjectItem(cJSON_GetArrayItem(faults["zones"], 0), "zone")->valueint, 2);

    Json rd(rainDelayPayload(s));
    EXPECT_TRUE(cJSON_IsTrue(rd["active"]));
    EXPECT_EQ(rd["remaining_s"]->valueint, 3601);
    EXPECT_EQ(rd["hours"]->valueint, 2) << "rounded up";
    s.rainDelayUntil = 0;
    Json none(rainDelayPayload(s));
    EXPECT_TRUE(cJSON_IsFalse(none["active"]));
    EXPECT_TRUE(cJSON_IsNull(none["until_utc"]));
    EXPECT_EQ(none["hours"]->valueint, 0);
}

TEST(MqttPayload, UpdateEntityState)
{
    ota::OtaStatus idle;
    Json a(updatePayload("1.2.3", idle));
    EXPECT_EQ(str(a["installed_version"]), "1.2.3");
    EXPECT_EQ(str(a["latest_version"]), "1.2.3");
    EXPECT_TRUE(cJSON_IsFalse(a["in_progress"]));
    EXPECT_TRUE(cJSON_IsNull(a["update_percentage"]));

    ota::OtaStatus run;
    run.phase = ota::OtaPhase::Receiving;
    run.receivedBytes = 500;
    run.totalBytes = 2000;
    run.newVersion.assign("1.3.0");
    Json b(updatePayload("1.2.3", run));
    EXPECT_EQ(str(b["latest_version"]), "1.3.0");
    EXPECT_TRUE(cJSON_IsTrue(b["in_progress"]));
    EXPECT_EQ(b["update_percentage"]->valueint, 25);
}

TEST(MqttPayload, ProgramDeviceQueueEvent)
{
    Program p = makeProgram(7, {360}, {{0, 600}});
    p.name.assign("Morning");
    QueueStatus q;
    q.state = QueueState::Running;
    q.length = 2;
    q.shownCount = 2;
    q.items[0] = {1, 0, 600, RunSource::ManualProgram, 7};
    q.items[1] = {2, 1, 300, RunSource::ManualZone, 0};
    Json prog(programPayload(p, q));
    EXPECT_EQ(prog["id"]->valueint, 7);
    EXPECT_TRUE(cJSON_IsTrue(prog["running"]));

    Json queue(queuePayload(q, 120));
    EXPECT_EQ(str(queue["state"]), "running");
    const cJSON *second = cJSON_GetArrayItem(queue["items"], 1);
    EXPECT_EQ(cJSON_GetObjectItem(second, "zone")->valueint, 2);
    EXPECT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(second, "program_id")));

    SystemState s;
    s.masterEnabled = true;
    DeviceInfo d{"1.2.3", "zoneflow-e5f6", "192.168.1.30", -61, true};
    Json dev(devicePayload(s, Config::defaults(), d));
    EXPECT_EQ(dev["rssi"]->valueint, -61);
    EXPECT_EQ(str(dev["firmware"]), "1.2.3");
    d.wifiConnected = false;
    Json dev2(devicePayload(s, Config::defaults(), d));
    EXPECT_TRUE(cJSON_IsNull(dev2["rssi"]));

    Event e{};
    e.type = EventType::CommandRejected;
    e.rejected = {CommandType::RunZone, 2, 0, RunError::LockedOut};
    Json ev(eventPayload(e));
    EXPECT_EQ(str(ev["type"]), "command_rejected");
    EXPECT_EQ(ev["zone"]->valueint, 3);
    EXPECT_EQ(str(ev["error"]), "locked_out");
}

// ---- commands ---------------------------------------------------------------------------------

TEST(MqttCommand, ZoneRunWithDuration)
{
    Fixture f;
    for (const char *payload : {"600", " 600 ", R"({"duration_s":600})"}) {
        const ParsedCommand c = f.cmd("zone/2/run", payload);
        EXPECT_EQ(c.outcome, Outcome::Queued) << payload << " " << c.error;
        ASSERT_EQ(c.action, Action::Post);
        EXPECT_EQ(c.command.type, CommandType::RunZone);
        EXPECT_EQ(c.command.zone, 1);
        EXPECT_EQ(c.command.durationSec, 600u);
    }
    const ParsedCommand def = f.cmd("zone/2/set", "OPEN");
    EXPECT_EQ(def.command.durationSec, 300u) << "OPEN runs the zone's default_run_s";
}

TEST(MqttCommand, EmptyMessagesAreNeverCommands)
{
    // Clearing a retained command (an empty retained publish) reaches every
    // live subscriber as an empty, non-retained message. Found by the broker
    // test (test/broker): it used to start the zone with its default time.
    Fixture f;
    for (const char *suffix : {"zone/1/run", "zone/1/set", "zone/1/stop", "program/7/run", "stop_all", "pause",
                               "rain_delay", "zone/1/default_run_s"}) {
        for (const char *payload : {"", " ", "\r\n"}) {
            const ParsedCommand c = f.cmd(suffix, payload);
            EXPECT_EQ(c.action, Action::None) << suffix;
            EXPECT_EQ(c.error, "empty_payload") << suffix;
        }
    }
}

TEST(MqttCommand, DurationsAreValidated)
{
    Fixture f;
    for (const char *payload : {"0", "-5", "14401", "1.5", "1e3", "0x10", R"({"duration_s":"600"})", R"({"seconds":5})",
                                R"({"duration_s":99999999999})", "run", "[600]"}) {
        const ParsedCommand c = f.cmd("zone/1/run", payload);
        EXPECT_EQ(c.outcome, Outcome::Invalid) << payload;
        EXPECT_EQ(c.action, Action::None) << payload;
    }
    const ParsedCommand max = f.cmd("zone/1/run", "14400");
    EXPECT_EQ(max.outcome, Outcome::Queued);
    EXPECT_EQ(max.command.durationSec, kHardMaxRunSec) << "the 4 h ceiling; the driver still clamps to max_run_s";
}

TEST(MqttCommand, ZoneBoundsAndDisabledZones)
{
    Fixture f;
    EXPECT_EQ(f.cmd("zone/0/run", "60").error, "no_such_zone");
    EXPECT_EQ(f.cmd("zone/5/run", "60").error, "no_such_zone") << "zone_count is 4";
    EXPECT_EQ(f.cmd("zone/01/run", "60").error, "no_such_zone");
    EXPECT_EQ(f.cmd("zone/x/run", "60").error, "no_such_zone");
    EXPECT_EQ(f.cmd("zone/3/run", "60").error, "zone_disabled");
    EXPECT_EQ(f.cmd("zone/3/set", "OPEN").error, "zone_disabled");
    EXPECT_EQ(f.cmd("zone/1/fly", "1").error, "unknown_command");
    EXPECT_EQ(f.cmd("zone/1/run/extra/levels", "1").error, "unknown_command");
}

TEST(MqttCommand, ValveAndSwitchPayloads)
{
    Fixture f;
    for (const char *open : {"OPEN", "ON", "open"}) {
        const ParsedCommand c = f.cmd("zone/1/set", open);
        ASSERT_EQ(c.action, Action::Post) << open;
        EXPECT_EQ(c.command.durationSec, 600u);
    }
    for (const char *close : {"CLOSE", "OFF", "STOP"}) {
        const ParsedCommand c = f.cmd("zone/1/set", close);
        EXPECT_EQ(c.action, Action::StopZone) << close;
        EXPECT_EQ(c.zone, 0);
    }
    EXPECT_EQ(f.cmd("zone/1/set", "TOGGLE").outcome, Outcome::Invalid);
    EXPECT_EQ(f.cmd("zone/1/set", "600").outcome, Outcome::Invalid);
    EXPECT_EQ(f.cmd("zone/4/stop", "STOP").action, Action::StopZone);
}

TEST(MqttCommand, StopZoneResolvesAgainstTheQueue)
{
    SystemState s;
    Command out;
    EXPECT_EQ(resolveStopZone(1, s, out), "not_running");
    s.queue.state = QueueState::Running;
    s.queue.shownCount = 2;
    s.queue.items[0].zone = 0;
    s.queue.items[1].zone = 1;
    s.zones[0].state = ValveState::Open;
    EXPECT_EQ(resolveStopZone(1, s, out), "not_running") << "queued behind the head, not open";
    EXPECT_EQ(resolveStopZone(0, s, out), "");
    EXPECT_EQ(out.type, CommandType::Skip);
}

TEST(MqttCommand, ProgramsAndSimpleCommands)
{
    Fixture f;
    const ParsedCommand run = f.cmd("program/7/run", "RUN");
    ASSERT_EQ(run.action, Action::Post);
    EXPECT_EQ(run.command.type, CommandType::RunProgram);
    EXPECT_EQ(run.command.programId, 7);
    EXPECT_EQ(f.cmd("program/8/run", "RUN").error, "no_such_program");
    const ParsedCommand off = f.cmd("program/7/enabled", "OFF");
    EXPECT_EQ(off.action, Action::SetProgramEnabled);
    EXPECT_FALSE(off.flag);
    EXPECT_EQ(f.cmd("program/7/enabled", "maybe").outcome, Outcome::Invalid);

    const std::pair<const char *, CommandType> simple[] = {{"stop_all", CommandType::StopAll},
                                                           {"pause", CommandType::Pause},
                                                           {"resume", CommandType::Resume},
                                                           {"skip", CommandType::Skip},
                                                           {"clear_faults", CommandType::ClearFaults}};
    for (const auto &[name, type] : simple) {
        const ParsedCommand c = f.cmd(name, "PRESS");
        ASSERT_EQ(c.action, Action::Post) << name;
        EXPECT_EQ(c.command.type, type);
        EXPECT_EQ(c.name, name);
    }
}

TEST(MqttCommand, RainDelay)
{
    Fixture f;
    const ParsedCommand h = f.cmd("rain_delay", "24");
    ASSERT_EQ(h.action, Action::Post);
    EXPECT_EQ(h.command.type, CommandType::SetRainDelay);
    EXPECT_EQ(h.command.until, kNow + 24 * 3600);
    EXPECT_EQ(f.cmd("rain_delay", "24.0").command.until, kNow + 24 * 3600) << "Home Assistant number payload";
    EXPECT_EQ(f.cmd("rain_delay", R"({"hours":2})").command.until, kNow + 7200);
    EXPECT_EQ(f.cmd("rain_delay", R"({"until_utc":1790003600})").command.until, kNow + 3600);
    EXPECT_EQ(f.cmd("rain_delay", "0").command.until, 0) << "0 clears";
    EXPECT_EQ(f.cmd("rain_delay", "0", false, false).command.until, 0) << "clearing needs no clock";
    EXPECT_EQ(f.cmd("rain_delay", "721").outcome, Outcome::Invalid);
    EXPECT_EQ(f.cmd("rain_delay", R"({"until_utc":1})").outcome, Outcome::Invalid);
    EXPECT_EQ(f.cmd("rain_delay", R"({"hours":1,"until_utc":1790003600})").outcome, Outcome::Invalid);
    EXPECT_EQ(f.cmd("rain_delay", "5", false, false).error, "time_not_set");
}

TEST(MqttCommand, RunTimeNumber)
{
    Fixture f;
    const ParsedCommand c = f.cmd("zone/1/default_run_s", "900");
    EXPECT_EQ(c.action, Action::SetRunTime);
    EXPECT_EQ(c.outcome, Outcome::Applied);
    EXPECT_EQ(c.value, 900u);
    EXPECT_EQ(f.cmd("zone/1/default_run_s", "0").outcome, Outcome::Invalid);
    EXPECT_EQ(f.cmd("zone/1/default_run_s", "20000").outcome, Outcome::Invalid);
}

TEST(MqttCommand, RetainedCommandsAreNeverExecuted)
{
    // A retained "run" left on the broker would start watering at every
    // reconnect. Every command topic ignores retained messages.
    Fixture f;
    for (const char *suffix : {"zone/1/run", "zone/1/set", "program/7/run", "stop_all", "rain_delay",
                               "zone/1/default_run_s", "program/7/enabled"}) {
        const ParsedCommand c = f.cmd(suffix, "60", true);
        EXPECT_EQ(c.action, Action::None) << suffix;
        EXPECT_EQ(c.outcome, Outcome::IgnoredRetained) << suffix;
        EXPECT_EQ(c.error, "retained");
    }
}

TEST(MqttCommand, MalformedInputIsRejected)
{
    Fixture f;
    EXPECT_EQ(f.cmd("zone/1/run", std::string(300, '1')).error, "payload_too_large");
    EXPECT_EQ(f.cmd("zone/1/run", R"({"a":{"b":{"c":1}}})").error, "bad_payload");
    EXPECT_EQ(f.cmd("zone/1/run", "{not json").error, "bad_payload");
    EXPECT_EQ(f.cmd("nonsense", "1").error, "unknown_command");
    const ParsedCommand other = parseCommand(f.t, "elsewhere/cmd/stop_all", "", false, f.cfg, kNow, true);
    EXPECT_EQ(other.outcome, Outcome::Invalid);
    std::string binary("\x00\xff\x01", 3);
    EXPECT_EQ(f.cmd("zone/1/run", binary).outcome, Outcome::Invalid);
}

TEST(MqttCommand, ResultPayloads)
{
    Json ok(resultPayload("zone_run", Outcome::Queued, "", 1, 0));
    EXPECT_EQ(str(ok["command"]), "zone_run");
    EXPECT_EQ(str(ok["status"]), "queued");
    EXPECT_TRUE(cJSON_IsNull(ok["error"]));
    EXPECT_EQ(ok["zone"]->valueint, 2);
    EXPECT_EQ(ok["program_id"], nullptr);

    Json rej(rejectionPayload({CommandType::RunZone, 3, 0, RunError::ZoneResting}));
    EXPECT_EQ(str(rej["command"]), "run_zone");
    EXPECT_EQ(str(rej["status"]), "rejected");
    EXPECT_EQ(str(rej["error"]), "zone_resting");
    EXPECT_EQ(rej["zone"]->valueint, 4);
    Json prog(rejectionPayload({CommandType::RunProgram, kNoZone, 9, RunError::MasterLockedOut}));
    EXPECT_EQ(prog["program_id"]->valueint, 9);
    EXPECT_EQ(prog["zone"], nullptr);
}

// ---- flood protection, backoff, edits -------------------------------------------------------------

TEST(MqttRateLimiter, BurstThenSteadyRate)
{
    RateLimiter r(10, 5);
    MonoMs t = 1000;
    int allowed = 0;
    for (int i = 0; i < 100; ++i) {
        allowed += r.allow(t) ? 1 : 0;
    }
    EXPECT_EQ(allowed, 10) << "a flood at one instant gets the burst only";
    t += 1000;
    allowed = 0;
    for (int i = 0; i < 100; ++i) {
        allowed += r.allow(t) ? 1 : 0;
    }
    EXPECT_EQ(allowed, 5);
    // 10 s of one message every 10 ms: about 5 per second pass.
    allowed = 0;
    for (int i = 0; i < 1000; ++i) {
        t += 10;
        allowed += r.allow(t) ? 1 : 0;
    }
    EXPECT_GE(allowed, 49);
    EXPECT_LE(allowed, 51);
}

TEST(MqttRateLimiter, FloodNeverFillsTheCommandQueue)
{
    // Worst case of what MQTT can post: the rate limiter in front of a
    // 16-entry queue that the control task drains 8 per 50 ms tick.
    RateLimiter r;
    CommandQueue q;
    int posted = 0;
    int full = 0;
    for (MonoMs t = 0; t < 10000; t += 1) {
        for (int i = 0; i < 10; ++i) { // 10 000 messages per second
            if (r.allow(t)) {
                full += q.post(Command::simple(CommandType::Pause)) ? 0 : 1;
                ++posted;
            }
        }
        if (t % 50 == 0) {
            Command c;
            for (int i = 0; i < 8 && q.pop(c); ++i) {
            }
        }
    }
    EXPECT_LE(posted, 10 + 5 * 10 + 1);
    EXPECT_EQ(full, 0);
}

TEST(MqttConnection, BackoffDoublesWithJitterAndResets)
{
    ConnectionPolicy p;
    MonoMs now = 0;
    const uint32_t mid = 20; // random % 41 == 20: factor 100 %
    uint32_t expected = 2000;
    for (int i = 0; i < 10; ++i) {
        p.onDisconnected(now, mid);
        EXPECT_TRUE(p.waiting());
        EXPECT_EQ(p.retryInMs(now), std::min<uint32_t>(expected, 300000)) << i;
        EXPECT_FALSE(p.retryDue(now + p.retryInMs(now) - 1));
        EXPECT_TRUE(p.retryDue(now + p.retryInMs(now)));
        p.retryStarted();
        expected *= 2;
    }
    EXPECT_EQ(p.attempts(), 10u);
    p.onDisconnected(now, 0); // factor 80 %
    EXPECT_EQ(p.retryInMs(now), 240000u);
    p.onConnected();
    EXPECT_EQ(p.attempts(), 0u);
    EXPECT_FALSE(p.waiting());
    p.onDisconnected(now, 40); // factor 120 %
    EXPECT_EQ(p.retryInMs(now), 2400u);
}

TEST(MqttConnection, BirthDelayIsOneToFiveSeconds)
{
    EXPECT_EQ(birthDelayMs(0), 1000u);
    EXPECT_EQ(birthDelayMs(4000), 5000u);
    EXPECT_EQ(birthDelayMs(4001), 1000u);
    for (uint32_t r = 0; r < 100000; r += 7919) {
        EXPECT_GE(birthDelayMs(r), 1000u);
        EXPECT_LE(birthDelayMs(r), 5000u);
    }
}

TEST(MqttEdits, CoalescedAndApplied)
{
    Config c = testConfig();
    PendingEdits e;
    EXPECT_TRUE(e.empty());
    e.setRunTime(0, 60);
    e.setRunTime(0, 120); // the last value wins
    e.setProgramEnabled(7, false);
    e.setProgramEnabled(99, false); // deleted meanwhile: ignored
    EXPECT_FALSE(e.empty());
    EXPECT_TRUE(e.applyTo(c));
    EXPECT_EQ(c.zones[0].defaultRunSec, 120u);
    EXPECT_FALSE(c.schedule.programs[0].enabled);
    EXPECT_TRUE(e.empty());
    e.setRunTime(0, 120);
    EXPECT_FALSE(e.applyTo(c)) << "no change, no flash write";
}

// ---- change detection -------------------------------------------------------------------------------

TEST(MqttDirty, OnlyChangedTopics)
{
    SystemState a;
    a.zoneCount = 4;
    SystemState b = a;
    Dirty d;
    markChanges(a, b, false, d);
    EXPECT_FALSE(d.any());

    b.zones[2].state = ValveState::Open;
    b.zones[2].remainingSec = 600;
    markChanges(a, b, false, d);
    EXPECT_EQ(d.zones, 1u << 2);
    EXPECT_FALSE(d.sensors);

    // A running zone counts down every second: only periodic calls report it.
    Dirty d2;
    SystemState c = b;
    c.zones[2].remainingSec = 599;
    markChanges(b, c, false, d2);
    EXPECT_EQ(d2.zones, 0u);
    markChanges(b, c, true, d2);
    EXPECT_EQ(d2.zones, 1u << 2);

    Dirty d3;
    SystemState e = a;
    e.sensors.rainActive = true;
    e.activeFaults = 1;
    e.rainDelayUntil = 5;
    markChanges(a, e, false, d3);
    EXPECT_TRUE(d3.sensors);
    EXPECT_TRUE(d3.faults);
    EXPECT_TRUE(d3.rainDelay);
    EXPECT_FALSE(d3.flow);

    Dirty d4;
    SystemState g = a;
    g.queue.state = QueueState::Running;
    g.queue.shownCount = 1;
    g.queue.items[0].id = 4;
    markChanges(a, g, false, d4);
    EXPECT_TRUE(d4.queue);
    EXPECT_TRUE(d4.programs);
}
