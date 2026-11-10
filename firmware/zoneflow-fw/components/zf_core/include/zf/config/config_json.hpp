// SPDX-License-Identifier: Apache-2.0
// JSON form of the configuration: used for storage, backup/restore and the
// REST API. The layout is documented in docs/design/firmware-architecture.md.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "zf/config/config.hpp"
#include "zf/sched/scheduler.hpp"

struct cJSON;

namespace zf {

std::string configToJson(const Config &cfg, bool pretty = false);

enum class ConfigOrigin : uint8_t {
    Current,  // stored at the current schema version
    Migrated, // older schema, migrated in memory
    Newer,    // newer schema (firmware rollback): parsed best-effort
};

struct ConfigParseResult {
    ValidationResult validation;
    ConfigOrigin origin = ConfigOrigin::Current;
    int storedVersion = 0;
    bool ok() const { return validation.ok; }
};

// Parses, migrates and validates. Fields that are absent take their default
// value; unknown fields are ignored. On failure `out` is unspecified.
ConfigParseResult configFromJson(std::string_view json, Config &out);

// One migration step upgrades the document from `fromVersion` to
// `fromVersion + 1` in place.
struct ConfigMigration {
    int fromVersion;
    bool (*apply)(cJSON *root);
};

// Runs the steps needed to bring `root` to `targetVersion`. Exposed for tests;
// configFromJson uses the built-in table.
bool migrateConfigJson(cJSON *root, int targetVersion, const ConfigMigration *steps, std::size_t stepCount,
                       std::string &error);

// Runtime state persisted next to the configuration.
std::string persistToJson(const SchedulerPersist &state);
bool persistFromJson(std::string_view json, SchedulerPersist &out);

// "HH:MM" <-> minutes after midnight.
bool parseClockTime(std::string_view s, uint16_t &minutes);
std::string formatClockTime(uint16_t minutes);
// "YYYY-MM-DD" <-> day serial.
bool parseIsoDate(std::string_view s, int64_t &daySerial);
std::string formatIsoDate(int64_t daySerial);

} // namespace zf
