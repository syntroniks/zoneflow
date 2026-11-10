// SPDX-License-Identifier: Apache-2.0
// REST API logic (/api/v1), independent of the HTTP server. The device glue
// (web.cpp) turns an esp_http_server request into an ApiRequest and writes
// the ApiResponse back; host tests call Api::handle() directly.
//
// Every core interaction goes through zf::AppServices: state snapshots,
// commands, ConfigStore and TimeService. Platform facts (firmware, network,
// OTA) come through IPlatformPort.
//
// Threads: handle() and admit() may run on any task; AuthService and the
// settings use their own locks. Config writes block on flash; never call
// handle() from the control or keep-alive task.
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "zf/app/services.hpp"
#include "zf/time/clock.hpp"
#include "zf_mqtt/mqtt_logic.hpp"
#include "zf_net/net_logic.hpp"
#include "zf_ota/ota_logic.hpp"
#include "zf_web/auth.hpp"
#include "zf_web/command_watch.hpp"
#include "zf_web/routes.hpp"

namespace zf::web {

inline constexpr std::string_view kSessionCookie = "zf_session";
inline constexpr std::string_view kCsrfHeader = "X-CSRF-Token";

struct ApiRequest {
    std::string_view method;
    std::string_view path; // without query
    std::string_view query;
    std::string_view body;
    std::size_t contentLength = 0; // as announced; streamed routes have no body here
    std::string_view host;
    std::string_view origin;
    std::string_view cookie;
    std::string_view authorization;
    std::string_view csrf; // X-CSRF-Token
    std::string_view contentType;
    std::string_view peer; // client address, for rate limiting
    bool tls = false;
};

// Work the HTTP glue does after it has sent the response.
enum class PostAction : uint8_t { None, Reboot, FactoryReset, Reprovision };

struct ApiResponse {
    int status = 200;
    std::string body;
    std::string contentType = "application/json";
    std::vector<std::pair<std::string, std::string>> headers;
    PostAction action = PostAction::None;
    net::ProvTransport transport = net::ProvTransport::None; // Reprovision
};

// Settings of the web server itself (NVS key "web"; not part of the config
// backup).
struct WebSettings {
    bool protectReads = false; // GET routes need a session too
    bool https = false;        // serve on 443 with the self-signed certificate; port 80 redirects
    bool checkHost = true;     // DNS-rebinding guard (hostAllowed)

    std::string encode() const;
    static WebSettings decode(std::string_view json);
};

struct FirmwareInfo {
    std::string version;
    std::string project;
    std::string idfVersion;
    std::string buildDate; // "Nov 10 2026 12:00:00"
    std::string board;
};

struct SystemInfo {
    uint64_t uptimeMs = 0;
    uint32_t freeHeap = 0;
    uint32_t minFreeHeap = 0;
    uint32_t largestFreeBlock = 0;
    uint32_t internalFree = 0;     // internal RAM only (PSRAM excluded)
    uint32_t internalMinFree = 0;  // low-water mark of internal RAM
    uint32_t httpStackFreeMin = 0; // bytes never used on the HTTP task stack
    std::string resetReason;
};

struct SlotInfo {
    std::string running;      // partition label, "ota_0"
    std::string next;         // where the next update goes
    std::string runningState; // "valid", "pending_verify", "undefined", ...
    std::string otherVersion; // version in the other slot, empty if none
};

struct MqttSecretsInfo {
    bool passwordSet = false;
    bool caCertSet = false;
};

struct WifiNetwork {
    std::string ssid;
    int8_t rssi = 0;
    bool secure = true;
};

class IPlatformPort {
public:
    virtual ~IPlatformPort() = default;
    virtual FirmwareInfo firmware() const = 0;
    virtual SystemInfo system() const = 0;
    virtual net::NetStatus network() const = 0;
    // Setup code shown on the display and the serial log. Never returned by the API.
    virtual std::string setupCode() const = 0;
    virtual std::vector<WifiNetwork> scan() = 0;
    // SoftAP provisioning only: hand credentials to the provisioning manager.
    virtual bool provisionWifi(std::string_view ssid, std::string_view password) = 0;
    virtual ota::OtaStatus otaStatus() const = 0;
    virtual SlotInfo slots() const = 0;
    virtual ota::OtaError startPull(std::string_view url, bool force) = 0;
    // SHA-256 of the TLS certificate (hex), empty when none exists yet.
    virtual std::string tlsFingerprint() const = 0;
    // Blocks the calling (API) task; used while waiting for a command outcome.
    virtual void sleepMs(uint32_t ms) = 0;

