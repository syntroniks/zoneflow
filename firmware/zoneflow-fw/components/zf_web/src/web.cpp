// SPDX-License-Identifier: Apache-2.0
#include "zf_web/web.hpp"

#include "esp_log.h"

namespace zf::web {

esp_err_t start(const AppServices & /*services*/)
{
    ESP_LOGW("zf.web", "not implemented in phase 1 (stub)");
    return ESP_OK;
}

} // namespace zf::web
