// SPDX-License-Identifier: Apache-2.0
// zf_net device glue: ESP-IDF WiFi, unified provisioning, mDNS and the
// captive-portal DNS responder. Decisions come from net_logic.hpp.
//
// Task: zf_net (priority 5, core 0). ESP-IDF event handlers only copy a small
// message into the zf_net queue; all WiFi and provisioning calls happen on
// the zf_net task.
#include "zf_net/net.hpp"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_srp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "protocomm_ble.h"
#include "protocomm_security.h"
#include "sdkconfig.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "zf_net/nvs_store.hpp"
#if CONFIG_BT_ENABLED
#include "esp_bt.h"
#include "wifi_provisioning/scheme_ble.h"
#endif

namespace zf::net {
namespace {

constexpr const char *kTag = "zf.net";
constexpr const char *kKeyCode = "setup_code";
constexpr const char *kKeySalt = "srp_salt";
constexpr const char *kKeyVerifier = "srp_verifier";
constexpr const char *kKeyReprov = "reprov";
constexpr int kSaltLen = 16;
constexpr uint32_t kSoftApIp = 0xC0A80401; // 192.168.4.1, ESP-IDF default AP address

// Returns the error from the enclosing function instead of aborting: a
// network failure must never stop the controller (it waters offline).
#define ZF_TRY(expr)                                                                                                   \
    do {                                                                                                               \
        const esp_err_t zfErr = (expr);                                                                                \
        if (zfErr != ESP_OK) {                                                                                         \
            ESP_LOGE(kTag, "%s failed: %s", #expr, esp_err_to_name(zfErr));                                            \
            return zfErr;                                                                                              \
        }                                                                                                              \
    } while (0)

enum class Msg : uint8_t {
    StaStart,
    StaDisconnected,
    GotIp,
    LostIp,
    ProvCredRecv,
    ProvCredFail,
    ProvCredSuccess,
    ProvEnd,
    SessionOpen,
    SessionClosed,
    ApJoin,
    ApLeave,
    Reprovision,
    FactoryReset,
};

struct NetMsg {
    Msg type;
    uint8_t arg;
};

struct Identity {
    std::string code;
    std::string salt;
    std::string verifier;
};

struct Module {
    const AppServices *services = nullptr;
    NvsNamespace store{"zf_net"};
    Identity identity;
    std::array<uint8_t, 6> mac{};
    std::string hostname;
    std::string serviceName;
    wifi_prov_security2_params_t sec2{};
    esp_netif_t *sta = nullptr;
    esp_netif_t *ap = nullptr;
    QueueHandle_t queue = nullptr;
    std::atomic<void *> httpd{nullptr};
    NetStatusStore statusStore;
    NetStatus status{}; // task-owned working copy
    NetStatus published{};
    MonoMs lastPublish = 0;
    StationPolicy station;
    ProvisioningFsm prov;
    std::atomic<bool> provActive{false};
    bool stationRunning = false;
    int apClients = 0;
    std::atomic<bool> dnsRun{false};
    std::mutex scanMutex;
};

Module m;

MonoMs nowMs() { return static_cast<MonoMs>(esp_timer_get_time() / 1000); }

void send(Msg type, uint8_t arg = 0)
{
    if (m.queue != nullptr) {
        const NetMsg msg{type, arg};
        xQueueSend(m.queue, &msg, 0); // never block an event-loop handler
    }
}

// ---- identity ---------------------------------------------------------------------

bool loadIdentity(Identity &id)
{
    return m.store.read(kKeyCode, id.code) && validSetupCode(id.code) && m.store.read(kKeySalt, id.salt) &&
           m.store.read(kKeyVerifier, id.verifier) && !id.salt.empty() && !id.verifier.empty();
}

bool storeIdentity(const Identity &id)
{
    return m.store.write(kKeyCode, id.code) && m.store.write(kKeySalt, id.salt) &&
           m.store.write(kKeyVerifier, id.verifier);
}

esp_err_t ensureIdentity()
{
    if (loadIdentity(m.identity)) {
        return ESP_OK;
    }
    // The hardware RNG is a true random source while the radio runs
    // (ESP32-S3 TRM, Random Number Generator), so start WiFi briefly.
    ZF_TRY(esp_wifi_set_mode(WIFI_MODE_STA));
    ZF_TRY(esp_wifi_start());
    uint8_t rnd[kSetupCodeLen];
    esp_fill_random(rnd, sizeof rnd);
    Identity id;
    id.code = makeSetupCode(rnd, sizeof rnd);
    char *salt = nullptr;
    char *verifier = nullptr;
    int verifierLen = 0;
    const esp_err_t err =
        esp_srp_gen_salt_verifier(kProvUsername.data(), static_cast<int>(kProvUsername.size()), id.code.c_str(),
                                  static_cast<int>(id.code.size()), &salt, kSaltLen, &verifier, &verifierLen);
    esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "SRP verifier generation failed: %s", esp_err_to_name(err));
        return err;
    }
    id.salt.assign(salt, kSaltLen);
    id.verifier.assign(verifier, static_cast<std::size_t>(verifierLen));
    free(salt);
    free(verifier);
    if (!storeIdentity(id)) {
        ESP_LOGE(kTag, "device identity not stored");
        return ESP_FAIL;
    }
    m.identity = id;
    ESP_LOGI(kTag, "device identity created (setup code stored in NVS)");
    return ESP_OK;
}

ProvTransport storedRequest()
{
    std::string v;
    if (!m.store.read(kKeyReprov, v) || v.size() != 1) {
        return ProvTransport::None;
    }
    return static_cast<ProvTransport>(v[0]) == ProvTransport::SoftAp ? ProvTransport::SoftAp : ProvTransport::Ble;
}

bool stationProvisioned()
{
    wifi_config_t c{};
    return esp_wifi_get_config(WIFI_IF_STA, &c) == ESP_OK && c.sta.ssid[0] != 0;
}

// ---- status ----------------------------------------------------------------------------

void ip4ToString(const esp_ip4_addr_t &a, FixedString<15> &out)
{
    char buf[16];
    esp_ip4addr_ntoa(&a, buf, sizeof buf);
    out.assign(buf);
}

void macToString(const uint8_t *mac, FixedString<17> &out)
{
    char buf[18];
    std::snprintf(buf, sizeof buf, "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    out.assign(buf);
}

bool materiallyEqual(const NetStatus &a, const NetStatus &b)
{
    return a.state == b.state && a.provisioning == b.provisioning && a.provisioned == b.provisioned &&
           a.ssid == b.ssid && a.bssid == b.bssid && a.channel == b.channel && a.ip == b.ip &&
           a.reconnectAttempt == b.reconnectAttempt && a.mdnsRunning == b.mdnsRunning && std::abs(a.rssi - b.rssi) < 5;
}

void publish(bool force = false)
{
    const MonoMs now = nowMs();
    m.status.reconnectAttempt = m.station.attempts();
    m.status.retryInMs = m.station.retryInMs(now);
    if (m.status.state == NetState::Connected) {
        wifi_ap_record_t ap{};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            m.status.rssi = ap.rssi;
            m.status.channel = ap.primary;
            m.status.ssid.assign(reinterpret_cast<const char *>(ap.ssid));
            macToString(ap.bssid, m.status.bssid);
        }
    }
    if (force || !materiallyEqual(m.status, m.published) || now - m.lastPublish >= 30000) {
        m.statusStore.publish(m.status);
        m.published = m.status;
        m.lastPublish = now;
    }
}

// ---- mDNS ----------------------------------------------------------------------------------

void startMdns()
{
    if (m.status.mdnsRunning) {
        return;
    }
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(kTag, "mDNS init failed");
        return;
    }
    mdns_hostname_set(m.hostname.c_str());
    const Config cfg = m.services->config.get();
    mdns_instance_name_set(cfg.deviceName.c_str());
    const esp_app_desc_t *app = esp_app_get_description();
    char mac[13];
    std::snprintf(mac, sizeof mac, "%02x%02x%02x%02x%02x%02x", m.mac[0], m.mac[1], m.mac[2], m.mac[3], m.mac[4],
                  m.mac[5]);
    mdns_txt_item_t txt[] = {
        {"api", "/api/v1"},
        {"fw", app->version},
        {"id", mac},
    };
    mdns_service_add(cfg.deviceName.c_str(), "_http", "_tcp", 80, txt, 1);
    mdns_service_add(cfg.deviceName.c_str(), "_zoneflow", "_tcp", 80, txt, sizeof txt / sizeof txt[0]);
    m.status.mdnsRunning = true;
    ESP_LOGI(kTag, "mDNS: %s.local, _http._tcp and _zoneflow._tcp on port 80", m.hostname.c_str());
}

