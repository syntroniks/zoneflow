// SPDX-License-Identifier: Apache-2.0
// zf_web: local web UI, REST API (/api/v1) and live event stream (spec 4.1).
// Design: docs/design/firmware-architecture.md, section 12; API reference:
// docs/firmware/api.md and docs/firmware/openapi.json.
//
// The request logic is in api.hpp and the helpers next to it (host-tested).
// This header is the device entry point.
//
// Rules for every module: use only AppServices (zf/app/services.hpp); never
// call the zone driver, scheduler or controller; never block in an EventBus
// handler.
#pragma once

#include "esp_err.h"
#include "zf/app/services.hpp"

namespace zf::web {

// Mounts the "www" LittleFS partition, starts the HTTP server (and the HTTPS
// server when enabled), the event-stream task and registers the server with
// zf_net for SoftAP provisioning. Call after net::start().
esp_err_t start(const AppServices &services);

} // namespace zf::web
