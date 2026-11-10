// SPDX-License-Identifier: Apache-2.0
// zf_mqtt device glue: esp-mqtt client, NVS secrets and the zf_mqtt task.
// Every decision is in mqtt_logic.cpp (host-tested); this file moves bytes.
//
// Tasks:
//   mqtt_task (esp-mqtt, prio 5): socket and protocol. Its event handler only
//     copies messages and connection events into gInbox (zero timeout; a
//     full inbox drops the message and counts it).
//   zf_mqtt (prio 3, core 0, 6 KB): everything else. Reads gInbox and the
//     event-bus queue, posts commands, writes coalesced config edits, and
//     publishes state. A slow broker blocks only this task (publish waits at
//     most the 5 s network timeout); the control task never waits on MQTT.
//
// Memory bound when the broker is slow or away: retained state is tracked
// as dirty bits (mqtt_logic.hpp, Dirty) and rebuilt from the newest snapshot
// at send time, so nothing accumulates. Events and command results are sent
// only while connected and dropped otherwise. QoS 1 messages (availability,
// discovery) wait in the esp-mqtt outbox, capped at kOutboxLimit bytes.
#include "zf_mqtt/mqtt.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "zf_board/board.hpp"
#include "zf_net/net.hpp"
#include "zf_net/nvs_store.hpp"
#include "zf_ota/ota.hpp"
#include "zf_platform/platform.hpp"
#include "zf_web/crypto.hpp"

namespace zf::mqtt {
namespace {

constexpr const char *kTag = "zf.mqtt";
constexpr const char *kPasswordKey = "password";
constexpr const char *kCaKey = "ca_pem";
constexpr uint32_t kLoopMs = 250;
constexpr uint32_t kPeriodicMs = 5000; // countdowns, flow, RSSI
constexpr uint64_t kOutboxLimit = 64 * 1024;
constexpr std::size_t kTopicMax = 160;
constexpr UBaseType_t kInboxDepth = 8;

enum class InKind : uint8_t { Message, Connected, Disconnected, Error };

struct Inbound {
    InKind kind;
    bool retain;
    bool tooLarge;
    uint16_t topicLen;
    uint16_t payloadLen;
    char topic[kTopicMax];
    char payload[kMaxCommandPayload];
    char error[40];
};

struct Module {
    const AppServices *services = nullptr;
    net::NvsNamespace store{"zf_mqtt"};
    std::mutex secretsMutex; // NVS writes from the HTTP task
    std::atomic<uint32_t> secretsGeneration{0};
    std::atomic<bool> passwordSet{false};
    std::atomic<bool> caSet{false};
    QueueHandle_t inbox = nullptr;
    std::atomic<uint32_t> inboxDropped{0};
    platform::EventQueueSubscriber events;
    platform::EspClock clock;
    MqttStatusStore statusStore;

