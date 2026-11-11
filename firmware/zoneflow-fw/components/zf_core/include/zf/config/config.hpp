// SPDX-License-Identifier: Apache-2.0
// Typed user configuration. Board facts (pins, ADC scaling) are not here;
// they live in the board header.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "zf/diag/current_monitor.hpp"
#include "zf/diag/sensors.hpp"
#include "zf/fixed_string.hpp"
#include "zf/sched/program.hpp"
#include "zf/types.hpp"

namespace zf {

// Bump when the JSON layout changes, and add a migration step in
// config_json.cpp (kMigrations).
// Schema 2 added the "mqtt" object and zones[].default_run_s.
inline constexpr int kConfigSchemaVersion = 2;

struct ZoneConfig {
    FixedString<23> name;
    bool enabled = true;
    uint32_t maxRunSec = 3600;
    CurrentWindow current{80, 700};
    // Duration of a run started without one (Home Assistant valve "open",
    // MQTT run with an empty payload). The zone driver still clamps it.
    uint32_t defaultRunSec = 600;
};

struct MasterConfig {
    bool enabled = false;
    uint32_t preOpenMs = 1000;
    uint32_t postCloseMs = 2000;
    CurrentWindow current{80, 700};
    // Consecutive short-circuit or over-current faults attributed to the
    // master output that lock it out (and with it every zone) until
    // ClearFaults.
    uint8_t faultLockoutCount = 3;
};

struct DriverSettings {
    uint8_t maxConcurrent = kMaxConcurrentZonesLimit;
    uint32_t interZoneDelayMs = 2000;
    // Closed time a zone needs after reaching its maximum run time, and the
    // closed time that ends a run of back-to-back runs (spec section 2 rule 4).
    uint32_t minRestSec = 1800;
    // Turn outputs on at a mains zero crossing when the board has the
    // signal; ignored without it.
    bool zeroCrossSwitching = false;
};

enum class SpareFunction : uint8_t {
    None, // reported only
    InhibitLikeRain,
};

struct DiagSettings {
    bool currentEnabled = true;
    uint32_t settleMs = 500;
    uint16_t idleMaxMa = kDefaultIdleMaxMa; // hardware design 19.2
    uint16_t shortCircuitMa = 3000;
    uint8_t confirmSamples = 3;
};

// Which Home Assistant entity represents a zone.
enum class HaZoneEntity : uint8_t {
    Valve,  // valve entity (open/close/stop); Home Assistant 2024.1 and newer
    Switch, // switch entity, for dashboards and automations built on switches
};

enum class MqttCaSource : uint8_t {
    Bundle, // ESP-IDF certificate bundle (public CAs)
    Custom, // a CA certificate stored with PUT /api/v1/mqtt (NVS, not in the config)
};

// MQTT client settings. The password and a custom CA certificate are not
// here: they live in NVS namespace "zf_mqtt", so the configuration document
// (REST, backup) never carries them.
struct MqttConfig {
    bool enabled = false;
    FixedString<63> host; // name or IP address, no scheme
    uint16_t port = 1883; // 8883 is usual with TLS
    FixedString<63> username;
    bool tls = false;
    MqttCaSource ca = MqttCaSource::Bundle;
    FixedString<63> clientId;  // empty: "zoneflow-<device id>"
    FixedString<63> baseTopic; // empty: "zoneflow/<device id>"
    bool discovery = true;     // Home Assistant discovery
    FixedString<31> discoveryPrefix{"homeassistant"};
    HaZoneEntity zoneEntity = HaZoneEntity::Valve;
};

struct Config {
    int schemaVersion = kConfigSchemaVersion;
    FixedString<31> deviceName{"zoneflow"};
    FixedString<63> timezone{"UTC0"}; // POSIX TZ string
    uint8_t zoneCount = kMaxZones;
    std::array<ZoneConfig, kMaxZones> zones{};
    MasterConfig master{};
    DriverSettings driver{};
    ContactInputConfig rain{false, ContactType::NormallyClosed, 2000};
    ContactInputConfig spare{false, ContactType::NormallyOpen, 200};
    SpareFunction spareFunction = SpareFunction::None;
    FlowConfig flow{};
    DiagSettings diag{};
    ScheduleConfig schedule{};
    MqttConfig mqtt{};

    static Config defaults();
};

struct ValidationResult {
    bool ok = true;
    std::string path; // JSON path of the offending field, e.g. "zones[2].max_run_s"
    std::string message;

    static ValidationResult good() { return {}; }
    static ValidationResult bad(std::string p, std::string m) { return {false, std::move(p), std::move(m)}; }
};

ValidationResult validate(const Config &cfg);

// An MQTT topic prefix that may be used to publish and subscribe: 1 to 63
// bytes, no wildcards ('+', '#'), no NUL or control characters, no empty
// level, no leading or trailing '/'.
bool validTopicPrefix(std::string_view topic);

} // namespace zf
