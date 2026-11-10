// SPDX-License-Identifier: Apache-2.0
#include "zf_web/api.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "cJSON.h"
#include "zf/time/time_zone.hpp"
#include "zf_web/http_util.hpp"
#include "zf_web/json_map.hpp"

namespace zf::web {
namespace {

constexpr int64_t kMaxRainDelaySec = 30 * 86400;

bool isWrite(std::string_view method) { return method != "GET" && method != "HEAD"; }

// Positive decimal without sign or leading zeros beyond "0".
bool parseUint(std::string_view s, uint32_t max, uint32_t &out)
{
    if (s.empty() || s.size() > 10) {
        return false;
    }
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    if (v > max) {
        return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

// Integer-valued JSON number within [min, max].
bool jsonInt(const cJSON *item, double min, double max, int64_t &out)
{
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    const double v = item->valuedouble;
    if (!std::isfinite(v) || v != std::floor(v) || v < min || v > max) {
        return false;
    }
    out = static_cast<int64_t>(v);
    return true;
}

const char *jsonString(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

// Body that must be a JSON object. Empty body counts as {} when allowed.
JsonPtr parseObject(std::string_view body, bool emptyOk, ApiResponse &err)
{
    if (body.empty() && emptyOk) {
        return JsonPtr(cJSON_CreateObject());
    }
    if (!jsonDepthOk(body)) {
        err = Api::error(400, "invalid_json", "nested more than 16 levels");
        return nullptr;
    }
    JsonPtr doc = parseJson(body);
    if (!doc || !cJSON_IsObject(doc.get())) {
        err = Api::error(400, "invalid_json", "body must be a JSON object");
        return nullptr;
    }
    return doc;
}

std::string bodyOf(cJSON *o)
{
    JsonPtr holder(o);
    return printJson(holder.get());
}

// Strips "prefix." or "prefix" from a validation path so it is relative to
// the request body.
std::string relativePath(const std::string &path, const std::string &prefix)
{
    if (path.rfind(prefix, 0) != 0) {
        return path;
    }
    std::string rest = path.substr(prefix.size());
    if (!rest.empty() && rest.front() == '.') {
        rest.erase(0, 1);
    }
    return rest;
}

cJSON *findProgram(cJSON *programs, uint32_t id, int &index)
{
    index = 0;
    cJSON *p = nullptr;
    cJSON_ArrayForEach(p, programs)
    {
        const cJSON *pid = cJSON_GetObjectItemCaseSensitive(p, "id");
        if (cJSON_IsNumber(pid) && static_cast<uint32_t>(pid->valuedouble) == id) {
            return p;
        }
        ++index;
    }
    index = -1;
    return nullptr;
}

cJSON *schedulePrograms(cJSON *doc)
{
    cJSON *schedule = cJSON_GetObjectItemCaseSensitive(doc, "schedule");
    return cJSON_GetObjectItemCaseSensitive(schedule, "programs");
}

void setItem(cJSON *obj, const char *key, cJSON *value)
{
    if (cJSON_HasObjectItem(obj, key)) {
        cJSON_ReplaceItemInObjectCaseSensitive(obj, key, value);
    } else {
        cJSON_AddItemToObject(obj, key, value);
    }
}

std::string cookieHeader(std::string_view token, int64_t maxAgeS, bool tls)
{
    std::string c = std::string(kSessionCookie) + "=" + std::string(token) +
                    "; Path=/; HttpOnly; SameSite=Strict; Max-Age=" + std::to_string(maxAgeS);
    if (tls) {
        c += "; Secure";
    }
    return c;
}

} // namespace

// ---- settings ---------------------------------------------------------------------

std::string WebSettings::encode() const
{
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddBoolToObject(o.get(), "protect_reads", protectReads);
    cJSON_AddBoolToObject(o.get(), "https", https);
    cJSON_AddBoolToObject(o.get(), "check_host", checkHost);
    return printJson(o.get());
}

WebSettings WebSettings::decode(std::string_view json)
{
    WebSettings s;
    JsonPtr o = parseJson(json);
    if (!o) {
        return s;
    }
    auto flag = [&](const char *key, bool &dst) {
        const cJSON *item = cJSON_GetObjectItemCaseSensitive(o.get(), key);
        if (cJSON_IsBool(item)) {
            dst = cJSON_IsTrue(item);
        }
    };
    flag("protect_reads", s.protectReads);
    flag("https", s.https);
    flag("check_host", s.checkHost);
    return s;
}

// ---- framework -------------------------------------------------------------------------

Api::Api(const AppServices &services, AuthService &auth, IPlatformPort &platform, IBlobStorage &settingsStore,
         const IClock &clock)
    : services_(services), auth_(auth), platform_(platform), settingsStore_(settingsStore), clock_(clock)
{
}

Api::~Api()
{
    if (watchHandle_ != EventBus::kInvalidHandle) {
        services_.events.unsubscribe(watchHandle_);
    }
}

void Api::begin()
{
    if (watchHandle_ == EventBus::kInvalidHandle) {
        watchHandle_ = watch_.attach(services_.events);
    }
    std::string stored;
    WebSettings s;
    if (settingsStore_.read(kSettingsKey, stored)) {
        s = WebSettings::decode(stored);
    }
    std::lock_guard<std::mutex> lock(settingsMutex_);
    settings_ = s;
}

WebSettings Api::settings() const
{
    std::lock_guard<std::mutex> lock(settingsMutex_);
    return settings_;
}

bool Api::streamMayContinue(std::string_view cookie, std::string_view authorization) const
{
    if (!settings().protectReads) {
        return true;
    }
    return auth_.sessionValid(cookieValue(cookie, kSessionCookie), bearerToken(authorization), clock_.monoMs());
}

ApiResponse Api::error(int status, std::string_view code, std::string_view message, std::string_view path)
{
    JsonPtr o(cJSON_CreateObject());
    cJSON *e = cJSON_AddObjectToObject(o.get(), "error");
    cJSON_AddStringToObject(e, "code", std::string(code).c_str());
    cJSON_AddStringToObject(e, "message", std::string(message).c_str());
    if (!path.empty()) {
        cJSON_AddStringToObject(e, "path", std::string(path).c_str());
    }
    return json(status, printJson(o.get()));
}

ApiResponse Api::json(int status, std::string body)
{
    ApiResponse r;
    r.status = status;
    r.body = std::move(body);
    return r;
}

Admission Api::admit(const ApiRequest &req)
{
    Admission a;
    const WebSettings s = settings();
    const net::NetStatus net = platform_.network();
    if (s.checkHost && !hostAllowed(req.host, net.hostname.view())) {
        a.error = error(403, "bad_host", "Host header is not this device (DNS rebinding guard)");
        return a;
    }
    a.match = matchRoute(req.method, req.path);
    if (a.match.route == nullptr) {
        if (a.match.pathKnown) {
            char buf[64];
            a.error = error(405, "method_not_allowed", "method not allowed on this path");
            a.error.headers.emplace_back("Allow", std::string(allowedMethods(req.path, buf, sizeof buf)));
        } else {
            a.error = error(404, "not_found", "no such API route");
        }
        return a;
    }
    const Route &route = *a.match.route;
    const bool write = isWrite(req.method);
    if (write && !originMatchesHost(req.origin, req.host)) {
        a.error = error(403, "bad_origin", "cross-origin request refused");
        return a;
    }

    a.principal =
        auth_.authenticate(cookieValue(req.cookie, kSessionCookie), bearerToken(req.authorization), clock_.monoMs());
    const bool needSession = route.access == Access::Admin || (route.access == Access::Read && s.protectReads);
    if (needSession && !a.principal.authenticated) {
        if (!auth_.passwordSet()) {
            a.error = error(401, "password_not_set", "set the admin password first (POST /api/v1/auth/setup)");
        } else {
            a.error = error(401, "unauthorized", "log in first (POST /api/v1/auth/login)");
        }
        a.error.headers.emplace_back("WWW-Authenticate", "Bearer realm=\"zoneflow\"");
        return a;
    }
    if (write && a.principal.authenticated && a.principal.viaCookie && !constantTimeEqual(req.csrf, a.principal.csrf)) {
        a.error = error(403, "csrf", "missing or wrong X-CSRF-Token header");
        return a;
    }
    if (route.access == Access::Provisioning && net.provisioning != net::ProvTransport::SoftAp) {
        a.error = error(409, "not_provisioning", "only available while the setup access point runs");
        return a;
    }
    // The station joins the home network before SoftAP provisioning ends,
    // so the state alone does not prove the client is on the setup AP.
    if (route.access == Access::Provisioning && !req.viaSetupAp) {
        a.error = error(403, "setup_ap_only", "only from a device joined to the setup access point");
        return a;
    }

    const std::size_t length = std::max(req.contentLength, req.body.size());
    if (length > route.maxBody) {
        a.error = error(413, "too_large", "request body exceeds " + std::to_string(route.maxBody) + " bytes");
        return a;
    }
    if (length > 0 || route.body == Body::Binary) {
        const std::string_view want = route.body == Body::Binary ? "application/octet-stream" : "application/json";
        if (!mediaTypeIs(req.contentType, want)) {
            a.error = error(415, "unsupported_media_type", "Content-Type must be " + std::string(want));
            return a;
        }
    }
    a.ok = true;
    return a;
}

ApiResponse Api::handle(const ApiRequest &req)
{
    const Admission a = admit(req);
    if (!a.ok) {
        return a.error;
    }
    return dispatch(req, a);
}

ApiResponse Api::dispatch(const ApiRequest &req, const Admission &a)
{
    const std::string_view p0 = a.match.paramCount > 0 ? a.match.params[0] : std::string_view{};
    switch (a.match.route->id) {
    case RouteId::AuthStatus:
        return authStatus(req);
    case RouteId::AuthLogin:
        return login(req);
    case RouteId::AuthSetup:
        return setup(req);
    case RouteId::AuthLogout:
        return logout(req);
    case RouteId::AuthPassword:
        return changePassword(req);
    case RouteId::SecurityGet:
        return securityGet();
    case RouteId::SecurityPut:
        return securityPut(req);
    case RouteId::Info:
        return info();
    case RouteId::Status:
        return status();
    case RouteId::ZonesList:
        return zonesList();
    case RouteId::ZoneGet:
        return zoneGet(p0);
    case RouteId::ZonePatch:
        return zonePatch(p0, req);
    case RouteId::ZoneRun:
        return zoneRun(p0, req);
    case RouteId::ZoneStop:
        return zoneStop(p0);
    case RouteId::ProgramsList:
        return programsList();
    case RouteId::ProgramCreate:
        return programWrite({}, req, true);
    case RouteId::ProgramGet:
        return programGet(p0);
    case RouteId::ProgramPut:
        return programWrite(p0, req, false);
    case RouteId::ProgramDelete:
        return programDelete(p0);
    case RouteId::ProgramRun:
        return programRun(p0);
    case RouteId::QueueGet:
        return queueGet();
    case RouteId::QueuePause:
        return queueCommand(CommandType::Pause);
    case RouteId::QueueResume:
        return queueCommand(CommandType::Resume);
    case RouteId::QueueSkip:
        return queueCommand(CommandType::Skip);
    case RouteId::QueueStop:
        return queueCommand(CommandType::StopAll);
    case RouteId::RainDelayGet:
        return rainDelayGet();
    case RouteId::RainDelayPut:
        return rainDelayPut(req);
    case RouteId::RainDelayDelete:
        return rainDelayDelete();
    case RouteId::Sensors:
        return sensors();
    case RouteId::FaultsList:
        return faultsList();
    case RouteId::FaultsClear:
        return queueCommand(CommandType::ClearFaults);
    case RouteId::ConfigGet:
        return configGet();
    case RouteId::ConfigPut:
        return configPut(req);
    case RouteId::ConfigBackup:
        return configBackup();
    case RouteId::ConfigRestore:
        return configRestore(req);
    case RouteId::TimeGet:
        return timeGet();
    case RouteId::TimePut:
        return timePut(req);
    case RouteId::NetworkGet:
        return networkGet();
    case RouteId::NetworkReprovision:
        return networkReprovision(req);
    case RouteId::ProvisionGet:
        return provisionGet();
    case RouteId::ProvisionNetworks:
        return provisionNetworks();
    case RouteId::ProvisionWifi:
        return provisionWifi(req);
    case RouteId::SystemReboot:
        return systemReboot();
    case RouteId::SystemFactoryReset:
        return systemFactoryReset(req);
    case RouteId::FirmwareGet:
        return firmwareGet();
    case RouteId::FirmwarePull:
        return firmwarePull(req);
    case RouteId::Events:
    case RouteId::FirmwareUpload:
    case RouteId::FirmwareAssets:
        break;
    }
    return error(500, "internal", "route is served by the HTTP layer");
}

ApiResponse Api::post(const Command &cmd)
{
    if (!services_.commands.post(cmd)) {
        ApiResponse r = error(503, "busy", "command queue full, try again");
        r.headers.emplace_back("Retry-After", "1");
        return r;
    }
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddBoolToObject(o.get(), "accepted", true);
    cJSON_AddStringToObject(o.get(), "command", toString(cmd.type));
    return json(202, printJson(o.get()));
}

namespace {

ApiResponse runErrorResponse(RunError e)
{
    switch (e) {
    case RunError::InvalidZone:
        return Api::error(404, toString(e), "no such zone");
    case RunError::UnknownProgram:
        return Api::error(404, toString(e), "no such program");
    case RunError::ZeroDuration:
        return Api::error(400, toString(e), "nothing to run: duration is zero");
    case RunError::QueueFull:
        return Api::error(409, toString(e), "the run queue is full (64 entries)");
    case RunError::AlreadyQueued:
        return Api::error(409, toString(e), "this program is already queued");
    case RunError::Inhibited:
        return Api::error(409, toString(e), "starts are blocked (no 24 VAC or maintenance)");
    case RunError::NothingToRun:
        return Api::error(409, toString(e), "the program has no enabled zone with a duration");
    case RunError::LockedOut:
        return Api::error(409, toString(e), "zone has a latched fault; clear faults first");
    case RunError::ZoneResting:
        return Api::error(409, toString(e), "zone reached its maximum run time and is resting");
    case RunError::MasterLockedOut:
        return Api::error(409, toString(e), "master valve locked out by repeated faults; clear faults first");
    case RunError::Ok:
        break;
    }
    return Api::error(500, "internal", "rejected without a reason");
}

} // namespace

// Rule: arm a watch for (command, zone, program), post, then poll:
//  - A matching CommandRejected arrives: answer with its error (409/404/400).
//    handle() publishes it synchronously while draining the queue.
//  - The state sequence advanced, and then either it advanced again or one
//    more control period (kControlSettleMs) passed without a rejection: the
//    command was accepted (202, "confirmed": true). The control task drains
//    the queue at the start of each period and publishes a snapshot in every
//    period that handled a command. The first advance may come from a period
//    that was already running when the command was posted; the next drain is
//    at most one period later, so waiting one more period covers it.
//  - kConfirmWaitMs passed: 202 with "confirmed": false. The outcome then
//    shows up only on the event stream (command_rejected). This happens when
//    the command queue was backed up or the control loop is stalled.
// One watch at a time (confirmMutex_), so concurrent requests never share a
// key. A command with the same key posted by another module inside the
// window would be attributed to this request; it carries the same verdict.
ApiResponse Api::postConfirmed(const Command &cmd)
{
    std::lock_guard<std::mutex> lock(confirmMutex_);
    watch_.arm(cmd);
    const uint32_t seq0 = services_.state.sequence();
    if (!services_.commands.post(cmd)) {
        watch_.disarm();
        ApiResponse r = error(503, "busy", "command queue full, try again");
        r.headers.emplace_back("Retry-After", "1");
        return r;
    }
    const MonoMs start = clock_.monoMs();
    MonoMs firstAdvance = 0;
    bool advanced = false;
    bool confirmed = false;
    std::optional<RunError> rejected;
    for (;;) {
        const uint32_t seq = services_.state.sequence();
        rejected = watch_.rejected(); // read after the sequence: published before it
        if (rejected.has_value()) {
            break;
        }
        const MonoMs now = clock_.monoMs();
        if (seq != seq0 && !advanced) {
            advanced = true;
            firstAdvance = now;
        }
        if (advanced && (seq - seq0 >= 2 || now - firstAdvance >= kControlSettleMs)) {
            confirmed = true;
            break;
        }
        if (now - start >= kConfirmWaitMs) {
            break;
        }
        platform_.sleepMs(kConfirmPollMs);
    }
    watch_.disarm();
    if (rejected.has_value()) {
        return runErrorResponse(*rejected);
    }
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddBoolToObject(o.get(), "accepted", true);
    cJSON_AddStringToObject(o.get(), "command", toString(cmd.type));
    cJSON_AddBoolToObject(o.get(), "confirmed", confirmed);
    return json(202, printJson(o.get()));
}

ApiResponse Api::applyConfigJson(const std::string &text, int okStatus, std::string okBody)
{
    const ValidationResult v = services_.config.updateFromJson(text);
    if (!v.ok) {
        if (v.path.empty() && v.message == "storage write failed") {
            return error(500, "storage_error", v.message);
        }
        // A document from newer firmware (core: path "schema") would lose
        // every field this firmware does not know if it were stored.
        return error(400, v.path == "schema" ? "unsupported_schema" : "invalid_config", v.message, v.path);
    }
    if (!services_.commands.post(Command::simple(CommandType::ReloadConfig))) {
        // Stored, but the controller has not picked it up yet. It re-reads on
        // the next successful ReloadConfig; report it so the client can retry.
        return error(503, "busy", "configuration stored; reload not queued, retry any write");
    }
    return json(okStatus, std::move(okBody));
}

// ---- auth --------------------------------------------------------------------------------

ApiResponse Api::sessionResponse(int status, const AuthOutcome &o, bool tls)
{
    if (o.error == AuthError::RateLimited) {
        ApiResponse r = error(429, toString(o.error), "too many failed attempts");
        r.headers.emplace_back("Retry-After", std::to_string(o.retryAfterS));
        return r;
    }
    if (o.error != AuthError::Ok) {
        const int code = o.error == AuthError::BadCredentials ? 401
                         : o.error == AuthError::WeakPassword ? 400
                         : o.error == AuthError::StorageError ? 500
                                                              : 409;
        const char *message = o.error == AuthError::WeakPassword
                                  ? "password must be 8 to 64 characters without control characters"
                                  : toString(o.error);
        return error(code, toString(o.error), message);
    }
    constexpr int64_t kMaxAge = static_cast<int64_t>(SessionTable::kAbsoluteTimeoutMs / 1000);
    JsonPtr body(cJSON_CreateObject());
    cJSON_AddStringToObject(body.get(), "token", o.session.token.c_str());
    cJSON_AddStringToObject(body.get(), "csrf", o.session.csrf.c_str());
    cJSON_AddNumberToObject(body.get(), "idle_timeout_s", static_cast<double>(SessionTable::kIdleTimeoutMs / 1000));
    ApiResponse r = json(status, printJson(body.get()));
    r.headers.emplace_back("Set-Cookie", cookieHeader(o.session.token, kMaxAge, tls));
    r.headers.emplace_back("Cache-Control", "no-store");
    return r;
}

ApiResponse Api::authStatus(const ApiRequest &req)
{
    const Principal p =
        auth_.authenticate(cookieValue(req.cookie, kSessionCookie), bearerToken(req.authorization), clock_.monoMs());
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddBoolToObject(o.get(), "password_set", auth_.passwordSet());
    cJSON_AddBoolToObject(o.get(), "authenticated", p.authenticated);
    cJSON_AddBoolToObject(o.get(), "protect_reads", settings().protectReads);
    if (p.authenticated && p.viaCookie) {
        // Lets a reloaded page recover its CSRF token from the cookie session.
        cJSON_AddStringToObject(o.get(), "csrf", p.csrf.c_str());
    }
    ApiResponse r = json(200, printJson(o.get()));
    r.headers.emplace_back("Cache-Control", "no-store");
    return r;
}

ApiResponse Api::login(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    const char *password = jsonString(body.get(), "password");
    if (password == nullptr) {
        return error(400, "invalid_body", "password is required", "password");
    }
    return sessionResponse(200, auth_.login(clientKey(req.peer), password, clock_.monoMs()), req.tls);
}

ApiResponse Api::setup(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    const char *code = jsonString(body.get(), "setup_code");
    const char *password = jsonString(body.get(), "password");
    if (code == nullptr || password == nullptr) {
        return error(400, "invalid_body", "setup_code and password are required");
    }
    return sessionResponse(
        201, auth_.setup(clientKey(req.peer), platform_.setupCode(), code, password, clock_.monoMs()), req.tls);
}

ApiResponse Api::logout(const ApiRequest &req)
{
    const std::string_view bearer = bearerToken(req.authorization);
    auth_.logout(bearer.empty() ? cookieValue(req.cookie, kSessionCookie) : bearer);
    ApiResponse r = json(200, "{\"logged_out\":true}");
    r.headers.emplace_back("Set-Cookie", cookieHeader("", 0, req.tls));
    return r;
}

ApiResponse Api::changePassword(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    const char *current = jsonString(body.get(), "current_password");
    const char *next = jsonString(body.get(), "new_password");
    if (current == nullptr || next == nullptr) {
        return error(400, "invalid_body", "current_password and new_password are required");
    }
    return sessionResponse(200, auth_.changePassword(clientKey(req.peer), current, next, clock_.monoMs()), req.tls);
}

ApiResponse Api::securityGet()
{
    const WebSettings s = settings();
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddBoolToObject(o.get(), "protect_reads", s.protectReads);
    cJSON_AddBoolToObject(o.get(), "https", s.https);
    cJSON_AddBoolToObject(o.get(), "check_host", s.checkHost);
    cJSON_AddBoolToObject(o.get(), "password_set", auth_.passwordSet());
    cJSON_AddNumberToObject(o.get(), "sessions", static_cast<double>(auth_.sessionCount()));
    cJSON_AddStringToObject(o.get(), "tls_fingerprint_sha256", platform_.tlsFingerprint().c_str());
    return json(200, printJson(o.get()));
}

ApiResponse Api::securityPut(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    WebSettings s = settings();
    const bool httpsBefore = s.https;
    const struct {
        const char *key;
        bool *dst;
    } fields[] = {{"protect_reads", &s.protectReads}, {"https", &s.https}, {"check_host", &s.checkHost}};
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, body.get())
    {
        bool known = false;
        for (const auto &f : fields) {
            if (std::string_view(item->string) == f.key) {
                if (!cJSON_IsBool(item)) {
                    return error(400, "invalid_body", "must be true or false", f.key);
                }
                *f.dst = cJSON_IsTrue(item);
                known = true;
            }
        }
        if (!known) {
            return error(400, "invalid_body", "unknown field", item->string);
        }
    }
    if (!settingsStore_.write(kSettingsKey, s.encode())) {
        return error(500, "storage_error", "settings not stored");
    }
    {
        std::lock_guard<std::mutex> lock(settingsMutex_);
        settings_ = s;
    }
    ApiResponse r = securityGet();
    if (s.https != httpsBefore) {
        JsonPtr o = parseJson(r.body);
        cJSON_AddBoolToObject(o.get(), "restart_required", true);
        r.body = printJson(o.get());
    }
    return r;
}

// ---- device -----------------------------------------------------------------------------------

ApiResponse Api::info()
{
    const FirmwareInfo fw = platform_.firmware();
    const SystemInfo sys = platform_.system();
    const net::NetStatus n = platform_.network();
    const Config cfg = services_.config.get();
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddStringToObject(o.get(), "api_version", "1");
    cJSON_AddStringToObject(o.get(), "device_name", cfg.deviceName.c_str());
    cJSON_AddStringToObject(o.get(), "hostname", n.hostname.c_str());
    cJSON_AddStringToObject(o.get(), "mac", n.mac.c_str());
    cJSON_AddStringToObject(o.get(), "board", fw.board.c_str());
    cJSON *f = cJSON_AddObjectToObject(o.get(), "firmware");
    cJSON_AddStringToObject(f, "version", fw.version.c_str());
    cJSON_AddStringToObject(f, "project", fw.project.c_str());
    cJSON_AddStringToObject(f, "idf_version", fw.idfVersion.c_str());
    cJSON_AddStringToObject(f, "build_date", fw.buildDate.c_str());
    cJSON_AddNumberToObject(o.get(), "uptime_s", static_cast<double>(sys.uptimeMs / 1000));
    cJSON_AddStringToObject(o.get(), "reset_reason", sys.resetReason.c_str());
    cJSON *heap = cJSON_AddObjectToObject(o.get(), "heap");
    cJSON_AddNumberToObject(heap, "free", sys.freeHeap);
    cJSON_AddNumberToObject(heap, "min_free", sys.minFreeHeap);
    cJSON_AddNumberToObject(heap, "largest_block", sys.largestFreeBlock);
    cJSON_AddNumberToObject(heap, "internal_free", sys.internalFree);
    cJSON_AddNumberToObject(heap, "internal_min_free", sys.internalMinFree);
    cJSON_AddNumberToObject(o.get(), "http_stack_free_min", sys.httpStackFreeMin);
    return json(200, printJson(o.get()));
}

ApiResponse Api::status()
{
    const SystemState s = services_.state.snapshot();
    const auto cfg = std::make_unique<Config>(services_.config.get());
    cJSON *o = stateToJson(s, *cfg);
    cJSON_AddItemToObject(o, "network", netStatusToJson(platform_.network()));
    cJSON_AddItemToObject(o, "ota", otaStatusToJson(platform_.otaStatus()));
    return json(200, bodyOf(o));
}

// ---- zones ---------------------------------------------------------------------------------------

ApiResponse Api::zonesList()
{
    const SystemState s = services_.state.snapshot();
    const auto cfg = std::make_unique<Config>(services_.config.get());
    cJSON *a = cJSON_CreateArray();
    for (std::size_t i = 0; i < cfg->zoneCount && i < kMaxZones; ++i) {
        cJSON_AddItemToArray(a, zoneToJson(i, s.zones[i], cfg->zones[i]));
    }
    return json(200, bodyOf(a));
}

namespace {

bool zoneIndex(std::string_view id, uint8_t zoneCount, ZoneIndex &out)
{
    uint32_t n = 0;
    if (!parseUint(id, kMaxZones, n) || n == 0 || n > zoneCount) {
        return false;
    }
    out = static_cast<ZoneIndex>(n - 1);
    return true;
}

} // namespace

ApiResponse Api::zoneGet(std::string_view id)
{
    const auto cfg = std::make_unique<Config>(services_.config.get());
    ZoneIndex z = 0;
    if (!zoneIndex(id, cfg->zoneCount, z)) {
        return error(404, "not_found", "no such zone");
    }
    const SystemState s = services_.state.snapshot();
    return json(200, bodyOf(zoneToJson(z, s.zones[z], cfg->zones[z])));
}

ApiResponse Api::zonePatch(std::string_view id, const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    JsonPtr doc = parseJson(services_.config.toJson());
    if (!doc) {
        return error(500, "internal", "configuration not readable");
    }
    const cJSON *count = cJSON_GetObjectItemCaseSensitive(doc.get(), "zone_count");
    ZoneIndex z = 0;
    if (!cJSON_IsNumber(count) || !zoneIndex(id, static_cast<uint8_t>(count->valuedouble), z)) {
        return error(404, "not_found", "no such zone");
    }
    cJSON *zone = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(doc.get(), "zones"), z);
    if (zone == nullptr) {
        return error(404, "not_found", "no such zone");
    }
    static constexpr std::string_view kAllowed[] = {"name", "enabled", "max_run_s", "current_min_ma", "current_max_ma"};
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, body.get())
    {
        if (std::find(std::begin(kAllowed), std::end(kAllowed), std::string_view(item->string)) == std::end(kAllowed)) {
            return error(400, "invalid_body", "field cannot be changed here", item->string);
        }
        setItem(zone, item->string, cJSON_Duplicate(item, true));
    }
    const std::string prefix = "zones[" + std::to_string(z) + "]";
    const ValidationResult v = services_.config.updateFromJson(printJson(doc.get()));
    if (!v.ok) {
        return error(400, "invalid_config", v.message, relativePath(v.path, prefix));
    }
    if (!services_.commands.post(Command::simple(CommandType::ReloadConfig))) {
        return error(503, "busy", "configuration stored; reload not queued, retry any write");
    }
    return zoneGet(id);
}

ApiResponse Api::zoneRun(std::string_view id, const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    const auto cfg = std::make_unique<Config>(services_.config.get());
    ZoneIndex z = 0;
    if (!zoneIndex(id, cfg->zoneCount, z)) {
        return error(404, "not_found", "no such zone");
    }
    int64_t seconds = 0;
    if (!jsonInt(cJSON_GetObjectItemCaseSensitive(body.get(), "duration_s"), 1, kHardMaxRunSec, seconds)) {
        return error(400, "invalid_body", "duration_s must be an integer from 1 to 14400", "duration_s");
    }
    if (!cfg->zones[z].enabled) {
        return error(409, "zone_disabled", "zone is disabled");
    }
    // Known blockers are answered from the snapshot at once; anything the
    // snapshot cannot show comes back from the controller (postConfirmed).
    const SystemState s = services_.state.snapshot();
    if (s.zones[z].lockedOut) {
        return runErrorResponse(RunError::LockedOut);
    }
    if (s.zones[z].restRemainingSec > 0) {
        ApiResponse r = runErrorResponse(RunError::ZoneResting);
        r.headers.emplace_back("Retry-After", std::to_string(s.zones[z].restRemainingSec));
        return r;
    }
    if (s.masterEnabled && s.masterLockedOut) {
        return runErrorResponse(RunError::MasterLockedOut);
    }
    if (s.maintenance || (s.inhibitFlags & kInhibitNoAc) != 0) {
        return error(409, "inhibited", s.maintenance ? "maintenance mode (update running)" : "no 24 VAC");
    }
    ApiResponse r = postConfirmed(Command::runZone(z, static_cast<uint32_t>(seconds)));
    if (r.status == 202) {
        JsonPtr o = parseJson(r.body);
        cJSON_AddNumberToObject(o.get(), "zone", z + 1);
        // The zone driver clamps to the zone's maximum run time.
        cJSON_AddNumberToObject(o.get(), "duration_s",
                                static_cast<double>(std::min<int64_t>(seconds, cfg->zones[z].maxRunSec)));
        r.body = printJson(o.get());
    }
    return r;
}

ApiResponse Api::zoneStop(std::string_view id)
{
    const auto cfg = std::make_unique<Config>(services_.config.get());
    ZoneIndex z = 0;
    if (!zoneIndex(id, cfg->zoneCount, z)) {
        return error(404, "not_found", "no such zone");
    }
    // The queue runs one entry at a time, so a zone that is open or waiting
    // is the head entry; skipping the head closes it. A zone that is only
    // queued further back cannot be removed singly yet (no core command).
    const SystemState s = services_.state.snapshot();
    const bool head = s.queue.state != QueueState::Idle && s.queue.shownCount > 0 && s.queue.items[0].zone == z;
    if (s.zones[z].state == ValveState::Off && !head) {
        return error(409, "not_running", "zone is not running");
    }
    if (!head) {
        return error(409, "not_head", "zone is not the running queue entry; use /queue/stop");
    }
    return post(Command::simple(CommandType::Skip));
}

// ---- programs --------------------------------------------------------------------------------------

ApiResponse Api::programsList()
{
    JsonPtr doc = parseJson(services_.config.toJson());
    cJSON *programs = schedulePrograms(doc.get());
    if (programs == nullptr) {
        return json(200, "[]");
    }
    return json(200, printJson(programs));
}

ApiResponse Api::programGet(std::string_view id)
{
    uint32_t pid = 0;
    if (!parseUint(id, 65535, pid) || pid == 0) {
        return error(404, "not_found", "no such program");
    }
    JsonPtr doc = parseJson(services_.config.toJson());
    int index = -1;
    cJSON *p = findProgram(schedulePrograms(doc.get()), pid, index);
    if (p == nullptr) {
        return error(404, "not_found", "no such program");
    }
    return json(200, printJson(p));
}

ApiResponse Api::programWrite(std::string_view id, const ApiRequest &req, bool create)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    JsonPtr doc = parseJson(services_.config.toJson());
    cJSON *programs = schedulePrograms(doc.get());
    if (programs == nullptr) {
        return error(500, "internal", "configuration not readable");
    }
    const cJSON *bodyId = cJSON_GetObjectItemCaseSensitive(body.get(), "id");
    int64_t given = 0;
    if (bodyId != nullptr && !cJSON_IsNull(bodyId) && !jsonInt(bodyId, 1, 65535, given)) {
        return error(400, "invalid_body", "id must be an integer from 1 to 65535", "id");
    }
    uint32_t pid = 0;
    int index = -1;
    if (create) {
        if (cJSON_GetArraySize(programs) >= static_cast<int>(kMaxPrograms)) {
            return error(409, "too_many_programs", "at most 16 programs");
        }
        if (given != 0) {
            pid = static_cast<uint32_t>(given);
            if (findProgram(programs, pid, index) != nullptr) {
                return error(409, "exists", "a program with this id exists");
            }
        } else {
            uint32_t maxId = 0;
            const cJSON *p = nullptr;
            cJSON_ArrayForEach(p, programs)
            {
                const cJSON *v = cJSON_GetObjectItemCaseSensitive(p, "id");
                if (cJSON_IsNumber(v)) {
                    maxId = std::max(maxId, static_cast<uint32_t>(v->valuedouble));
                }
            }
            pid = maxId + 1;
            for (uint32_t cand = 1; pid > 65535 && cand <= 65535; ++cand) {
                if (findProgram(programs, cand, index) == nullptr) {
                    pid = cand;
                }
            }
        }
        index = cJSON_GetArraySize(programs);
    } else {
        if (!parseUint(id, 65535, pid) || pid == 0 || findProgram(programs, pid, index) == nullptr) {
            return error(404, "not_found", "no such program");
        }
        if (given != 0 && static_cast<uint32_t>(given) != pid) {
            return error(400, "invalid_body", "id does not match the path", "id");
        }
    }
    cJSON *program = cJSON_Duplicate(body.get(), true);
    setItem(program, "id", cJSON_CreateNumber(pid));
    if (create) {
        cJSON_AddItemToArray(programs, program);
    } else {
        cJSON_ReplaceItemInArray(programs, index, program);
    }
    const std::string prefix = "schedule.programs[" + std::to_string(index) + "]";
    const ValidationResult v = services_.config.updateFromJson(printJson(doc.get()));
    if (!v.ok) {
        return error(400, "invalid_config", v.message, relativePath(v.path, prefix));
    }
    if (!services_.commands.post(Command::simple(CommandType::ReloadConfig))) {
        return error(503, "busy", "configuration stored; reload not queued, retry any write");
    }
    ApiResponse r = programGet(std::to_string(pid));
    if (create && r.status == 200) {
        r.status = 201;
        r.headers.emplace_back("Location", std::string(kApiPrefix) + "/programs/" + std::to_string(pid));
    }
    return r;
}