    // Owned by the zf_mqtt task.
    esp_mqtt_client_handle_t client = nullptr;
    MqttStatus st;
    MqttConfig active{}; // settings the client was created with
    uint32_t activeSecrets = 0;
    std::string host, user, password, clientId, caPem, lwtTopic;
    Topics topics;
    Identity identity;
    ConnectionPolicy policy;
    RateLimiter limiter;
    PendingEdits edits;
    MonoMs lastConfigWrite = 0;
    DiscoveryPlanner planner;
    Dirty dirty;
    SystemState last{};
    uint32_t lastSeq = 0;
    uint32_t configRevision = 0;
    MonoMs lastPeriodic = 0;
    MonoMs birthAt = 0; // 0: no discovery pending from a Home Assistant birth
    bool discoveryDue = false;
    ota::OtaStatus lastOta{};
    uint16_t publishedZones = 0;
    std::vector<uint16_t> publishedPrograms;
};

Module *gm = nullptr;

// Overwrites a string that held the broker password before it is freed or
// reused (zf_web secureZero, as for the admin password hash).
void wipe(std::string &s)
{
    web::secureZero(s.data(), s.size());
    s.clear();
}

// ---- esp-mqtt event handler (esp-mqtt task): copy and return ------------------------------

void enqueue(const Inbound &in)
{
    if (xQueueSend(gm->inbox, &in, 0) != pdTRUE) {
        gm->inboxDropped.fetch_add(1, std::memory_order_relaxed);
    }
}

void onMqttEvent(void *, esp_event_base_t, int32_t id, void *data)
{
    auto *e = static_cast<esp_mqtt_event_handle_t>(data);
    // Built on this task's stack, about 470 bytes.
    Inbound in{};
    switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED:
        in.kind = InKind::Connected;
        enqueue(in);
        break;
    case MQTT_EVENT_DISCONNECTED:
        in.kind = InKind::Disconnected;
        enqueue(in);
        break;
    case MQTT_EVENT_ERROR:
        in.kind = InKind::Error;
        if (e->error_handle != nullptr) {
            const esp_mqtt_error_codes_t *eh = e->error_handle;
            if (eh->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                std::snprintf(in.error, sizeof in.error, "refused (code %d)",
                              static_cast<int>(eh->connect_return_code));
            } else if (eh->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                std::snprintf(in.error, sizeof in.error, "transport: %s", esp_err_to_name(eh->esp_tls_last_esp_err));
            } else {
                std::snprintf(in.error, sizeof in.error, "error type %d", static_cast<int>(eh->error_type));
            }
        }
        enqueue(in);
        break;
    case MQTT_EVENT_DATA:
        // Later fragments of a long message carry no topic; the first one
        // already reported it as too large.
        if (e->current_data_offset != 0) {
            break;
        }
        in.kind = InKind::Message;
        in.retain = e->retain;
        if (e->topic_len <= 0 || static_cast<std::size_t>(e->topic_len) > kTopicMax) {
            break; // not one of our topics (ours are shorter)
        }
        in.topicLen = static_cast<uint16_t>(e->topic_len);
        std::memcpy(in.topic, e->topic, in.topicLen);
        if (e->total_data_len > static_cast<int>(kMaxCommandPayload)) {
            in.tooLarge = true;
        } else {
            in.payloadLen = static_cast<uint16_t>(e->data_len);
            std::memcpy(in.payload, e->data, in.payloadLen);
        }
        enqueue(in);
        break;
    default:
        break;
    }
}

// ---- publishing ----------------------------------------------------------------------------

bool connected() { return gm->st.state == ConnState::Connected; }

bool publish(const std::string &topic, const std::string &payload, int qos, bool retain)
{
    if (!connected()) {
        return false;
    }
    const int id = esp_mqtt_client_publish(gm->client, topic.c_str(), payload.data(), static_cast<int>(payload.size()),
                                           qos, retain ? 1 : 0);
    if (id < 0) {
        ++gm->st.publishFailed;
        return false;
    }
    ++gm->st.published;
    return true;
}

void publishResult(const std::string &payload) { publish(gm->topics.result(), payload, 0, false); }

DeviceInfo deviceInfo()
{
    const net::NetStatus n = net::status();
    DeviceInfo d;
    d.firmware = gm->identity.firmware;
    d.hostname = std::string(n.hostname.view());
    d.ip = std::string(n.ip.view());
    d.rssi = n.rssi;
    d.wifiConnected = n.state == net::NetState::Connected;
    return d;
}

void publishDiscovery(const Config &cfg)
{
    if (!cfg.mqtt.discovery) {
        return;
    }
    const std::string topic = Topics::discovery(cfg.mqtt.discoveryPrefix.view(), gm->identity.deviceId);
    // Not retained: sent on every connect and after every Home Assistant
    // birth message, so no stale discovery stays on the broker.
    for (const std::string &payload : gm->planner.plan(cfg, gm->identity, gm->topics)) {
        if (!publish(topic, payload, 1, false)) {
            ESP_LOGW(kTag, "discovery not sent (%u bytes)", static_cast<unsigned>(payload.size()));
            return;
        }
    }
    gm->st.discoveryPublished = true;
}

// Sends what is dirty; stops at the first failure and keeps the rest dirty.
void publishDirty(const Config &cfg)
{
    Dirty &d = gm->dirty;
    const SystemState &s = gm->last;
    const auto send = [&](bool &flag, const std::string &topic, const std::string &payload) {
        if (flag && publish(topic, payload, 0, true)) {
            flag = false;
        }
        return !flag;
    };
    if (!send(d.device, gm->topics.device(), devicePayload(s, cfg, deviceInfo())) ||
        !send(d.queue, gm->topics.queue(), queuePayload(s.queue, cfg.schedule.pauseTimeoutMin)) ||
        !send(d.sensors, gm->topics.sensors(), sensorsPayload(s)) || !send(d.flow, gm->topics.flow(), flowPayload(s)) ||
        !send(d.faults, gm->topics.faults(), faultsPayload(s)) ||
        !send(d.rainDelay, gm->topics.rainDelay(), rainDelayPayload(s)) ||
        !send(d.update, gm->topics.update(), updatePayload(gm->identity.firmware, gm->lastOta))) {
        return;
    }
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1u << i);
        if ((d.zones & bit) == 0) {
            continue;
        }
        bool ok = true;
        if (i < cfg.zoneCount) {
            ok = publish(gm->topics.zone(i + 1), zonePayload(i, s.zones[i], cfg.zones[i], s.queue), 0, true);
            gm->publishedZones = static_cast<uint16_t>(gm->publishedZones | bit);
        } else if ((gm->publishedZones & bit) != 0) {
            // Zone count went down: clear the retained topic.
            ok = publish(gm->topics.zone(i + 1), "", 0, true);
            gm->publishedZones = static_cast<uint16_t>(gm->publishedZones & ~bit);
        }
        if (!ok) {
            return;
        }
        d.zones = static_cast<uint16_t>(d.zones & ~bit);
    }
    d.zones = 0;
    if (d.programs) {
        std::vector<uint16_t> now;
        for (std::size_t i = 0; i < cfg.schedule.programCount; ++i) {
            const Program &p = cfg.schedule.programs[i];
            if (!publish(gm->topics.program(p.id), programPayload(p, s.queue), 0, true)) {
                return;
            }
            now.push_back(p.id);
        }
        for (const uint16_t old : gm->publishedPrograms) {
            if (std::find(now.begin(), now.end(), old) == now.end()) {
                publish(gm->topics.program(old), "", 0, true); // deleted program
            }
        }
        gm->publishedPrograms = now;
        d.programs = false;
    }
}

