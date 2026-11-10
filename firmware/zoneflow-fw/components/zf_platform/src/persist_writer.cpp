// SPDX-License-Identifier: Apache-2.0
// Low-priority task that stores runtime state (zf/app/persist.hpp). It builds
// the JSON and writes NVS, so the control task never allocates for
// persistence and never waits on flash or on the NVS lock.
#include "freertos/FreeRTOS.h"

#include "esp_log.h"
#include "freertos/task.h"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

constexpr const char *kTag = "zf.persist";
// The controller posts at most every 2 s; polling at 4 Hz adds little delay.
constexpr TickType_t kPollTicks = pdMS_TO_TICKS(250);

StaticTask_t sPersistTcb;
StackType_t sPersistStack[6144]; // cJSON print of a small record, plus the NVS write path
PersistMailbox *sBox = nullptr;
IBlobStorage *sStorage = nullptr;

void persistTask(void *)
{
    for (;;) {
        persistWriteOnce(*sBox, *sStorage);
        vTaskDelay(kPollTicks);
    }
}

} // namespace

esp_err_t startPersistWriter(PersistMailbox &box, IBlobStorage &storage)
{
    sBox = &box;
    sStorage = &storage;
    // Below every application task that matters; on core 0, away from the
    // control task on core 1.
    const TaskHandle_t t = xTaskCreateStaticPinnedToCore(&persistTask, "zf_persist", sizeof sPersistStack, nullptr, 3,
                                                         sPersistStack, &sPersistTcb, 0);
    if (t == nullptr) {
        ESP_LOGE(kTag, "persist writer not started");
        return ESP_FAIL;
    }
    return ESP_OK;
}

} // namespace zf::platform
