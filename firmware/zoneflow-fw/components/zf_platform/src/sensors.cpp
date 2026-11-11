// SPDX-License-Identifier: Apache-2.0
// Field inputs (GPIO contacts, PCNT flow counter), the sensor supply switch,
// AC presence from the zero-cross edges, the over-current latch, and the
// valve current: ADC1 continuous mode in 100 ms windows (hardware design
// 13, 19.2 and 19.6).
#include <algorithm>

#include "freertos/FreeRTOS.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "driver/temperature_sensor.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "zf_board/board.hpp"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

constexpr const char *kTag = "zf.sensors";
constexpr int kPcntLimit = 30000;

bool readContactClosed(int gpio)
{
    return (gpio_get_level(static_cast<gpio_num_t>(gpio)) != 0) == board::kContactClosedLevel;
}

// ---- current sampling --------------------------------------------------------------------------
constexpr uint32_t kSamplesPerWindow = board::kIsenseSampleHzPerChannel * board::kIsenseWindowMs / 1000; // 500
// ISENSE_HI, ISENSE_LO, GPIO7 (the relay read-back, review A2 4.6
// question 7) and VIN_MON (hardware design 19.10), each at
// kIsenseSampleHzPerChannel: 20 kS/s in all.
constexpr uint32_t kChannels = 4;
// Conversion frames of 10 ms: 4 channels x 50 conversions x 4 bytes.
constexpr uint32_t kConvPerFrame = kChannels * kSamplesPerWindow / 10;
constexpr uint32_t kFrameBytes = kConvPerFrame * SOC_ADC_DIGI_RESULT_BYTES;
// A window that took longer than this lost samples (task delayed, pool
// overflow): it is dropped, not used.
constexpr uint32_t kMaxWindowMs = board::kIsenseWindowMs + 20;
// No new window for this long: the current sense is unavailable.
constexpr uint32_t kStaleMs = 500;

adc_atten_t atten()
{
    static_assert(board::kAdcAttenuationDb == 6, "ADC_ATTEN_DB_6 below");
    return ADC_ATTEN_DB_6;
}

StaticTask_t sCurrentTcb;
StackType_t sCurrentStack[3072];

} // namespace

esp_err_t CurrentSampler::start()
{
    adc_unit_t unit = ADC_UNIT_1;
    adc_channel_t hi = ADC_CHANNEL_0;
    adc_channel_t lo = ADC_CHANNEL_0;
    adc_channel_t rb = ADC_CHANNEL_0;
    adc_channel_t vin = ADC_CHANNEL_0;
    esp_err_t err = adc_continuous_io_to_channel(board::kIsenseHiGpio, &unit, &hi);
    if (err == ESP_OK && unit == ADC_UNIT_1) {
        err = adc_continuous_io_to_channel(board::kIsenseLoGpio, &unit, &lo);
    }
    if (err == ESP_OK && unit == ADC_UNIT_1) {
        err = adc_continuous_io_to_channel(board::kRelayOffReadbackGpio, &unit, &rb);
    }
    if (err == ESP_OK && unit == ADC_UNIT_1) {
        err = adc_continuous_io_to_channel(board::kVinMonGpio, &unit, &vin);
    }
    if (err != ESP_OK || unit != ADC_UNIT_1) {
        ESP_LOGE(kTag, "current sense, relay read-back and VIN_MON pins are not on ADC1");
        return err == ESP_OK ? ESP_ERR_INVALID_ARG : err;
    }
    chHi_ = static_cast<int>(hi);
    chLo_ = static_cast<int>(lo);
    chRb_ = static_cast<int>(rb);
    chVin_ = static_cast<int>(vin);

    adc_continuous_handle_cfg_t handleCfg = {};
    handleCfg.max_store_buf_size = kFrameBytes * 8;
    handleCfg.conv_frame_size = kFrameBytes;
    handleCfg.flags.flush_pool = 1;
    adc_continuous_handle_t handle = nullptr;
    err = adc_continuous_new_handle(&handleCfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "ADC continuous init failed: %s", esp_err_to_name(err));
        return err;
    }
    const adc_channel_t chans[kChannels] = {hi, lo, rb, vin};
    adc_digi_pattern_config_t pattern[kChannels] = {};
    for (uint32_t i = 0; i < kChannels; ++i) {
        pattern[i].atten = atten();
        pattern[i].channel = static_cast<uint8_t>(chans[i]);
        pattern[i].unit = ADC_UNIT_1;
        pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }
    adc_continuous_config_t cfg = {};
    cfg.pattern_num = kChannels;
    cfg.adc_pattern = pattern;
    cfg.sample_freq_hz = board::kIsenseSampleHzPerChannel * kChannels;
    cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    err = adc_continuous_config(handle, &cfg);
    // adc_continuous_config() turns the pins' digital input buffers off
    // (gpio_config_as_analog). GPIO7 is still read as a level by the
    // keep-alive interrupt: turn its buffer back on. Rule 12: still an
    // input, no pull. If this did not work the pin would read low, which
    // fails the idle test on the safe side. A bench item.
    if (err == ESP_OK) {
        err = gpio_input_enable(static_cast<gpio_num_t>(board::kRelayOffReadbackGpio));
    }

    // Calibration to millivolts, one scheme per channel.
    void **cali[kChannels] = {&caliHi_, &caliLo_, &caliRb_, &caliVin_};
    for (uint32_t i = 0; err == ESP_OK && i < kChannels; ++i) {
        adc_cali_curve_fitting_config_t c = {};
        c.unit_id = ADC_UNIT_1;
        c.chan = chans[i];
        c.atten = atten();
        c.bitwidth = ADC_BITWIDTH_12;
        adc_cali_handle_t h = nullptr;
        err = adc_cali_create_scheme_curve_fitting(&c, &h);
        *cali[i] = h;
    }
    if (err == ESP_OK) {
        err = adc_continuous_start(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "current sense unavailable (%s): zones still run", esp_err_to_name(err));
        return err;
    }
    adc_ = handle;
    // Core 0, above the network and UI tasks, below the control task's
    // priority on core 1: it only accumulates and publishes.
    const TaskHandle_t t = xTaskCreateStaticPinnedToCore(&CurrentSampler::task, "zf_isense", sizeof sCurrentStack, this,
                                                         6, sCurrentStack, &sCurrentTcb, 0);
    return t == nullptr ? ESP_FAIL : ESP_OK;
}

