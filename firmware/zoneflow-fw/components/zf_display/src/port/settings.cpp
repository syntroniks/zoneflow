// SPDX-License-Identifier: Apache-2.0
// The display's own settings in NVS (namespace "zf_display"), and LVGL's
// heap on PSRAM.
#include <atomic>
#include <cstring>

#include "esp_heap_caps.h"
#include "nvs.h"
#include "port.hpp"

namespace zf::display::port {
namespace {

constexpr const char *kNamespace = "zf_display";
constexpr const char *kKey = "settings";

// Stored layout. Fields are only ever added at the end; `version` says how
// many of them a stored blob has.
struct Stored {
    uint32_t version;
    uint8_t brightnessPercent;
    uint8_t wizardDone;
    uint8_t lastManualZone;
    uint8_t reserved;
    uint16_t dimAfterSec;
    uint16_t offAfterSec;
    uint16_t lastManualMinutes;
    uint16_t reserved2;
};

std::atomic<uint32_t> sHeapUsed{0};
std::atomic<uint32_t> sHeapPeak{0};

void account(void *p, bool add)
{
    if (p == nullptr) {
        return;
    }
    const auto size = static_cast<uint32_t>(heap_caps_get_allocated_size(p));
    if (!add) {
        sHeapUsed.fetch_sub(size, std::memory_order_relaxed);
        return;
    }
    const uint32_t used = sHeapUsed.fetch_add(size, std::memory_order_relaxed) + size;
    if (used > sHeapPeak.load(std::memory_order_relaxed)) {
        sHeapPeak.store(used, std::memory_order_relaxed);
    }
}

} // namespace

bool NvsSettingsStore::load(UiSettings &out)
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    Stored s = {};
    std::size_t size = sizeof s;
    const esp_err_t err = nvs_get_blob(handle, kKey, &s, &size);
    nvs_close(handle);
    if (err != ESP_OK || size != sizeof s || s.version != UiSettings::kVersion) {
        return false;
    }
    out.brightnessPercent = s.brightnessPercent;
    out.wizardDone = s.wizardDone != 0;
    out.lastManualZone = s.lastManualZone;
    out.dimAfterSec = s.dimAfterSec;
    out.offAfterSec = s.offAfterSec;
    out.lastManualMinutes = s.lastManualMinutes;
    return true;
}

bool NvsSettingsStore::save(const UiSettings &settings)
{
    Stored s = {};
    s.version = UiSettings::kVersion;
    s.brightnessPercent = settings.brightnessPercent;
    s.wizardDone = settings.wizardDone ? 1 : 0;
    s.lastManualZone = settings.lastManualZone;
    s.dimAfterSec = settings.dimAfterSec;
    s.offAfterSec = settings.offAfterSec;
    s.lastManualMinutes = settings.lastManualMinutes;
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const bool ok = nvs_set_blob(handle, kKey, &s, sizeof s) == ESP_OK && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

bool NvsSettingsStore::erase()
{
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const esp_err_t err = nvs_erase_key(handle, kKey);
    const bool ok = (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) && nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

uint32_t lvglHeapUsed() { return sHeapUsed.load(std::memory_order_relaxed); }
uint32_t lvglHeapPeak() { return sHeapPeak.load(std::memory_order_relaxed); }

} // namespace zf::display::port

// ---- LVGL allocator (CONFIG_LV_USE_CUSTOM_MALLOC) ---------------------------------------
//
// LVGL's objects, styles and text go to PSRAM, so they do not compete with
// WiFi and BLE for internal RAM. If PSRAM has no room the same calls fall
// back to internal RAM. (As configured in sdkconfig.defaults the firmware
// does not boot on a module without PSRAM at all.)

extern "C" {

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}

lv_mem_pool_t lv_mem_add_pool(void * /*mem*/, size_t /*bytes*/) { return nullptr; }
void lv_mem_remove_pool(lv_mem_pool_t /*pool*/) {}

void *lv_malloc_core(size_t size)
{
    void *p =
        heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    zf::display::port::account(p, true);
    return p;
}

void *lv_realloc_core(void *p, size_t newSize)
{
    zf::display::port::account(p, false);
    void *q = heap_caps_realloc_prefer(p, newSize, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // A failed realloc leaves the old block in place.
    zf::display::port::account(q != nullptr ? q : p, true);
    return q;
}

void lv_free_core(void *p)
{
    zf::display::port::account(p, false);
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    if (mon != nullptr) {
        std::memset(mon, 0, sizeof *mon);
        mon->max_used = zf::display::port::lvglHeapPeak();
    }
}

lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }

} // extern "C"
