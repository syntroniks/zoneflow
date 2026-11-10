// SPDX-License-Identifier: Apache-2.0
// zf_ota: OTA update with rollback (spec 4.1).
//
// PHASE 1 STUB. start() logs that the module is not implemented and returns
// ESP_OK so app_main can call every module unconditionally.
//
// Implemented now: markRunningAppValid(), called by app_main after the
// controller is up, so a new image is confirmed and not rolled back.
//
// To implement (phase 2):
//  - HTTPS OTA (esp_https_ota) triggered from the web UI/REST.
//  - Before writing flash: post Command::maintenance(true) and wait until
//    StateStore shows maintenance && no zone open. The zone driver also
//    turns outputs off in the esp_restart shutdown handler.
//  - Image self-test before markRunningAppValid() (e.g. config loads,
//    control loop ran N ticks).
//
// Rules for every module: use only AppServices (zf/app/services.hpp); never
// call the zone driver, scheduler or controller; never block in an EventBus
// handler.
#pragma once

#include "esp_err.h"
#include "zf/app/services.hpp"

namespace zf::ota {

esp_err_t start(const AppServices &services);
// Confirms the running image if it is pending verification. Safe to call
// on every boot.
esp_err_t markRunningAppValid();

} // namespace zf::ota
