// SPDX-License-Identifier: Apache-2.0
// The interfaces later modules (web, MQTT, display, provisioning, OTA) use.
// A module never touches the zone driver, the scheduler or the controller
// directly:
//  - read state:      services.state.snapshot() / sequence()
//  - follow changes:  services.events.subscribe(handler, ctx, mask)
//  - change things:   services.commands.post(Command::...)
//  - configuration:   services.config.get() / update() then post ReloadConfig
//  - wall clock:      services.time.status() / setManual()
#pragma once

#include "zf/config/config_store.hpp"
#include "zf/events/command.hpp"
#include "zf/events/event_bus.hpp"
#include "zf/events/state.hpp"
#include "zf/time/time_service.hpp"

namespace zf {

struct AppServices {
    EventBus &events;
    StateStore &state;
    ICommandSink &commands;
    ConfigStore &config;
    TimeService &time;
};

} // namespace zf
