// SPDX-License-Identifier: Apache-2.0
// Ported from firmware/zoneflow-fw/components/zf_platform (outputs.cpp,
// sensors.cpp, zero_cross.cpp). Differences, and why:
//  - AC_ZC is sampled in the 1 ms timer interrupt instead of a GPIO
//    interrupt. ESPHome installs the GPIO interrupt service without the
//    IRAM flag, and refuses to attach its own pins (encoder, flow meter) if
//    another component installed it first; a non-IRAM handler would miss
//    edges during a flash erase and report a loss of 24 VAC. 1 ms resolution
//    is enough for presence (25 ms) and the 45 to 66 Hz plausibility check;
//    zero-cross switching is off in this package.
//  - No panic-handler wrap: ESPHome's crash handler already wraps
//    esp_panic_handler and the linker allows one. A panic stops the timer
//    interrupt with the CPU, so the keep-alive gets no more edges and the
//    detectors remove drive within their hold-up (hardware design 19.4:
//    1.2 s at most if the pin stopped high); the reset then releases the pin
//    to R33, low.
#include "board_io.h"

#ifdef USE_ESP32

#include <cstring>

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esphome/core/log.h"
#include "freertos/task.h"
#include "hal/gpio_ll.h"
#include "sdkconfig.h"
#include "soc/gpio_struct.h"

#if !CONFIG_GPTIMER_ISR_CACHE_SAFE
#error "The keep-alive must run while the flash cache is off: CONFIG_GPTIMER_ISR_CACHE_SAFE (set by the zoneflow component)"
#endif

