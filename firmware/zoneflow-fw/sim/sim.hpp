// SPDX-License-Identifier: Apache-2.0
// The simulator: the firmware's UI logic and LVGL view on a framebuffer in
// memory, driven by the real controller on fake hardware (UiHarness).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "lvgl.h"
#include "ui_harness.hpp"
#include "zf_view/view.hpp"

namespace zf::sim {

inline constexpr int kWidth = 320;
inline constexpr int kHeight = 240;
inline constexpr int kStripeLines = 24;

// One LVGL display that renders RGB565 in partial mode into a framebuffer,
// the way the device sends stripes to the panel.
class Screen {
public:
    Screen();
    // Draws the frame and brings the framebuffer up to date.
    void render(const ui::Frame &frame);
    const uint16_t *pixels() const { return frame_.data(); }
    // Writes the framebuffer as a PNG, each pixel `scale` times enlarged.
    // backlightPercent < 100 darkens it, to show the dimmed state.
    bool savePng(const std::string &path, int scale, int backlightPercent = 100) const;

    // Milliseconds for lv_tick; the owner advances it.
    static uint32_t tickMs;

private:
    static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *px);

    lv_display_t *display_ = nullptr;
    view::View view_;
    std::array<uint16_t, kWidth * kHeight> frame_{};
    // Same stripe height as the firmware (zf_display/src/port/port.hpp).
    std::array<uint16_t, kWidth * kStripeLines> draw_{};
};

// A world to show: controller, fake hardware, display logic. Starts on
// Tuesday 2026-11-10 at 14:32 Berlin time with the demo garden.
struct World {
    explicit World(bool wizardDone = true, bool clockValid = true, bool wifi = true, bool revA = false);
    test::UiHarness h;
};

// Walks every screen in representative states and calls `shot(name)` for
// each. Returns the number of scenes.
using ShotFn = void (*)(void *ctx, const std::string &name, test::UiHarness &world);
int runScenes(ShotFn shot, void *ctx);

#ifdef ZF_SIM_HAVE_SDL
// Opens a window and runs until it is closed. Returns the exit code.
int runWindow(Screen &screen, bool wizard, int scale);
#endif

} // namespace zf::sim
