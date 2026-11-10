// SPDX-License-Identifier: Apache-2.0
#include "freertos/FreeRTOS.h"

#include "esp_log.h"
#include "freertos/task.h"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

constexpr const char *kTag = "zf.event";

void logEvent(const Event &e)
{
    switch (e.type) {
    case EventType::ZoneChanged:
        ESP_LOGI(kTag, "zone %u %s (%s)", e.zone.zone + 1u, toString(e.zone.state), toString(e.zone.reason));
        break;
    case EventType::MasterChanged:
        ESP_LOGI(kTag, "master %s", e.master.on ? "on" : "off");
        break;
    case EventType::ProgramStarted:
        ESP_LOGI(kTag, "program %u started (%s%s)", e.program.programId, toString(e.program.source),
                 e.program.late ? ", late" : "");
        break;
    case EventType::ProgramSkipped:
        ESP_LOGW(kTag, "program %u skipped: %s", e.program.programId, toString(e.program.skipReason));
        break;
    case EventType::ProgramMissed:
        ESP_LOGW(kTag, "program %u missed start at %lld", e.program.programId,
                 static_cast<long long>(e.program.scheduledAt));
        break;
    case EventType::SensorChanged:
        ESP_LOGI(kTag, "sensor %u -> %s", static_cast<unsigned>(e.sensor.sensor), e.sensor.active ? "active" : "idle");
        break;
    case EventType::FaultRaised:
        ESP_LOGE(kTag, "fault %s (zone %d)", toString(e.fault.code), e.fault.zone == kNoZone ? 0 : e.fault.zone + 1);
        break;
    case EventType::FaultCleared:
        ESP_LOGI(kTag, "fault cleared: %s", toString(e.fault.code));
        break;
    case EventType::RainDelayChanged:
        ESP_LOGI(kTag, "rain delay until %lld", static_cast<long long>(e.rainDelay.until));
        break;
    case EventType::TimeStatusChanged:
        ESP_LOGI(kTag, "time status %s", toString(e.time.status));
        break;
    case EventType::MaintenanceChanged:
        ESP_LOGI(kTag, "maintenance %s", e.flag.on ? "on" : "off");
        break;
    case EventType::CommandRejected:
        ESP_LOGW(kTag, "command %s rejected: %s", toString(e.rejected.command), toString(e.rejected.error));
        break;
    case EventType::RunDropped:
        ESP_LOGW(kTag, "zone %u run dropped: %s", e.dropped.zone + 1u, toString(e.dropped.reason));
        break;
    case EventType::PauseExpired:
        ESP_LOGW(kTag, "queue paused too long: stopped");
        break;
    case EventType::QueueChanged:
    case EventType::ConfigApplied:
    case EventType::Count:
        ESP_LOGD(kTag, "%s", toString(e.type));
        break;
    }
}

EventQueueSubscriber sLogSubscriber;

void loggerTask(void *)
{
    Event e{};
    for (;;) {
        if (sLogSubscriber.receive(e, portMAX_DELAY)) {
            logEvent(e);
        }
    }
}

} // namespace

void EventQueueSubscriber::onEvent(const Event &e, void *ctx)
{
    auto *self = static_cast<EventQueueSubscriber *>(ctx);
    if (xQueueSend(self->queue_, &e, 0) != pdTRUE) {
        self->dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

esp_err_t EventQueueSubscriber::begin(EventBus &bus, EventMask mask, UBaseType_t depth)
{
    queue_ = xQueueCreate(depth, sizeof(Event));
    if (queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    return bus.subscribe(&EventQueueSubscriber::onEvent, this, mask) != EventBus::kInvalidHandle ? ESP_OK
                                                                                                 : ESP_ERR_NO_MEM;
}

bool EventQueueSubscriber::receive(Event &out, TickType_t wait)
{
    return queue_ != nullptr && xQueueReceive(queue_, &out, wait) == pdTRUE;
}

esp_err_t startEventLogger(EventBus &bus)
{
    esp_err_t err = sLogSubscriber.begin(bus, kAllEvents, 32);
    if (err != ESP_OK) {
        return err;
    }
    return xTaskCreatePinnedToCore(&loggerTask, "zf_evlog", 4096, nullptr, 2, nullptr, 0) == pdPASS ? ESP_OK : ESP_FAIL;
}

} // namespace zf::platform