ApiResponse Api::programDelete(std::string_view id)
{
    uint32_t pid = 0;
    JsonPtr doc = parseJson(services_.config.toJson());
    cJSON *programs = schedulePrograms(doc.get());
    int index = -1;
    if (!parseUint(id, 65535, pid) || pid == 0 || findProgram(programs, pid, index) == nullptr) {
        return error(404, "not_found", "no such program");
    }
    cJSON_DeleteItemFromArray(programs, index);
    return applyConfigJson(printJson(doc.get()), 200, "{\"deleted\":true}");
}

ApiResponse Api::programRun(std::string_view id)
{
    uint32_t pid = 0;
    const auto cfg = std::make_unique<Config>(services_.config.get());
    if (!parseUint(id, 65535, pid) || pid == 0) {
        return error(404, "not_found", "no such program");
    }
    const auto end = cfg->schedule.programs.begin() + cfg->schedule.programCount;
    if (std::find_if(cfg->schedule.programs.begin(), end, [&](const Program &p) { return p.id == pid; }) == end) {
        return error(404, "not_found", "no such program");
    }
    const SystemState s = services_.state.snapshot();
    if (s.masterEnabled && s.masterLockedOut) {
        return runErrorResponse(RunError::MasterLockedOut);
    }
    if (s.maintenance || (s.inhibitFlags & kInhibitNoAc) != 0) {
        return error(409, "inhibited", s.maintenance ? "maintenance mode (update running)" : "no 24 VAC");
    }
    return postConfirmed(Command::runProgram(static_cast<uint16_t>(pid)));
}

