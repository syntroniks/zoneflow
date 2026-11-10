// SPDX-License-Identifier: Apache-2.0
// ESP-IDF side of the rev A board for the ESPHome package: the keep-alive and
// zero-cross interrupt, the ADC sampler and the expander bus. Everything that
// decides anything is in zf_core or supervisor.h; this file only moves bits.
#pragma once

#ifdef USE_ESP32

#include <atomic>
#include <cstdint>

#include "esphome/components/i2c/i2c.h"
#include "freertos/FreeRTOS.h"
#include "zf/diag/current_sense.hpp"
#include "zf/hw/output_stage.hpp"
#include "zf/zone/output_enable.hpp"
#include "zf/zone/zero_cross.hpp"

namespace esphome::zoneflow {

struct BoardPins {
  int keepalive = -1;        // KA, GPIO38: plain GPIO output, every edge decided in the interrupt
  int drive_readback = -1;   // A_RB, GPIO42: input, internal pull-down
  int relay_readback = -1;   // B_RB, GPIO7: input, no pull
  int overcurrent = -1;      // OC, GPIO40: input, no pull, low = latch set
  int zero_cross = -1;       // AC_ZC, GPIO41: input, R58 pull-up on the board
  int sensor_supply = -1;    // SENS_PWR_EN, GPIO45: output after start-up, never a pull-up
  int current_high = -1;     // ISENSE_HI, GPIO2, ADC1
  int current_low = -1;      // ISENSE_LO, GPIO1, ADC1
};

// The keep-alive guard and the zero-cross tracker live in DRAM and are used
// by the 1 ms timer interrupt. One board per firmware.
zf::OutputEnableGuard &keepalive_guard();
zf::ZeroCrossTracker &zero_cross();
// Microseconds as counted by the interrupt (1000 per period). The zero-cross
// tracker's edges use this time base, so presence and frequency are read
// against it too.
uint32_t isr_micros();

// Keep-alive pin low and an output, read-back pins as inputs with the pulls
// of hardware design 19.1 rule 12. First thing in setup().
bool init_output_stage_pins(const BoardPins &pins);
// The 1 ms GPTimer alarm interrupt (IRAM, runs while the flash cache is off):
//  - every period: samples AC_ZC and feeds edges to the zero-cross tracker;
//  - every fifth period: OutputEnableGuard::step(5), which decides the
//    keep-alive level from the control-loop heartbeat, writes it to the pin,
//    and records the two detector read-backs and the over-current latch.
bool start_keepalive_timer(const BoardPins &pins);
// Pin low now and the guard tripped (shutdown, restart, brownout).
void force_outputs_off();
// SENS_PWR_EN high: latch the level, then make the pin an output, no pull.
bool enable_sensor_supply(const BoardPins &pins);

// ADC1 continuous mode on ISENSE_HI and ISENSE_LO, 5 kS/s each, 6 dB,
// calibrated to millivolts, windows of exactly 500 samples (100 ms, whole
// mains cycles at 50 and 60 Hz). Hardware design 19.2. A task on core 0
// accumulates; the main loop takes the latest window.
class CurrentSampler {
 public:
  bool start(const BoardPins &pins);
  bool available() const;
  zf::CurrentWindowStats latest() const;
  uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

 private:
  static void task(void *arg);
  void run();
  void *adc_ = nullptr;
  void *cali_hi_ = nullptr;
  void *cali_lo_ = nullptr;
  int ch_hi_ = -1;
  int ch_lo_ = -1;
  mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  zf::CurrentWindowStats latest_{};
  std::atomic<bool> running_{false};
  std::atomic<uint32_t> dropped_{0};
};

// The TCA9539 through ESPHome's I2C bus. Every call is bounded by the bus
// driver's transaction timeout. ESPHome offers no bus-clear call after
// set-up, so recover() only logs: the output stage has already stopped the
// keep-alive, which resets the expander in hardware.
class ExpanderBus final : public zf::hw::IExpanderBus {
 public:
  explicit ExpanderBus(i2c::I2CDevice *dev) : dev_(dev) {}
  zf::hw::BusStatus write(uint8_t reg, uint8_t value) override;
  zf::hw::BusStatus read(uint8_t reg, uint8_t &value) override;
  zf::hw::BusStatus probe() override;
  void recover() override;

 private:
  i2c::I2CDevice *dev_;
};

}  // namespace esphome::zoneflow

#endif  // USE_ESP32