    // MQTT module (zf_mqtt). The defaults describe a build without it.
    virtual mqtt::MqttStatus mqttStatus() const { return {}; }
    virtual MqttSecretsInfo mqttSecrets() const { return {}; }
    // nullopt leaves a value unchanged, an empty string removes it.
    virtual bool setMqttSecrets(std::optional<std::string_view> password, std::optional<std::string_view> caPem)
    {
        (void)password;
        (void)caPem;
        return false;
    }
};

struct Admission {
    bool ok = false;
    ApiResponse error; // when !ok
    RouteMatch match;
    Principal principal;
};

class Api {
public:
    static constexpr const char *kSettingsKey = "web";
    // How long a run request waits for the control task to accept or reject
    // it. The control loop drains commands every 50 ms.
    static constexpr uint32_t kConfirmWaitMs = 250;
    static constexpr uint32_t kConfirmPollMs = 10;
    // One control period (CONFIG_ZF_CONTROL_PERIOD_MS, 50 ms) plus margin.
    static constexpr uint32_t kControlSettleMs = 60;

    Api(const AppServices &services, AuthService &auth, IPlatformPort &platform, IBlobStorage &settingsStore,
        const IClock &clock);
    ~Api();
    Api(const Api &) = delete;
    Api &operator=(const Api &) = delete;

    // Loads the web settings; call once before serving.
    void begin();

    // Host check, routing, access, CSRF, content type and size. The glue
    // calls this before reading a streamed body or opening the event stream.
    Admission admit(const ApiRequest &req);

    // Admits, then runs the handler. Streamed routes (firmware upload, asset
    // upload, events) are answered with 500 here: the glue owns them.
    ApiResponse handle(const ApiRequest &req);

    WebSettings settings() const;

    // Helpers shared with the glue.
    static ApiResponse error(int status, std::string_view code, std::string_view message, std::string_view path = {});
    static ApiResponse json(int status, std::string body);

private:
    ApiResponse dispatch(const ApiRequest &req, const Admission &adm);

    ApiResponse authStatus(const ApiRequest &req);
    ApiResponse login(const ApiRequest &req);
    ApiResponse setup(const ApiRequest &req);
    ApiResponse logout(const ApiRequest &req);
    ApiResponse changePassword(const ApiRequest &req);
    ApiResponse securityGet();
    ApiResponse securityPut(const ApiRequest &req);
    ApiResponse info();
    ApiResponse status();
    ApiResponse zonesList();
    ApiResponse zoneGet(std::string_view id);
    ApiResponse zonePatch(std::string_view id, const ApiRequest &req);
    ApiResponse zoneRun(std::string_view id, const ApiRequest &req);
    ApiResponse zoneStop(std::string_view id);
    ApiResponse programsList();
    ApiResponse programGet(std::string_view id);
    ApiResponse programWrite(std::string_view id, const ApiRequest &req, bool create);
    ApiResponse programDelete(std::string_view id);
    ApiResponse programRun(std::string_view id);
    ApiResponse queueGet();
    ApiResponse queueCommand(CommandType type);
    ApiResponse rainDelayGet();
    ApiResponse rainDelayPut(const ApiRequest &req);
    ApiResponse rainDelayDelete();
    ApiResponse sensors();
    ApiResponse faultsList();
    ApiResponse configGet();
    ApiResponse configPut(const ApiRequest &req);
    ApiResponse configBackup();
    ApiResponse configRestore(const ApiRequest &req);
    ApiResponse timeGet();
    ApiResponse timePut(const ApiRequest &req);
    ApiResponse networkGet();
    ApiResponse networkReprovision(const ApiRequest &req);
    ApiResponse provisionGet();
    ApiResponse provisionNetworks();
    ApiResponse provisionWifi(const ApiRequest &req);
    ApiResponse systemReboot();
    ApiResponse systemFactoryReset(const ApiRequest &req);
    ApiResponse firmwareGet();
    ApiResponse firmwarePull(const ApiRequest &req);
    ApiResponse mqttGet();
    ApiResponse mqttPut(const ApiRequest &req);

    ApiResponse post(const Command &cmd);
    // Posts a command that the controller may reject (RunZone, RunProgram)
    // and waits up to kConfirmWaitMs for the outcome. See postConfirmed() in
    // api.cpp for the rule.
    ApiResponse postConfirmed(const Command &cmd);
    // Writes a whole configuration document (JSON text) and reloads.
    ApiResponse applyConfigJson(const std::string &json, int okStatus, std::string okBody);
    ApiResponse sessionResponse(int status, const AuthOutcome &o, bool tls);

    AppServices services_;
    AuthService &auth_;
    IPlatformPort &platform_;
    IBlobStorage &settingsStore_;
    const IClock &clock_;
    mutable std::mutex settingsMutex_;
    WebSettings settings_{};
    CommandWatch watch_;
    EventBus::Handle watchHandle_ = EventBus::kInvalidHandle;
    std::mutex confirmMutex_; // one armed watch at a time; never taken by the control task
};

} // namespace zf::web
