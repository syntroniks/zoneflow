// SPDX-License-Identifier: Apache-2.0
// Rev A output stage on ESP-IDF: keep-alive pin and its timer interrupt, the
// detector read-backs, the TCA9539 bus, state kept in RTC memory and the
// IRAM force-off path. Hardware design sections 8 and 19.
#include <cstring>

#include "freertos/FreeRTOS.h"

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "hal/gpio_ll.h"
#include "sdkconfig.h"
#include "soc/gpio_struct.h"
#include "zf_board/board.hpp"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

constexpr const char *kTag = "zf.out";

// Keep-alive: a GPTimer alarm interrupt every CONFIG_ZF_OE_TOGGLE_PERIOD_MS.
// With CONFIG_GPTIMER_ISR_CACHE_SAFE the interrupt is in IRAM and stays
// enabled while the flash cache is off (NVS writes, OTA), so the pin keeps
// its edges through a flash erase. Everything it touches is in IRAM or DRAM:
// OutputEnableGuard::step() and sample() are forced inline, the guard lives
// in .bss, the pin accesses are register accesses. The guard stops the edges
// when the control-loop heartbeat goes stale, so a dead control loop still
// stops the keep-alive (hardware design 19.4: code decides every edge, no
// peripheral drives the pin; it is a plain GPIO output).
DRAM_ATTR OutputEnableGuard *sGuard = nullptr;
gptimer_handle_t sKeepAliveTimer = nullptr;

IRAM_ATTR bool onKeepAlive(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *)
{
    const bool on = sGuard->step(CONFIG_ZF_OE_TOGGLE_PERIOD_MS);
    gpio_ll_set_level(&GPIO, static_cast<uint32_t>(board::kKeepAliveGpio), on ? 1u : 0u);
    // The read-backs and the latch, every period, for the self-test timing.
    const bool driveEnabled = gpio_ll_get_level(&GPIO, static_cast<uint32_t>(board::kDriveEnableReadbackGpio)) != 0;
    const bool relayOff = gpio_ll_get_level(&GPIO, static_cast<uint32_t>(board::kRelayOffReadbackGpio)) != 0;
    const bool latched = gpio_ll_get_level(&GPIO, static_cast<uint32_t>(board::kOverCurrentGpio)) == 0;
    sGuard->sample(driveEnabled, relayOff, latched);
    return false; // no task woken
}

constexpr int kI2cTimeoutMs = board::kExpanderI2cTimeoutMs;

hw::BusStatus mapI2c(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return hw::BusStatus::Ok;
    case ESP_ERR_NOT_FOUND:     // probe: no acknowledge
    case ESP_ERR_INVALID_STATE: // transaction did not complete: no acknowledge (or an SCL timeout)
        return hw::BusStatus::Nack;
    case ESP_ERR_TIMEOUT:
        return hw::BusStatus::Timeout;
    default:
        return hw::BusStatus::BusError;
    }
}

// RTC slow memory that a software reset keeps. Raw bytes: an object with a
// constructor would be zeroed at start-up.
RTC_NOINIT_ATTR alignas(4) uint8_t sRetained[sizeof(RetainedRecord)];

} // namespace

// Called from the global-scope C entry point below.
IRAM_ATTR void forceOutputsOff() { gpio_ll_set_level(&GPIO, static_cast<uint32_t>(board::kKeepAliveGpio), 0); }

