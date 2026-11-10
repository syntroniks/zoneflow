// SPDX-License-Identifier: Apache-2.0
// Broker integration harness (not part of CI; see run.sh). A host build of
// the zf_mqtt logic and the real zf_core Controller (fake outputs and
// sensors), connected to a real MQTT broker through libmosquitto. It plays
// the role of the zf_mqtt task: last will, discovery, retained state,
// command parsing, rate limiting, rejections from CommandRejected events.
//
// It does NOT run the ESP-IDF glue (components/zf_mqtt/src/mqtt.cpp) or
// esp-mqtt; it reimplements the glue's loop in a few lines so the logic can
// be exercised against a broker. check.py drives it.
//
// Usage: harness <port> <seconds>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "../host/fakes.hpp"
#include "mosquitto.h"
#include "zf/app/controller.hpp"
#include "zf_mqtt/mqtt_logic.hpp"

using namespace zf;
using namespace zf::mqtt;
using namespace zf::test;

namespace {

struct Inbound {
    std::string topic;
    std::string payload;
    bool retain;
};

std::deque<Inbound> gInbox;
bool gConnected = false;

void onConnect(struct mosquitto *, void *, int rc) { gConnected = rc == 0; }

void onMessage(struct mosquitto *, void *, const struct mosquitto_message *m)
{
    gInbox.push_back({m->topic, std::string(static_cast<const char *>(m->payload), m->payloadlen), m->retain});
}

std::vector<Event> gEvents;
void onEvent(const Event &e, void *) { gEvents.push_back(e); }

} // namespace

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <port> <seconds>\n", argv[0]);
        return 2;
    }
    const int port = std::atoi(argv[1]);
    const int seconds = std::atoi(argv[2]);

    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    MemoryStorage storage;
    ConfigStore config{storage};
    CommandQueue queue;
    Config c = Config::defaults();
    c.deviceName.assign("Harness");
    c.zoneCount = 4;
    c.zones[2].enabled = false;
    c.driver.interZoneDelayMs = 0;
    c.rain.enabled = true;
    c.flow.enabled = true;
    c.schedule.programs[0] = makeProgram(4, {}, {{0, 600}, {1, 300}});
    c.schedule.programs[0].name.assign("Morning");
    c.schedule.programCount = 1;
    c.mqtt.enabled = true;
    c.mqtt.host.assign("127.0.0.1");
    if (!config.update(c).ok) {
        return 3;
    }
    Controller ctrl(ControllerDeps{clock, out, sensors, oe, bus, state, config, &storage});
    ctrl.begin();
    ctrl.setTimeStatus(TimeStatus::Synced);
    bus.subscribe(&onEvent, nullptr, eventBit(EventType::CommandRejected) | eventBit(EventType::ZoneChanged));

    const Identity id{"a1b2c3d4e5f6", "a1:b2:c3:d4:e5:f6", "zoneflow-e5f6", "harness", "host"};
    const Topics t(rootTopic(c.mqtt, id.deviceId));
    DiscoveryPlanner planner;
    RateLimiter limiter;
    Dirty dirty;
    SystemState last{};

    mosquitto_lib_init();
    struct mosquitto *m = mosquitto_new(clientId(c.mqtt, id.deviceId).c_str(), true, nullptr);
    mosquitto_connect_callback_set(m, &onConnect);
    mosquitto_message_callback_set(m, &onMessage);
    mosquitto_will_set(m, t.availability().c_str(), 7, "offline", 1, true);
    if (mosquitto_connect(m, "127.0.0.1", port, 5) != MOSQ_ERR_SUCCESS) {
        std::fprintf(stderr, "connect failed\n");
        return 4;
    }
    const auto pub = [&](const std::string &topic, const std::string &payload, int qos, bool retain) {
        mosquitto_publish(m, nullptr, topic.c_str(), static_cast<int>(payload.size()), payload.data(), qos, retain);
    };

    bool announced = false;
    MonoMs birthAt = 0;
    MonoMs lastPeriodic = 0;
    const MonoMs end = clock.mono + static_cast<MonoMs>(seconds) * 1000;
    while (clock.mono < end) {
        mosquitto_loop(m, 50, 1); // also paces the loop: about one control period
        clock.advanceMs(50);
        if (!gConnected) {
            continue;
        }
        if (!announced) {
            pub(t.availability(), "online", 1, true);
            mosquitto_subscribe(m, nullptr, t.commandFilter().c_str(), 1);
            mosquitto_subscribe(m, nullptr, Topics::haStatus("homeassistant").c_str(), 0);
            for (const std::string &d : planner.plan(config.get(), id, t)) {
                pub(Topics::discovery("homeassistant", id.deviceId), d, 1, false);
            }
            dirty.all();
            announced = true;
        }
        const Config cfg = config.get();
        while (!gInbox.empty()) {
            const Inbound in = gInbox.front();
            gInbox.pop_front();
            if (in.topic == Topics::haStatus("homeassistant")) {
                if (in.payload == "online") {
                    birthAt = clock.mono + birthDelayMs(static_cast<uint32_t>(std::rand()));
                }
                continue;
            }
            if (!limiter.allow(clock.mono)) {
                pub(t.result(), resultPayload("unknown", Outcome::RateLimited, "rate_limited", kNoZone, 0), 0, false);
                continue;
            }
            ParsedCommand pc = parseCommand(t, in.topic, in.payload, in.retain, cfg, clock.nowUtc(), true);
            if (pc.action == Action::StopZone) {
                const std::string err = resolveStopZone(pc.zone, state.snapshot(), pc.command);
                if (!err.empty()) {
                    pc.action = Action::None;
                    pc.outcome = Outcome::Invalid;
                    pc.error = err;
                } else {
                    pc.action = Action::Post;
                }
            }
            if (pc.action == Action::Post) {
                const bool ok = queue.post(pc.command);
                pub(t.result(),
                    resultPayload(pc.name, ok ? Outcome::Queued : Outcome::Busy, ok ? "" : "queue_full", pc.zone,
                                  pc.programId),
                    0, false);
            } else {
                pub(t.result(), resultPayload(pc.name, pc.outcome, pc.error, pc.zone, pc.programId), 0, false);
            }
        }
        // The control task: at most 8 commands per tick, then tick.
        Command cmd;
        for (int i = 0; i < 8 && queue.pop(cmd); ++i) {
            ctrl.handle(cmd);
        }
        ctrl.tick();
        for (const Event &e : gEvents) {
            if (e.type == EventType::CommandRejected) {
                pub(t.result(), rejectionPayload(e.rejected), 0, false);
            }
            pub(t.event(), eventPayload(e), 0, false);
        }
        gEvents.clear();
        if (birthAt != 0 && clock.mono >= birthAt) {
            for (const std::string &d : planner.plan(cfg, id, t)) {
                pub(Topics::discovery("homeassistant", id.deviceId), d, 1, false);
            }
            dirty.all();
            birthAt = 0;
        }
        const bool periodic = clock.mono - lastPeriodic >= 5000;
        const SystemState s = state.snapshot();
        markChanges(last, s, periodic, dirty);
        last = s;
        if (periodic) {
            lastPeriodic = clock.mono;
        }
        const DeviceInfo info{"harness", "zoneflow-e5f6", "127.0.0.1", -50, true};
        if (dirty.device) {
            pub(t.device(), devicePayload(s, cfg, info), 0, true);
        }
        if (dirty.queue) {
            pub(t.queue(), queuePayload(s.queue, cfg.schedule.pauseTimeoutMin), 0, true);
        }
        if (dirty.sensors) {
            pub(t.sensors(), sensorsPayload(s), 0, true);
        }
        if (dirty.flow) {
            pub(t.flow(), flowPayload(s), 0, true);
        }
        if (dirty.faults) {
            pub(t.faults(), faultsPayload(s), 0, true);
        }
        if (dirty.rainDelay) {
            pub(t.rainDelay(), rainDelayPayload(s), 0, true);
        }
        if (dirty.update) {
            pub(t.update(), updatePayload("harness", ota::OtaStatus{}), 0, true);
        }
        for (std::size_t i = 0; i < cfg.zoneCount; ++i) {
            if ((dirty.zones & (1u << i)) != 0) {
                pub(t.zone(i + 1), zonePayload(i, s.zones[i], cfg.zones[i], s.queue), 0, true);
            }
        }
        if (dirty.programs) {
            for (std::size_t i = 0; i < cfg.schedule.programCount; ++i) {
                pub(t.program(cfg.schedule.programs[i].id), programPayload(cfg.schedule.programs[i], s.queue), 0, true);
            }
        }
        dirty = Dirty{};
    }
    // Leaves without DISCONNECT so the broker sends the last will, as after
    // a power cut.
    std::_Exit(0);
}
