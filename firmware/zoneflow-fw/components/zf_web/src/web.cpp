// SPDX-License-Identifier: Apache-2.0
// zf_web device glue: esp_http_server / esp_https_server, static files from
// LittleFS, the Server-Sent Events stream and the platform port for Api.
//
// Tasks:
//   httpd (and httpd for HTTPS)  prio 5, core 0, 12 KB stack: every request.
//     Config writes run here; the core review measured about 4.6 KB of stack
//     for config parsing, so 12 KB leaves room for the handler frames, cJSON
//     printing and a TLS handshake. The smallest free stack seen is reported
//     in GET /api/v1/info (http_stack_free_min) so it can be checked on a board.
//   zf_web_sse  prio 3, core 0, 6 KB: formats and sends stream messages. The
//     event-bus handler only copies the event into its queue (non-blocking).
#include "zf_web/web.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <memory>
#include <mutex>
#include <sys/stat.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_https_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "http_parser.h"
#include "lwip/sockets.h"
#include "tls.hpp"
#include "zf_board/board.hpp"
#include "zf_net/net.hpp"
#include "zf_net/nvs_store.hpp"
#include "zf_ota/ota.hpp"
#include "zf_platform/platform.hpp"
#include "zf_web/api.hpp"
#include "zf_web/http_util.hpp"
#include "zf_web/json_map.hpp"

extern const char kFallbackHtmlStart[] asm("_binary_fallback_html_start");
extern const char kFallbackHtmlEnd[] asm("_binary_fallback_html_end");
extern const char kSetupHtmlStart[] asm("_binary_setup_html_start");
extern const char kSetupHtmlEnd[] asm("_binary_setup_html_end");

namespace zf::web {
namespace {

constexpr const char *kTag = "zf.web";
constexpr const char *kMount = "/www";
constexpr const char *kPartition = "www";
constexpr std::size_t kMaxStreams = 3;
constexpr std::size_t kChunk = 4096;
// CSP for files from the assets partition: no inline script, same-origin
// API and stream. The built-in pages carry their script inline.
constexpr const char *kUiCsp = "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; "
                               "connect-src 'self'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'";
constexpr const char *kBuiltinCsp = "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' "
                                    "'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'";

class EspRandom final : public IRandom {
public:
    void fill(uint8_t *out, std::size_t len) override { esp_fill_random(out, len); }
};

struct Module;
Module *gm = nullptr;

class Platform final : public IPlatformPort {
public:
    FirmwareInfo firmware() const override
    {
        const esp_app_desc_t *d = esp_app_get_description();
        FirmwareInfo f;
        f.version = d->version;
        f.project = d->project_name;
        f.idfVersion = d->idf_ver;
        f.buildDate = std::string(d->date) + " " + d->time;
        f.board = board::kBoardName;
        return f;
    }
    SystemInfo system() const override;
    net::NetStatus network() const override { return net::status(); }
    std::string setupCode() const override { return net::setupInfo().setupCode; }
    std::vector<WifiNetwork> scan() override
    {
        std::vector<WifiNetwork> out;
        for (const net::ScanEntry &e : net::scan()) {
            out.push_back({e.ssid, e.rssi, e.secure});
        }
        return out;
    }
    bool provisionWifi(std::string_view ssid, std::string_view password) override
    {
        return net::provisionWifi(ssid, password);
    }
    ota::OtaStatus otaStatus() const override { return ota::status(); }
    SlotInfo slots() const override
    {
        const ota::Slots s = ota::slots();
        return {s.running, s.next, s.runningState, s.otherVersion};
    }
    ota::OtaError startPull(std::string_view url, bool force) override { return ota::startPull(url, force); }
    std::string tlsFingerprint() const override;
    void sleepMs(uint32_t ms) override { vTaskDelay(pdMS_TO_TICKS(ms) > 0 ? pdMS_TO_TICKS(ms) : 1); }
};

struct Module {
    const AppServices *services = nullptr;
    net::NvsNamespace store{"zf_web"};
    EspRandom rng;
    platform::EspClock clock;
    Platform port;
    std::unique_ptr<AuthService> auth;
    std::unique_ptr<Api> api;
    WebSettings settings;
    TlsIdentity tls;
    httpd_handle_t http = nullptr;  // port 80
    httpd_handle_t https = nullptr; // port 443, when enabled
    bool mounted = false;
    std::atomic<uint32_t> stackFreeMin{UINT32_MAX};
    std::mutex streamMutex;
    std::array<httpd_req_t *, kMaxStreams> streams{};
    platform::EventQueueSubscriber events;
};

SystemInfo Platform::system() const
{
    SystemInfo s;
    s.uptimeMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    s.freeHeap = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_8BIT));
    s.minFreeHeap = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    s.largestFreeBlock = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    s.internalFree = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    s.internalMinFree = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const uint32_t stack = gm->stackFreeMin.load();
    s.httpStackFreeMin = stack == UINT32_MAX ? 0 : stack;
    static constexpr const char *kReasons[] = {"unknown",  "poweron", "external",   "software",  "panic", "int_wdt",
                                               "task_wdt", "wdt",     "deepsleep",  "brownout",  "sdio",  "usb",
                                               "jtag",     "efuse",   "pwr_glitch", "cpu_lockup"};
    const auto r = static_cast<std::size_t>(esp_reset_reason());
    s.resetReason = r < sizeof kReasons / sizeof kReasons[0] ? kReasons[r] : "other";
    return s;
}

