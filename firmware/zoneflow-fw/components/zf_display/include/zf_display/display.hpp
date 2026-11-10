// SPDX-License-Identifier: Apache-2.0
// zf_display: OLED (SSD1306/SH1106 128x64) and local controls (spec 4.1).
//
// PHASE 1 STUB. start() logs that the module is not implemented and returns
// ESP_OK so app_main can call every module unconditionally.
//
// To implement (phase 2):
//  - OLED driver on the shared I2C bus (esp_lcd SSD1306 or an SH1106 driver).
//  - Rotary encoder (PCNT or GPIO ISR) and buttons; pins from zf_board.
//  - Screens: status, manual run, network info, faults. Read StateStore;
//    act with Command::...; wake on an EventQueueSubscriber.
//
// Rules for every module: use only AppServices (zf/app/services.hpp); never
// call the zone driver, scheduler or controller; never block in an EventBus
// handler.
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "zf/app/services.hpp"

namespace zf::display {

esp_err_t start(const AppServices &services, i2c_master_bus_handle_t i2c);

} // namespace zf::display
