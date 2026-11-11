// SPDX-License-Identifier: Apache-2.0
// Wires the ZoneFlow core to the hardware and starts the tasks.
//
// Tasks (see docs/design/firmware-architecture.md, threading model):
//   keep-alive    GPTimer ISR (IRAM)  drives OE from OutputEnableGuard::step()
//   zf_control    prio 10,    core 1  commands -> Controller::tick(), TWDT
//   zf_persist    prio 3,     core 0  stores runtime state (JSON, NVS)
//   zf_evlog      prio 2,     core 0  logs events
//   zf_history    prio 2,     core 0  stores the event history (NVS, batched)
//   main          prio 1             status LED and a periodic state log
#include <cinttypes>

#include "display_ports.hpp"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "zf/app/controller.hpp"
#include "zf/app/services.hpp"
#include "zf/hw/reference_store.hpp"
#include "zf_board/board.hpp"
#include "zf_display/display.hpp"
#include "zf_mqtt/mqtt.hpp"
#include "zf_net/net.hpp"
#include "zf_ota/ota.hpp"
#include "zf_platform/platform.hpp"
#include "zf_web/web.hpp"

namespace {

constexpr const char *kTag = "zoneflow";

using namespace zf;

// Everything lives in static storage: nothing in the control path allocates.
platform::SensorInputs gSensors;
platform::CurrentSampler gCurrent;
platform::Tca9539Bus gExpander;
platform::RtcRetainedStore gRetained;
platform::NvsBlobStorage gStorage;
platform::I2cBus gI2c;
platform::Pcf8563Rtc gRtc;
platform::EspClock gClock;
platform::EspSystemTime gSystemTime;
OutputEnableGuard gOutputEnable(platform::configuredOutputEnableMode(), CONFIG_ZF_OE_HEARTBEAT_TIMEOUT_MS);
EventBus gEvents;
StateStore gState;
CommandQueue gCommands;
ConfigStore gConfig(gStorage);
HistoryLog gHistory(gStorage);
platform::ZeroCrossInput gZeroCross;

// The output stage: expander bits from the board header (pin map).
hw::ExpanderLayout expanderLayout()
{
    hw::ExpanderLayout l;
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        l.zone[z] = {board::kExpanderBits.zone[z].port, board::kExpanderBits.zone[z].bit};
    }
    l.master = {board::kExpanderBits.master.port, board::kExpanderBits.master.bit};
    l.relay = {board::kExpanderBits.relay.port, board::kExpanderBits.relay.bit};
    l.variant8ZoneN = {board::kExpanderBits.variant8ZoneN.port, board::kExpanderBits.variant8ZoneN.bit};
    l.sensorFaultN = {board::kExpanderBits.sensorFaultN.port, board::kExpanderBits.sensorFaultN.bit};
    return l;
}

hw::OutputStageConfig stageConfig()
{
    hw::OutputStageConfig c;
    c.maxValveBitsLow = static_cast<uint8_t>(board::kMaxOutputsOn);
    c.tickMs = CONFIG_ZF_CONTROL_PERIOD_MS;
    return c;
}

// This board's detector release times (19.7, the limits of step 9), in NVS.
// Loaded on the first control tick, after gStorage.init().
hw::BlobReferenceStore gReference(gStorage);
hw::OutputStage gStage(gExpander, gOutputEnable, gClock, expanderLayout(), stageConfig(), &gReference);
TimeService *gTime = nullptr;
Controller *gController = nullptr;

StaticTask_t sControlTcb;
StackType_t sControlStack[6144];

void controlTask(void *)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        Command cmd;
        for (int i = 0; i < 8 && gCommands.pop(cmd); ++i) {
            if (!gController->handle(cmd)) {
                ESP_LOGW(kTag, "command %s rejected: %s", toString(cmd.type), toString(gController->lastRunError()));
            }
        }
        gController->setTimeStatus(gTime->status());
        gController->tick();
        // Rule 17: the display, the backlight and the PSRAM busy while the
        // float test runs.
        platform::setFloatTestLoad(gStage.status().floatTest);
        esp_task_wdt_reset();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(CONFIG_ZF_CONTROL_PERIOD_MS));
    }
}

void setStatusLed(bool on) { platform::setStatusLed(on); }

void logState(const SystemState &s)
{
    unsigned open = 0;
    for (std::size_t i = 0; i < s.zoneCount; ++i) {
        open |= s.zones[i].state == ValveState::Open ? (1u << i) : 0u;
    }
    ESP_LOGI(kTag,
             "state seq=%" PRIu32 " time=%s utc=%lld zones_open=0x%02x master=%d queue=%s/%u ac=%d rain=%d "
             "current=%" PRId32 "mA faults=0x%08" PRIx32,
             s.sequence, toString(s.timeStatus), static_cast<long long>(s.utc), open, s.masterOn,
             toString(s.queue.state), s.queue.length, s.sensors.acPresent, s.sensors.rainActive,
             s.sensors.valveCurrentMa, s.activeFaults);
}

} // namespace