std::string Platform::tlsFingerprint() const { return gm->tls.fingerprint; }

// ---- request plumbing --------------------------------------------------------------------

void noteStack()
{
    const uint32_t free = uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t);
    uint32_t prev = gm->stackFreeMin.load();
    while (free < prev && !gm->stackFreeMin.compare_exchange_weak(prev, free)) {
    }
}

std::string header(httpd_req_t *req, const char *name)
{
    const size_t len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0) {
        return {};
    }
    std::string v(len + 1, '\0');
    if (httpd_req_get_hdr_value_str(req, name, v.data(), v.size()) != ESP_OK) {
        return {};
    }
    v.resize(len);
    return v;
}

std::string peerAddress(httpd_req_t *req)
{
    sockaddr_storage addr{};
    socklen_t len = sizeof addr;
    char buf[48] = "?";
    if (getpeername(httpd_req_to_sockfd(req), reinterpret_cast<sockaddr *>(&addr), &len) == 0) {
        if (addr.ss_family == AF_INET) {
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in *>(&addr)->sin_addr, buf, sizeof buf);
        } else if (addr.ss_family == AF_INET6) {
            inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6 *>(&addr)->sin6_addr, buf, sizeof buf);
        }
    }
    return buf;
}

// Owns the strings an ApiRequest points into.
struct RawRequest {
    std::string method, path, query, host, origin, cookie, authorization, csrf, contentType, peer, body;
    std::size_t contentLength = 0;
    bool tls = false;

    explicit RawRequest(httpd_req_t *req)
    {
        method = http_method_str(static_cast<http_method>(req->method));
        const std::string_view uri(req->uri);
        const std::size_t q = uri.find('?');
        path = std::string(uri.substr(0, q));
        query = q == std::string_view::npos ? std::string{} : std::string(uri.substr(q + 1));
        host = header(req, "Host");
        origin = header(req, "Origin");
        cookie = header(req, "Cookie");
        authorization = header(req, "Authorization");
        csrf = header(req, "X-CSRF-Token");
        contentType = header(req, "Content-Type");
        peer = peerAddress(req);
        contentLength = req->content_len;
        tls = req->handle == gm->https && gm->https != nullptr;
    }

    ApiRequest view() const
    {
        ApiRequest r;
        r.method = method;
        r.path = path;
        r.query = query;
        r.body = body;
        r.contentLength = contentLength;
        r.host = host;
        r.origin = origin;
        r.cookie = cookie;
        r.authorization = authorization;
        r.csrf = csrf;
        r.contentType = contentType;
        r.peer = peer;
        r.tls = tls;
        return r;
    }
};

const char *statusLine(int status)
{
    switch (status) {
    case 200:
        return "200 OK";
    case 201:
        return "201 Created";
    case 202:
        return "202 Accepted";
    case 301:
        return "301 Moved Permanently";
    case 302:
        return "302 Found";
    case 400:
        return "400 Bad Request";
    case 401:
        return "401 Unauthorized";
    case 403:
        return "403 Forbidden";
    case 404:
        return "404 Not Found";
    case 405:
        return "405 Method Not Allowed";
    case 408:
        return "408 Request Timeout";
    case 409:
        return "409 Conflict";
    case 413:
        return "413 Content Too Large";
    case 415:
        return "415 Unsupported Media Type";
    case 429:
        return "429 Too Many Requests";
    case 503:
        return "503 Service Unavailable";
    default:
        return "500 Internal Server Error";
    }
}