// ---- captive DNS ------------------------------------------------------------------------------

void dnsTask(void *)
{
    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock >= 0) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(53);
        addr.sin_addr.s_addr = htonl(kSoftApIp); // AP interface only, never the home LAN
        timeval tv{1, 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0) {
            uint8_t in[512];
            uint8_t out[512 + 16];
            while (m.dnsRun.load()) {
                sockaddr_in from{};
                socklen_t fromLen = sizeof from;
                const int n = recvfrom(sock, in, sizeof in, 0, reinterpret_cast<sockaddr *>(&from), &fromLen);
                if (n <= 0) {
                    continue;
                }
                const std::size_t len = captiveDnsAnswer(in, static_cast<std::size_t>(n), kSoftApIp, out, sizeof out);
                if (len > 0) {
                    sendto(sock, out, len, 0, reinterpret_cast<sockaddr *>(&from), fromLen);
                }
            }
        } else {
            ESP_LOGW(kTag, "captive DNS: bind failed");
        }
        close(sock);
    }
    vTaskDelete(nullptr);
}

void startDns()
{
    if (!m.dnsRun.exchange(true)) {
        xTaskCreatePinnedToCore(&dnsTask, "zf_dns", 3072, nullptr, 3, nullptr, 0);
    }
}

void stopDns() { m.dnsRun.store(false); }

