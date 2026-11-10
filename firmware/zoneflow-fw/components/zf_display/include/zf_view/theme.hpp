// SPDX-License-Identifier: Apache-2.0
// Visual language of the display: palette, fonts, sizes. Rationale in
// docs/design/display-ui.md.
#pragma once

#include "lvgl.h"
#include "zf_ui/model.hpp"

// Both the simulator (sim/lv_conf.h) and the firmware (sdkconfig.defaults)
// must build LVGL with what the view uses.
#if LV_COLOR_DEPTH != 16
#error "zf_display needs LV_COLOR_DEPTH 16 (RGB565 panel)"
#endif
#if !LV_FONT_MONTSERRAT_16 || !LV_FONT_MONTSERRAT_20 || !LV_FONT_MONTSERRAT_28 || !LV_FONT_MONTSERRAT_48
#error "zf_display needs the Montserrat 16, 20, 28 and 48 fonts"
#endif
#if !LV_USE_QRCODE
#error "zf_display needs LV_USE_QRCODE (WiFi provisioning screen)"
#endif
#if LV_USE_THEME_DEFAULT
#error "zf_display styles every object itself: disable LV_USE_THEME_DEFAULT"
#endif

namespace zf::view {

inline constexpr int kScreenW = 320;
inline constexpr int kScreenH = 240;
inline constexpr int kBarH = 30;

struct Palette {
    lv_color_t bg;        // screen background
    lv_color_t surface;   // status bar
    lv_color_t surfaceHi; // buttons, badges, progress track
    lv_color_t line;      // dividers, outlines
    lv_color_t text;
    lv_color_t muted;   // secondary text
    lv_color_t focus;   // background of the focused item
    lv_color_t onFocus; // text on the focused item and on tone fills
    lv_color_t running;
    lv_color_t warn;
    lv_color_t fault;
    lv_color_t ok;
};

const Palette &palette();
// Text or fill colour for a tone on the dark background.
lv_color_t toneColor(ui::Tone tone);

inline const lv_font_t *fontSmall() { return &lv_font_montserrat_16; }
inline const lv_font_t *fontBody() { return &lv_font_montserrat_20; }
inline const lv_font_t *fontTitle() { return &lv_font_montserrat_28; }
inline const lv_font_t *fontNumerals() { return &lv_font_montserrat_48; }

} // namespace zf::view