// ---- queue, rain delay, sensors, faults ---------------------------------------------------------------

ApiResponse Api::queueGet()
{
    const SystemState s = services_.state.snapshot();
    return json(200, bodyOf(queueToJson(s.queue, services_.config.get().schedule.pauseTimeoutMin)));
}

ApiResponse Api::queueCommand(CommandType type) { return post(Command::simple(type)); }

ApiResponse Api::rainDelayGet()
{
    const SystemState s = services_.state.snapshot();
    return json(200, bodyOf(rainDelayToJson(s)));
}

ApiResponse Api::rainDelayPut(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    if (services_.time.status() == TimeStatus::Unset) {
        return error(409, "time_not_set", "the clock is not set");
    }
    const UtcSeconds now = clock_.nowUtc();
    const cJSON *until = cJSON_GetObjectItemCaseSensitive(body.get(), "until_utc");
    const cJSON *hours = cJSON_GetObjectItemCaseSensitive(body.get(), "hours");
    int64_t value = 0;
    UtcSeconds target = 0;
    if ((until == nullptr) == (hours == nullptr)) {
        return error(400, "invalid_body", "give exactly one of until_utc or hours");
    }
    if (hours != nullptr) {
        if (!jsonInt(hours, 1, kMaxRainDelaySec / 3600, value)) {
            return error(400, "invalid_body", "hours must be an integer from 1 to 720", "hours");
        }
        target = now + value * 3600;
    } else {
        if (!jsonInt(until, static_cast<double>(now + 1), static_cast<double>(now + kMaxRainDelaySec), value)) {
            return error(400, "invalid_body", "until_utc must be in the next 30 days", "until_utc");
        }
        target = value;
    }
    ApiResponse r = post(Command::rainDelay(target));
    if (r.status == 202) {
        JsonPtr o = parseJson(r.body);
        cJSON_AddNumberToObject(o.get(), "until_utc", static_cast<double>(target));
        r.body = printJson(o.get());
    }
    return r;
}

