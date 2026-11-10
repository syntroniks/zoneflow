// SPDX-License-Identifier: Apache-2.0
#include "zf_web/routes.hpp"

#include <cstring>

namespace zf::web {
namespace {

// Body caps. A program is about 300 bytes of JSON; a full configuration with
// 16 programs and 8 zones is about 7 KB, so 16 KB leaves room for whitespace.
constexpr uint32_t kSmall = 512;
constexpr uint32_t kProgram = 2048;
constexpr uint32_t kConfig = 16384;
constexpr uint32_t kEmpty = 64;
// MQTT settings: about 400 bytes of fields plus an optional PEM CA
// certificate (a 4096-bit RSA CA is about 2 KB; chains up to 6 KB accepted).
constexpr uint32_t kMqtt = 8192;
// Slot sizes from partitions.csv (ota_0/ota_1 0x400000, www 0x7c0000). The
// HTTP glue also checks the real partition size.
constexpr uint32_t kAppSlot = 0x400000;
constexpr uint32_t kAssets = 0x7c0000;

using enum RouteId;
constexpr Access P = Access::Public;
constexpr Access R = Access::Read;
constexpr Access A = Access::Admin;
constexpr Access V = Access::Provisioning;

constexpr std::array kRoutes = {
    Route{"GET", "/auth", AuthStatus, P, Body::None, 0},
    Route{"POST", "/auth/login", AuthLogin, P, Body::Json, kSmall},
    Route{"POST", "/auth/setup", AuthSetup, P, Body::Json, kSmall},
    Route{"POST", "/auth/logout", AuthLogout, A, Body::None, kEmpty},
    Route{"POST", "/auth/password", AuthPassword, A, Body::Json, kSmall},
    Route{"GET", "/security", SecurityGet, R, Body::None, 0},
    Route{"PUT", "/security", SecurityPut, A, Body::Json, kSmall},
    Route{"GET", "/info", Info, R, Body::None, 0},
    Route{"GET", "/status", Status, R, Body::None, 0},
    Route{"GET", "/events", Events, R, Body::None, 0, Response::EventStream},
    Route{"GET", "/zones", ZonesList, R, Body::None, 0},
    Route{"GET", "/zones/{zone}", ZoneGet, R, Body::None, 0},
    Route{"PATCH", "/zones/{zone}", ZonePatch, A, Body::Json, kSmall},
    Route{"POST", "/zones/{zone}/run", ZoneRun, A, Body::Json, kSmall},
    Route{"POST", "/zones/{zone}/stop", ZoneStop, A, Body::None, kEmpty},
    Route{"GET", "/programs", ProgramsList, R, Body::None, 0},
    Route{"POST", "/programs", ProgramCreate, A, Body::Json, kProgram},
    Route{"GET", "/programs/{id}", ProgramGet, R, Body::None, 0},
    Route{"PUT", "/programs/{id}", ProgramPut, A, Body::Json, kProgram},
    Route{"DELETE", "/programs/{id}", ProgramDelete, A, Body::None, kEmpty},
    Route{"POST", "/programs/{id}/run", ProgramRun, A, Body::None, kEmpty},
    Route{"GET", "/queue", QueueGet, R, Body::None, 0},
    Route{"POST", "/queue/pause", QueuePause, A, Body::None, kEmpty},
    Route{"POST", "/queue/resume", QueueResume, A, Body::None, kEmpty},
    Route{"POST", "/queue/skip", QueueSkip, A, Body::None, kEmpty},
    Route{"POST", "/queue/stop", QueueStop, A, Body::None, kEmpty},
    Route{"GET", "/rain-delay", RainDelayGet, R, Body::None, 0},
    Route{"PUT", "/rain-delay", RainDelayPut, A, Body::Json, kSmall},
    Route{"DELETE", "/rain-delay", RainDelayDelete, A, Body::None, kEmpty},
    Route{"GET", "/sensors", Sensors, R, Body::None, 0},
    Route{"GET", "/faults", FaultsList, R, Body::None, 0},
    Route{"POST", "/faults/clear", FaultsClear, A, Body::None, kEmpty},
    Route{"POST", "/faults/acknowledge-repair", FaultsAcknowledgeRepair, A, Body::Json, kSmall},
    Route{"GET", "/config", ConfigGet, R, Body::None, 0},
    Route{"PUT", "/config", ConfigPut, A, Body::Json, kConfig},
    Route{"GET", "/config/backup", ConfigBackup, R, Body::None, 0},
    Route{"POST", "/config/restore", ConfigRestore, A, Body::Json, kConfig},
    Route{"GET", "/time", TimeGet, R, Body::None, 0},
    Route{"PUT", "/time", TimePut, A, Body::Json, kSmall},
    Route{"GET", "/network", NetworkGet, R, Body::None, 0},
    Route{"POST", "/network/reprovision", NetworkReprovision, A, Body::Json, kSmall},
    Route{"GET", "/provision", ProvisionGet, P, Body::None, 0},
    Route{"GET", "/provision/networks", ProvisionNetworks, V, Body::None, 0},
    Route{"POST", "/provision/wifi", ProvisionWifi, V, Body::Json, kSmall},
    Route{"POST", "/system/reboot", SystemReboot, A, Body::None, kEmpty},
    Route{"POST", "/system/factory-reset", SystemFactoryReset, A, Body::Json, kSmall},
    Route{"GET", "/firmware", FirmwareGet, R, Body::None, 0},
    Route{"POST", "/firmware/upload", FirmwareUpload, A, Body::Binary, kAppSlot},
    Route{"POST", "/firmware/pull", FirmwarePull, A, Body::Json, kSmall},
    Route{"POST", "/firmware/assets", FirmwareAssets, A, Body::Binary, kAssets},
    Route{"GET", "/mqtt", MqttGet, R, Body::None, 0},
    Route{"PUT", "/mqtt", MqttPut, A, Body::Json, kMqtt},
    Route{"GET", "/history", HistoryGet, R, Body::None, 0},
    Route{"PUT", "/setup", SetupPut, A, Body::Json, kEmpty},
};

// Matches one pattern; fills params.
bool matchPattern(std::string_view pattern, std::string_view path, RouteMatch &m)
{
    std::size_t count = 0;
    while (!pattern.empty() || !path.empty()) {
        if (pattern.empty() || path.empty() || pattern[0] != '/' || path[0] != '/') {
            return false;
        }
        pattern.remove_prefix(1);
        path.remove_prefix(1);
        const std::size_t pe = pattern.find('/');
        const std::size_t qe = path.find('/');
        const std::string_view ps = pattern.substr(0, pe);
        const std::string_view qs = path.substr(0, qe);
        if (ps.size() > 2 && ps.front() == '{' && ps.back() == '}') {
            if (qs.empty() || count == m.params.size()) {
                return false;
            }
            m.params[count++] = qs;
        } else if (ps != qs) {
            return false;
        }
        pattern = pe == std::string_view::npos ? std::string_view{} : pattern.substr(pe);
        path = qe == std::string_view::npos ? std::string_view{} : path.substr(qe);
    }
    m.paramCount = count;
    return true;
}

} // namespace

std::span<const Route> routes() { return kRoutes; }

RouteMatch matchRoute(std::string_view method, std::string_view path)
{
    RouteMatch m;
    if (path.substr(0, kApiPrefix.size()) != kApiPrefix) {
        return m;
    }
    path.remove_prefix(kApiPrefix.size());
    if (path.size() > 1 && path.back() == '/') {
        path.remove_suffix(1);
    }
    for (const Route &r : kRoutes) {
        RouteMatch candidate;
        if (!matchPattern(r.pattern, path, candidate)) {
            continue;
        }
        m.pathKnown = true;
        if (r.method == method) {
            candidate.route = &r;
            candidate.pathKnown = true;
            return candidate;
        }
    }
    return m;
}

std::string_view allowedMethods(std::string_view path, char *buf, std::size_t cap)
{
    std::size_t len = 0;
    if (path.substr(0, kApiPrefix.size()) == kApiPrefix) {
        path.remove_prefix(kApiPrefix.size());
    }
    for (const Route &r : kRoutes) {
        RouteMatch m;
        if (!matchPattern(r.pattern, path, m)) {
            continue;
        }
        const std::size_t need = r.method.size() + (len > 0 ? 2 : 0);
        if (len + need >= cap) {
            break;
        }
        if (len > 0) {
            std::memcpy(buf + len, ", ", 2);
            len += 2;
        }
        std::memcpy(buf + len, r.method.data(), r.method.size());
        len += r.method.size();
    }
    return {buf, len};
}

} // namespace zf::web
