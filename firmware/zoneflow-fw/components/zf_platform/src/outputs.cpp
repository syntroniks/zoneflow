// SPDX-License-Identifier: Apache-2.0
// Valve outputs, OE keep-alive task and the IRAM force-off path.
#include <array>

#include "freertos/FreeRTOS.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "hal/gpio_ll.h"
#include "sdkconfig.h"
#include "soc/gpio_struct.h"
#include "zf_board/board.hpp"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

constexpr const char *kTag = "zf.out";

constexpr uint32_t level(bool on, bool activeHigh) { return (on == activeHigh) ? 1u : 0u; }

constexpr std::size_t kForceOffCount = board::kZoneOutputs + 2;
constexpr std::array<int, kForceOffCount> forceOffPins()
{
    std::array<int, kForceOffCount> pins{};
    for (std::size_t i = 0; i < board::kZoneOutputs; ++i) {
        pins[i] = board::kZoneGpio[i];
    }
    pins[board::kZoneOutputs] = board::kMasterGpio;
    pins[board::kZoneOutputs + 1] = board::kOutputEnableGpio;
    return pins;
}

// In DRAM so the panic and brownout paths do not touch flash.
DRAM_ATTR const std::array<int, kForceOffCount> sForceOffPins = forceOffPins();
DRAM_ATTR const uint32_t sOutputOffLevel = level(false, board::kOutputActiveHigh);
DRAM_ATTR const uint32_t sOeOffLevel = level(false, board::kOutputEnableActiveHigh);

void setPin(int gpio, uint32_t lvl) { gpio_set_level(static_cast<gpio_num_t>(gpio), lvl); }

// Keep-alive task: static allocation, no heap, no locks.
StaticTask_t sKeepAliveTcb;
StackType_t sKeepAliveStack[2048];
OutputEnableGuard *sGuard = nullptr;

void keepAliveTask(void *)
{
    const uint32_t active = level(true, board::kOutputEnableActiveHigh);
    const uint32_t inactive = level(false, board::kOutputEnableActiveHigh);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        const auto nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        setPin(board::kOutputEnableGpio, sGuard->nextLevel(nowMs) ? active : inactive);
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CONFIG_ZF_OE_TOGGLE_PERIOD_MS));
    }
}

} // namespace

// Called from the global-scope C entry point below.
IRAM_ATTR void forceOutputsOff()
{
    // OE first: it gates every driver even if a GPIO write below is late.
    gpio_ll_set_level(&GPIO, static_cast<uint32_t>(board::kOutputEnableGpio), sOeOffLevel);
    for (std::size_t i = 0; i < kForceOffCount - 1; ++i) {
        gpio_ll_set_level(&GPIO, static_cast<uint32_t>(sForceOffPins[i]), sOutputOffLevel);
    }
}

esp_err_t GpioOutputs::init()
{
    uint64_t mask = 0;
    for (int pin : sForceOffPins) {
        mask |= 1ULL << pin;
    }
    // Set the output latch to "off" before enabling the driver, so the pin
    // goes from high impedance (held off by the external pull-down) straight
    // to driven-off.
    for (int pin : board::kZoneGpio) {
        setPin(pin, sOutputOffLevel);
    }
    setPin(board::kMasterGpio, sOutputOffLevel);
    setPin(board::kOutputEnableGpio, sOeOffLevel);
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    allOff();

    const gpio_config_t led = {
        .pin_bit_mask = 1ULL << board::kStatusLedGpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (err == ESP_OK) {
        err = gpio_config(&led);
    }
    if (err == ESP_OK && board::kHasOverCurrentTrip) {
        setPin(board::kOverCurrentClearGpio, level(false, board::kOverCurrentClearActiveHigh));
        const gpio_config_t clear = {
            .pin_bit_mask = 1ULL << board::kOverCurrentClearGpio,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&clear);
    }
    ESP_LOGI(kTag, "outputs initialised off (%s)", board::kBoardName);
    return err;
}

void GpioOutputs::setZone(ZoneIndex zone, bool on)
{
    if (zone < board::kZoneOutputs) {
        setPin(board::kZoneGpio[zone], level(on, board::kOutputActiveHigh));
    }
}

void GpioOutputs::setMaster(bool on) { setPin(board::kMasterGpio, level(on, board::kOutputActiveHigh)); }

void GpioOutputs::clearOverCurrentTrip()
{
    if (!board::kHasOverCurrentTrip) {
        return;
    }
    setPin(board::kOverCurrentClearGpio, level(true, board::kOverCurrentClearActiveHigh));
    esp_rom_delay_us(board::kOverCurrentClearPulseUs);
    setPin(board::kOverCurrentClearGpio, level(false, board::kOverCurrentClearActiveHigh));
}

void GpioOutputs::allOff()
{
    for (int pin : board::kZoneGpio) {
        setPin(pin, sOutputOffLevel);
    }
    setPin(board::kMasterGpio, sOutputOffLevel);
}

OutputEnableMode configuredOutputEnableMode()
{
#if CONFIG_ZF_OE_MODE_STATIC
    return OutputEnableMode::StaticLevel;
#else
    return OutputEnableMode::Toggle;
#endif
}

esp_err_t startKeepAlive(OutputEnableGuard &guard)
{
    sGuard = &guard;
    // Highest application priority below the IDF system tasks, on the APP core.
    const TaskHandle_t t =
        xTaskCreateStaticPinnedToCore(&keepAliveTask, "zf_keepalive", sizeof sKeepAliveStack, nullptr,
                                      configMAX_PRIORITIES - 2, sKeepAliveStack, &sKeepAliveTcb, 1);
    return t != nullptr ? ESP_OK : ESP_FAIL;
}

} // namespace zf::platform

extern "C" IRAM_ATTR void zf_platform_force_outputs_off(void) { zf::platform::forceOutputsOff(); }