ApiResponse Api::rainDelayDelete() { return post(Command::rainDelay(0)); }

ApiResponse Api::sensors()
{
    const SystemState s = services_.state.snapshot();
    return json(200, bodyOf(sensorsToJson(s)));
}

ApiResponse Api::faultsList()
{
    const SystemState s = services_.state.snapshot();
    return json(200, bodyOf(faultsToJson(s)));
}

// ---- config ---------------------------------------------------------------------------------------------

ApiResponse Api::configGet() { return json(200, services_.config.toJson()); }

ApiResponse Api::configPut(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    return applyConfigJson(std::string(req.body), 200, "{\"applied\":true}");
}

ApiResponse Api::configBackup()
{
    const FirmwareInfo fw = platform_.firmware();
    const net::NetStatus n = platform_.network();
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddStringToObject(o.get(), "format", "zoneflow-backup");
    cJSON_AddNumberToObject(o.get(), "format_version", 1);
    cJSON_AddNumberToObject(o.get(), "created_utc", static_cast<double>(clock_.nowUtc()));
    cJSON_AddStringToObject(o.get(), "firmware_version", fw.version.c_str());
    cJSON_AddStringToObject(o.get(), "hostname", n.hostname.c_str());
    // No secrets: WiFi credentials, the admin password hash, sessions, the
    // setup code and the TLS key are never part of a backup.
    cJSON_AddItemToObject(o.get(), "config", cJSON_Parse(services_.config.toJson().c_str()));
    ApiResponse r = json(200, printJson(o.get()));
    r.headers.emplace_back("Content-Disposition",
                           "attachment; filename=\"" + std::string(n.hostname.view()) + "-backup.json\"");
    return r;
}

