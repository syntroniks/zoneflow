// SPDX-License-Identifier: Apache-2.0
// Persistent history writer. Events reach it through its own queue (the bus
// handler copies and never waits); this low-priority task converts them to
// records and writes the open chunk in batches (zf/history/history.hpp). The
// control task never touches flash for the history.
#include "freertos/FreeRTOS.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

constexpr const char *kTag = "zf.history";

// Not stored: zone open/close (RunEnded summarises a run), the master,
// queue changes and refused commands.
constexpr EventMask kHistoryEvents = kAllEvents & ~eventBit(EventType::ZoneChanged) &
                                     ~eventBit(EventType::MasterChanged) & ~eventBit(EventType::QueueChanged) &
                                     ~eventBit(EventType::CommandRejected);

EventQueueSubscriber sSubscriber;
HistoryLog *sLog = nullptr;
StaticTask_t sTcb;
StackType_t sStack[6144]; // bytes on ESP-IDF; as the persist writer: the NVS write path, chunk copies on the heap

MonoMs nowMs() { return static_cast<MonoMs>(esp_timer_get_time() / 1000); }

void recorderTask(void *)
{
    Event e{};
    uint32_t reportedDrops = 0;
    for (;;) {
        // Wake at least every 500 ms so urgent records from other tasks
        // (a firmware update just before its restart) are written soon.
        if (sSubscriber.receive(e, pdMS_TO_TICKS(500))) {
            if (const auto r = historyFromEvent(e)) {
                sLog->append(*r, nowMs(), e.type == EventType::FaultRaised);
            }
        }
        if (sLog->flushDue(nowMs()) && !sLog->flush(nowMs())) {
            ESP_LOGW(kTag, "history chunk not written; retrying later");
        }
        const uint32_t drops = sSubscriber.dropped();
        if (drops != reportedDrops) {
            ESP_LOGW(kTag, "%u events not recorded (queue full)", static_cast<unsigned>(drops - reportedDrops));
            reportedDrops = drops;
        }
    }
}

} // namespace

HistoryResetReason historyResetReason()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return HistoryResetReason::PowerOn;
    case ESP_RST_SW:
        return HistoryResetReason::Software;
    case ESP_RST_PANIC:
        return HistoryResetReason::Panic;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return HistoryResetReason::Watchdog;
    case ESP_RST_BROWNOUT:
        return HistoryResetReason::Brownout;
    case ESP_RST_EXT:
        return HistoryResetReason::External;
    case ESP_RST_DEEPSLEEP:
        return HistoryResetReason::DeepSleep;
    default:
        return HistoryResetReason::Unknown;
    }
}

esp_err_t startHistoryRecorder(EventBus &bus, HistoryLog &log)
{
    sLog = &log;
    esp_err_t err = sSubscriber.begin(bus, kHistoryEvents, 32);
    if (err != ESP_OK) {
        return err;
    }
    // Priority 2 on core 0, beside the event logger; below the persist writer.
    const TaskHandle_t t =
        xTaskCreateStaticPinnedToCore(&recorderTask, "zf_history", sizeof sStack, nullptr, 2, sStack, &sTcb, 0);
    if (t == nullptr) {
        ESP_LOGE(kTag, "history recorder not started");
        return ESP_FAIL;
    }
    return ESP_OK;
}

} // namespace zf::platform
