// SPDX-License-Identifier: Apache-2.0
// zf_net: WiFi station, BLE provisioning and the fallback access point (spec 4.1).
//
// PHASE 1 STUB. start() logs that the module is not implemented and returns
// ESP_OK so app_main can call every module unconditionally.
//
// To implement (phase 2):
//  - WiFi station with credentials from NVS; reconnect with backoff.
//  - BLE provisioning (ESP-IDF wifi_provisioning, security 1 or 2).
//  - Fallback SoftAP when unprovisioned or the station cannot connect.
//  - Publish connectivity in the StateStore: extend SystemState with a
//    network block (one writer: post a command or add a NetworkStatus
//    setter on the control task; do not write StateStore from here).
//  - SNTP starts by itself on IP_EVENT_STA_GOT_IP (zf_platform startTimeSync).
//
// Rules for every module: use only AppServices (zf/app/services.hpp); never
// call the zone driver, scheduler or controller; never block in an EventBus
// handler.
#pragma once

#include "esp_err.h"
#include "zf/app/services.hpp"

namespace zf::net {

esp_err_t start(const AppServices &services);

} // namespace zf::net