ApiResponse Api::configRestore(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    const char *format = jsonString(body.get(), "format");
    std::string text;
    if (format != nullptr) {
        const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(body.get(), "config");
        if (std::string_view(format) != "zoneflow-backup" || !cJSON_IsObject(cfg)) {
            return error(400, "invalid_backup", "not a ZoneFlow backup", "format");
        }
        text = printJson(cfg);
    } else {
        text = std::string(req.body); // a bare configuration document
    }
    return applyConfigJson(text, 200, "{\"restored\":true}");
}

// ---- time --------------------------------------------------------------------------------------------------

ApiResponse Api::timeGet()
{
    const auto cfg = std::make_unique<Config>(services_.config.get());
    const UtcSeconds now = clock_.nowUtc();
    const TimeStatus st = services_.time.status();
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddNumberToObject(o.get(), "utc", static_cast<double>(now));
    cJSON_AddStringToObject(o.get(), "status", toString(st));
    cJSON_AddBoolToObject(o.get(), "rtc_present", services_.time.rtcPresent());
    cJSON_AddStringToObject(o.get(), "timezone", cfg->timezone.c_str());
    const auto tz = TimeZone::parse(cfg->timezone.view());
    if (tz.has_value() && st != TimeStatus::Unset) {
        const civil::LocalDateTime l = tz->toLocal(now);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%04d-%02u-%02uT%02d:%02d:%02d", l.date.year, l.date.month, l.date.day, l.hour(),
                      l.minute(), l.second());
        cJSON_AddStringToObject(o.get(), "local", buf);
        cJSON_AddNumberToObject(o.get(), "utc_offset_s", l.utcOffsetSec);
        cJSON_AddBoolToObject(o.get(), "dst", l.dst);
    } else {
        cJSON_AddNullToObject(o.get(), "local");
    }
    return json(200, printJson(o.get()));
}

