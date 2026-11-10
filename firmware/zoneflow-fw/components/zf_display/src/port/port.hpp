// SPDX-License-Identifier: Apache-2.0
// ESP-IDF side of the display. Private to src/port. All pin numbers come
// from zf_board/board.hpp.
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "zf_display/ports.hpp"
#include "zf_ui/input.hpp"

namespace zf::display::port {

// Rendered stripe: 320 x 24 px RGB565 = 15360 bytes, two of them, in
// internal DMA-capable RAM. See lcd.cpp for why this size.
inline constexpr int kStripeLines = 24;

// SPI bus, ST7789 panel and the LVGL display that flushes to it.
// On success *out is the display to build the view on.
esp_err_t lcdInit(lv_display_t **out);

// Backlight PWM. percent 0..100 as the UI means it (perceived brightness).
esp_err_t backlightInit();
void backlightSet(uint8_t percent);

// Encoder (PCNT) and buttons, sampled every 5 ms from an esp_timer and
// decoded with zf_ui/input.hpp. Decoded events go to `queue`
// (items: zf::ui::InputEvent).
esp_err_t inputsInit(QueueHandle_t queue);

class NvsSettingsStore final : public ISettingsStore {
public:
    bool load(UiSettings &out) override;
    bool save(const UiSettings &settings) override;
    bool erase() override;
};

// Bytes LVGL holds right now and the most it held (lv_mem.cpp).
uint32_t lvglHeapUsed();
uint32_t lvglHeapPeak();

} // namespace zf::display::port