void CurrentSampler::task(void *arg) { static_cast<CurrentSampler *>(arg)->run(); }

void CurrentSampler::run()
{
    running_.store(true, std::memory_order_release);
    static adc_continuous_data_t parsed[kConvPerFrame];
    CurrentWindowStats w;
    uint32_t seq = 0;
    MonoMs windowStart = static_cast<MonoMs>(esp_timer_get_time() / 1000);
    for (;;) {
        uint32_t n = 0;
        const esp_err_t err =
            adc_continuous_read_parse(static_cast<adc_continuous_handle_t>(adc_), parsed, kConvPerFrame, &n, 100);
        if (err != ESP_OK) {
            continue; // timeout: the window that spans it is dropped below
        }
        for (uint32_t i = 0; i < n; ++i) {
            if (!parsed[i].valid || parsed[i].unit != ADC_UNIT_1) {
                continue;
            }
            const int ch = static_cast<int>(parsed[i].channel);
            ChannelStats *stats = ch == chHi_    ? &w.hi
                                  : ch == chLo_  ? &w.lo
                                  : ch == chRb_  ? &w.relayRb
                                  : ch == chVin_ ? &w.vinMon
                                                 : nullptr;
            void *calib = ch == chHi_ ? caliHi_ : ch == chLo_ ? caliLo_ : ch == chRb_ ? caliRb_ : caliVin_;
            if (stats == nullptr) {
                continue;
            }
            int mv = 0;
            if (adc_cali_raw_to_voltage(static_cast<adc_cali_handle_t>(calib), static_cast<int>(parsed[i].raw_data),
                                        &mv) != ESP_OK) {
                continue;
            }
            stats->add(mv);
            if (w.hi.n < kSamplesPerWindow) {
                continue;
            }
            const MonoMs now = static_cast<MonoMs>(esp_timer_get_time() / 1000);
            if (now - windowStart <= kMaxWindowMs) {
                // Exactly kSamplesPerWindow samples of the high-gain channel:
                // 100 ms of the ADC's own clock, whole mains cycles.
                w.seq = ++seq;
                w.endMs = now;
                w.startMs = now - board::kIsenseWindowMs;
                taskENTER_CRITICAL(&lock_);
                latest_ = w;
                taskEXIT_CRITICAL(&lock_);
            } else {
                dropped_.fetch_add(1, std::memory_order_relaxed);
            }
            w.hi.reset();
            w.lo.reset();
            w.relayRb.reset();
            w.vinMon.reset();
            windowStart = now;
        }
    }
}

bool CurrentSampler::available() const
{
    if (!running_.load(std::memory_order_acquire)) {
        return false;
    }
    const CurrentWindowStats w = latest();
    const auto now = static_cast<MonoMs>(esp_timer_get_time() / 1000);
    return w.seq != 0 && now - w.endMs <= kStaleMs;
}