esp_err_t initOutputStagePins()
{
    // Keep-alive: latch low first, then the driver on. R33 holds the pin low
    // until then (hardware design 8.7).
    gpio_set_level(static_cast<gpio_num_t>(board::kKeepAliveGpio), 0);
    const gpio_config_t ka = {
        .pin_bit_mask = 1ULL << board::kKeepAliveGpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&ka);
    // Rule 12: GPIO42 an input with the internal pull-down (an open R92
    // reads low, the safe side); GPIO7 an input with no pull. Neither is
    // ever an output.
    const gpio_config_t driveRb = {
        .pin_bit_mask = 1ULL << board::kDriveEnableReadbackGpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t noPull = {
        .pin_bit_mask = (1ULL << board::kRelayOffReadbackGpio) | (1ULL << board::kOverCurrentGpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t led = {
        .pin_bit_mask = 1ULL << board::kStatusLedGpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (err == ESP_OK) {
        err = gpio_config(&driveRb);
    }
    if (err == ESP_OK) {
        err = gpio_config(&noPull);
    }
    if (err == ESP_OK) {
        err = gpio_config(&led);
    }
    ESP_LOGI(kTag, "keep-alive low, read-backs configured (%s)", board::kBoardName);
    return err;
}

void setStatusLed(bool on)
{
    gpio_set_level(static_cast<gpio_num_t>(board::kStatusLedGpio), on == board::kStatusLedActiveHigh ? 1 : 0);
}

OutputEnableMode configuredOutputEnableMode() { return OutputEnableMode::Toggle; }

esp_err_t startKeepAlive(OutputEnableGuard &guard)
{
#if !CONFIG_GPTIMER_ISR_CACHE_SAFE
#error "The keep-alive must run while the flash cache is off: set CONFIG_GPTIMER_ISR_CACHE_SAFE"
#endif
    sGuard = &guard;
    const gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, // 1 us
        .intr_priority = 0,
        .flags = {},
    };
    esp_err_t err = gptimer_new_timer(&cfg, &sKeepAliveTimer);
    if (err != ESP_OK) {
        return err;
    }
    const gptimer_event_callbacks_t cbs = {.on_alarm = &onKeepAlive};
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = static_cast<uint64_t>(CONFIG_ZF_OE_TOGGLE_PERIOD_MS) * 1000u;
    alarm.reload_count = 0;
    alarm.flags.auto_reload_on_alarm = true;
    err = gptimer_register_event_callbacks(sKeepAliveTimer, &cbs, nullptr);
    if (err == ESP_OK) {
        err = gptimer_set_alarm_action(sKeepAliveTimer, &alarm);
    }
    if (err == ESP_OK) {
        err = gptimer_enable(sKeepAliveTimer);
    }
    if (err == ESP_OK) {
        err = gptimer_start(sKeepAliveTimer);
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "keep-alive timer failed: %s", esp_err_to_name(err));
    }
    return err;
}

// ---- TCA9539 ----------------------------------------------------------------------------

esp_err_t Tca9539Bus::init(I2cBus &bus)
{
    if (bus.handle() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    bus_ = bus.handle();
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = board::kExpanderI2cAddress;
    dev.scl_speed_hz = board::kI2cFrequencyHz;
    return i2c_master_bus_add_device(bus_, &dev, &dev_);
}

hw::BusStatus Tca9539Bus::write(uint8_t reg, uint8_t value)
{
    if (dev_ == nullptr) {
        return hw::BusStatus::BusError;
    }
    const uint8_t buf[2] = {reg, value};
    return mapI2c(i2c_master_transmit(dev_, buf, sizeof buf, kI2cTimeoutMs));
}

hw::BusStatus Tca9539Bus::read(uint8_t reg, uint8_t &value)
{
    if (dev_ == nullptr) {
        return hw::BusStatus::BusError;
    }
    return mapI2c(i2c_master_transmit_receive(dev_, &reg, 1, &value, 1, kI2cTimeoutMs));
}

hw::BusStatus Tca9539Bus::probe()
{
    if (bus_ == nullptr) {
        return hw::BusStatus::BusError;
    }
    return mapI2c(i2c_master_probe(bus_, board::kExpanderI2cAddress, kI2cTimeoutMs));
}

void Tca9539Bus::recover()
{
    if (bus_ != nullptr) {
        // Nine clock pulses and a controller reset; bounded by the driver's
        // 50 ms bus-clear timeout.
        const esp_err_t err = i2c_master_bus_reset(bus_);
        ESP_LOGW(kTag, "I2C bus reset: %s", esp_err_to_name(err));
    }
}

// ---- RTC memory ---------------------------------------------------------------------------

bool RtcRetainedStore::load(RetainedRecord &out)
{
    std::memcpy(&out, sRetained, sizeof out);
    return intact(out);
}

void RtcRetainedStore::save(const RetainedRecord &r) { std::memcpy(sRetained, &r, sizeof r); }

} // namespace zf::platform

extern "C" IRAM_ATTR void zf_platform_force_outputs_off(void) { zf::platform::forceOutputsOff(); }
