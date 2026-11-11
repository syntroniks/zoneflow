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
//    detectors remove drive within their hold-up (hardware design 19.4, as
//    review A2 left it: valve current ends within 1.25 s if the pin stopped
//    high; the relay transistor alone may take 2.19 s); the reset then
//    releases the pin to R33, low.
#include "board_io.h"

#ifdef USE_ESP32

#include <cstring>

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/temperature_sensor.h"
#include "driver/uart.h"
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
#include "esp_heap_caps.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/gpio_struct.h"
#include "soc/io_mux_reg.h"

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
// Contract 19.4 as review A2 left it (finding M6): an edge at least every
// 20 ms and no phase shorter than 2.5 ms. The pin changes level only on a
// keep-alive period, so the period is the shortest phase.
static_assert(kKeepAlivePeriods * kTimerPeriodUs == kKeepAlivePeriodMs * 1000, "keep-alive period");
static_assert(zf::keepalive::periodMeetsContract(kKeepAlivePeriods * kTimerPeriodUs),
              "hardware design 19.4: keep-alive period between 2.5 and 20 ms");
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
  const auto ka = static_cast<uint32_t>(s_pin_ka);
  // Step 2b: while the keep-alive pin floats, read it every 1 ms.
  if (s_guard.pinIsInput())
    s_guard.sampleFloat(gpio_ll_get_level(&GPIO, ka) != 0);
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
    if (s_guard.pinIsInput()) {
      gpio_ll_output_disable(&GPIO, ka);  // step 2b: driver off, no pull; R33 holds the net low
    } else {
      gpio_ll_set_level(&GPIO, ka, on ? 1u : 0u);  // level first, then the driver
      gpio_ll_output_enable(&GPIO, ka);
    }
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
// ISENSE_HI, ISENSE_LO, GPIO7, the relay read-back (review A2 4.6 question 7),
// and VIN_MON (hardware design 19.10). 20 kS/s in all.
constexpr uint32_t kChannels = 4;
constexpr uint32_t kConvPerFrame = kChannels * kSamplesPerWindow / 10;  // 10 ms frames
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
  if (s_pin_ka >= 0) {
    gpio_ll_set_level(&GPIO, static_cast<uint32_t>(s_pin_ka), 0);
    gpio_ll_output_enable(&GPIO, static_cast<uint32_t>(s_pin_ka));
  }
}

bool keepalive_pin_plain() {
  // Rule 16 and contract 19.4: the IO_MUX selects the GPIO function and the
  // GPIO matrix routes no peripheral signal to the pin.
  if (s_pin_ka < 0)
    return true;
  const auto ka = static_cast<uint32_t>(s_pin_ka);
  const uint32_t mux = REG_GET_FIELD(IO_MUX_GPIO0_REG + ka * 4u, MCU_SEL);
  const uint32_t out = REG_GET_FIELD(GPIO_FUNC0_OUT_SEL_CFG_REG + ka * 4u, GPIO_FUNC0_OUT_SEL);
  return mux == PIN_FUNC_GPIO && out == SIG_GPIO_OUT_IDX;
}

// ---- rule 17: PSRAM traffic during the float test ----------------------------------------

namespace {
// Larger than the 64 kB data cache: a pass reads and writes back the PSRAM.
constexpr std::size_t kLoadBytes = 256u * 1024u;
std::atomic<bool> s_load{false};
volatile uint32_t *s_load_buf = nullptr;
StaticTask_t s_load_tcb;
StackType_t s_load_stack[2048];

// U0TXD: 0x55 at the console baud rate, a square wave at half of it.
constexpr uart_port_t kUart = UART_NUM_0;
constexpr int kUartBaud = 115200;
bool s_uart_ok = false;

void load_task(void *) {
  uint32_t sum = 0;
  static const uint8_t kPattern[64] = {
      0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
      0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
      0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
      0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55};
  for (;;) {
    if (!s_load.load(std::memory_order_relaxed)) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    // Keep the UART FIFO full: 64 bytes last 5.6 ms at 115200 baud, so a
    // pass of the PSRAM loop never lets the line go idle.
    if (s_uart_ok)
      uart_write_bytes(kUart, kPattern, sizeof kPattern);
    if (s_load_buf != nullptr) {
      for (std::size_t i = 0; i < kLoadBytes / sizeof(uint32_t); i += 8) {
        sum += s_load_buf[i];
        s_load_buf[i] = sum;
      }
    }
    vTaskDelay(1);
  }
}

temperature_sensor_handle_t s_temp = nullptr;
bool s_temp_tried = false;
}  // namespace

bool die_temperature(float &celsius) {
  if (!s_temp_tried) {
    s_temp_tried = true;
    // The range whose error is smallest around 25 to 50 C (ESP-IDF v5.5,
    // temperature sensor: -10 to 80 C, under 1 C).
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_temp) != ESP_OK || temperature_sensor_enable(s_temp) != ESP_OK) {
      ESP_LOGW(TAG, "die temperature sensor not available: the board does not commission (19.7)");
      s_temp = nullptr;
    }
  }
  return s_temp != nullptr && temperature_sensor_get_celsius(s_temp, &celsius) == ESP_OK;
}