// ---- client lifecycle ---------------------------------------------------------------------------

void stopClient(const char *why)
{
    if (gm->client == nullptr) {
        return;
    }
    if (connected()) {
        // The broker sends the last will only on an unclean disconnect.
        esp_mqtt_client_publish(gm->client, gm->lwtTopic.c_str(), "offline", 7, 1, 1);
    }
    esp_mqtt_client_stop(gm->client);
    esp_mqtt_client_destroy(gm->client);
    gm->client = nullptr;
    gm->st.state = ConnState::Disabled;
    gm->policy.reset();
    ESP_LOGI(kTag, "client stopped: %s", why);
}

bool sameSettings(const MqttConfig &a, const MqttConfig &b)
{
    return a.enabled == b.enabled && a.host == b.host && a.port == b.port && a.username == b.username &&
           a.tls == b.tls && a.ca == b.ca && a.clientId == b.clientId && a.baseTopic == b.baseTopic &&
           a.discovery == b.discovery && a.discoveryPrefix == b.discoveryPrefix; // subscriptions depend on these
}

void loadSecrets()
{
    std::lock_guard<std::mutex> lock(gm->secretsMutex);
    wipe(gm->password);
    gm->caPem.clear();
    gm->store.read(kPasswordKey, gm->password);
    gm->store.read(kCaKey, gm->caPem);
}

void setError(const char *e)
{
    gm->st.lastError.assign(e);
    ESP_LOGW(kTag, "%s", e);
}

