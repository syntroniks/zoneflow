// SPDX-License-Identifier: Apache-2.0
// The UI task: owns LVGL, the view and the display logic.
//
//   esp_timer (5 ms)   controls -> decoders -> input queue
//   control task       EventBus handler -> event queue (copy, never blocks)
//   zf_ui task         queues -> zf::ui::Ui -> Frame -> zf::view::View -> LVGL -> SPI
//
// Nothing else touches LVGL, so LVGL runs without its OS layer or a lock.
#include <cinttypes>
#include <cstdio>

#include "esp_app_desc.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "port.hpp"
#include "zf_board/board.hpp"
#include "zf_display/display.hpp"
#include "zf_display/stubs.hpp"
#include "zf_platform/platform.hpp"
#include "zf_ui/ui.hpp"
#include "zf_view/view.hpp"

namespace zf::display {
namespace {

constexpr const char *kTag = "zf.display";
constexpr UBaseType_t kTaskPriority = 4; // below the control task (10), above logging (2)
constexpr BaseType_t kTaskCore = 0;      // the control and keep-alive tasks run on core 1
// LVGL's software renderer, the frame builder (std::string, std::vector)
// and ConfigStore::update() with its JSON writer all run on this stack.
constexpr uint32_t kTaskStackBytes = 12288;
constexpr UBaseType_t kInputQueueDepth = 32;
constexpr UBaseType_t kEventQueueDepth = 16;
// Longest sleep between loop passes; the state is polled this often.
constexpr uint32_t kMaxSleepMs = 20;
// Rule 17: a duty well away from 0 and 100 %, so that the PWM net toggles.
constexpr uint8_t kFloatTestBacklightPercent = 50;

class EspSystemPort final : public ISystemPort {
public:
    const char *firmwareVersion() const override { return esp_app_get_description()->version; }
    const char *idfVersion() const override { return esp_get_idf_version(); }
    const char *boardName() const override { return board::kBoardShortName; }
    const char *graphicsVersion() const override
    {
        static char version[16];
        std::snprintf(version, sizeof version, "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
        return version;
    }
    // The shutdown handler installed by zf_platform drives the outputs off.
    void restart() override { esp_restart(); }
};

struct Context {
    Context(const AppServices &s, const Ports &p) : services(s), ports(p) {}
    AppServices services;
    Ports ports;
    QueueHandle_t inputs = nullptr;
    platform::EventQueueSubscriber events;
    lv_display_t *display = nullptr;
};

StubNetworkPort sStubNetwork;
StubZoneDiagPort sStubDiag;
EspSystemPort sSystem;
port::NvsSettingsStore sSettings;

MonoMs nowMs() { return static_cast<MonoMs>(esp_timer_get_time() / 1000); }

void uiTask(void *arg)
{
    auto *ctx = static_cast<Context *>(arg);
    static view::View view;
    static ui::Ui ui(ctx->services, ctx->ports);

    view.create(ctx->display);
    ui.begin(nowMs());
    ESP_LOGI(kTag, "view built: LVGL heap %" PRIu32 " bytes", port::lvglHeapUsed());

    uint32_t shown = 0;
    bool drawn = false;
    for (;;) {
        const MonoMs now = nowMs();
        ui::InputEvent input;
        while (xQueueReceive(ctx->inputs, &input, 0) == pdTRUE) {
            ui.handleInput(input, now);
        }
        Event event;
        while (ctx->events.receive(event, 0)) {
            ui.handleEvent(event, now);
        }
        ui.tick(now);

        // Nothing is drawn while the backlight is off; the first frame after
        // waking is drawn before the light comes back.
        const bool lit = ui.power() != ui::Power::Off;
        if (lit && (ui.frameSerial() != shown || !drawn)) {
            shown = ui.frameSerial();
            view.render(ui.frame());
            drawn = true;
        } else if (!lit) {
            drawn = false;
        }
        // Hardware design 19.1 rule 17: during the float test the backlight
        // PWM runs at a mid duty, whatever the user set, and the whole screen
        // is sent to the panel again and again, so that the PWM net and the
        // SPI bus toggle. A bridge from either to the keep-alive net then
        // shows in step 2b.
        const bool floatLoad = platform::floatTestLoad();
        if (floatLoad) {
            lv_obj_invalidate(lv_screen_active());
        }
        const uint32_t next = lv_timer_handler();
        port::backlightSet(floatLoad ? kFloatTestBacklightPercent : drawn ? ui.backlightPercent() : 0);

        // Sleep until LVGL needs to run again, at most kMaxSleepMs, and wake
        // at once when a control is used.
        const uint32_t sleepMs = floatLoad ? 1 : next < kMaxSleepMs ? (next == 0 ? 1 : next) : kMaxSleepMs;
        xQueuePeek(ctx->inputs, &input, pdMS_TO_TICKS(sleepMs));
    }
}

} // namespace

esp_err_t start(const AppServices &services, const StartOptions &options)
{
    static Context ctx(services, Ports{options.network != nullptr ? *options.network : sStubNetwork,
                                       options.zoneDiag != nullptr ? *options.zoneDiag : sStubDiag,
                                       options.system != nullptr ? *options.system : sSystem, sSettings});

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return static_cast<uint32_t>(esp_timer_get_time() / 1000); });

    esp_err_t err = port::backlightInit();
    if (err == ESP_OK) {
        err = port::lcdInit(&ctx.display);
    }
    if (err != ESP_OK) {
        // No display is not a reason to stop watering.
        ESP_LOGW(kTag, "display not started (%s); the controller runs without it", esp_err_to_name(err));
        return ESP_OK;
    }

    ctx.inputs = xQueueCreate(kInputQueueDepth, sizeof(ui::InputEvent));
    if (ctx.inputs == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    err = port::inputsInit(ctx.inputs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "controls not started (%s); the display shows status only", esp_err_to_name(err));
    }
    err = ctx.events.begin(services.events, kAllEvents, kEventQueueDepth);
    if (err != ESP_OK) {
        return err;
    }
    if (xTaskCreatePinnedToCore(&uiTask, "zf_ui", kTaskStackBytes, &ctx, kTaskPriority, nullptr, kTaskCore) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(kTag, "started: %dx%d ST7789, LVGL %d.%d.%d", board::kLcdWidth, board::kLcdHeight, LVGL_VERSION_MAJOR,
             LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    return ESP_OK;
}

} // namespace zf::display
