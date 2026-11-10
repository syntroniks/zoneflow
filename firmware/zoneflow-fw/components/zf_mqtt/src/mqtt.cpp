// SPDX-License-Identifier: Apache-2.0
#include "zf_mqtt/mqtt.hpp"

#include "esp_log.h"

namespace zf::mqtt {

esp_err_t start(const AppServices & /*services*/)
{
    ESP_LOGW("zf.mqtt", "not implemented in phase 1 (stub)");
    return ESP_OK;
}

} // namespace zf::mqtt
