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
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"
#include "zf_board/board.hpp"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

constexpr const char *kTag = "zf.out";

// Contract 19.4, changed by review A2 (finding M6): an edge at least every
// 20 ms and no phase shorter than 2.5 ms. The pin changes level only on a
// keep-alive period, so the period is the shortest phase.
static_assert(keepalive::periodMeetsContract(CONFIG_ZF_OE_TOGGLE_PERIOD_MS * 1000u),
              "hardware design 19.4: keep-alive period between 2.5 and 20 ms");

// Rule 16: GPIO17 is UART1's default TXD. A console on UART1 with its default
// pins would idle high and send edges on the keep-alive net.
#if defined(CONFIG_ESP_CONSOLE_UART_NUM) && CONFIG_ESP_CONSOLE_UART_NUM == 1 && !defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
#error "hardware design 19.1 rule 16: no console on UART1 with its default pins (TXD is the keep-alive pin)"
#endif
#if defined(CONFIG_ESP_CONSOLE_UART_TX_GPIO)
static_assert(CONFIG_ESP_CONSOLE_UART_TX_GPIO != board::kKeepAliveGpio &&
                  CONFIG_ESP_CONSOLE_UART_RX_GPIO != board::kKeepAliveGpio,
              "hardware design 19.1 rule 16: the console UART must not use the keep-alive pin");
#endif

// The interrupt runs every 1 ms; every CONFIG_ZF_OE_TOGGLE_PERIOD_MS-th one
// is a keep-alive period. The 1 ms ones only read the keep-alive pin while
// it floats (hardware design 19.7 step 2b: "read KA as an input every 1 ms").
constexpr uint32_t kIsrPeriodUs = 1000;
DRAM_ATTR uint32_t sDiv = 0;

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
    const auto ka = static_cast<uint32_t>(board::kKeepAliveGpio);
    if (sGuard->pinIsInput()) {
        sGuard->sampleFloat(gpio_ll_get_level(&GPIO, ka) != 0);
    }
    if (++sDiv < CONFIG_ZF_OE_TOGGLE_PERIOD_MS) {
        return false;
    }
    sDiv = 0;
    const bool on = sGuard->step(CONFIG_ZF_OE_TOGGLE_PERIOD_MS);
    if (sGuard->pinIsInput()) {
        // Step 2b: driver off, no pull. R33 holds the net low unless
        // something else drives it.
        gpio_ll_output_disable(&GPIO, ka);
    } else {
        // Level first, then the driver: coming back from the float test the
        // pin goes straight to low.
        gpio_ll_set_level(&GPIO, ka, on ? 1u : 0u);
        gpio_ll_output_enable(&GPIO, ka);
    }
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
IRAM_ATTR void forceOutputsOff()
{
    gpio_ll_set_level(&GPIO, static_cast<uint32_t>(board::kKeepAliveGpio), 0);
    gpio_ll_output_enable(&GPIO, static_cast<uint32_t>(board::kKeepAliveGpio));
}

bool keepAlivePinPlain()
{
    // Rule 16 and contract 19.4: the IO_MUX selects the GPIO function and the
    // GPIO matrix routes no peripheral signal to the pin.
    const auto ka = static_cast<uint32_t>(board::kKeepAliveGpio);
    const uint32_t mux = REG_GET_FIELD(IO_MUX_GPIO0_REG + ka * 4u, MCU_SEL);
    const uint32_t out = REG_GET_FIELD(GPIO_FUNC0_OUT_SEL_CFG_REG + ka * 4u, GPIO_FUNC0_OUT_SEL);
    return mux == PIN_FUNC_GPIO && out == SIG_GPIO_OUT_IDX;
}

esp_err_t initOutputStagePins()
{
    // Keep-alive: latch low first, then the driver on. R33 holds the pin low
    // until then (hardware design 8.7).
    gpio_set_level(static_cast<gpio_num_t>(board::kKeepAliveGpio), 0);
    // Input buffer on as well: step 2b reads the pin while its driver is off.
    const gpio_config_t ka = {
        .pin_bit_mask = 1ULL << board::kKeepAliveGpio,
        .mode = GPIO_MODE_INPUT_OUTPUT,
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
    alarm.alarm_count = kIsrPeriodUs;
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

hw::BusStatus Tca9539Bus::writePair(uint8_t reg, uint8_t v0, uint8_t v1)
{
    if (dev_ == nullptr) {
        return hw::BusStatus::BusError;
    }
    // Address, command byte, port 0, port 1: one transaction, the second
    // port nine SCL periods after the first.
    const uint8_t buf[3] = {reg, v0, v1};
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
