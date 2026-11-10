// SPDX-License-Identifier: Apache-2.0
// zf_mqtt: MQTT client with Home Assistant discovery (spec 4.1).
// Design: docs/design/firmware-architecture.md, section 17. User guide and
// topic reference: docs/firmware/mqtt.md.
//
//  - esp-mqtt client on its own task; the zf_mqtt task (prio 3, core 0)
//    owns all MQTT state, builds payloads and decides what to publish.
//  - Starts only once the station has an IP address; reconnects with
//    exponential backoff; last will "offline" on <root>/status.
//  - State: retained JSON topics rebuilt from StateStore snapshots;
//    events arrive through an EventQueueSubscriber (copy, never waits).
//  - Commands: <root>/cmd/... parsed by mqtt_logic.cpp and posted on the
//    command queue; retained command messages are ignored.
//  - Settings: Config::mqtt (typed, validated). The password and a custom CA
//    certificate are in NVS namespace "zf_mqtt", set through setSecrets().
//
// Rules for every module: use only AppServices (zf/app/services.hpp); never
// call the zone driver, scheduler or controller; never block in an EventBus
// handler.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "zf/app/services.hpp"
#include "zf_mqtt/mqtt_logic.hpp"

namespace zf::mqtt {

esp_err_t start(const AppServices &services);

// Connection state for the REST API. Lock held for one struct copy.
MqttStatus status();

struct SecretsInfo {
    bool passwordSet = false;
    bool caCertSet = false;
};
SecretsInfo secrets();

// Stores the password and/or the custom CA certificate (PEM) in NVS and
// reconnects. std::nullopt leaves a value unchanged; an empty string
// removes it. Returns false if NVS could not be written.
bool setSecrets(std::optional<std::string_view> password, std::optional<std::string_view> caPem);

} // namespace zf::mqtt