esp_err_t sendResponse(httpd_req_t *req, const ApiResponse &r)
{
    httpd_resp_set_status(req, statusLine(r.status));
    httpd_resp_set_type(req, r.contentType.c_str());
    bool cache = false;
    for (const auto &[k, v] : r.headers) {
        httpd_resp_set_hdr(req, k.c_str(), v.c_str());
        cache = cache || k == "Cache-Control";
    }
    if (!cache) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    }
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, r.body.data(), static_cast<ssize_t>(r.body.size()));
}

// Unread request bodies would be parsed as the next request: close instead.
esp_err_t sendAndClose(httpd_req_t *req, const ApiResponse &r)
{
    httpd_resp_set_hdr(req, "Connection", "close");
    const esp_err_t err = sendResponse(req, r);
    httpd_sess_trigger_close(req->handle, httpd_req_to_sockfd(req));
    return err;
}

bool readBody(httpd_req_t *req, std::string &body)
{
    body.resize(req->content_len);
    std::size_t got = 0;
    int timeouts = 0;
    while (got < body.size()) {
        const int n = httpd_req_recv(req, body.data() + got, body.size() - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        got += static_cast<std::size_t>(n);
    }
    return true;
}

void restartSoon()
{
    static esp_timer_handle_t timer = nullptr;
    if (timer == nullptr) {
        esp_timer_create_args_t args{};
        args.callback = [](void *) { esp_restart(); };
        args.name = "zf_web_restart";
        esp_timer_create(&args, &timer);
    }
    esp_timer_start_once(timer, 500 * 1000);
}

void runPostAction(const ApiResponse &r)
{
    switch (r.action) {
    case PostAction::None:
        break;
    case PostAction::Reboot:
        ESP_LOGW(kTag, "reboot requested over the API");
        restartSoon();
        break;
    case PostAction::FactoryReset:
        ESP_LOGW(kTag, "factory reset requested over the API");
        net::factoryReset();
        break;
    case PostAction::Reprovision:
        net::requestProvisioning(r.transport);
        break;
    }
}

// HTTPS on: port 80 only redirects, except while the setup access point runs
// (phones reach the setup page over plain HTTP; the AP itself is WPA2).
bool redirectToHttps(httpd_req_t *req)
{
    if (gm->https == nullptr || req->handle != gm->http || net::status().provisioning == net::ProvTransport::SoftAp) {
        return false;
    }
    const std::string host(hostWithoutPort(header(req, "Host")));
    const std::string location = "https://" + (host.empty() ? std::string(net::status().ip.view()) : host) + req->uri;
    httpd_resp_set_status(req, statusLine(301));
    httpd_resp_set_hdr(req, "Location", location.c_str());
    httpd_resp_send(req, nullptr, 0);
    return true;
}

// ---- API ----------------------------------------------------------------------------------

esp_err_t apiHandler(httpd_req_t *req)
{
    noteStack();
    if (redirectToHttps(req)) {
        return ESP_OK;
    }
    RawRequest raw(req);
    const Admission a = gm->api->admit(raw.view());
    if (!a.ok) {
        return raw.contentLength > 0 ? sendAndClose(req, a.error) : sendResponse(req, a.error);
    }
    if (raw.contentLength > 0 && !readBody(req, raw.body)) {
        return sendAndClose(req, Api::error(408, "timeout", "request body not received"));
    }
    const ApiResponse r = gm->api->handle(raw.view());
    const esp_err_t err = sendResponse(req, r);
    runPostAction(r);
    noteStack();
    return err;
}

ApiResponse otaErrorResponse(ota::OtaError e)
{
    switch (e) {
    case ota::OtaError::ZonesActive:
        return Api::error(409, ota::toString(e), "watering is active; stop it or add ?force=true");
    case ota::OtaError::Busy:
        return Api::error(409, ota::toString(e), "another update is running");
    case ota::OtaError::TooLarge:
        return Api::error(413, ota::toString(e), "image larger than the partition");
    case ota::OtaError::BadImage:
    case ota::OtaError::WrongChip:
    case ota::OtaError::WrongProject:
    case ota::OtaError::Truncated:
    case ota::OtaError::VerifyFailed:
        return Api::error(400, ota::toString(e), "image rejected");
    default:
        return Api::error(500, ota::toString(e), "update failed");
    }
}

using StreamBegin = ota::OtaError (*)(std::size_t, bool);
using StreamWrite = ota::OtaError (*)(const uint8_t *, std::size_t);
using StreamFinish = ota::OtaError (*)();
using StreamAbort = void (*)();

esp_err_t streamUpload(httpd_req_t *req, StreamBegin begin, StreamWrite write, StreamFinish finish, StreamAbort abort,
                       const char *okBody)
{
    RawRequest raw(req);
    const Admission a = gm->api->admit(raw.view());
    if (!a.ok) {
        return sendAndClose(req, a.error);
    }
    if (raw.contentLength == 0) {
        return sendResponse(req, Api::error(400, "empty", "no image in the request body"));
    }
    ota::OtaError e = begin(raw.contentLength, queryFlag(raw.query, "force"));
    if (e != ota::OtaError::None) {
        return sendAndClose(req, otaErrorResponse(e));
    }
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunk]);
    if (!buf) {
        abort();
        return sendAndClose(req, Api::error(500, "no_memory", "no buffer for the upload"));
    }
    std::size_t left = raw.contentLength;
    int timeouts = 0;
    while (left > 0) {
        const int n = httpd_req_recv(req, reinterpret_cast<char *>(buf.get()), std::min(left, kChunk));
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) {
            continue;
        }
        if (n <= 0) {
            abort();
            return sendAndClose(req, Api::error(408, "aborted", "upload interrupted"));
        }
        timeouts = 0;
        left -= static_cast<std::size_t>(n);
        e = write(buf.get(), static_cast<std::size_t>(n));
        if (e != ota::OtaError::None) {
            return sendAndClose(req, otaErrorResponse(e)); // write() already ended the session
        }
    }
    e = finish();
    if (e != ota::OtaError::None) {
        return sendResponse(req, otaErrorResponse(e));
    }
    ApiResponse ok = Api::json(202, okBody);
    return sendResponse(req, ok);
}

