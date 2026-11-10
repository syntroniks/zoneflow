/* SPDX-License-Identifier: Apache-2.0
 *
 * LVGL configuration of the desktop simulator. Anything not set here takes
 * LVGL's default (include/lvgl/config/lv_conf_internal.h).
 *
 * The firmware configures LVGL through Kconfig (CONFIG_LV_* lines in
 * sdkconfig.defaults). The settings marked [fw] must say the same in both
 * places, or the simulator stops showing what the device draws.
 * zf_view/theme.hpp fails the build on either side if one is missing.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565 /* [fw] as the ST7789 panel */

/* Built-in allocator with a fixed pool, so `--report` can read how much of
 * it the display used (lv_mem_monitor). The firmware allocates from PSRAM
 * instead. The pool is large because the PNG encoder also takes from it. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (16 * 1024 * 1024)
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_USE_OS LV_OS_NONE /* [fw] one task owns LVGL */
#define LV_DEF_REFR_PERIOD 20

/* [fw] Fonts: Montserrat (SIL OFL 1.1), shipped with LVGL. */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

/* [fw] The view styles every object itself. */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#define LV_USE_QRCODE 1 /* [fw] WiFi provisioning screen */

/* Simulator only: PNG encoder for screenshots, logging to stderr. */
#define LV_USE_LODEPNG 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
