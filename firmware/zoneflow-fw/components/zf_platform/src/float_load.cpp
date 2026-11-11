// SPDX-License-Identifier: Apache-2.0
// Hardware design 19.1 rule 17: while the float test (19.7 step 2b) runs, the
// nets that toggle with no control loop behind them must toggle, so that a
// bridge from one of them to the keep-alive net shows. This file keeps the
// PSRAM busy and, since the hardware answers (19.7, note to step 2b), sends
// 0x55 on UART0 without pause: U0TXD toggles at half the baud rate instead
// of idling high, the one steady net the float test finds only under
// 1 kohm. A terminal on J8 shows a line of "U" at every test. The display
// task keeps the backlight PWM at 50 % and 4.7 kHz and the panel's SPI bus
// busy (zf_display/src/port/display.cpp).
#include <atomic>
#include <cstdint>

#include "freertos/FreeRTOS.h"

#include "driver/uart.h"
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

// UART0 is free: the console is on the USB serial/JTAG port
// (sdkconfig.defaults), and the ROM boot log at 115200 baud is the only other
// user of U0TXD.
constexpr uart_port_t kUart = UART_NUM_0;
constexpr int kUartBaud = 115200;
bool sUartOk = false;
constexpr uint8_t kPattern[64] = {0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
                                  0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};

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
        // 64 bytes fill the FIFO for 5.6 ms at 115200 baud, longer than a
        // pass below, so the line never goes idle while the test runs.
        if (sUartOk) {
            uart_write_bytes(kUart, kPattern, sizeof kPattern);
        }
        // One pass reads and rewrites one word per cache line, a few ms at
        // 80 MHz octal. Yield after each pass: the task is below everything
        // that matters, and the float test lasts 0.5 s.
        for (std::size_t i = 0; sBuffer != nullptr && i < kBufferBytes / sizeof(uint32_t);
             i += kLine / sizeof(uint32_t)) {
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
    }
    uart_config_t u = {};
    u.baud_rate = kUartBaud;
    u.data_bits = UART_DATA_8_BITS;
    u.parity = UART_PARITY_DISABLE;
    u.stop_bits = UART_STOP_BITS_1;
    u.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    u.source_clk = UART_SCLK_DEFAULT;
    sUartOk = uart_driver_install(kUart, 256, 0, 0, nullptr, 0) == ESP_OK && uart_param_config(kUart, &u) == ESP_OK;
    if (!sUartOk) {
        ESP_LOGW(kTag, "UART0 not available: the float test runs without the 0x55 load on U0TXD");
    }
    const TaskHandle_t t =
        xTaskCreateStaticPinnedToCore(&task, "zf_float", sizeof sStack, nullptr, 2, sStack, &sTcb, 0);
    return t == nullptr ? ESP_FAIL : ESP_OK;
}

} // namespace zf::platform
