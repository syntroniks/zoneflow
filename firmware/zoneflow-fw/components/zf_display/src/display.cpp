// SPDX-License-Identifier: Apache-2.0
#include "zf_display/display.hpp"

#include "esp_log.h"

namespace zf::display {

esp_err_t start(const AppServices & /*services*/, i2c_master_bus_handle_t /*i2c*/)
{
    ESP_LOGW("zf.display", "not implemented in phase 1 (stub)");
    return ESP_OK;
}

} // namespace zf::display
