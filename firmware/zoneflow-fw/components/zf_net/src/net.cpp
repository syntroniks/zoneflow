// SPDX-License-Identifier: Apache-2.0
#include "zf_net/net.hpp"

#include "esp_log.h"

namespace zf::net {

esp_err_t start(const AppServices & /*services*/)
{
    ESP_LOGW("zf.net", "not implemented in phase 1 (stub)");
    return ESP_OK;
}

} // namespace zf::net
