// SPDX-License-Identifier: Apache-2.0
// zf_display: the local user interface (spec 4.1). A 320x240 ST7789-class
// TFT on SPI, a rotary encoder with push switch, Back and Manual/Home
// buttons. Design: docs/design/display-ui.md.
//
// start() brings up the panel, the backlight and the controls and starts
// the UI task. If the panel does not answer (no display fitted) it logs a
// warning and returns ESP_OK: the controller works without a display.
//
// Rules for every module, kept here: only AppServices (zf/app/services.hpp)
// reaches the core; nothing calls the zone driver, scheduler or controller;
// the EventBus handler only copies into the task's queue.
#pragma once

#include "esp_err.h"
#include "zf/app/services.hpp"
#include "zf_display/ports.hpp"

namespace zf::display {

// What the display needs beyond AppServices (ports.hpp). Leave a pointer
// null to use the stand-in from stubs.hpp. The objects must outlive the
// UI task, which never ends.
struct StartOptions {
    INetworkPort *network = nullptr;   // zf_net
    IZoneDiagPort *zoneDiag = nullptr; // zf_core, learned currents
    ISystemPort *system = nullptr;     // versions, restart
};

esp_err_t start(const AppServices &services, const StartOptions &options = {});

} // namespace zf::display
