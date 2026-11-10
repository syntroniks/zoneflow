// SPDX-License-Identifier: Apache-2.0
// The REST route table (/api/v1). One table drives the HTTP dispatcher, the
// access checks and the body caps; test/host/web_openapi_test.cpp checks it
// against docs/firmware/openapi.json in both directions.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace zf::web {

inline constexpr std::string_view kApiPrefix = "/api/v1";

enum class RouteId : uint8_t {
    AuthStatus,
    AuthLogin,
    AuthSetup,
    AuthLogout,
    AuthPassword,
    SecurityGet,
    SecurityPut,
    Info,
    Status,
    Events,
    ZonesList,
    ZoneGet,
    ZonePatch,
    ZoneRun,
    ZoneStop,
    ProgramsList,
    ProgramCreate,
    ProgramGet,
    ProgramPut,
    ProgramDelete,
    ProgramRun,
    QueueGet,
    QueuePause,
    QueueResume,
    QueueSkip,
    QueueStop,
    RainDelayGet,
    RainDelayPut,
    RainDelayDelete,
    Sensors,
    FaultsList,
    FaultsClear,
    ConfigGet,
    ConfigPut,
    ConfigBackup,
    ConfigRestore,
    TimeGet,
    TimePut,
    NetworkGet,
    NetworkReprovision,
    ProvisionGet,
    ProvisionNetworks,
    ProvisionWifi,
    SystemReboot,
    SystemFactoryReset,
    FirmwareGet,
    FirmwareUpload,
    FirmwarePull,
    FirmwareAssets,
    MqttGet,
    MqttPut,
    HistoryGet,
    SetupPut,
    FaultsAcknowledgeRepair,
};

enum class Access : uint8_t {
    Public,       // no session needed (login, setup, auth status)
    Read,         // needs a session only when "protect reads" is on
    Admin,        // always needs a session; cookie sessions also need the CSRF header
    Provisioning, // only while SoftAP provisioning runs; the WPA2 setup code is the proof
};

enum class Body : uint8_t {
    None,   // empty or "{}" accepted
    Json,   // application/json, parsed after the size cap
    Binary, // application/octet-stream, streamed by the HTTP glue
};

enum class Response : uint8_t {
    Json,
    EventStream, // text/event-stream, handled by the HTTP glue
};

struct Route {
    std::string_view method;
    std::string_view pattern; // relative to /api/v1; "{name}" matches one segment
    RouteId id;
    Access access;
    Body body;
    uint32_t maxBody; // bytes; checked against Content-Length before reading
    Response response = Response::Json;
};

std::span<const Route> routes();

struct RouteMatch {
    const Route *route = nullptr;
    bool pathKnown = false; // some route has this path (405 if route is null)
    std::array<std::string_view, 2> params{};
    std::size_t paramCount = 0;
};

// `path` is the full request path without the query string.
RouteMatch matchRoute(std::string_view method, std::string_view path);

// "GET, POST" for the Allow header of a 405 answer.
std::string_view allowedMethods(std::string_view path, char *buf, std::size_t cap);

} // namespace zf::web
