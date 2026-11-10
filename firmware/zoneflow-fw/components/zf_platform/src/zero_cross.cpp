// SPDX-License-Identifier: Apache-2.0
// AC_ZC (hardware design 11 and 19.9): a square wave at mains frequency.
// Both edges are timestamped by an IRAM-safe GPIO interrupt into a
// ZeroCrossTracker, which gives presence (an edge within 25 ms), the mains
// frequency and the crossings for zero-cross switching. The interrupt keeps
// running while the flash cache is off, so a flash erase is not mistaken for
// a loss of 24 VAC.
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "zf_board/board.hpp"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {
constexpr const char *kTag = "zf.zc";
} // namespace

ZeroCrossInput::ZeroCrossInput() : tracker_(board::kZeroCrossRisingLagUs, board::kZeroCrossFallingLeadUs) {}

void IRAM_ATTR ZeroCrossInput::onEdge(void *arg)
{
    auto *self = static_cast<ZeroCrossInput *>(arg);
    const auto now = static_cast<uint32_t>(esp_timer_get_time()); // IRAM (CONFIG_ESP_TIMER_IN_IRAM)
    const bool high = gpio_ll_get_level(&GPIO, static_cast<uint32_t>(board::kZeroCrossGpio)) != 0;
    self->tracker_.onEdge(now, high);
}

esp_err_t ZeroCrossInput::init()
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << board::kZeroCrossGpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, // R58 on the board
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        // IRAM: the service must be installed here first, before any other
        // module installs it without the flag.
        err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(kTag, "GPIO ISR service already installed; zero-cross edges may stop during flash erases");
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        err = gpio_isr_handler_add(static_cast<gpio_num_t>(board::kZeroCrossGpio), &ZeroCrossInput::onEdge, this);
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "zero-cross input init failed: %s", esp_err_to_name(err));
    }
    return err;
}

ZeroCrossSample ZeroCrossInput::sample() const { return tracker_.sample(static_cast<uint32_t>(esp_timer_get_time())); }

bool ZeroCrossInput::acPresent() const { return tracker_.present(static_cast<uint32_t>(esp_timer_get_time())); }

uint32_t ZeroCrossInput::mainsMilliHz() const { return tracker_.milliHz(static_cast<uint32_t>(esp_timer_get_time())); }

void ZeroCrossInput::delayUs(uint32_t us) { esp_rom_delay_us(us); }

} // namespace zf::platform