esp_err_t firmwareUploadHandler(httpd_req_t *req)
{
    noteStack();
    if (redirectToHttps(req)) {
        return ESP_OK;
    }
    return streamUpload(req, &ota::uploadBegin, &ota::uploadWrite, &ota::uploadFinish, &ota::uploadAbort,
                        "{\"accepted\":true,\"note\":\"verified; restarting into the new image\"}");
}

void mountAssets()
{
    esp_vfs_littlefs_conf_t conf{};
    conf.base_path = kMount;
    conf.partition_label = kPartition;
    conf.format_if_mount_failed = false;
    conf.dont_mount = false;
    gm->mounted = esp_vfs_littlefs_register(&conf) == ESP_OK;
    if (!gm->mounted) {
        ESP_LOGW(kTag, "no web UI in the www partition; serving the built-in page");
    }
}

ota::OtaError assetsBegin(std::size_t total, bool force)
{
    if (gm->mounted) {
        esp_vfs_littlefs_unregister(kPartition);
        gm->mounted = false;
    }
    const ota::OtaError e = ota::assetsBegin(total, force);
    if (e != ota::OtaError::None) {
        mountAssets();
    }
    return e;
}
ota::OtaError assetsWrite(const uint8_t *d, std::size_t n)
{
    const ota::OtaError e = ota::assetsWrite(d, n);
    if (e != ota::OtaError::None) {
        mountAssets();
    }
    return e;
}
ota::OtaError assetsFinish()
{
    const ota::OtaError e = ota::assetsFinish();
    mountAssets();
    return e;
}
void assetsAbort()
{
    ota::assetsAbort();
    mountAssets();
}

esp_err_t assetsUploadHandler(httpd_req_t *req)
{
    noteStack();
    if (redirectToHttps(req)) {
        return ESP_OK;
    }
    return streamUpload(req, &assetsBegin, &assetsWrite, &assetsFinish, &assetsAbort,
                        "{\"accepted\":true,\"note\":\"web UI image written\"}");
}

