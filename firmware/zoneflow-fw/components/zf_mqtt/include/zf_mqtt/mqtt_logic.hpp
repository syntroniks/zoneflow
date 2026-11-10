// SPDX-License-Identifier: Apache-2.0
// zf_mqtt logic, independent of ESP-IDF and esp-mqtt: topic names, state
// payloads, command parsing and validation, Home Assistant device discovery,
// rate limiting and the connection policy. Host-tested
// (test/host/mqtt_*_test.cpp); the device glue (mqtt.cpp) only moves bytes.
//
// Topic tree and payloads: docs/firmware/mqtt.md. Design:
// docs/design/firmware-architecture.md, section 18.
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "zf/config/config.hpp"
#include "zf/events/command.hpp"
#include "zf/events/event.hpp"
#include "zf/events/state.hpp"
#include "zf/fixed_string.hpp"
#include "zf_net/net_logic.hpp"
#include "zf_ota/ota_logic.hpp"

namespace zf::mqtt {

// ---- identity and topics ----------------------------------------------------------------

// What the device publishes about itself. The device id is the station MAC
// as 12 lower-case hex digits; it never changes, so Home Assistant unique ids
// built from it survive renames and base-topic changes.
struct Identity {
    std::string deviceId; // "a1b2c3d4e5f6"
    std::string mac;      // "a1:b2:c3:d4:e5:f6"
    std::string hostname; // "zoneflow-e5f6"
    std::string firmware; // esp_app_desc_t version
    std::string board;    // zf_board kBoardName
};

// "a1:b2:c3:d4:e5:f6" -> "a1b2c3d4e5f6". Empty if `mac` is not a MAC address.
std::string deviceIdFromMac(std::string_view mac);

// Root of every topic: mqtt.base_topic, or "zoneflow/<device id>".
std::string rootTopic(const MqttConfig &cfg, std::string_view deviceId);
// mqtt.client_id, or "zoneflow-<device id>".
std::string clientId(const MqttConfig &cfg, std::string_view deviceId);

class Topics {
public:
    Topics() = default;
    explicit Topics(std::string root) : root_(std::move(root)) {}

    const std::string &root() const { return root_; }
    std::string availability() const { return root_ + "/status"; } // retained, last will
    std::string device() const { return root_ + "/device"; }
    std::string zone(std::size_t number) const { return root_ + "/zone/" + std::to_string(number); }
    std::string program(uint16_t id) const { return root_ + "/program/" + std::to_string(id); }
    std::string queue() const { return root_ + "/queue"; }
    std::string sensors() const { return root_ + "/sensors"; }
    std::string flow() const { return root_ + "/flow"; }
    std::string faults() const { return root_ + "/faults"; }
    std::string rainDelay() const { return root_ + "/rain_delay"; }
    std::string update() const { return root_ + "/update"; }
    std::string event() const { return root_ + "/event"; }   // not retained
    std::string result() const { return root_ + "/result"; } // not retained
    std::string commandFilter() const { return root_ + "/cmd/#"; }
    std::string command(std::string_view suffix) const { return root_ + "/cmd/" + std::string(suffix); }