namespace esphome::zoneflow {
namespace {

constexpr const char *const TAG = "zoneflow.io";

// Hardware design 19.4: one edge every 5 ms; heartbeat timeout 500 ms. Do
// not lower the timeout: a 4 kB flash sector erase stalls the loop for up to
// about 400 ms.
constexpr uint32_t kTimerPeriodUs = 1000;
constexpr uint32_t kKeepAlivePeriods = 5;
constexpr uint32_t kKeepAlivePeriodMs = 5;
constexpr uint32_t kHeartbeatTimeoutMs = 500;
// Hardware design 19.9, at 24 VAC (zf_board/zoneflow_rev_a.hpp).
constexpr int32_t kZeroCrossRisingLagUs = 300;
constexpr int32_t kZeroCrossFallingLeadUs = 90;

// DRAM: the interrupt touches only these, through always-inline code.
DRAM_ATTR zf::OutputEnableGuard s_guard{zf::OutputEnableMode::Toggle, kHeartbeatTimeoutMs};
DRAM_ATTR zf::ZeroCrossTracker s_zc{kZeroCrossRisingLagUs, kZeroCrossFallingLeadUs};
DRAM_ATTR std::atomic<uint32_t> s_isr_us{0};
DRAM_ATTR uint32_t s_div = 0;
DRAM_ATTR bool s_zc_level = false;
DRAM_ATTR int s_pin_ka = -1;
DRAM_ATTR int s_pin_arb = -1;
DRAM_ATTR int s_pin_brb = -1;
DRAM_ATTR int s_pin_oc = -1;
DRAM_ATTR int s_pin_zc = -1;
gptimer_handle_t s_timer = nullptr;

IRAM_ATTR bool on_timer(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *) {
  const uint32_t now = s_isr_us.load(std::memory_order_relaxed) + kTimerPeriodUs;
  s_isr_us.store(now, std::memory_order_relaxed);
  const bool zc = gpio_ll_get_level(&GPIO, static_cast<uint32_t>(s_pin_zc)) != 0;
  if (zc != s_zc_level) {
    s_zc_level = zc;
    s_zc.onEdge(now, zc);
  }
  if (++s_div >= kKeepAlivePeriods) {
    s_div = 0;
    // The guard decides the edge: low unless the control loop's heartbeat
    // is younger than 500 ms, the output stage asks for it, and the guard is
    // not tripped (hardware design 19.4).
    const bool on = s_guard.step(kKeepAlivePeriodMs);
    gpio_ll_set_level(&GPIO, static_cast<uint32_t>(s_pin_ka), on ? 1u : 0u);
    const bool drive = gpio_ll_get_level(&GPIO, static_cast<uint32_t>(s_pin_arb)) != 0;
    const bool relay_off = gpio_ll_get_level(&GPIO, static_cast<uint32_t>(s_pin_brb)) != 0;
    const bool latched = gpio_ll_get_level(&GPIO, static_cast<uint32_t>(s_pin_oc)) == 0;
    s_guard.sample(drive, relay_off, latched);
  }
  return false;
}

// Current sampling: hardware design 19.2.
constexpr uint32_t kSampleHzPerChannel = 5000;
constexpr uint32_t kWindowMs = 100;
constexpr uint32_t kSamplesPerWindow = kSampleHzPerChannel * kWindowMs / 1000;  // 500
constexpr uint32_t kConvPerFrame = 2 * kSamplesPerWindow / 10;                 // 10 ms frames
constexpr uint32_t kFrameBytes = kConvPerFrame * SOC_ADC_DIGI_RESULT_BYTES;
constexpr uint32_t kMaxWindowMs = kWindowMs + 20;  // longer: samples were lost, drop it
constexpr uint32_t kStaleMs = 500;

StaticTask_t s_sampler_tcb;
StackType_t s_sampler_stack[3072];

zf::hw::BusStatus map_error(i2c::ErrorCode e) {
  switch (e) {
    case i2c::ERROR_OK:
      return zf::hw::BusStatus::Ok;
    case i2c::ERROR_NOT_ACKNOWLEDGED:
      return zf::hw::BusStatus::Nack;
    case i2c::ERROR_TIMEOUT:
      return zf::hw::BusStatus::Timeout;
    default:
      return zf::hw::BusStatus::BusError;
  }
}

void shutdown_handler() { force_outputs_off(); }

}  // namespace

zf::OutputEnableGuard &keepalive_guard() { return s_guard; }
zf::ZeroCrossTracker &zero_cross() { return s_zc; }
uint32_t isr_micros() { return s_isr_us.load(std::memory_order_relaxed); }

IRAM_ATTR void force_outputs_off() {
  s_guard.trip();
  if (s_pin_ka >= 0)
    gpio_ll_set_level(&GPIO, static_cast<uint32_t>(s_pin_ka), 0);
}

bool init_output_stage_pins(const BoardPins &p) {
  s_pin_ka = p.keepalive;
  s_pin_arb = p.drive_readback;
  s_pin_brb = p.relay_readback;
  s_pin_oc = p.overcurrent;
  s_pin_zc = p.zero_cross;
  // Keep-alive: latch low first, then the driver on. R33 holds it low before.
  gpio_set_level(static_cast<gpio_num_t>(p.keepalive), 0);
  gpio_config_t ka = {};
  ka.pin_bit_mask = 1ULL << p.keepalive;
  ka.mode = GPIO_MODE_OUTPUT;
  esp_err_t err = gpio_config(&ka);
  // Rule 12: GPIO42 input with the internal pull-down (an open R92 reads
  // low, the safe side); GPIO7 input with no pull. Never outputs.
  gpio_config_t arb = {};
  arb.pin_bit_mask = 1ULL << p.drive_readback;
  arb.mode = GPIO_MODE_INPUT;
  arb.pull_down_en = GPIO_PULLDOWN_ENABLE;
  gpio_config_t in = {};
  in.pin_bit_mask = (1ULL << p.relay_readback) | (1ULL << p.overcurrent) | (1ULL << p.zero_cross);
  in.mode = GPIO_MODE_INPUT;
  if (err == ESP_OK)
    err = gpio_config(&arb);
  if (err == ESP_OK)
    err = gpio_config(&in);
  s_zc_level = gpio_get_level(static_cast<gpio_num_t>(p.zero_cross)) != 0;
  // esp_restart() from anywhere (OTA reboot, `restart` button, API).
  if (err == ESP_OK)
    err = esp_register_shutdown_handler(&shutdown_handler);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "output stage pins: %s", esp_err_to_name(err));
  return err == ESP_OK;
}