// ---- event stream ------------------------------------------------------------------------------

std::string stateMessage(uint32_t id)
{
    const SystemState s = gm->services->state.snapshot();
    const auto cfg = std::make_unique<Config>(gm->services->config.get());
    cJSON *o = stateToJson(s, *cfg);
    cJSON_AddItemToObject(o, "network", netStatusToJson(net::status()));
    cJSON_AddItemToObject(o, "ota", otaStatusToJson(ota::status()));
    JsonPtr holder(o);
    return sseMessage("state", id, printJson(o));
}

esp_err_t eventsHandler(httpd_req_t *req)
{
    noteStack();
    if (redirectToHttps(req)) {
        return ESP_OK;
    }
    RawRequest raw(req);
    const Admission a = gm->api->admit(raw.view());
    if (!a.ok) {
        return sendResponse(req, a.error);
    }
    bool free = false;
    {
        std::lock_guard<std::mutex> lock(gm->streamMutex);
        free = std::find(gm->streams.begin(), gm->streams.end(), nullptr) != gm->streams.end();
    }
    if (!free) {
        ApiResponse busy = Api::error(503, "too_many_streams", "at most 3 event streams");
        busy.headers.emplace_back("Retry-After", "10");
        return sendResponse(req, busy);
    }
    httpd_req_t *async = nullptr;
    if (httpd_req_async_handler_begin(req, &async) != ESP_OK) {
        return sendResponse(req, Api::error(500, "internal", "stream not started"));
    }
    httpd_resp_set_type(async, "text/event-stream");
    httpd_resp_set_hdr(async, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(async, "X-Content-Type-Options", "nosniff");
    // Sent without the stream lock: a slow client must not hold up the
    // stream task or other requests.
    const std::string first = "retry: 3000\n\n" + stateMessage(0);
    if (httpd_resp_send_chunk(async, first.data(), static_cast<ssize_t>(first.size())) != ESP_OK) {
        httpd_req_async_handler_complete(async);
        return ESP_OK;
    }
    std::lock_guard<std::mutex> lock(gm->streamMutex);
    auto slot = std::find(gm->streams.begin(), gm->streams.end(), nullptr);
    if (slot == gm->streams.end()) {
        httpd_req_async_handler_complete(async); // lost a race for the last slot
    } else {
        *slot = async;
    }
    return ESP_OK;
}

// Only this task removes streams, so the copy taken under the lock stays
// valid while sending without it.
void broadcast(const std::string &msg)
{
    std::array<httpd_req_t *, kMaxStreams> clients{};
    {
        std::lock_guard<std::mutex> lock(gm->streamMutex);
        clients = gm->streams;
    }
    for (httpd_req_t *c : clients) {
        if (c != nullptr && httpd_resp_send_chunk(c, msg.data(), static_cast<ssize_t>(msg.size())) != ESP_OK) {
            {
                std::lock_guard<std::mutex> lock(gm->streamMutex);
                std::replace(gm->streams.begin(), gm->streams.end(), c, static_cast<httpd_req_t *>(nullptr));
            }
            httpd_req_async_handler_complete(c);
        }
    }
}

bool anyStream()
{
    std::lock_guard<std::mutex> lock(gm->streamMutex);
    return std::any_of(gm->streams.begin(), gm->streams.end(), [](httpd_req_t *c) { return c != nullptr; });
}

void streamTask(void *)
{
    uint32_t id = 0;
    uint32_t stateSeq = gm->services->state.sequence();
    uint32_t netSeq = net::statusSequence();
    ota::OtaStatus lastOta = ota::status();
    int64_t lastState = 0;
    int64_t lastSend = 0;
    int64_t lastOtaSend = 0;
    for (;;) {
        Event e;
        const bool got = gm->events.receive(e, pdMS_TO_TICKS(250));
        const int64_t now = esp_timer_get_time() / 1000;
        if (!anyStream()) {
            stateSeq = gm->services->state.sequence();
            continue;
        }
        if (got) {
            JsonPtr o(eventToJson(e));
            broadcast(sseMessage("event", ++id, printJson(o.get())));
            lastSend = now;
        }
        const uint32_t seq = gm->services->state.sequence();
        if ((seq != stateSeq && now - lastState >= 500) || now - lastState >= 30000) {
            stateSeq = seq;
            lastState = now;
            lastSend = now;
            broadcast(stateMessage(++id));
        }
        if (const uint32_t ns = net::statusSequence(); ns != netSeq) {
            netSeq = ns;
            JsonPtr o(netStatusToJson(net::status()));
            broadcast(sseMessage("network", ++id, printJson(o.get())));
            lastSend = now;
        }
        const ota::OtaStatus os = ota::status();
        if ((os.phase != lastOta.phase || os.error != lastOta.error ||
             (os.receivedBytes != lastOta.receivedBytes && now - lastOtaSend >= 1000))) {
            lastOta = os;
            lastOtaSend = now;
            JsonPtr o(otaStatusToJson(os));
            broadcast(sseMessage("ota", ++id, printJson(o.get())));
            lastSend = now;
        }
        if (now - lastSend >= 15000) {
            broadcast(": keep-alive\n\n");
            lastSend = now;
        }
    }
}

// ---- static files and built-in pages -------------------------------------------------------------

esp_err_t sendBuiltin(httpd_req_t *req, const char *start, const char *end)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Content-Security-Policy", kBuiltinCsp);
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    return httpd_resp_send(req, start, end - start - 1); // EMBED_TXTFILES adds a NUL
}

