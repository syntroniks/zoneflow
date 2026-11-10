// SPDX-License-Identifier: Apache-2.0
// Field inputs: GPIO contacts, PCNT flow counter, ADC1 valve current.
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
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

} // namespace

esp_err_t SensorInputs::init()
{
    const gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << board::kRainGpio) | (1ULL << board::kSpareSensorGpio) |
                        (1ULL << board::kAcPresentGpio) | (1ULL << board::kFlowPulseGpio),
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

    // Valve current on ADC1.
    adc_unit_t adcUnit = ADC_UNIT_1;
    adc_channel_t channel = ADC_CHANNEL_0;
    err = adc_oneshot_io_to_channel(board::kValveCurrentGpio, &adcUnit, &channel);
    if (err == ESP_OK && adcUnit == ADC_UNIT_1) {
        adc_oneshot_unit_init_cfg_t unitInit = {};
        unitInit.unit_id = ADC_UNIT_1;
        adc_oneshot_unit_handle_t adc = nullptr;
        err = adc_oneshot_new_unit(&unitInit, &adc);
        if (err == ESP_OK) {
            adc_oneshot_chan_cfg_t chanCfg = {};
            chanCfg.atten = ADC_ATTEN_DB_12;
            chanCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
            err = adc_oneshot_config_channel(adc, channel, &chanCfg);
            adc_ = adc;
            adcChannel_ = static_cast<int>(channel);
            adc_cali_curve_fitting_config_t cali = {};
            cali.unit_id = ADC_UNIT_1;
            cali.chan = channel;
            cali.atten = ADC_ATTEN_DB_12;
            cali.bitwidth = ADC_BITWIDTH_DEFAULT;
            adc_cali_handle_t caliHandle = nullptr;
            if (adc_cali_create_scheme_curve_fitting(&cali, &caliHandle) == ESP_OK) {
                cali_ = caliHandle;
            } else {
                ESP_LOGW(kTag, "ADC calibration unavailable; current readings disabled");
            }
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "valve current ADC init failed: %s", esp_err_to_name(err));
    }
    return ESP_OK; // a missing sensor is reported as "unavailable", not fatal
}

SensorSample SensorInputs::read()
{
    SensorSample s;
    s.rainContactClosed = readContactClosed(board::kRainGpio);
    s.spareContactClosed = readContactClosed(board::kSpareSensorGpio);
    const bool acLevel = gpio_get_level(static_cast<gpio_num_t>(board::kAcPresentGpio)) != 0;
    s.acPresent = board::kAcPresentActiveLow ? !acLevel : acLevel;

    if (pcnt_ != nullptr) {
        int count = 0;
        if (pcnt_unit_get_count(static_cast<pcnt_unit_handle_t>(pcnt_), &count) == ESP_OK) {
            s.flowPulses = static_cast<uint32_t>(count);
        }
    }

    if (adc_ != nullptr && cali_ != nullptr) {
        int64_t sumMv = 0;
        int n = 0;
        for (int i = 0; i < board::kValveCurrentSamples; ++i) {
            int raw = 0;
            int mv = 0;
            if (adc_oneshot_read(static_cast<adc_oneshot_unit_handle_t>(adc_), static_cast<adc_channel_t>(adcChannel_),
                                 &raw) == ESP_OK &&
                adc_cali_raw_to_voltage(static_cast<adc_cali_handle_t>(cali_), raw, &mv) == ESP_OK) {
                sumMv += mv;
                ++n;
            }
        }
        if (n > 0) {
            const float mv = static_cast<float>(sumMv) / static_cast<float>(n) - board::kValveCurrentOffsetMv;
            s.valveCurrentMa = mv <= 0.0f ? 0 : static_cast<int32_t>(mv * board::kValveCurrentMaPerMv);
        }
    }
    return s;
}

} // namespace zf::platform
