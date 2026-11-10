// SPDX-License-Identifier: Apache-2.0
// Fault paths that end with the outputs off: restart (including the restart
// after an OTA update), brownout and panic. Hardware still has the final say:
// the board's pull-downs and the OE gate turn outputs off once the MCU stops.
#include "esp_attr.h"
#include "esp_brownout.h"
#include "esp_log.h"
#include "esp_private/panic_internal.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

Controller *sController = nullptr;
OutputEnableGuard *sGuard = nullptr;

// esp_restart() calls this before resetting the chip, on the calling task.
void onShutdown()
{
    if (sGuard != nullptr) {
        sGuard->trip();
    }
    zf_platform_force_outputs_off();
    if (sController != nullptr) {
        sController->emergencyOff();
    }
}

#if CONFIG_ESP_BROWNOUT_USE_INTR
// Brownout interrupt, just before the chip resets. IRAM only.
IRAM_ATTR void onBrownout() { zf_platform_force_outputs_off(); }
#endif

} // namespace

esp_err_t installSafetyHooks(Controller &controller, OutputEnableGuard &guard)
{
    sController = &controller;
    sGuard = &guard;
    esp_err_t err = esp_register_shutdown_handler(&onShutdown);
#if CONFIG_ESP_BROWNOUT_USE_INTR
    if (err == ESP_OK) {
        err = esp_brownout_register_callback(&onBrownout);
    }
#endif
    return err;
}

} // namespace zf::platform

// Linked with -Wl,--wrap=esp_panic_handler (see CMakeLists.txt): runs first on
// every panic, abort and watchdog panic, before the core dump is written.
extern "C" void __real_esp_panic_handler(panic_info_t *info);
extern "C" IRAM_ATTR void __wrap_esp_panic_handler(panic_info_t *info)
{
    zf_platform_force_outputs_off();
    __real_esp_panic_handler(info);
}