esp_err_t staticHandler(httpd_req_t *req)
{
    noteStack();
    if (redirectToHttps(req)) {
        return ESP_OK;
    }
    const net::NetStatus n = net::status();
    const std::string host = header(req, "Host");
    const std::string_view uri(req->uri);
    const std::string_view path = uri.substr(0, uri.find('?'));
    if (n.provisioning == net::ProvTransport::SoftAp) {
        // Captive portal: any foreign host name (OS connectivity checks)
        // goes to the setup page.
        if (!hostAllowed(host, n.hostname.view())) {
            httpd_resp_set_status(req, statusLine(302));
            httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/setup");
            return httpd_resp_send(req, nullptr, 0);
        }
        if (path == "/setup" || path == "/") {
            return sendBuiltin(req, kSetupHtmlStart, kSetupHtmlEnd);
        }
    }
    StaticTarget t;
    if (gm->mounted) {
        t = resolveStatic(path, acceptsGzip(header(req, "Accept-Encoding")), [](const std::string &p) {
            struct stat st{};
            const std::string full = std::string(kMount) + p;
            return stat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode);
        });
    }
    if (t.kind == StaticTarget::Kind::BadPath) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    if (t.kind != StaticTarget::Kind::File) {
        if (path == "/" || path == "/index.html") {
            return sendBuiltin(req, kFallbackHtmlStart, kFallbackHtmlEnd);
        }
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }
    const std::string full = std::string(kMount) + t.path;
    FILE *f = std::fopen(full.c_str(), "rb");
    if (f == nullptr) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }
    httpd_resp_set_type(req, t.contentType);
    httpd_resp_set_hdr(req, "Cache-Control", t.cacheControl);
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "Vary", "Accept-Encoding");
    if (t.gzip) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    if (std::string_view(t.contentType).rfind("text/html", 0) == 0) {
        httpd_resp_set_hdr(req, "Content-Security-Policy", kUiCsp);
    }
    std::unique_ptr<char[]> buf(new (std::nothrow) char[kChunk]);
    esp_err_t err = buf ? ESP_OK : ESP_ERR_NO_MEM;
    while (err == ESP_OK) {
        const std::size_t n = std::fread(buf.get(), 1, kChunk, f);
        if (n == 0) {
            break;
        }
        err = httpd_resp_send_chunk(req, buf.get(), static_cast<ssize_t>(n));
    }
    std::fclose(f);
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, nullptr, 0);
    }
    return err;
}

// ---- server setup --------------------------------------------------------------------------------------

httpd_config_t serverConfig(uint16_t port)
{
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.server_port = port;
    c.ctrl_port = static_cast<uint16_t>(32768 + port);
    c.stack_size = 12288;
    c.core_id = 0;
    c.task_priority = 5;
    c.max_uri_handlers = 20; // ours (11) + SoftAP provisioning endpoints
    c.max_resp_headers = 12;
    c.max_open_sockets = 7;
    c.lru_purge_enable = true;
    c.recv_wait_timeout = 10;
    c.send_wait_timeout = 10;
    c.uri_match_fn = httpd_uri_match_wildcard;
    return c;
}

