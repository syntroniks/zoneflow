// SPDX-License-Identifier: Apache-2.0
// ST7789 panel on SPI with DMA, LVGL partial rendering, backlight PWM.
//
// Never run on hardware. Checked: it compiles against ESP-IDF v5.5.6 and
// LVGL 9.6.0. Pin numbers, SPI mode, mirror flags and colour inversion are
// provisional board constants.
#include <algorithm>
#include <cmath>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "port.hpp"
#include "zf_board/board.hpp"

namespace zf::display::port {
namespace {

constexpr const char *kTag = "zf.display";
constexpr spi_host_device_t kHost = SPI2_HOST;
constexpr ledc_mode_t kLedcMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kLedcTimer = LEDC_TIMER_0;
constexpr ledc_channel_t kLedcChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_bit_t kLedcBits = LEDC_TIMER_10_BIT;

// Draw buffers.
//
// LVGL renders the changed area in stripes and hands each stripe to
// flush(). With two buffers it renders the next stripe while the SPI driver
// still sends the previous one by DMA.
//
// Size: 320 px x 24 lines x 2 bytes = 15360 bytes per buffer, 30720 bytes
// for both. A full redraw is 240 / 24 = 10 stripes. At 40 MHz SPI one
// stripe takes 15360 x 8 / 40e6 = 3.1 ms on the wire, a full screen 31 ms.
//
// They are in internal RAM, not PSRAM: the SPI driver sends by DMA straight
// from the buffer, and a PSRAM buffer would be copied through a bounce
// buffer for every stripe. Internal RAM is what WiFi and BLE provisioning
// are short of (firmware architecture, memory budget), so the stripes are a
// tenth of the screen, the smallest LVGL's documentation suggests, and not
// more. LVGL's own heap is on PSRAM (settings.cpp).
constexpr std::size_t kStripeBytes = static_cast<std::size_t>(board::kLcdWidth) * kStripeLines * 2;

esp_lcd_panel_handle_t sPanel = nullptr;

bool onTransferDone(esp_lcd_panel_io_handle_t /*io*/, esp_lcd_panel_io_event_data_t * /*data*/, void *ctx)
{
    // ISR context. lv_display_flush_ready() only clears a flag.
    lv_display_flush_ready(static_cast<lv_display_t *>(ctx));
    return false;
}

void flush(lv_display_t * /*display*/, const lv_area_t *area, uint8_t *px)
{
    // LVGL renders little-endian RGB565; the panel takes the high byte first.
    const uint32_t pixels =
        static_cast<uint32_t>(lv_area_get_width(area)) * static_cast<uint32_t>(lv_area_get_height(area));
    lv_draw_sw_rgb565_swap(px, pixels);
    // Queued for DMA; onTransferDone() tells LVGL when the buffer is free.
    esp_lcd_panel_draw_bitmap(sPanel, area->x1, area->y1, area->x2 + 1, area->y2 + 1, px);
}

} // namespace

esp_err_t lcdInit(lv_display_t **out)
{
    *out = nullptr;

    spi_bus_config_t bus = {};
    bus.sclk_io_num = board::kLcdSclkGpio;
    bus.mosi_io_num = board::kLcdMosiGpio;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = static_cast<int>(kStripeBytes);
    esp_err_t err = spi_bus_initialize(kHost, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "spi bus: %s", esp_err_to_name(err));
        return err;
    }

    lv_display_t *display = lv_display_create(board::kLcdWidth, board::kLcdHeight);
    if (display == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    esp_lcd_panel_io_spi_config_t io = {};
    io.dc_gpio_num = board::kLcdDcGpio;
    io.cs_gpio_num = board::kLcdCsGpio;
    io.pclk_hz = board::kLcdSpiHz;
    io.lcd_cmd_bits = 8;
    io.lcd_param_bits = 8;
    io.spi_mode = board::kLcdSpiMode;
    io.trans_queue_depth = 4;
    io.on_color_trans_done = &onTransferDone;
    io.user_ctx = display;
    esp_lcd_panel_io_handle_t ioHandle = nullptr;
    err = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kHost), &io, &ioHandle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "panel io: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_dev_config_t dev = {};
    dev.reset_gpio_num = board::kLcdResetGpio; // GPIO14, 10 kohm pull-up on the board
    dev.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    dev.bits_per_pixel = 16;
    err = esp_lcd_new_panel_st7789(ioHandle, &dev, &sPanel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_reset(sPanel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(sPanel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_invert_color(sPanel, board::kLcdInvertColors);
    }
    if (err == ESP_OK) {
        // The controller's native scan is 240 x 320 portrait.
        err = esp_lcd_panel_swap_xy(sPanel, board::kLcdSwapXy);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_mirror(sPanel, board::kLcdMirrorX, board::kLcdMirrorY);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(sPanel, true);
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "panel: %s", esp_err_to_name(err));
        return err;
    }

    void *buf1 = heap_caps_malloc(kStripeBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *buf2 = heap_caps_malloc(kStripeBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf1 == nullptr || buf2 == nullptr) {
        ESP_LOGE(kTag, "no memory for the draw buffers (2 x %u bytes)", static_cast<unsigned>(kStripeBytes));
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display, &flush);
    lv_display_set_buffers(display, buf1, buf2, static_cast<uint32_t>(kStripeBytes), LV_DISPLAY_RENDER_MODE_PARTIAL);
    *out = display;
    return ESP_OK;
}

// ---- backlight -----------------------------------------------------------------------

esp_err_t backlightInit()
{
    ledc_timer_config_t timer = {};
    timer.speed_mode = kLedcMode;
    timer.timer_num = kLedcTimer;
    timer.duty_resolution = kLedcBits;
    timer.freq_hz = board::kLcdBacklightPwmHz;
    timer.clk_cfg = LEDC_AUTO_CLK;
    esp_err_t err = ledc_timer_config(&timer);
    if (err != ESP_OK) {
        return err;
    }
    ledc_channel_config_t channel = {};
    channel.gpio_num = board::kLcdBacklightGpio;
    channel.speed_mode = kLedcMode;
    channel.channel = kLedcChannel;
    channel.timer_sel = kLedcTimer;
    channel.duty = 0; // dark until the first frame is drawn
    channel.flags.output_invert = board::kLcdBacklightActiveHigh ? 0 : 1;
    return ledc_channel_config(&channel);
}

namespace {
int sShownPercent = -1;
} // namespace

void backlightFloatTest(bool on)
{
    ledc_set_freq(kLedcMode, kLedcTimer, on ? board::kFloatTestBacklightHz : board::kLcdBacklightPwmHz);
    if (on) {
        ledc_set_duty(kLedcMode, kLedcChannel, 1u << (kLedcBits - 1)); // 50 % duty
        ledc_update_duty(kLedcMode, kLedcChannel);
    }
    sShownPercent = -1; // the next backlightSet() applies the user's level again
}

void backlightSet(uint8_t percent)
{
    if (percent == sShownPercent) {
        return;
    }
    sShownPercent = percent;
    // The eye is not linear: duty follows the square of the setting, so
    // "50 %" looks about half as bright and the dim level (a fifth of the
    // setting) is clearly dim. 100 % -> 1023, 80 % -> 655, 16 % -> 26.
    const uint32_t full = (1u << kLedcBits) - 1;
    const uint32_t p = std::min<uint32_t>(percent, 100);
    const uint32_t duty = p == 0 ? 0 : std::max<uint32_t>(1, full * p * p / 10000);
    ledc_set_duty(kLedcMode, kLedcChannel, duty);
    ledc_update_duty(kLedcMode, kLedcChannel);
}

} // namespace zf::display::port