    static std::string discovery(std::string_view prefix, std::string_view deviceId)
    {
        return std::string(prefix) + "/device/zoneflow_" + std::string(deviceId) + "/config";
    }
    static std::string haStatus(std::string_view prefix) { return std::string(prefix) + "/status"; }

private:
    std::string root_;
};

inline constexpr std::string_view kOnline = "online";
inline constexpr std::string_view kOffline = "offline";

// ---- state payloads (JSON, retained) ------------------------------------------------------

struct DeviceInfo {
    std::string firmware;
    std::string hostname;
    std::string ip;
    int8_t rssi = 0;
    bool wifiConnected = false;
};

std::string devicePayload(const SystemState &s, const Config &cfg, const DeviceInfo &d);
std::string zonePayload(std::size_t index, const ZoneStatus &z, const ZoneConfig &cfg, const QueueStatus &q);
std::string programPayload(const Program &p, const QueueStatus &q);
std::string queuePayload(const QueueStatus &q, uint16_t pauseTimeoutMin);
std::string sensorsPayload(const SystemState &s);
std::string flowPayload(const SystemState &s);
std::string faultsPayload(const SystemState &s);
std::string rainDelayPayload(const SystemState &s);
// Home Assistant update entity state (installed/latest version, progress).
std::string updatePayload(std::string_view installed, const ota::OtaStatus &o);
std::string eventPayload(const Event &e);

// Valve state names Home Assistant expects on a valve state topic.
const char *haValveState(ValveState s); // "closed", "opening", "open"

// ---- what changed --------------------------------------------------------------------------

// Which retained topics need publishing. One bit per topic, so a slow or
// absent broker costs no memory: the payload is built from the newest
// snapshot when the topic is finally sent.
struct Dirty {
    bool device = false;
    bool queue = false;
    bool sensors = false;
    bool flow = false;
    bool faults = false;
    bool rainDelay = false;
    bool update = false;
    uint16_t zones = 0;    // bit n = zone n + 1
    bool programs = false; // all program topics
    bool any() const
    {
        return device || queue || sensors || flow || faults || rainDelay || update || zones || programs;
    }
    void all()
    {
        device = queue = sensors = flow = faults = rainDelay = update = programs = true;
        zones = 0xFFFF;
    }
};

// Compares two snapshots and marks the topics whose payload would differ.
// Fields that change every second (uptime, utc, remaining times, flow) mark
// their topic only when `periodic` is true, so a running zone is reported
// once per call with periodic set (the glue sets it every 5 s).
void markChanges(const SystemState &before, const SystemState &after, bool periodic, Dirty &d);

// ---- commands -------------------------------------------------------------------------------

enum class Outcome : uint8_t {
    Queued,          // posted to the command queue; a rejection follows on the result topic if any
    Applied,         // configuration edit accepted (written within a few seconds)
    Rejected,        // the controller refused it (CommandRejected); `error` is the RunError name
    Invalid,         // malformed topic or payload, out of range, unknown zone or program
    IgnoredRetained, // the message carried the retain flag; commands are never taken from retained messages
    RateLimited,     // more than the command budget per second
    Busy,            // command queue full
};
const char *toString(Outcome o);

enum class Action : uint8_t {
    None,       // nothing to do (see outcome)
    Post,       // post `command`
    StopZone,   // close `zone` if it is the running entry (resolved against a snapshot)
    SetRunTime, // zones[zone].default_run_s = value
    SetProgramEnabled,
};

struct ParsedCommand {
    Action action = Action::None;
    Outcome outcome = Outcome::Invalid;
    std::string name;  // stable command name for the result topic, "run_zone"
    std::string error; // machine-readable reason when not accepted
    Command command{};
    ZoneIndex zone = kNoZone;
    uint16_t programId = 0;
    uint32_t value = 0;
    bool flag = false;
};

inline constexpr std::size_t kMaxCommandPayload = 256;
inline constexpr uint32_t kMaxRainDelayHours = 720; // same limit as the REST API (30 days)

// Parses a message received on `topic`. Retained and empty messages are
// never commands (see the rules in mqtt_logic.cpp and docs/firmware/mqtt.md). `cfg` bounds zone numbers, program
// ids and durations; `now` and `timeValid` are needed for a rain delay.
// Never throws, never allocates beyond the returned strings.
ParsedCommand parseCommand(const Topics &topics, std::string_view topic, std::string_view payload, bool retained,
                           const Config &cfg, UtcSeconds now, bool timeValid);

// Stopping one zone: the queue runs one entry at a time, so a zone that is
// open or waiting is the head entry, and skipping the head closes it. Same
// rule as POST /api/v1/zones/{n}/stop. Returns the error name or empty.
std::string resolveStopZone(ZoneIndex zone, const SystemState &s, Command &out);

// Result topic message. `zone` and `programId` are echoed when set.
std::string resultPayload(std::string_view command, Outcome outcome, std::string_view error, ZoneIndex zone,
                          uint16_t programId);
// For a CommandRejected event.
std::string rejectionPayload(const CommandRejectedData &r);

// Configuration edits from MQTT (run-time numbers, program switches),
// collected and written at most once per kConfigWriteIntervalMs: every
// write erases flash, and a slider dragged in Home Assistant sends many.
class PendingEdits {
public:
    static constexpr uint32_t kConfigWriteIntervalMs = 2000;