bool start_keepalive_timer(const BoardPins &) {
  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = 1000000;
  esp_err_t err = gptimer_new_timer(&cfg, &s_timer);
  if (err == ESP_OK) {
    gptimer_event_callbacks_t cbs = {};
    cbs.on_alarm = &on_timer;
    err = gptimer_register_event_callbacks(s_timer, &cbs, nullptr);
  }
  if (err == ESP_OK) {
    gptimer_alarm_config_t alarm = {};
    alarm.alarm_count = kTimerPeriodUs;
    alarm.reload_count = 0;
    alarm.flags.auto_reload_on_alarm = true;
    err = gptimer_set_alarm_action(s_timer, &alarm);
  }
  if (err == ESP_OK)
    err = gptimer_enable(s_timer);
  if (err == ESP_OK)
    err = gptimer_start(s_timer);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "keep-alive timer: %s; outputs stay off", esp_err_to_name(err));
  return err == ESP_OK;
}

bool enable_sensor_supply(const BoardPins &p) {
  if (p.sensor_supply < 0)
    return false;
  // Rule 13: from the reset pull-down (and R86) straight to driven high.
  gpio_set_level(static_cast<gpio_num_t>(p.sensor_supply), 1);
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << p.sensor_supply;
  cfg.mode = GPIO_MODE_OUTPUT;
  return gpio_config(&cfg) == ESP_OK;
}

// ---- current sampler ----------------------------------------------------------------------

bool CurrentSampler::start(const BoardPins &p) {
  adc_unit_t unit = ADC_UNIT_1;
  adc_channel_t hi = ADC_CHANNEL_0;
  adc_channel_t lo = ADC_CHANNEL_0;
  esp_err_t err = adc_continuous_io_to_channel(p.current_high, &unit, &hi);
  if (err == ESP_OK && unit == ADC_UNIT_1)
    err = adc_continuous_io_to_channel(p.current_low, &unit, &lo);
  if (err != ESP_OK || unit != ADC_UNIT_1) {
    ESP_LOGE(TAG, "current sense pins are not on ADC1");
    return false;
  }
  ch_hi_ = static_cast<int>(hi);
  ch_lo_ = static_cast<int>(lo);

  adc_continuous_handle_cfg_t handle_cfg = {};
  handle_cfg.max_store_buf_size = kFrameBytes * 8;
  handle_cfg.conv_frame_size = kFrameBytes;
  handle_cfg.flags.flush_pool = 1;
  adc_continuous_handle_t handle = nullptr;
  err = adc_continuous_new_handle(&handle_cfg, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ADC continuous mode: %s", esp_err_to_name(err));
    return false;
  }
  adc_digi_pattern_config_t pattern[2] = {};
  for (int i = 0; i < 2; ++i) {
    pattern[i].atten = ADC_ATTEN_DB_6;  // pin map: 6 dB, 0 to 1600 mV
    pattern[i].channel = static_cast<uint8_t>(i == 0 ? hi : lo);
    pattern[i].unit = ADC_UNIT_1;
    pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  }
  adc_continuous_config_t cfg = {};
  cfg.pattern_num = 2;
  cfg.adc_pattern = pattern;
  cfg.sample_freq_hz = kSampleHzPerChannel * 2;
  cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
  err = adc_continuous_config(handle, &cfg);
  for (int i = 0; err == ESP_OK && i < 2; ++i) {
    adc_cali_curve_fitting_config_t cali = {};
    cali.unit_id = ADC_UNIT_1;
    cali.chan = i == 0 ? hi : lo;
    cali.atten = ADC_ATTEN_DB_6;
    cali.bitwidth = ADC_BITWIDTH_12;
    adc_cali_handle_t h = nullptr;
    err = adc_cali_create_scheme_curve_fitting(&cali, &h);
    (i == 0 ? cali_hi_ : cali_lo_) = h;
  }
  if (err == ESP_OK)
    err = adc_continuous_start(handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "current sense unavailable (%s): zones still run", esp_err_to_name(err));
    return false;
  }
  adc_ = handle;
  const TaskHandle_t t = xTaskCreateStaticPinnedToCore(&CurrentSampler::task, "zf_isense", sizeof s_sampler_stack,
                                                       this, 6, s_sampler_stack, &s_sampler_tcb, 0);
  return t != nullptr;
}

