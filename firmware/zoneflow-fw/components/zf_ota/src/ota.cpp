// SPDX-License-Identifier: Apache-2.0
#include "zf_ota/ota.hpp"

#include "esp_log.h"
#include "esp_ota_ops.h"

namespace zf::ota {

esp_err_t start(const AppServices & /*services*/)
{
    ESP_LOGW("zf.ota", "not implemented in phase 1 (stub)");
    return ESP_OK;
}

esp_err_t markRunningAppValid()
{
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI("zf.ota", "new image confirmed; rollback cancelled");
        return esp_ota_mark_app_valid_cancel_rollback();
    }
    return ESP_OK;
}

} // namespace zf::ota