void startClient(const Config &cfg)
{
    const net::NetStatus n = net::status();
    gm->identity.deviceId = deviceIdFromMac(n.mac.view());
    gm->identity.mac = std::string(n.mac.view());
    gm->identity.hostname = std::string(n.hostname.view());
    gm->identity.firmware = esp_app_get_description()->version;
    gm->identity.board = board::kBoardName;
    if (gm->identity.deviceId.empty()) {
        setError("no station MAC address yet");
        return;
    }
    const MqttConfig &m = cfg.mqtt;
    gm->active = m;
    gm->activeSecrets = gm->secretsGeneration.load();
    loadSecrets();
    if (m.tls && m.ca == MqttCaSource::Custom && gm->caPem.empty()) {
        setError("tls with a custom CA, but no CA certificate stored");
        gm->st.state = ConnState::Disabled;
        return;
    }
    gm->topics = Topics(rootTopic(m, gm->identity.deviceId));
    gm->host = std::string(m.host.view());
    gm->user = std::string(m.username.view());
    gm->clientId = clientId(m, gm->identity.deviceId);
    gm->lwtTopic = gm->topics.availability();

    esp_mqtt_client_config_t c{};
    c.broker.address.hostname = gm->host.c_str();
    c.broker.address.port = m.port;
    c.broker.address.transport = m.tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    if (m.tls) {
        if (m.ca == MqttCaSource::Custom) {
            c.broker.verification.certificate = gm->caPem.c_str();
            c.broker.verification.certificate_len = gm->caPem.size() + 1; // PEM length includes the NUL
        } else {
            c.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        }
    }
    c.credentials.client_id = gm->clientId.c_str();
    c.credentials.username = gm->user.empty() ? nullptr : gm->user.c_str();
    c.credentials.authentication.password = gm->password.empty() ? nullptr : gm->password.c_str();
    c.session.last_will.topic = gm->lwtTopic.c_str();
    c.session.last_will.msg = "offline";
    c.session.last_will.msg_len = 7;
    c.session.last_will.qos = 1;
    c.session.last_will.retain = 1;
    c.session.keepalive = 60;
    c.network.disable_auto_reconnect = true; // ConnectionPolicy decides
    c.network.timeout_ms = 5000;
    c.buffer.size = 1024;     // inbound: commands are at most 256 bytes
    c.buffer.out_size = 2048; // longer publishes (discovery) are written in fragments
    c.outbox.limit = kOutboxLimit;
    gm->client = esp_mqtt_client_init(&c);
    // esp-mqtt keeps its own copy (strdup); ours is not needed any more.
    wipe(gm->password);
    if (gm->client == nullptr) {
        setError("esp_mqtt_client_init failed");
        return;
    }
    esp_mqtt_client_register_event(gm->client, MQTT_EVENT_ANY, &onMqttEvent, nullptr);
    if (esp_mqtt_client_start(gm->client) != ESP_OK) {
        setError("esp_mqtt_client_start failed");
        esp_mqtt_client_destroy(gm->client);
        gm->client = nullptr;
        return;
    }
    char broker[64];
    std::snprintf(broker, sizeof broker, "%s://%s:%u", m.tls ? "mqtts" : "mqtt", gm->host.c_str(),
                  static_cast<unsigned>(m.port));
    gm->st.broker.assign(broker);
    gm->st.root.assign(gm->topics.root());
    gm->st.state = ConnState::Connecting;
    ESP_LOGI(kTag, "connecting to %s as %s, root %s", broker, gm->clientId.c_str(), gm->topics.root().c_str());
}

void onConnected(const Config &cfg, MonoMs now)
{
    gm->st.state = ConnState::Connected;
    gm->st.connectedSinceMs = now;
    ++gm->st.connects;
    gm->st.lastError.assign("");
    gm->policy.onConnected();
    publish(gm->topics.availability(), std::string(kOnline), 1, true);
    esp_mqtt_client_subscribe(gm->client, gm->topics.commandFilter().c_str(), 1);
    if (cfg.mqtt.discovery) {
        esp_mqtt_client_subscribe(gm->client, Topics::haStatus(cfg.mqtt.discoveryPrefix.view()).c_str(), 0);
    }
    gm->discoveryDue = true;
    gm->dirty.all();
    ESP_LOGI(kTag, "connected");
}

// ---- commands -------------------------------------------------------------------------------------

