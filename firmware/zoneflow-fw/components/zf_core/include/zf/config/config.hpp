// SPDX-License-Identifier: Apache-2.0
// Typed user configuration. Board facts (pins, ADC scaling) are not here;
// they live in the board header.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "zf/diag/current_monitor.hpp"
#include "zf/diag/sensors.hpp"
#include "zf/fixed_string.hpp"
#include "zf/sched/program.hpp"
#include "zf/types.hpp"

namespace zf {

// Bump when the JSON layout changes, and add a migration step in
// config_json.cpp (kMigrations).
inline constexpr int kConfigSchemaVersion = 1;

struct ZoneConfig {
    FixedString<23> name;
    bool enabled = true;
    uint32_t maxRunSec = 3600;
    CurrentWindow current{80, 700};
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
};

enum class SpareFunction : uint8_t {
    None, // reported only
    InhibitLikeRain,
};

struct DiagSettings {
    bool currentEnabled = true;
    uint32_t settleMs = 500;
    uint16_t idleMaxMa = 60;
    uint16_t shortCircuitMa = 3000;
    uint8_t confirmSamples = 3;
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

} // namespace zf
