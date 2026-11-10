// SPDX-License-Identifier: Apache-2.0
// zf_mqtt: MQTT client with Home Assistant discovery (spec 4.1).
//
// PHASE 1 STUB. start() logs that the module is not implemented and returns
// ESP_OK so app_main can call every module unconditionally.
//
// To implement (phase 2):
//  - esp-mqtt client, broker settings in config (new schema version + migration).
//  - Home Assistant discovery for zones (valve/switch), sensors, faults.
//  - State from an EventQueueSubscriber + StateStore; commands map to
//    Command::... posted on services.commands. Never block the bus handler.
//
// Rules for every module: use only AppServices (zf/app/services.hpp); never
// call the zone driver, scheduler or controller; never block in an EventBus
// handler.
#pragma once

#include "esp_err.h"
#include "zf/app/services.hpp"

namespace zf::mqtt {

esp_err_t start(const AppServices &services);

} // namespace zf::mqtt