void handleMessage(const Inbound &in, const Config &cfg, MonoMs now)
{
    const std::string_view topic(in.topic, in.topicLen);
    const std::string_view payload(in.payload, in.payloadLen);
    if (cfg.mqtt.discovery && topic == Topics::haStatus(cfg.mqtt.discoveryPrefix.view())) {
        if (payload == "online") {
            gm->birthAt = now + birthDelayMs(esp_random());
        }
        return;
    }
    ++gm->st.commandsReceived;
    const auto reject = [&](const std::string &name, Outcome o, std::string_view err, ZoneIndex z, uint16_t pid) {
        ++gm->st.commandsRejected;
        publishResult(resultPayload(name, o, err, z, pid));
    };
    if (!gm->limiter.allow(now)) {
        reject("unknown", Outcome::RateLimited, "rate_limited", kNoZone, 0);
        return;
    }
    const bool timeValid = gm->services->time.status() != TimeStatus::Unset;
    ParsedCommand pc = in.tooLarge
                           ? ParsedCommand{}
                           : parseCommand(gm->topics, topic, payload, in.retain, cfg, gm->clock.nowUtc(), timeValid);
    if (in.tooLarge) {
        pc.name = "unknown";
        pc.error = "payload_too_large";
    }
    switch (pc.action) {
    case Action::None:
        reject(pc.name, pc.outcome, pc.error, pc.zone, pc.programId);
        return;
    case Action::StopZone: {
        const SystemState s = gm->services->state.snapshot();
        const std::string err = resolveStopZone(pc.zone, s, pc.command);
        if (!err.empty()) {
            reject(pc.name, Outcome::Invalid, err, pc.zone, 0);
            return;
        }
        break;
    }
    case Action::SetRunTime:
        gm->edits.setRunTime(pc.zone, pc.value);
        publishResult(resultPayload(pc.name, Outcome::Applied, "", pc.zone, 0));
        return;
    case Action::SetProgramEnabled:
        gm->edits.setProgramEnabled(pc.programId, pc.flag);
        publishResult(resultPayload(pc.name, Outcome::Applied, "", kNoZone, pc.programId));
        return;
    case Action::Post:
        break;
    }
    if (!gm->services->commands.post(pc.command)) {
        reject(pc.name, Outcome::Busy, "queue_full", pc.zone, pc.programId);
        return;
    }
    publishResult(resultPayload(pc.name, Outcome::Queued, "", pc.zone, pc.programId));
}

void applyEdits(MonoMs now)
{
    if (gm->edits.empty() || now - gm->lastConfigWrite < PendingEdits::kConfigWriteIntervalMs) {
        return;
    }
    gm->lastConfigWrite = now;
    auto cfg = std::make_unique<Config>(gm->services->config.get());
    if (!gm->edits.applyTo(*cfg)) {
        return;
    }
    const ValidationResult v = gm->services->config.update(*cfg);
    if (!v.ok) {
        ESP_LOGW(kTag, "config edit rejected at %s: %s", v.path.c_str(), v.message.c_str());
        publishResult(resultPayload("config_edit", Outcome::Invalid, v.message, kNoZone, 0));
        return;
    }
    if (!gm->services->commands.post(Command::simple(CommandType::ReloadConfig))) {
        ESP_LOGW(kTag, "config stored, reload not queued");
    }
}

// ---- task ----------------------------------------------------------------------------------------