// ---- station ------------------------------------------------------------------------------------

esp_err_t startStation()
{
    wifi_config_t c{};
    esp_wifi_get_config(WIFI_IF_STA, &c);
    // Never lock to one access point or channel (prior-art recommendation 8,
    // OpenSprinkler #413): every connect scans all channels and joins the
    // strongest AP of the SSID, so mesh nodes and channel changes work.
    c.sta.bssid_set = false;
    c.sta.channel = 0;
    c.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    c.sta.threshold.authmode = c.sta.password[0] != 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    c.sta.pmf_cfg.capable = true;
    c.sta.pmf_cfg.required = false;
    c.sta.rm_enabled = 1;  // 802.11k neighbour reports
    c.sta.btm_enabled = 1; // 802.11v BSS transition: mesh systems steer clients
    c.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    c.sta.failure_retry_cnt = 2;
    ZF_TRY(esp_wifi_set_mode(WIFI_MODE_STA));
    ZF_TRY(esp_wifi_set_config(WIFI_IF_STA, &c));
    m.station.start();
    m.stationRunning = true;
    m.status.state = NetState::Connecting;
    m.status.provisioned = c.sta.ssid[0] != 0;
    m.status.ssid.assign(reinterpret_cast<const char *>(c.sta.ssid));
    // WIFI_EVENT_STA_START triggers the first connect (see handle()).
    ZF_TRY(esp_wifi_start());
    return ESP_OK;
}

// ---- provisioning ---------------------------------------------------------------------------------

void logSetup(ProvTransport t)
{
    // The setup code appears in the log only while provisioning runs, when
    // the person at the device needs it. The API never returns it.
    ESP_LOGW(kTag, "provisioning over %s as \"%s\"; setup code %s", toString(t), m.serviceName.c_str(),
             m.identity.code.c_str());
    ESP_LOGI(kTag, "QR payload: %s", qrPayload(m.serviceName, m.identity.code, t).c_str());
}

bool startProvisioning(ProvTransport t)
{
    wifi_prov_mgr_config_t cfg{};
#if CONFIG_BT_ENABLED
    if (t == ProvTransport::Ble) {
        cfg.scheme = wifi_prov_scheme_ble;
        // Frees the BT controller and NimBLE memory when provisioning ends.
        cfg.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM;
    } else
#endif
    {
        t = ProvTransport::SoftAp;
        cfg.scheme = wifi_prov_scheme_softap;
        cfg.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
        // Share zf_web's server on port 80 so the setup page and the
        // provisioning endpoints live together. Wait for it briefly.
        for (int i = 0; i < 100 && m.httpd.load() == nullptr; ++i) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (void *h = m.httpd.load()) {
            wifi_prov_scheme_softap_set_httpd_handle(h);
        }
    }
    if (wifi_prov_mgr_init(cfg) != ESP_OK) {
        ESP_LOGE(kTag, "provisioning manager init failed");
        return false;
    }
    m.sec2.salt = m.identity.salt.data();
    m.sec2.salt_len = static_cast<uint16_t>(m.identity.salt.size());
    m.sec2.verifier = m.identity.verifier.data();
    m.sec2.verifier_len = static_cast<uint16_t>(m.identity.verifier.size());
    // SoftAP: WPA2 with the setup code as passphrase, so the setup page and
    // the credentials it carries are encrypted on the air.
    const char *key = t == ProvTransport::SoftAp ? m.identity.code.c_str() : nullptr;
    const esp_err_t err = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_2, &m.sec2, m.serviceName.c_str(), key);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "provisioning start failed: %s", esp_err_to_name(err));
        wifi_prov_mgr_deinit();
        return false;
    }
    m.prov.begin(t, m.status.provisioned, nowMs());
    m.provActive = true;
    m.stationRunning = false;
    m.status.state = NetState::Provisioning;
    m.status.provisioning = t;
    if (t == ProvTransport::SoftAp) {
        startDns();
    }
    logSetup(t);
    return true;
}