CurrentWindowStats CurrentSampler::latest() const
{
    taskENTER_CRITICAL(&lock_);
    const CurrentWindowStats w = latest_;
    taskEXIT_CRITICAL(&lock_);
    return w;
}

// ---- sensor inputs ------------------------------------------------------------------------------

esp_err_t SensorInputs::init(ZeroCrossInput &zeroCross, CurrentSampler &current)
{
    zc_ = &zeroCross;
    current_ = &current;
    const gpio_config_t inputs = {
        .pin_bit_mask =
            (1ULL << board::kRainGpio) | (1ULL << board::kSpareSensorGpio) | (1ULL << board::kFlowPulseGpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = board::kInputsUseInternalPullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&inputs);
    if (err != ESP_OK) {
        return err;
    }

    // Flow: count falling edges (contact closing) with hardware accumulation.
    pcnt_unit_config_t unitCfg = {};
    unitCfg.low_limit = -kPcntLimit;
    unitCfg.high_limit = kPcntLimit;
    unitCfg.flags.accum_count = 1;
    pcnt_unit_handle_t unit = nullptr;
    err = pcnt_new_unit(&unitCfg, &unit);
    if (err == ESP_OK) {
        pcnt_glitch_filter_config_t filter = {.max_glitch_ns = board::kFlowGlitchFilterNs};
        pcnt_unit_set_glitch_filter(unit, &filter);
        pcnt_chan_config_t chanCfg = {};
        chanCfg.edge_gpio_num = board::kFlowPulseGpio;
        chanCfg.level_gpio_num = -1;
        pcnt_channel_handle_t chan = nullptr;
        err = pcnt_new_channel(unit, &chanCfg, &chan);
        if (err == ESP_OK) {
            pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
            pcnt_unit_add_watch_point(unit, kPcntLimit);
            pcnt_unit_enable(unit);
            pcnt_unit_clear_count(unit);
            pcnt_unit_start(unit);
            pcnt_ = unit;
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "flow counter init failed: %s", esp_err_to_name(err));
    }
    // The die temperature (hardware design 19.7 item 2), range -10 to 80 C,
    // whose error ESP-IDF gives as under 1 C. Without it the board does not
    // commission and reports that it is not commissioned.
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    temperature_sensor_handle_t th = nullptr;
    if (temperature_sensor_install(&tcfg, &th) == ESP_OK && temperature_sensor_enable(th) == ESP_OK) {
        temp_ = th;
    } else {
        ESP_LOGW(kTag, "die temperature sensor not available: the board does not commission");
    }
    // A missing flow counter, current sense or temperature is not fatal.
    return ESP_OK;
}

esp_err_t SensorInputs::enableSensorSupply()
{
    // Latch high first, then the driver on: the pin goes from the weak
    // pull-down of reset (and R86) straight to driven high. No pull-up.
    gpio_set_level(static_cast<gpio_num_t>(board::kSensorSupplyGpio), 1);
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << board::kSensorSupplyGpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_err_t err = gpio_config(&cfg);
    supplyOn_ = err == ESP_OK;
    return err;
}

SensorSample SensorInputs::read()
{
    SensorSample s;
    s.rainContactClosed = readContactClosed(board::kRainGpio);
    s.spareContactClosed = readContactClosed(board::kSpareSensorGpio);
    s.acPresent = zc_ != nullptr && zc_->acPresent();
    s.mainsMilliHz = zc_ != nullptr ? zc_->mainsMilliHz() : 0;
    s.overCurrentTripped = gpio_get_level(static_cast<gpio_num_t>(board::kOverCurrentGpio)) == 0;
    s.sensorSupplyOn = supplyOn_;
    s.keepAlivePinPlain = keepAlivePinPlain();

    if (pcnt_ != nullptr) {
        int count = 0;
        if (pcnt_unit_get_count(static_cast<pcnt_unit_handle_t>(pcnt_), &count) == ESP_OK) {
            s.flowPulses = static_cast<uint32_t>(count);
        }
    }

    const int64_t nowUs = esp_timer_get_time();
    if (temp_ != nullptr && (tempReadUs_ == 0 || nowUs - tempReadUs_ >= 10'000'000)) {
        tempReadUs_ = nowUs;
        tempKnown_ =
            temperature_sensor_get_celsius(static_cast<temperature_sensor_handle_t>(temp_), &tempC_) == ESP_OK;
    }
    s.dieTempKnown = tempKnown_;
    s.dieTempC = tempC_;

    s.currentSenseAvailable = current_ != nullptr && current_->available();
    if (current_ != nullptr) {
        s.currentWindow = current_->latest();
    }
    return s;
}

} // namespace zf::platform