ApiResponse Api::timePut(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    const cJSON *utc = cJSON_GetObjectItemCaseSensitive(body.get(), "utc");
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(body.get(), "timezone");
    if (utc == nullptr && tz == nullptr) {
        return error(400, "invalid_body", "give utc, timezone or both");
    }
    int64_t t = 0;
    if (utc != nullptr && !jsonInt(utc, static_cast<double>(TimeService::kMinValidUtc), 4102444800.0, t)) {
        return error(400, "invalid_body", "utc must be seconds since 1970 between 2025 and 2100", "utc");
    }
    if (tz != nullptr) {
        if (!cJSON_IsString(tz) || std::string_view(tz->valuestring).size() > 63 ||
            !TimeZone::parse(tz->valuestring).has_value()) {
            return error(400, "invalid_body", "timezone must be a POSIX TZ string", "timezone");
        }
        JsonPtr doc = parseJson(services_.config.toJson());
        setItem(doc.get(), "timezone", cJSON_CreateString(tz->valuestring));
        const ApiResponse r = applyConfigJson(printJson(doc.get()), 200, "");
        if (r.status != 200) {
            return r;
        }
    }
    if (utc != nullptr && !services_.time.setManual(t)) {
        return error(500, "time_not_set", "the clock rejected the time");
    }
    return timeGet();
}

