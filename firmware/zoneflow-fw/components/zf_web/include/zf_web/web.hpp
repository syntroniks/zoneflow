// SPDX-License-Identifier: Apache-2.0
// zf_web: Local web UI, REST API and live status stream (spec 4.1).
//
// PHASE 1 STUB. start() logs that the module is not implemented and returns
// ESP_OK so app_main can call every module unconditionally.
//
// To implement (phase 2):
//  - esp_http_server serving the UI from the "www" LittleFS partition.
//  - REST API: GET/PUT /api/config (ConfigStore::toJson / updateFromJson,
//    then post CommandType::ReloadConfig), GET /api/state
//    (StateStore::snapshot), POST /api/zones/{n}/run, /api/programs/{id}/run,
//    /api/stop, /api/pause, /api/resume, /api/skip, /api/rain-delay
//    (post Command::...).
//  - Live stream: WebSocket or SSE fed by an EventQueueSubscriber; on each
//    event send the event and/or a fresh snapshot.
//
// Rules for every module: use only AppServices (zf/app/services.hpp); never
// call the zone driver, scheduler or controller; never block in an EventBus
// handler.
#pragma once

#include "esp_err.h"
#include "zf/app/services.hpp"

namespace zf::web {

esp_err_t start(const AppServices &services);

} // namespace zf::web