void CurrentSampler::task(void *arg) { static_cast<CurrentSampler *>(arg)->run(); }

void CurrentSampler::run() {
  running_.store(true, std::memory_order_release);
  static adc_continuous_data_t parsed[kConvPerFrame];
  zf::CurrentWindowStats w;
  uint32_t seq = 0;
  auto window_start = static_cast<zf::MonoMs>(esp_timer_get_time() / 1000);
  for (;;) {
    uint32_t n = 0;
    if (adc_continuous_read_parse(static_cast<adc_continuous_handle_t>(adc_), parsed, kConvPerFrame, &n, 100) !=
        ESP_OK)
      continue;  // the window spanning the timeout is dropped below
    for (uint32_t i = 0; i < n; ++i) {
      if (!parsed[i].valid || parsed[i].unit != ADC_UNIT_1)
        continue;
      const bool is_hi = static_cast<int>(parsed[i].channel) == ch_hi_;
      const bool is_lo = static_cast<int>(parsed[i].channel) == ch_lo_;
      if (!is_hi && !is_lo)
        continue;
      int mv = 0;
      if (adc_cali_raw_to_voltage(static_cast<adc_cali_handle_t>(is_hi ? cali_hi_ : cali_lo_),
                                  static_cast<int>(parsed[i].raw_data), &mv) != ESP_OK)
        continue;
      (is_hi ? w.hi : w.lo).add(mv);
      if (w.hi.n < kSamplesPerWindow)
        continue;
      const auto now = static_cast<zf::MonoMs>(esp_timer_get_time() / 1000);
      if (now - window_start <= kMaxWindowMs) {
        w.seq = ++seq;
        w.endMs = now;
        w.startMs = now - kWindowMs;
        taskENTER_CRITICAL(&lock_);
        latest_ = w;
        taskEXIT_CRITICAL(&lock_);
      } else {
        dropped_.fetch_add(1, std::memory_order_relaxed);
      }
      w.hi.reset();
      w.lo.reset();
      window_start = now;
    }
  }
}

bool CurrentSampler::available() const {
  if (!running_.load(std::memory_order_acquire))
    return false;
  const zf::CurrentWindowStats w = latest();
  const auto now = static_cast<zf::MonoMs>(esp_timer_get_time() / 1000);
  return w.seq != 0 && now - w.endMs <= kStaleMs;
}

zf::CurrentWindowStats CurrentSampler::latest() const {
  taskENTER_CRITICAL(&lock_);
  const zf::CurrentWindowStats w = latest_;
  taskEXIT_CRITICAL(&lock_);
  return w;
}

// ---- expander bus -------------------------------------------------------------------------

zf::hw::BusStatus ExpanderBus::write(uint8_t reg, uint8_t value) {
  return map_error(dev_->write_register(reg, &value, 1));
}

zf::hw::BusStatus ExpanderBus::read(uint8_t reg, uint8_t &value) {
  return map_error(dev_->read_register(reg, &value, 1));
}

zf::hw::BusStatus ExpanderBus::probe() { return map_error(dev_->write(nullptr, 0)); }

void ExpanderBus::recover() { ESP_LOGW(TAG, "I2C bus error; the keep-alive is stopped, which resets the expander"); }

}  // namespace esphome::zoneflow

#endif  // USE_ESP32