extern "C" void app_main(void)
{
    // Keep-alive low before anything else can fail: with it low the expander
    // is in reset and the relay open, whatever else happens.
    ESP_ERROR_CHECK(platform::initOutputStagePins());
    ESP_LOGI(kTag, "ZoneFlow firmware, board %s", board::kBoardName);
    // First user of the GPIO interrupt service: installs it IRAM-safe.
    if (gZeroCross.init() != ESP_OK) {
        ESP_LOGE(kTag, "no zero-cross input: 24 VAC will read as absent and nothing runs");
    }

    ESP_ERROR_CHECK(gStorage.init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const ConfigStore::LoadResult loaded = gConfig.load();
    ESP_LOGI(kTag, "config: %s", ConfigStore::toString(loaded.status));
    if (!loaded.validation.ok) {
        ESP_LOGE(kTag, "stored config rejected at %s: %s", loaded.validation.path.c_str(),
                 loaded.validation.message.c_str());
    }

    IRtc *rtc = nullptr;
    if (gI2c.init() == ESP_OK) {
        if (gRtc.init(gI2c) == ESP_OK) {
            rtc = &gRtc;
        }
        if (gExpander.init(gI2c) != ESP_OK) {
            ESP_LOGE(kTag, "expander device not added: the self-test will fail with a bus error");
        }
    }
    static TimeService timeService(gSystemTime, rtc);
    gTime = &timeService;
    ESP_LOGI(kTag, "time at boot: %s", toString(timeService.boot()));

    if (gCurrent.start() != ESP_OK) {
        ESP_LOGE(kTag, "valve current not measured: current_sense_unavailable");
    }
    ESP_ERROR_CHECK(gSensors.init(gZeroCross, gCurrent));
    // Rule 13: the application runs, so the strapping pin may now be driven.
    ESP_ERROR_CHECK(gSensors.enableSensorSupply());

    static Controller controller(
        ControllerDeps{gClock, gStage, gSensors, gOutputEnable, gEvents, gState, gConfig, &gStorage, &gRetained});
    gController = &controller;
    CurrentSenseConfig sense;
    sense.hiMaPerMv = board::kIsenseHiMaPerMv;
    sense.loMaPerMv = board::kIsenseLoMaPerMv;
    sense.hiMinMv = board::kIsenseHiMinMv;
    sense.hiMaxMv = board::kIsenseHiMaxMv;
    controller.configureCurrentSense(sense);
    // The tracker already gives the crossings themselves: offset 0.
    controller.attachZeroCross(&gZeroCross, 0);
    ESP_ERROR_CHECK(platform::installSafetyHooks(controller, gOutputEnable));
    ESP_ERROR_CHECK(platform::startEventLogger(gEvents));
    gHistory.load();
    setHistoryLog(&gHistory);
    {
        HistoryRecord boot;
        boot.utc = gClock.nowUtc();
        boot.kind = HistoryKind::Boot;
        boot.a = static_cast<uint8_t>(platform::historyResetReason());
        gHistory.append(boot, gClock.monoMs(), true);
    }
    controller.begin();
    // After begin(): the start-up fault and configuration are not news.
    ESP_ERROR_CHECK(platform::startHistoryRecorder(gEvents, gHistory));
    ESP_ERROR_CHECK(platform::startPersistWriter(controller.persistMailbox(), gStorage));
    if (loaded.status == ConfigStore::LoadStatus::InvalidDefaults) {
        controller.reportFault(FaultCode::ConfigInvalid);
    }
    controller.setTimeStatus(timeService.status());

    if (platform::startFloatTestLoad() != ESP_OK) {
        ESP_LOGW(kTag, "float test load: PSRAM part not started");
    }
    ESP_ERROR_CHECK(platform::startKeepAlive(gOutputEnable));
    const TaskHandle_t control = xTaskCreateStaticPinnedToCore(&controlTask, "zf_control", sizeof sControlStack,
                                                               nullptr, 10, sControlStack, &sControlTcb, 1);
    if (control == nullptr) {
        ESP_LOGE(kTag, "control task not started; restarting");
        esp_restart();
    }

    ESP_ERROR_CHECK(platform::startTimeSync(timeService));

    // Extension points. Each module gets the same service interfaces.
    static AppServices services{gEvents, gState, gCommands, gConfig, timeService};
    ESP_ERROR_CHECK(net::start(services));
    ESP_ERROR_CHECK(web::start(services));
    ESP_ERROR_CHECK(mqtt::start(services));
    ESP_ERROR_CHECK(ota::start(services));
    static DisplayNetworkPort displayNetwork;
    display::StartOptions displayOptions;
    displayOptions.network = &displayNetwork;
    ESP_ERROR_CHECK(display::start(services, displayOptions));

    ESP_ERROR_CHECK(ota::markRunningAppValid());

    // Status LED: on while a valve is open, fast blink on a fault, slow
    // heartbeat otherwise. State is logged once a minute.
    uint32_t tick = 0;
    for (;;) {
        const SystemState s = gState.snapshot();
        bool anyOpen = s.masterOn;
        for (const ZoneStatus &z : s.zones) {
            anyOpen = anyOpen || z.state == ValveState::Open;
        }
        const uint32_t latched = s.activeFaults & ~(1u << static_cast<unsigned>(FaultCode::TimeInvalid));
        if (anyOpen) {
            setStatusLed(true);
        } else if (latched != 0) {
            setStatusLed((tick % 2) == 0);
        } else {
            setStatusLed((tick % 20) == 0);
        }
        if (tick % 600 == 0) {
            logState(s);
        }
        ++tick;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