// ---- network and provisioning ----------------------------------------------------------------------------------

ApiResponse Api::networkGet() { return json(200, bodyOf(netStatusToJson(platform_.network()))); }

ApiResponse Api::networkReprovision(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, true, err);
    if (!body) {
        return err;
    }
    const char *t = jsonString(body.get(), "transport");
    const net::ProvTransport transport = t == nullptr ? net::ProvTransport::Ble : net::transportFromString(t);
    if (transport == net::ProvTransport::None) {
        return error(400, "invalid_body", "transport must be ble or softap", "transport");
    }
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddBoolToObject(o.get(), "accepted", true);
    cJSON_AddStringToObject(o.get(), "transport", net::toString(transport));
    cJSON_AddStringToObject(o.get(), "note",
                            "the device restarts into provisioning; stored WiFi credentials stay until "
                            "new ones work");
    ApiResponse r = json(202, printJson(o.get()));
    r.action = PostAction::Reprovision;
    r.transport = transport;
    return r;
}

ApiResponse Api::provisionGet()
{
    const net::NetStatus n = platform_.network();
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddBoolToObject(o.get(), "active", n.provisioning != net::ProvTransport::None);
    cJSON_AddStringToObject(o.get(), "transport", net::toString(n.provisioning));
    cJSON_AddStringToObject(o.get(), "state", net::toString(n.state));
    cJSON_AddBoolToObject(o.get(), "provisioned", n.provisioned);
    cJSON_AddBoolToObject(o.get(), "password_set", auth_.passwordSet());
    cJSON_AddStringToObject(o.get(), "hostname", n.hostname.c_str());
    cJSON_AddStringToObject(o.get(), "ip", n.ip.c_str());
    return json(200, printJson(o.get()));
}