void endProvisioning()
{
    stopDns();
    wifi_prov_mgr_deinit();
    m.provActive = false;
    m.apClients = 0;
    m.status.provisioning = ProvTransport::None;
}

void runProvAction(ProvAction a)
{
    switch (a) {
    case ProvAction::None:
        break;
    case ProvAction::SwitchToSoftAp:
        ESP_LOGI(kTag, "no BLE setup within the window; switching to the setup access point");
        endProvisioning();
        startProvisioning(ProvTransport::SoftAp);
        break;
    case ProvAction::AbandonToStation:
        ESP_LOGI(kTag, "re-provisioning timed out; back to the stored network");
        endProvisioning();
        m.store.erase(kKeyReprov);
        esp_wifi_stop();
        startStation();
        break;
    case ProvAction::RetryAfterFailure:
        wifi_prov_mgr_reset_sm_state_on_failure();
        break;
    case ProvAction::Finish:
        m.store.erase(kKeyReprov);
        break;
    }
}

// ---- factory reset ----------------------------------------------------------------------------------

bool outputsOff(const SystemState &s)
{
    return s.maintenance && !s.masterOn &&
           std::all_of(s.zones.begin(), s.zones.end(), [](const ZoneStatus &z) { return z.state == ValveState::Off; });
}

void doFactoryReset()
{
    ESP_LOGW(kTag, "factory reset: stopping watering");
    m.services->commands.post(Command::maintenance(true));
    for (int i = 0; i < 50 && !outputsOff(m.services->state.snapshot()); ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    // The restart below forces outputs off in any case (shutdown handler).
    const Identity keep = m.identity;
    nvs_flash_erase();
    nvs_flash_init();
    NvsNamespace fresh("zf_net");
    if (fresh.open() == ESP_OK) {
        m.store = fresh;
        storeIdentity(keep);
    }
    ESP_LOGW(kTag, "factory reset: NVS erased (setup code kept); restarting");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

// ---- event plumbing ------------------------------------------------------------------------------------

void onEvent(void *, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            send(Msg::StaStart);
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            send(Msg::StaDisconnected, static_cast<wifi_event_sta_disconnected_t *>(data)->reason);
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            send(Msg::ApJoin);
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            send(Msg::ApLeave);
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            send(Msg::GotIp);
        } else if (id == IP_EVENT_STA_LOST_IP) {
            send(Msg::LostIp);
        }
    } else if (base == WIFI_PROV_EVENT) {
        switch (id) {
        case WIFI_PROV_CRED_RECV:
            send(Msg::ProvCredRecv);
            break;
        case WIFI_PROV_CRED_FAIL:
            send(Msg::ProvCredFail, static_cast<uint8_t>(*static_cast<wifi_prov_sta_fail_reason_t *>(data)));
            break;
        case WIFI_PROV_CRED_SUCCESS:
            send(Msg::ProvCredSuccess);
            break;
        case WIFI_PROV_END:
            send(Msg::ProvEnd);
            break;
        default:
            break;
        }
    } else if (base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        // A BLE connection alone proves nothing: anyone in radio range can
        // open one, so it must not hold the window (security review F3).
        if (id == PROTOCOMM_TRANSPORT_BLE_DISCONNECTED) {
            send(Msg::SessionClosed);
        }
    } else if (base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        if (id == PROTOCOMM_SECURITY_SESSION_SETUP_OK) {
            // Security 2 handshake done: the phone knows the setup code.
            send(Msg::SessionOpen);
        } else if (id == PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH) {
            ESP_LOGW(kTag, "provisioning: wrong setup code from the phone");
        }
    }
}

