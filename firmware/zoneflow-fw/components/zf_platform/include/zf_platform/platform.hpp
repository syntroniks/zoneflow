// SPDX-License-Identifier: Apache-2.0
// ESP-IDF implementations of the zf_core interfaces. All pin numbers come
// from zf_board/board.hpp.
#pragma once

#include <atomic>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "zf/app/controller.hpp"
#include "zf/app/retained.hpp"
#include "zf/config/config_store.hpp"
#include "zf/events/event_bus.hpp"
#include "zf/hal.hpp"
#include "zf/history/history.hpp"
#include "zf/hw/output_stage.hpp"
#include "zf/time/clock.hpp"
#include "zf/time/time_service.hpp"
#include "zf/zone/output_enable.hpp"
#include "zf/zone/zero_cross.hpp"

// IRAM-safe: the keep-alive pin low by a direct register write. On rev A
// that is the only way a GPIO can remove valve drive: the detectors then
// reset the expander and open the relay within their hold-up (about 1 s).
// Safe in panic, brownout and shutdown handlers.
extern "C" void zf_platform_force_outputs_off(void);

namespace zf::platform {

// ---- clocks -------------------------------------------------------------------

class EspClock final : public IClock {
public:
    UtcSeconds nowUtc() const override;
    MonoMs monoMs() const override;
};

class EspSystemTime final : public ISystemTime {
public:
    UtcSeconds now() const override;
    bool set(UtcSeconds utc) override;
};

// ---- output stage pins and keep-alive ------------------------------------------------

// Keep-alive pin low and an output, the two detector read-backs and the
// over-current latch inputs (hardware design 19.1 rules 1 and 12), the
// status LED. Call first thing in app_main.
esp_err_t initOutputStagePins();

OutputEnableMode configuredOutputEnableMode();
// Starts the keep-alive: an IRAM-safe GPTimer interrupt every 1 ms. Every
// CONFIG_ZF_OE_TOGGLE_PERIOD_MS-th one calls guard.step(), drives the
// keep-alive pin (or turns its driver off for the float test, 19.7 step 2b),
// then samples GPIO42, GPIO7 and GPIO40 into guard.sample(). While the pin
// floats, every one reads it into guard.sampleFloat().
esp_err_t startKeepAlive(OutputEnableGuard &guard);
// Rule 16 and contract 19.4: the keep-alive pin's IO_MUX function is GPIO and
// no peripheral signal is routed to it. Any task.
bool keepAlivePinPlain();

// Rule 17: while the float test runs, the nets that toggle by themselves
// must toggle. On: the backlight PWM at a fixed mid duty, the display
// redrawn continuously, and a task streaming through a PSRAM buffer. The
// control task sets it from OutputStageStatus::floatTest.
void setFloatTestLoad(bool on);
bool floatTestLoad();
// Starts the PSRAM task (static allocation, core 0, low priority).
esp_err_t startFloatTestLoad();

void setStatusLed(bool on);

// ---- I2C ------------------------------------------------------------------------------------

class I2cBus {
public:
    esp_err_t init();
    i2c_master_bus_handle_t handle() const { return bus_; }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
};

// TCA9539 at board::kExpanderI2cAddress. Every call is bounded by
// board::kExpanderI2cTimeoutMs (plus the driver's 50 ms bus clear for
// recover()). Control task only.
class Tca9539Bus final : public hw::IExpanderBus {
public:
    esp_err_t init(I2cBus &bus);
    hw::BusStatus write(uint8_t reg, uint8_t value) override;
    hw::BusStatus read(uint8_t reg, uint8_t &value) override;
    hw::BusStatus probe() override;
    void recover() override;

private:
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
};

// ---- mains zero-cross input --------------------------------------------------------

// AC_ZC, both edges, IRAM-safe GPIO interrupt (it keeps running while the
// flash cache is off, so a flash erase does not look like a loss of 24 VAC).
class ZeroCrossInput final : public IZeroCrossHal {
public:
    ZeroCrossInput();
    esp_err_t init();
    ZeroCrossSample sample() const override;
    void delayUs(uint32_t us) override;
    // An edge within 25 ms (hardware design 19.9).
    bool acPresent() const;
    uint32_t mainsMilliHz() const;
    ZeroCrossTracker &tracker() { return tracker_; }

private:
    static void onEdge(void *arg);
    ZeroCrossTracker tracker_;
};

// ---- current sampling ----------------------------------------------------------------

// ADC1 continuous mode on ISENSE_HI and ISENSE_LO, 6 dB, calibrated to mV,
// accumulated into 100 ms windows by a task (hardware design 19.2).
// GPIO7 (B_RB) is sampled with them as a third channel: review A2 4.6
// question 7. Its digital input buffer is turned back on after the ADC is
// configured, because the keep-alive interrupt still reads it as a level.
class CurrentSampler {
public:
    // Starts the ADC and the task. Not fatal on failure: available() stays
    // false and the controller raises current_sense_unavailable.
    esp_err_t start();
    bool available() const;
    // The latest complete window (seq 0: none yet).
    CurrentWindowStats latest() const;

private:
    static void task(void *arg);
    void run();
    void *adc_ = nullptr;    // adc_continuous_handle_t
    void *caliHi_ = nullptr; // adc_cali_handle_t
    void *caliLo_ = nullptr;
    void *caliRb_ = nullptr;
    int chHi_ = -1;
    int chLo_ = -1;
    int chRb_ = -1;
    mutable portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    CurrentWindowStats latest_{};
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> dropped_{0};
};

// ---- sensors -----------------------------------------------------------------------

class SensorInputs final : public ISensorHal {
public:
    esp_err_t init(ZeroCrossInput &zeroCross, CurrentSampler &current);
    SensorSample read() override;
    // GPIO45: left alone until the application runs (strapping pin), then
    // driven high. Never a pull-up (hardware design 19.1 rule 13).
    esp_err_t enableSensorSupply();

private:
    ZeroCrossInput *zc_ = nullptr;
    CurrentSampler *current_ = nullptr;
    void *pcnt_ = nullptr; // pcnt_unit_handle_t
    bool supplyOn_ = false;
};

// ---- state kept over a software restart ----------------------------------------------------

// RTC_NOINIT memory: survives esp_restart, panics and watchdog resets, not
// a power cycle. The record carries its own integrity check.
class RtcRetainedStore final : public IRetainedStore {
public:
    bool load(RetainedRecord &out) override;
    void save(const RetainedRecord &r) override;
};

// ---- storage -------------------------------------------------------------------------

class NvsBlobStorage final : public IBlobStorage {
public:
    // Initialises the NVS partition (erasing it if its layout is from a newer
    // IDF or it has no free pages) and opens the "zoneflow" namespace.
    esp_err_t init();
    bool read(const char *key, std::string &out) override;
    bool write(const char *key, std::string_view data) override;
    bool erase(const char *key) override;

private:
    uint32_t handle_ = 0;
    bool open_ = false;
};

// Starts the low-priority task that drains the controller's PersistMailbox
// into storage (static allocation, core 0).
esp_err_t startPersistWriter(PersistMailbox &box, IBlobStorage &storage);

// ---- RTC ------------------------------------------------------------------------------------

// NXP PCF8563 (also BM8563). Stores UTC. Register map from the NXP datasheet
// rev 11.1, Table 4.
class Pcf8563Rtc final : public IRtc {
public:
    esp_err_t init(I2cBus &bus);
    bool read(UtcSeconds &out) override;
    bool write(UtcSeconds utc) override;

private:
    i2c_master_dev_handle_t dev_ = nullptr;
};

// ---- network time ---------------------------------------------------------------------------

// Starts SNTP once an IP address is obtained (any interface) and reports each
// sync to the TimeService, which writes the RTC.
esp_err_t startTimeSync(TimeService &time);

// ---- safety hooks -----------------------------------------------------------------------------

// Registers: esp_restart shutdown handler (OTA reboot included), brownout
// callback, and the panic-handler wrapper. Each drives outputs off.
esp_err_t installSafetyHooks(Controller &controller, OutputEnableGuard &guard);

// ---- event queue subscriber ---------------------------------------------------------------------

// Copies events into a FreeRTOS queue from the bus handler (non-blocking,
// drops when full). Use one per consuming task.
class EventQueueSubscriber {
public:
    esp_err_t begin(EventBus &bus, EventMask mask, UBaseType_t depth);
    bool receive(Event &out, TickType_t wait);
    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    static void onEvent(const Event &e, void *ctx);
    QueueHandle_t queue_ = nullptr;
    std::atomic<uint32_t> dropped_{0};
};

// Logs every event (ESP_LOGI) from a low-priority task.
esp_err_t startEventLogger(EventBus &bus);

// ---- persistent history --------------------------------------------------------------------------

// Records events into the history log and writes it in batches from a
// low-priority task (static allocation, core 0). Call log.load() first.
esp_err_t startHistoryRecorder(EventBus &bus, HistoryLog &log);
// Why the chip last reset, for the boot record.
HistoryResetReason historyResetReason();

} // namespace zf::platform