bool start_float_test_load() {
  s_load_buf = static_cast<volatile uint32_t *>(heap_caps_calloc(1, kLoadBytes, MALLOC_CAP_SPIRAM));
  if (s_load_buf == nullptr)
    ESP_LOGW(TAG, "no PSRAM buffer: the float test runs without PSRAM traffic");
  // The logger is on USB_SERIAL_JTAG (base.yaml), so UART0 is free. A
  // configuration that opens UART0 itself keeps it: the float test then runs
  // without the U0TXD load, and the log says so.
  if (uart_is_driver_installed(kUart)) {
    ESP_LOGW(TAG, "UART0 in use: the float test runs without the 0x55 load on U0TXD");
  } else {
    uart_config_t u = {};
    u.baud_rate = kUartBaud;
    u.data_bits = UART_DATA_8_BITS;
    u.parity = UART_PARITY_DISABLE;
    u.stop_bits = UART_STOP_BITS_1;
    u.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    u.source_clk = UART_SCLK_DEFAULT;
    s_uart_ok = uart_driver_install(kUart, 256, 0, 0, nullptr, 0) == ESP_OK && uart_param_config(kUart, &u) == ESP_OK;
  }
  return xTaskCreateStaticPinnedToCore(&load_task, "zf_float", sizeof s_load_stack, nullptr, 2, s_load_stack,
                                       &s_load_tcb, 0) != nullptr;
}

void set_float_test_load(bool on) { s_load.store(on, std::memory_order_relaxed); }

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
  ka.mode = GPIO_MODE_INPUT_OUTPUT;  // input buffer on: step 2b reads the pin with its driver off
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
  adc_channel_t rb = ADC_CHANNEL_0;
  adc_channel_t vin = ADC_CHANNEL_0;
  esp_err_t err = adc_continuous_io_to_channel(p.current_high, &unit, &hi);
  if (err == ESP_OK && unit == ADC_UNIT_1)
    err = adc_continuous_io_to_channel(p.current_low, &unit, &lo);
  if (err == ESP_OK && unit == ADC_UNIT_1)
    err = adc_continuous_io_to_channel(p.relay_readback, &unit, &rb);
  // Without VIN_MON the fourth slot samples GPIO7 again and is not used.
  if (err == ESP_OK && unit == ADC_UNIT_1)
    err = adc_continuous_io_to_channel(p.vin_monitor >= 0 ? p.vin_monitor : p.relay_readback, &unit, &vin);
  if (err != ESP_OK || unit != ADC_UNIT_1) {
    ESP_LOGE(TAG, "current sense, relay read-back and VIN_MON pins are not on ADC1");
    return false;
  }
  ch_hi_ = static_cast<int>(hi);
  ch_lo_ = static_cast<int>(lo);
  ch_rb_ = static_cast<int>(rb);
  ch_vin_ = p.vin_monitor >= 0 ? static_cast<int>(vin) : -1;

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
  const adc_channel_t chans[kChannels] = {hi, lo, rb, vin};
  adc_digi_pattern_config_t pattern[kChannels] = {};
  for (uint32_t i = 0; i < kChannels; ++i) {
    pattern[i].atten = ADC_ATTEN_DB_6;  // pin map: 6 dB, 0 to 1600 mV
    pattern[i].channel = static_cast<uint8_t>(chans[i]);
    pattern[i].unit = ADC_UNIT_1;
    pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  }
  adc_continuous_config_t cfg = {};
  cfg.pattern_num = kChannels;
  cfg.adc_pattern = pattern;
  cfg.sample_freq_hz = kSampleHzPerChannel * kChannels;
  cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
  err = adc_continuous_config(handle, &cfg);
  // adc_continuous_config() turns the pins' digital input buffers off. GPIO7
  // is still read as a level in the keep-alive interrupt: buffer back on, no
  // pull (rule 12). Had it failed, the pin would read low: the idle test
  // fails on the safe side. A bench item.
  if (err == ESP_OK)
    err = gpio_input_enable(static_cast<gpio_num_t>(p.relay_readback));
  void **cali[kChannels] = {&cali_hi_, &cali_lo_, &cali_rb_, &cali_vin_};
  for (uint32_t i = 0; err == ESP_OK && i < kChannels; ++i) {
    adc_cali_curve_fitting_config_t c = {};
    c.unit_id = ADC_UNIT_1;
    c.chan = chans[i];
    c.atten = ADC_ATTEN_DB_6;
    c.bitwidth = ADC_BITWIDTH_12;
    adc_cali_handle_t h = nullptr;
    err = adc_cali_create_scheme_curve_fitting(&c, &h);
    *cali[i] = h;
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
      const int ch = static_cast<int>(parsed[i].channel);
      zf::ChannelStats *stats = ch == ch_hi_    ? &w.hi
                                : ch == ch_lo_  ? &w.lo
                                : ch == ch_rb_  ? &w.relayRb
                                : ch == ch_vin_ ? &w.vinMon
                                                : nullptr;
      void *calib = ch == ch_hi_ ? cali_hi_ : ch == ch_lo_ ? cali_lo_ : ch == ch_rb_ ? cali_rb_ : cali_vin_;
      if (stats == nullptr)
        continue;
      int mv = 0;
      if (adc_cali_raw_to_voltage(static_cast<adc_cali_handle_t>(calib), static_cast<int>(parsed[i].raw_data), &mv) !=
          ESP_OK)
        continue;
      stats->add(mv);
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
      w.relayRb.reset();
      w.vinMon.reset();
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

zf::hw::BusStatus ExpanderBus::writePair(uint8_t reg, uint8_t v0, uint8_t v1) {
  const uint8_t data[2] = {v0, v1};
  return map_error(dev_->write_register(reg, data, 2));
}

zf::hw::BusStatus ExpanderBus::read(uint8_t reg, uint8_t &value) {
  return map_error(dev_->read_register(reg, &value, 1));
}

zf::hw::BusStatus ExpanderBus::probe() { return map_error(dev_->write(nullptr, 0)); }

void ExpanderBus::recover() { ESP_LOGW(TAG, "I2C bus error; the keep-alive is stopped, which resets the expander"); }

}  // namespace esphome::zoneflow

#endif  // USE_ESP32