void handle(const NetMsg &msg)
{
    const MonoMs now = nowMs();
    switch (msg.type) {
    case Msg::StaStart:
        if (m.stationRunning) {
            esp_wifi_connect();
        }
        break;
    case Msg::StaDisconnected:
        m.status.lastDisconnectReason = msg.arg;
        ++m.status.disconnects;
        m.status.ip.assign("");
        m.status.gateway.assign("");
        m.status.netmask.assign("");
        if (m.stationRunning) {
            m.station.onDisconnected(now, esp_random());
            m.status.state = NetState::WaitingRetry;
            ESP_LOGI(kTag, "disconnected (reason %u); retry in %" PRIu32 " ms", msg.arg, m.station.retryInMs(now));
        }
        break;
    case Msg::GotIp: {
        esp_netif_ip_info_t ip{};
        esp_netif_get_ip_info(m.sta, &ip);
        ip4ToString(ip.ip, m.status.ip);
        ip4ToString(ip.gw, m.status.gateway);
        ip4ToString(ip.netmask, m.status.netmask);
        m.station.onConnected();
        if (!m.provActive) {
            m.stationRunning = true;
            m.status.state = NetState::Connected;
        }
        m.status.connectedSinceMs = now;
        m.status.provisioned = true;
        ESP_LOGI(kTag, "connected, IP %s", m.status.ip.c_str());
        startMdns();
        break;
    }
    case Msg::LostIp:
        m.status.ip.assign("");
        if (m.status.state == NetState::Connected) {
            m.status.state = NetState::Connecting;
        }
        break;
    case Msg::ProvCredRecv:
        m.prov.onSessionStarted();
        ESP_LOGI(kTag, "provisioning: credentials received, trying them");
        break;
    case Msg::ProvCredFail:
        ESP_LOGW(kTag, "provisioning: %s",
                 msg.arg == WIFI_PROV_STA_AUTH_ERROR ? "wrong WiFi password" : "AP not found");
        runProvAction(m.prov.onCredentialsFailed());
        break;
    case Msg::ProvCredSuccess:
        ESP_LOGI(kTag, "provisioning: credentials work");
        runProvAction(m.prov.onSuccess());
        break;
    case Msg::ProvEnd:
        endProvisioning();
        m.stationRunning = true;
        esp_wifi_set_mode(WIFI_MODE_STA);
        m.status.state = m.status.ip.empty() ? NetState::Connecting : NetState::Connected;
        break;
    case Msg::SessionOpen:
        m.prov.onSessionStarted();
        break;
    case Msg::SessionClosed:
        m.prov.onSessionEnded();
        break;
    case Msg::ApJoin:
        if (++m.apClients == 1) {
            m.prov.onSessionStarted();
        }
        break;
    case Msg::ApLeave:
        m.apClients = std::max(0, m.apClients - 1);
        if (m.apClients == 0) {
            m.prov.onSessionEnded();
        }
        break;
    case Msg::Reprovision:
        ESP_LOGW(kTag, "restarting into provisioning (%s)", toString(static_cast<ProvTransport>(msg.arg)));
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();
        break;
    case Msg::FactoryReset:
        doFactoryReset();
        break;
    }
}

void netTask(void *)
{
    const bool provisioned = stationProvisioned();
    m.status.provisioned = provisioned;
#if CONFIG_BT_ENABLED
    constexpr bool kBle = true;
#else
    constexpr bool kBle = false;
#endif
    const BootPlan plan = ProvisioningFsm::plan(provisioned, storedRequest(), kBle);
    bool started = false;
    if (plan.provision) {
        started = startProvisioning(plan.transport);
    }
    if (!started) {
#if CONFIG_BT_ENABLED
        // Not provisioning this boot: give the BT controller memory back
        // to the heap. Re-provisioning restarts the device first.
        esp_bt_mem_release(ESP_BT_MODE_BTDM);
#endif
        startStation();
    }
    publish(true);
    for (;;) {
        NetMsg msg;
        if (xQueueReceive(m.queue, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
            handle(msg);
        }
        const MonoMs now = nowMs();
        if (m.provActive) {
            runProvAction(m.prov.tick(now));
        } else if (m.stationRunning && m.station.due(now)) {
            m.status.state = NetState::Connecting;
            esp_wifi_connect();
        }
        publish();
    }
}

// ---- start -------------------------------------------------------------------------------------------------

esp_err_t startImpl(const AppServices &services)
{
    m.services = &services;
    ZF_TRY(m.store.open());
    esp_read_mac(m.mac.data(), ESP_MAC_WIFI_STA);
    m.hostname = makeHostname(m.mac);
    m.serviceName = makeServiceName(m.mac);
    m.status.hostname.assign(m.hostname);
    macToString(m.mac.data(), m.status.mac);

    m.sta = esp_netif_create_default_wifi_sta();
    m.ap = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(m.sta, m.hostname.c_str());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ZF_TRY(esp_wifi_init(&cfg));
    ZF_TRY(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    ZF_TRY(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onEvent, nullptr));
    ZF_TRY(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &onEvent, nullptr));
    ZF_TRY(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &onEvent, nullptr));
    ZF_TRY(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &onEvent, nullptr));
    ZF_TRY(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &onEvent, nullptr));

    ZF_TRY(ensureIdentity());
    m.queue = xQueueCreate(16, sizeof(NetMsg));
    if (m.queue == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    m.statusStore.publish(m.status);
    if (xTaskCreatePinnedToCore(&netTask, "zf_net", 6144, nullptr, 5, nullptr, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "hostname %s, service name %s", m.hostname.c_str(), m.serviceName.c_str());
    return ESP_OK;
}

} // namespace

