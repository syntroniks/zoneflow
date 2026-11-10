// SPDX-License-Identifier: Apache-2.0
// zf_net: WiFi station, provisioning (BLE Security 2, SoftAP fallback with a
// captive setup page), hostname and mDNS, re-provisioning and factory reset.
// Design: docs/design/firmware-architecture.md, section 12.
//
// The decisions live in net_logic.hpp (host-tested); this header is the
// device API other modules call. Every function here is safe from any task:
// requests are queued to the zf_net task.
//
// Rules for every module: use only AppServices (zf/app/services.hpp); never
// call the zone driver, scheduler or controller; never block in an EventBus
// handler.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "esp_err.h"
#include "zf/app/services.hpp"
#include "zf_net/net_logic.hpp"

namespace zf::net {

// Starts WiFi and the zf_net task. At first boot creates the device
// identity (setup code and Security 2 verifier) and stores it in NVS.
esp_err_t start(const AppServices &services);

// Current network state. Lock held for one struct copy.
NetStatus status();
// Advances on every change; poll it to notice changes cheaply.
uint32_t statusSequence();

// For the display and the serial console. Never sent over the network.
struct SetupInfo {
    std::string setupCode;   // also the Security 2 PoP and the SoftAP passphrase
    std::string serviceName; // BLE name and SoftAP SSID, "ZoneFlow-AB12"
    std::string qrPayload;   // Espressif provisioning app QR content (BLE transport)
    std::string qrPayloadSoftAp;
};
SetupInfo setupInfo();

// Re-enter provisioning: stores the request and restarts. Bound to the
// long-press on the controls (display module) and to the REST API. The
// stored WiFi credentials stay until new ones connect; after 15 minutes
// without a new setup the device returns to the stored network.
bool requestProvisioning(ProvTransport transport);

// Factory reset: stops watering (maintenance), waits until the outputs report
// off, erases NVS except the device identity and the stored core dump,
// restarts. Asynchronous. The www and OTA partitions are kept.
bool factoryReset();

// zf_web hands over its HTTP server so SoftAP provisioning shares port 80
// with the setup page.
void setHttpServer(void *httpdHandle);

// SoftAP setup page: pass credentials to the provisioning manager.
bool provisionWifi(std::string_view ssid, std::string_view password);

// True when `ipv4` (network byte order, as in sockaddr_in) is the setup
// access point's own address and not the station's. zf_web passes the local
// address of a connection to tell setup-AP clients from home-network ones:
// SoftAP provisioning keeps running for a while after the station has joined
// the home network.
bool isSetupApAddress(uint32_t ipv4);

struct ScanEntry {
    std::string ssid;
    int8_t rssi = 0;
    bool secure = true;
};
// Blocking scan (about 2 s); only meaningful during SoftAP provisioning.
std::vector<ScanEntry> scan();

} // namespace zf::net
