// SPDX-License-Identifier: Apache-2.0
// JSON forms of the core state, events, network and OTA status, shared by the
// REST handlers and the event stream. Zone numbers in the API are 1-based,
// as printed on the terminal block (Z1 = core zone index 0).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "zf/config/config.hpp"
#include "zf/events/event.hpp"
#include "zf/events/state.hpp"
#include "zf/history/history.hpp"
#include "zf_net/net_logic.hpp"
#include "zf_ota/ota_logic.hpp"

struct cJSON;

namespace zf::web {

struct JsonDeleter {
    void operator()(cJSON *p) const;
};
using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

// Compact text; empty string if printing failed (out of memory).
std::string printJson(const cJSON *json);

// cJSON parses recursively; the request is rejected before parsing when it
// nests deeper than this, or closes a bracket it never opened. Same limit as
// the configuration parser.
inline constexpr int kMaxJsonDepth = 16;
bool jsonDepthOk(std::string_view text, int maxDepth = kMaxJsonDepth);
// Parses after the depth check; null on error.
JsonPtr parseJson(std::string_view text);

cJSON *zoneToJson(std::size_t index, const ZoneStatus &status, const ZoneConfig &config);
// `pauseTimeoutMin`: schedule.pause_timeout_min, after which a paused queue stops.
cJSON *queueToJson(const QueueStatus &q, uint16_t pauseTimeoutMin);
// Sensor readings plus the current-sense and over-current-trip conditions.
cJSON *sensorsToJson(const SystemState &s);
cJSON *faultsToJson(const SystemState &s);
cJSON *rainDelayToJson(const SystemState &s);
// Everything a UI needs for its main screen. `cfg` supplies zone names.
cJSON *stateToJson(const SystemState &s, const Config &cfg);
cJSON *eventToJson(const Event &e);
// GET /history body. capacity: records the log holds at most.
cJSON *historyPageToJson(const HistoryPage &page, std::size_t capacity);
cJSON *historyRecordToJson(uint64_t id, const HistoryRecord &r);
cJSON *netStatusToJson(const net::NetStatus &n);
cJSON *otaStatusToJson(const ota::OtaStatus &o);

// One Server-Sent Events message: "id: <id>\nevent: <name>\ndata: <json>\n\n".
// `json` must be a single line (printJson output is).
std::string sseMessage(std::string_view event, uint32_t id, std::string_view json);

const char *sensorName(SensorId id);

} // namespace zf::web