// ---- public API ------------------------------------------------------------------------------------------

esp_err_t start(const AppServices &services)
{
    if (startImpl(services) != ESP_OK) {
        ESP_LOGE(kTag, "network disabled; the controller keeps watering offline");
    }
    return ESP_OK;
}

NetStatus status() { return m.statusStore.snapshot(); }

uint32_t statusSequence() { return m.statusStore.sequence(); }

SetupInfo setupInfo()
{
    SetupInfo s;
    s.setupCode = m.identity.code;
    s.serviceName = m.serviceName;
    s.qrPayload = qrPayload(m.serviceName, m.identity.code, ProvTransport::Ble);
    s.qrPayloadSoftAp = qrPayload(m.serviceName, m.identity.code, ProvTransport::SoftAp);
    return s;
}

bool requestProvisioning(ProvTransport transport)
{
    if (transport == ProvTransport::None) {
        return false;
    }
    const char v = static_cast<char>(transport);
    if (!m.store.write(kKeyReprov, std::string_view(&v, 1))) {
        return false;
    }
    send(Msg::Reprovision, static_cast<uint8_t>(transport));
    return true;
}

bool factoryReset()
{
    send(Msg::FactoryReset);
    return m.queue != nullptr;
}

void setHttpServer(void *httpdHandle) { m.httpd.store(httpdHandle); }

bool isSetupApAddress(uint32_t ipv4)
{
    esp_netif_ip_info_t ap{};
    if (ipv4 == 0 || m.ap == nullptr || esp_netif_get_ip_info(m.ap, &ap) != ESP_OK || ap.ip.addr != ipv4) {
        return false;
    }
    esp_netif_ip_info_t sta{};
    return m.sta == nullptr || esp_netif_get_ip_info(m.sta, &sta) != ESP_OK || sta.ip.addr != ipv4;
}

bool provisionWifi(std::string_view ssid, std::string_view password)
{
    if (!m.provActive || ssid.empty() || ssid.size() > 32 || password.size() > 64) {
        return false;
    }
    wifi_config_t c{};
    std::memcpy(c.sta.ssid, ssid.data(), ssid.size());
    std::memcpy(c.sta.password, password.data(), password.size());
    return wifi_prov_mgr_configure_sta(&c) == ESP_OK;
}

std::vector<ScanEntry> scan()
{
    std::lock_guard<std::mutex> lock(m.scanMutex);
    std::vector<ScanEntry> out;
    wifi_scan_config_t sc{};
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        return out;
    }
    uint16_t n = 20;
    wifi_ap_record_t recs[20];
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        return out;
    }
    for (uint16_t i = 0; i < n; ++i) {
        const std::string ssid(reinterpret_cast<const char *>(recs[i].ssid));
        if (ssid.empty()) {
            continue;
        }
        auto it = std::find_if(out.begin(), out.end(), [&](const ScanEntry &e) { return e.ssid == ssid; });
        if (it == out.end()) {
            out.push_back({ssid, recs[i].rssi, recs[i].authmode != WIFI_AUTH_OPEN});
        } else {
            it->rssi = std::max(it->rssi, recs[i].rssi);
        }
    }
    std::sort(out.begin(), out.end(), [](const ScanEntry &a, const ScanEntry &b) { return a.rssi > b.rssi; });
    return out;
}

} // namespace zf::net
