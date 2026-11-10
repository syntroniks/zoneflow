// SPDX-License-Identifier: Apache-2.0
// Mains zero-cross input for zero-cross aware switching (zf/zone/zero_cross.hpp).
// A GPIO interrupt timestamps each crossing. The interrupt is not IRAM-safe:
// edges are missed while the flash cache is off, the signal then looks stale
// for a moment and the driver switches without waiting, as without a signal.
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "zf_board/board.hpp"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {
constexpr const char *kTag = "zf.zc";
// Edges closer than this are noise, not crossings (50 Hz: 10 000 us apart).
constexpr uint32_t kMinEdgeSpacingUs = 2000;
} // namespace

void IRAM_ATTR ZeroCrossInput::onEdge(void *arg)
{
    auto *self = static_cast<ZeroCrossInput *>(arg);
    const auto now = static_cast<uint32_t>(esp_timer_get_time());
    const uint32_t last = self->lastEdgeUs_.load(std::memory_order_relaxed);
    const uint32_t edges = self->edges_.load(std::memory_order_relaxed);
    if (edges > 0 && now - last < kMinEdgeSpacingUs) {
        return;
    }
    // Sequence lock: odd while the pair is being written.
    const uint32_t seq = self->seq_.load(std::memory_order_relaxed);
    self->seq_.store(seq + 1, std::memory_order_release);
    if (edges > 0) {
        self->intervalUs_.store(now - last, std::memory_order_relaxed);
    }
    self->lastEdgeUs_.store(now, std::memory_order_relaxed);
    self->seq_.store(seq + 2, std::memory_order_release);
    self->edges_.store(edges + 1, std::memory_order_relaxed);
}

esp_err_t ZeroCrossInput::init()
{
    if (!board::kHasZeroCross) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << board::kZeroCrossGpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = board::kZeroCrossRisingEdge ? GPIO_INTR_POSEDGE : GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err == ESP_OK) {
        err = gpio_install_isr_service(0);
        if (err == ESP_ERR_INVALID_STATE) {
            err = ESP_OK; // already installed by another module
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

ZeroCrossSample ZeroCrossInput::sample() const
{
    ZeroCrossSample s;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const uint32_t before = seq_.load(std::memory_order_acquire);
        s.lastEdgeUs = lastEdgeUs_.load(std::memory_order_relaxed);
        s.intervalUs = intervalUs_.load(std::memory_order_relaxed);
        const uint32_t after = seq_.load(std::memory_order_acquire);
        if (before == after && (before & 1u) == 0) {
            s.present = edges_.load(std::memory_order_relaxed) >= 2;
            break;
        }
    }
    s.nowUs = static_cast<uint32_t>(esp_timer_get_time());
    return s;
}

void ZeroCrossInput::delayUs(uint32_t us) { esp_rom_delay_us(us); }

} // namespace zf::platform
