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
#include "zf/config/config_store.hpp"
#include "zf/events/event_bus.hpp"
#include "zf/hal.hpp"
#include "zf/history/history.hpp"
#include "zf/time/clock.hpp"
#include "zf/time/time_service.hpp"
#include "zf/zone/output_enable.hpp"
#include "zf/zone/zero_cross.hpp"

// IRAM-safe: valve outputs and OE inactive using direct register writes.
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

// ---- valve outputs --------------------------------------------------------------

class GpioOutputs final : public IOutputHal {
public:
    // Drives every output and OE inactive, then configures them as outputs.
    // Call first thing in app_main.
    esp_err_t init();
    void setZone(ZoneIndex zone, bool on) override;
    void setMaster(bool on) override;
    void allOff() override;
    void clearOverCurrentTrip() override;
};

// ---- mains zero-cross input --------------------------------------------------------

class ZeroCrossInput final : public IZeroCrossHal {
public:
    // ESP_ERR_NOT_SUPPORTED on a board without the signal.
    esp_err_t init();
    ZeroCrossSample sample() const override;
    void delayUs(uint32_t us) override;

private:
    static void onEdge(void *arg);
    std::atomic<uint32_t> seq_{0};
    std::atomic<uint32_t> edges_{0};
    std::atomic<uint32_t> lastEdgeUs_{0};
    std::atomic<uint32_t> intervalUs_{0};
};

// ---- output-enable keep-alive ------------------------------------------------------

OutputEnableMode configuredOutputEnableMode();
// Starts the keep-alive: an IRAM-safe GPTimer interrupt that calls
// guard.step() every CONFIG_ZF_OE_TOGGLE_PERIOD_MS and drives the OE pin.
esp_err_t startKeepAlive(OutputEnableGuard &guard);

// ---- sensors -----------------------------------------------------------------------

class SensorInputs final : public ISensorHal {
public:
    esp_err_t init();
    SensorSample read() override;

private:
    void *pcnt_ = nullptr; // pcnt_unit_handle_t
    void *adc_ = nullptr;  // adc_oneshot_unit_handle_t
    void *cali_ = nullptr; // adc_cali_handle_t
    int adcChannel_ = -1;
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

// ---- I2C and RTC ------------------------------------------------------------------------

class I2cBus {
public:
    esp_err_t init();
    i2c_master_bus_handle_t handle() const { return bus_; }

private:
    i2c_master_bus_handle_t bus_ = nullptr;
};

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