    void setRunTime(ZoneIndex z, uint32_t sec);
    void setProgramEnabled(uint16_t id, bool on);
    bool empty() const { return runMask_ == 0 && programCount_ == 0; }
    // Applies to `cfg`; returns true if anything changed. Clears the edits.
    bool applyTo(Config &cfg);

private:
    uint16_t runMask_ = 0;
    std::array<uint32_t, kMaxZones> runSec_{};
    struct ProgramEdit {
        uint16_t id;
        bool on;
    };
    std::array<ProgramEdit, kMaxPrograms> programs_{};
    std::size_t programCount_ = 0;
};

// Token bucket: `burst` commands at once, then `perSecond`. Protects the
// command queue (16 entries, drained 8 per 50 ms control tick) and the
// result topic from a flood; the control loop itself never waits on MQTT.
class RateLimiter {
public:
    RateLimiter(uint32_t burst = 10, uint32_t perSecond = 5) : burst_(burst), perSecond_(perSecond), tokens_(burst) {}
    bool allow(MonoMs now);

private:
    uint32_t burst_;
    uint32_t perSecond_;
    uint32_t tokens_;
    MonoMs last_ = 0;
    bool started_ = false;
};

// ---- Home Assistant discovery -------------------------------------------------------------

inline constexpr std::string_view kSupportUrl = "https://github.com/syntroniks/zoneflow";

// One device discovery message (topic Topics::discovery()). Components that
// are not wanted (disabled zones, zones above zone_count, the other zone
// entity type, sensors that are off, deleted programs) are removed by
// listing them with only their platform, as the Home Assistant MQTT
// documentation describes; the planner then sends the payload again
// without them.
class DiscoveryPlanner {
public:
    // Returns one or two payloads to publish in order on the discovery
    // topic. The first carries removal entries for components that are no
    // longer wanted and were not removed since boot.
    std::vector<std::string> plan(const Config &cfg, const Identity &id, const Topics &t);

    // Exposed for tests: the component id -> platform pairs a configuration
    // wants.
    struct Component {
        std::string id;
        const char *platform;
    };
    static std::vector<Component> wanted(const Config &cfg);

private:
    std::vector<Component> removedOrAbsent_; // removal already sent (since boot)
    std::vector<uint16_t> programsSeen_;     // program ids published since boot
};

// The full payload with optional removal entries. Exposed for tests.
std::string discoveryPayload(const Config &cfg, const Identity &id, const Topics &t,
                             const std::vector<DiscoveryPlanner::Component> &removals);

// ---- connection policy -----------------------------------------------------------------------

enum class ConnState : uint8_t {
    Disabled,       // mqtt.enabled is false
    WaitingNetwork, // no IP address yet
    Connecting,
    Connected,
    WaitingRetry, // backoff before the next attempt
};
const char *toString(ConnState s);

// Reconnect with exponential backoff and jitter (2 s doubling to 5 min,
// +-20 %), reset on every successful connection. esp-mqtt's own fixed-delay
// reconnect is turned off; the glue asks this policy when to try again.
class ConnectionPolicy {
public:
    ConnectionPolicy() : backoff_(2000, 5u * 60u * 1000u, 20) {}
    void onConnected();
    void onDisconnected(MonoMs now, uint32_t random);
    bool retryDue(MonoMs now) const { return waiting_ && now >= retryAt_; }
    void retryStarted() { waiting_ = false; }
    uint32_t attempts() const { return backoff_.attempts(); }
    uint32_t retryInMs(MonoMs now) const
    {
        return waiting_ && retryAt_ > now ? static_cast<uint32_t>(retryAt_ - now) : 0;
    }
    bool waiting() const { return waiting_; }
    void reset()
    {
        backoff_.reset();
        waiting_ = false;
    }

private:
    net::Backoff backoff_;
    MonoMs retryAt_ = 0;
    bool waiting_ = false;
};

// Delay before discovery after a Home Assistant birth message: 1 to 5 s,
// random, as the Home Assistant documentation recommends so that many
// devices do not answer at once.
uint32_t birthDelayMs(uint32_t random);

// ---- status (REST API) ---------------------------------------------------------------------------

struct MqttStatus {
    ConnState state = ConnState::Disabled;
    FixedString<63> broker; // "mqtt://host:port" or "mqtts://host:port"
    FixedString<63> root;   // topic root
    uint32_t reconnectAttempt = 0;
    uint32_t retryInMs = 0;
    uint32_t connects = 0; // since boot
    MonoMs connectedSinceMs = 0;
    FixedString<47> lastError;
    uint32_t published = 0;
    uint32_t publishFailed = 0;
    uint32_t commandsReceived = 0;
    uint32_t commandsRejected = 0; // invalid, retained, rate-limited or refused by the controller
    uint32_t eventsDropped = 0;    // event-bus messages lost because the module's queue was full
    bool discoveryPublished = false;
};

// Single writer (the zf_mqtt task), any number of readers.
class MqttStatusStore {
public:
    void publish(const MqttStatus &s)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = s;
    }
    MqttStatus snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

private:
    mutable std::mutex mutex_;
    MqttStatus status_{};
};

} // namespace zf::mqtt