esp_err_t registerHandlers(httpd_handle_t h)
{
    const httpd_uri_t uris[] = {
        {"/api/v1/events", HTTP_GET, &eventsHandler, nullptr},
        {"/api/v1/firmware/upload", HTTP_POST, &firmwareUploadHandler, nullptr},
        {"/api/v1/firmware/assets", HTTP_POST, &assetsUploadHandler, nullptr},
        {"/api/v1/*", HTTP_GET, &apiHandler, nullptr},
        {"/api/v1/*", HTTP_POST, &apiHandler, nullptr},
        {"/api/v1/*", HTTP_PUT, &apiHandler, nullptr},
        {"/api/v1/*", HTTP_PATCH, &apiHandler, nullptr},
        {"/api/v1/*", HTTP_DELETE, &apiHandler, nullptr},
        {"/*", HTTP_GET, &staticHandler, nullptr},
    };
    for (const httpd_uri_t &u : uris) {
        const esp_err_t err = httpd_register_uri_handler(h, &u);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t startHttps()
{
    if (!loadOrCreateTls(gm->store, std::string(net::status().hostname.view()), gm->tls)) {
        return ESP_FAIL;
    }
    httpd_ssl_config_t c = HTTPD_SSL_CONFIG_DEFAULT();
    c.httpd = serverConfig(443);
    c.httpd.max_open_sockets = 4; // each TLS session needs tens of KB of heap
    c.port_secure = 443;
    c.servercert = reinterpret_cast<const uint8_t *>(gm->tls.certPem.c_str());
    c.servercert_len = gm->tls.certPem.size() + 1;
    c.prvtkey_pem = reinterpret_cast<const uint8_t *>(gm->tls.keyPem.c_str());
    c.prvtkey_len = gm->tls.keyPem.size() + 1;
    const esp_err_t err = httpd_ssl_start(&gm->https, &c);
    if (err != ESP_OK) {
        gm->https = nullptr;
        return err;
    }
    return registerHandlers(gm->https);
}

esp_err_t startImpl(const AppServices &services)
{
    static Module module;
    gm = &module;
    gm->services = &services;
    if (const esp_err_t err = gm->store.open(); err != ESP_OK) {
        return err;
    }
    gm->auth = std::make_unique<AuthService>(gm->store, gm->rng);
    if (!gm->auth->load()) {
        ESP_LOGE(kTag, "stored admin password record is malformed; treating it as unset");
    }
    gm->api = std::make_unique<Api>(services, *gm->auth, gm->port, gm->store, gm->clock);
    gm->api->begin();
    gm->settings = gm->api->settings();
    mountAssets();

    httpd_config_t c = serverConfig(80);
    esp_err_t err = httpd_start(&gm->http, &c);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "HTTP server failed to start: %s", esp_err_to_name(err));
        return err;
    }
    if (err = registerHandlers(gm->http); err != ESP_OK) {
        return err;
    }
    net::setHttpServer(gm->http);
    if (gm->settings.https) {
        err = startHttps();
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "HTTPS server failed (%s); serving HTTP only", esp_err_to_name(err));
        } else {
            ESP_LOGI(kTag, "HTTPS on 443 (self-signed, SHA-256 %s); port 80 redirects", gm->tls.fingerprint.c_str());
        }
    }

    if (gm->events.begin(services.events, kAllEvents, 32) != ESP_OK ||
        xTaskCreatePinnedToCore(&streamTask, "zf_web_sse", 6144, nullptr, 3, nullptr, 0) != pdPASS) {
        ESP_LOGE(kTag, "event stream not started; REST still works");
    }
    ota::notifyWebStarted();
    ESP_LOGI(kTag, "web server on port 80 (%s, password %s)", gm->mounted ? "UI from www" : "built-in page",
             gm->auth->passwordSet() ? "set" : "not set");
    return ESP_OK;
}

} // namespace

esp_err_t start(const AppServices &services)
{
    if (const esp_err_t err = startImpl(services); err != ESP_OK) {
        // Never fatal: the controller waters without its web server. A new
        // image whose server does not start fails the OTA self-test.
        ESP_LOGE(kTag, "web server disabled: %s", esp_err_to_name(err));
    }
    return ESP_OK;
}

} // namespace zf::web