void task(void *)
{
    auto cfg = std::make_unique<Config>(gm->services->config.get());
    gm->configRevision = gm->services->config.revision();
    for (;;) {
        Inbound in;
        bool got = xQueueReceive(gm->inbox, &in, pdMS_TO_TICKS(kLoopMs)) == pdTRUE;
        const MonoMs now = gm->clock.monoMs();

        // Configuration changes: restart the client for connection settings,
        // otherwise re-announce (names, zones, programs) and republish.
        if (gm->services->config.revision() != gm->configRevision) {
            gm->configRevision = gm->services->config.revision();
            *cfg = gm->services->config.get();
            if (gm->client != nullptr && !sameSettings(cfg->mqtt, gm->active)) {
                stopClient("settings changed");
            } else {
                gm->discoveryDue = true;
                gm->dirty.all();
            }
        }
        if (gm->client != nullptr && gm->secretsGeneration.load() != gm->activeSecrets) {
            stopClient("credentials changed");
        }

        for (int i = 0; got && i < 16; ++i) {
            switch (in.kind) {
            case InKind::Connected:
                onConnected(*cfg, now);
                break;
            case InKind::Disconnected:
                if (gm->st.state != ConnState::Disabled) {
                    gm->st.state = ConnState::WaitingRetry;
                    gm->policy.onDisconnected(now, esp_random());
                }
                break;
            case InKind::Error:
                if (in.error[0] != '\0') {
                    in.error[sizeof in.error - 1] = '\0';
                    setError(in.error);
                }
                break;
            case InKind::Message:
                handleMessage(in, *cfg, now);
                break;
            }
            got = xQueueReceive(gm->inbox, &in, 0) == pdTRUE;
        }

        // Events: rejections go to the result topic, everything to the event
        // topic. Only while connected; otherwise they are dropped.
        Event e;
        while (gm->events.receive(e, 0)) {
            if (!connected()) {
                continue;
            }
            if (e.type == EventType::CommandRejected) {
                ++gm->st.commandsRejected;
                publishResult(rejectionPayload(e.rejected));
            }
            publish(gm->topics.event(), eventPayload(e), 0, false);
        }

        const net::NetStatus n = net::status();
        const bool netUp = n.state == net::NetState::Connected;
        if (!cfg->mqtt.enabled) {
            stopClient("disabled");
            gm->st.state = ConnState::Disabled;
        } else if (gm->client == nullptr) {
            gm->st.state = netUp ? ConnState::Disabled : ConnState::WaitingNetwork;
            if (netUp && gm->policy.retryInMs(now) == 0) {
                startClient(*cfg);
                if (gm->client == nullptr) {
                    gm->policy.onDisconnected(now, esp_random()); // e.g. no CA: retry with backoff
                }
            }
        } else if (gm->st.state == ConnState::WaitingRetry && netUp && gm->policy.retryDue(now)) {
            gm->policy.retryStarted();
            gm->st.state = ConnState::Connecting;
            ++gm->st.reconnectAttempt;
            esp_mqtt_client_reconnect(gm->client);
        } else if (!netUp && gm->st.state == ConnState::WaitingRetry) {
            gm->st.state = ConnState::WaitingNetwork;
        } else if (netUp && gm->st.state == ConnState::WaitingNetwork) {
            gm->st.state = ConnState::WaitingRetry;
        }

        if (connected()) {
            if (gm->discoveryDue || (gm->birthAt != 0 && now >= gm->birthAt)) {
                publishDiscovery(*cfg);
                if (gm->birthAt != 0) {
                    gm->dirty.all(); // Home Assistant restarted: states again
                }
                gm->discoveryDue = false;
                gm->birthAt = 0;
            }
            const uint32_t seq = gm->services->state.sequence();
            const bool periodic = now - gm->lastPeriodic >= kPeriodicMs;
            if (seq != gm->lastSeq || periodic) {
                const SystemState s = gm->services->state.snapshot();
                markChanges(gm->last, s, periodic, gm->dirty);
                gm->last = s;
                gm->lastSeq = seq;
            }
            const ota::OtaStatus o = ota::status();
            if (o.phase != gm->lastOta.phase || o.receivedBytes / 65536 != gm->lastOta.receivedBytes / 65536 ||
                !(o.newVersion == gm->lastOta.newVersion)) {
                gm->lastOta = o;
                gm->dirty.update = true;
            }
            if (periodic) {
                gm->lastPeriodic = now;
            }
            publishDirty(*cfg);
        }
        applyEdits(now);

        gm->st.reconnectAttempt = gm->policy.attempts();
        gm->st.retryInMs = gm->policy.retryInMs(now);
        gm->st.eventsDropped = gm->events.dropped() + gm->inboxDropped.load(std::memory_order_relaxed);
        gm->statusStore.publish(gm->st);
    }
}

} // namespace

esp_err_t start(const AppServices &services)
{
    static Module module;
    gm = &module;
    gm->services = &services;
    esp_err_t err = gm->store.open();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "NVS namespace: %s", esp_err_to_name(err));
        return err;
    }
    std::string v;
    gm->passwordSet = gm->store.read(kPasswordKey, v) && !v.empty();
    wipe(v);
    gm->caSet = gm->store.read(kCaKey, v) && !v.empty();
    gm->inbox = xQueueCreate(kInboxDepth, sizeof(Inbound));
    if (gm->inbox == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    err = gm->events.begin(services.events, kAllEvents, 32);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreatePinnedToCore(&task, "zf_mqtt", 6144, nullptr, 3, nullptr, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

MqttStatus status()
{
    if (gm == nullptr) {
        return {};
    }
    return gm->statusStore.snapshot();
}

SecretsInfo secrets()
{
    if (gm == nullptr) {
        return {};
    }
    return {gm->passwordSet.load(), gm->caSet.load()};
}

bool setSecrets(std::optional<std::string_view> password, std::optional<std::string_view> caPem)
{
    if (gm == nullptr) {
        return false;
    }
    std::lock_guard<std::mutex> lock(gm->secretsMutex);
    const auto put = [](const char *key, std::string_view v) {
        return v.empty() ? (gm->store.erase(key), true) : gm->store.write(key, v);
    };
    if (password && !put(kPasswordKey, *password)) {
        return false;
    }
    if (caPem && !put(kCaKey, *caPem)) {
        return false;
    }
    if (password) {
        gm->passwordSet = !password->empty();
    }
    if (caPem) {
        gm->caSet = !caPem->empty();
    }
    gm->secretsGeneration.fetch_add(1);
    return true;
}

} // namespace zf::mqtt