ApiResponse Api::provisionNetworks()
{
    cJSON *a = cJSON_CreateArray();
    for (const WifiNetwork &w : platform_.scan()) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "ssid", w.ssid.c_str());
        cJSON_AddNumberToObject(e, "rssi", w.rssi);
        cJSON_AddBoolToObject(e, "secure", w.secure);
        cJSON_AddItemToArray(a, e);
    }
    return json(200, bodyOf(a));
}

ApiResponse Api::provisionWifi(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    const char *ssid = jsonString(body.get(), "ssid");
    const char *password = jsonString(body.get(), "password");
    const char *admin = jsonString(body.get(), "admin_password");
    if (ssid == nullptr || std::string_view(ssid).empty() || std::string_view(ssid).size() > 32) {
        return error(400, "invalid_body", "ssid must be 1 to 32 bytes", "ssid");
    }
    const std::string_view pw = password == nullptr ? std::string_view{} : std::string_view(password);
    if (!pw.empty() && (pw.size() < 8 || pw.size() > 64)) {
        return error(400, "invalid_body", "WPA passphrase must be 8 to 63 characters (or 64 hex digits)", "password");
    }
    if (admin != nullptr && !auth_.passwordSet()) {
        const AuthError e = auth_.setInitialPassword(admin);
        if (e != AuthError::Ok) {
            return error(e == AuthError::WeakPassword ? 400 : 500, toString(e),
                         "admin password must be 8 to 64 characters", "admin_password");
        }
    }
    if (!platform_.provisionWifi(ssid, pw)) {
        return error(409, "not_provisioning", "provisioning is not accepting credentials now");
    }
    return json(202, "{\"accepted\":true}");
}

// ---- system and firmware ------------------------------------------------------------------------------------

ApiResponse Api::systemReboot()
{
    ApiResponse r = json(202, "{\"accepted\":true}");
    r.action = PostAction::Reboot;
    return r;
}

ApiResponse Api::systemFactoryReset(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    const char *confirm = jsonString(body.get(), "confirm");
    if (confirm == nullptr || std::string_view(confirm) != "factory-reset") {
        return error(400, "confirm_required", "send {\"confirm\":\"factory-reset\"}", "confirm");
    }
    ApiResponse r = json(202, "{\"accepted\":true}");
    r.action = PostAction::FactoryReset;
    return r;
}

ApiResponse Api::firmwareGet()
{
    const FirmwareInfo fw = platform_.firmware();
    const SlotInfo slots = platform_.slots();
    JsonPtr o(cJSON_CreateObject());
    cJSON_AddStringToObject(o.get(), "version", fw.version.c_str());
    cJSON_AddStringToObject(o.get(), "idf_version", fw.idfVersion.c_str());
    cJSON_AddStringToObject(o.get(), "build_date", fw.buildDate.c_str());
    cJSON_AddStringToObject(o.get(), "running_slot", slots.running.c_str());
    cJSON_AddStringToObject(o.get(), "running_state", slots.runningState.c_str());
    cJSON_AddStringToObject(o.get(), "next_slot", slots.next.c_str());
    cJSON_AddStringToObject(o.get(), "other_slot_version", slots.otherVersion.c_str());
    cJSON_AddItemToObject(o.get(), "ota", otaStatusToJson(platform_.otaStatus()));
    return json(200, printJson(o.get()));
}

ApiResponse Api::firmwarePull(const ApiRequest &req)
{
    ApiResponse err;
    JsonPtr body = parseObject(req.body, false, err);
    if (!body) {
        return err;
    }
    const char *url = jsonString(body.get(), "url");
    const cJSON *force = cJSON_GetObjectItemCaseSensitive(body.get(), "force");
    if (url == nullptr || std::string_view(url).rfind("https://", 0) != 0 || std::string_view(url).size() > 512) {
        return error(400, "invalid_body", "url must be an https:// URL of at most 512 bytes", "url");
    }
    if (force != nullptr && !cJSON_IsBool(force)) {
        return error(400, "invalid_body", "force must be true or false", "force");
    }
    const ota::OtaError e = platform_.startPull(url, cJSON_IsTrue(force));
    if (e == ota::OtaError::ZonesActive) {
        return error(409, ota::toString(e), "watering is active; stop it or send force=true");
    }
    if (e != ota::OtaError::None) {
        return error(409, ota::toString(e), "update not started");
    }
    return json(202, "{\"accepted\":true}");
}

} // namespace zf::web
