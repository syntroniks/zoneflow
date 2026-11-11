// SPDX-License-Identifier: Apache-2.0
// Hardware design 19.1 rule 17: while the float test (19.7 step 2b) runs, the
// nets that toggle with no control loop behind them must toggle, so that a
// bridge from one of them to the keep-alive net shows. This file keeps the
// PSRAM busy; the display task keeps the backlight PWM at a mid duty and the
// panel's SPI bus busy (zf_display/src/port/display.cpp).
#include <atomic>
#include <cstdint>

#include "freertos/FreeRTOS.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

constexpr const char *kTag = "zf.float";
// Larger than the 64 kB data cache, so that a pass misses the cache and every
// line is read from and written back to the PSRAM.
constexpr std::size_t kBufferBytes = 256u * 1024u;
constexpr std::size_t kLine = 32;

std::atomic<bool> sLoad{false};
volatile uint32_t *sBuffer = nullptr;
StaticTask_t sTcb;
StackType_t sStack[2048];

void task(void *)
{
    uint32_t sum = 0;
    for (;;) {
        if (!sLoad.load(std::memory_order_relaxed)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        // One pass reads and rewrites one word per cache line, a few ms at
        // 80 MHz octal. Yield after each pass: the task is below everything
        // that matters, and the float test lasts 0.5 s.
        for (std::size_t i = 0; i < kBufferBytes / sizeof(uint32_t); i += kLine / sizeof(uint32_t)) {
            sum += sBuffer[i];
            sBuffer[i] = sum;
        }
        vTaskDelay(1);
    }
}

} // namespace

void setFloatTestLoad(bool on) { sLoad.store(on, std::memory_order_relaxed); }

bool floatTestLoad() { return sLoad.load(std::memory_order_relaxed); }

esp_err_t startFloatTestLoad()
{
    sBuffer = static_cast<volatile uint32_t *>(heap_caps_calloc(1, kBufferBytes, MALLOC_CAP_SPIRAM));
    if (sBuffer == nullptr) {
        // Not fatal: the float test still runs, with less on the PSRAM bus.
        ESP_LOGW(kTag, "no PSRAM buffer: the float test runs without PSRAM traffic");
        return ESP_ERR_NO_MEM;
    }
    const TaskHandle_t t =
        xTaskCreateStaticPinnedToCore(&task, "zf_float", sizeof sStack, nullptr, 2, sStack, &sTcb, 0);
    return t == nullptr ? ESP_FAIL : ESP_OK;
}

} // namespace zf::platform
