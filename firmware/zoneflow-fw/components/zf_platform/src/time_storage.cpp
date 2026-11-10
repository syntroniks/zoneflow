// SPDX-License-Identifier: Apache-2.0
// Clocks, NVS storage, I2C bus, PCF8563 RTC and SNTP.
#include <sys/time.h>

#include <ctime>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "zf/time/civil.hpp"
#include "zf_board/board.hpp"
#include "zf_platform/platform.hpp"

namespace zf::platform {
namespace {

constexpr const char *kTag = "zf.time";
constexpr const char *kNvsNamespace = "zoneflow";
constexpr int kI2cTimeoutMs = 50;

uint8_t toBcd(unsigned v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }
unsigned fromBcd(uint8_t v) { return (v >> 4) * 10u + (v & 0x0Fu); }

} // namespace

// ---- clocks ---------------------------------------------------------------------

UtcSeconds EspClock::nowUtc() const
{
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec;
}

MonoMs EspClock::monoMs() const { return static_cast<MonoMs>(esp_timer_get_time() / 1000); }

UtcSeconds EspSystemTime::now() const
{
    timeval tv{};
    gettimeofday(&tv, nullptr);
    return tv.tv_sec;
}

bool EspSystemTime::set(UtcSeconds utc)
{
    const timeval tv = {.tv_sec = static_cast<time_t>(utc), .tv_usec = 0};
    return settimeofday(&tv, nullptr) == 0;
}

// ---- NVS --------------------------------------------------------------------------

esp_err_t NvsBlobStorage::init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW("zf.nvs", "NVS partition unusable (%s); erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return err;
    }
    nvs_handle_t h = 0;
    err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        handle_ = h;
        open_ = true;
    }
    return err;
}

bool NvsBlobStorage::read(const char *key, std::string &out)
{
    if (!open_) {
        return false;
    }
    size_t len = 0;
    if (nvs_get_blob(handle_, key, nullptr, &len) != ESP_OK) {
        return false;
    }
    out.resize(len);
    return nvs_get_blob(handle_, key, out.data(), &len) == ESP_OK;
}

bool NvsBlobStorage::write(const char *key, std::string_view data)
{
    return open_ && nvs_set_blob(handle_, key, data.data(), data.size()) == ESP_OK && nvs_commit(handle_) == ESP_OK;
}

bool NvsBlobStorage::erase(const char *key)
{
    return open_ && nvs_erase_key(handle_, key) == ESP_OK && nvs_commit(handle_) == ESP_OK;
}

// ---- I2C ------------------------------------------------------------------------------

esp_err_t I2cBus::init()
{
    i2c_master_bus_config_t cfg = {};
    cfg.i2c_port = I2C_NUM_0;
    cfg.sda_io_num = static_cast<gpio_num_t>(board::kI2cSdaGpio);
    cfg.scl_io_num = static_cast<gpio_num_t>(board::kI2cSclGpio);
    cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    cfg.glitch_ignore_cnt = 7;
    cfg.flags.enable_internal_pullup = true; // external pull-ups expected as well
    return i2c_new_master_bus(&cfg, &bus_);
}

// ---- PCF8563 ------------------------------------------------------------------------------
// Registers (NXP PCF8563 datasheet rev 11.1, Table 4):
//   00h Control_status_1 (bit 5 STOP), 02h VL_seconds (bit 7 VL),
//   03h minutes, 04h hours, 05h days, 06h weekdays, 07h century_months
//   (bit 7 C), 08h years. All BCD.

esp_err_t Pcf8563Rtc::init(I2cBus &bus)
{
    if (bus.handle() == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = i2c_master_probe(bus.handle(), board::kRtcI2cAddress, kI2cTimeoutMs);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "RTC not found at 0x%02x: %s", board::kRtcI2cAddress, esp_err_to_name(err));
        return err;
    }
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = board::kRtcI2cAddress;
    dev.scl_speed_hz = board::kI2cFrequencyHz;
    return i2c_master_bus_add_device(bus.handle(), &dev, &dev_);
}

bool Pcf8563Rtc::read(UtcSeconds &out)
{
    if (dev_ == nullptr) {
        return false;
    }
    const uint8_t reg = 0x02;
    uint8_t r[7] = {};
    if (i2c_master_transmit_receive(dev_, &reg, 1, r, sizeof r, kI2cTimeoutMs) != ESP_OK) {
        return false;
    }
    if ((r[0] & 0x80u) != 0) {
        ESP_LOGW(kTag, "RTC reports voltage-low: time not trusted");
        return false;
    }
    const unsigned sec = fromBcd(r[0] & 0x7Fu);
    const unsigned min = fromBcd(r[1] & 0x7Fu);
    const unsigned hour = fromBcd(r[2] & 0x3Fu);
    const unsigned day = fromBcd(r[3] & 0x3Fu);
    const unsigned month = fromBcd(r[5] & 0x1Fu);
    const int year = 2000 + ((r[5] & 0x80u) != 0 ? 100 : 0) + static_cast<int>(fromBcd(r[6]));
    if (sec > 59 || min > 59 || hour > 23 || month < 1 || month > 12 || day < 1 ||
        day > civil::daysInMonth(year, month)) {
        return false;
    }
    out = civil::daysFromCivil(year, month, day) * civil::kSecondsPerDay + hour * 3600 + min * 60 + sec;
    return true;
}

bool Pcf8563Rtc::write(UtcSeconds utc)
{
    if (dev_ == nullptr || utc < 0) {
        return false;
    }
    const int64_t days = civil::floorDiv(utc, civil::kSecondsPerDay);
    const auto sod = static_cast<unsigned>(utc - days * civil::kSecondsPerDay);
    const civil::Date d = civil::civilFromDays(days);
    if (d.year < 2000 || d.year > 2199) {
        return false;
    }
    const uint8_t start[2] = {0x00, 0x00}; // Control_status_1: clear STOP
    const uint8_t buf[8] = {
        0x02,
        toBcd(sod % 60), // writing seconds clears VL
        toBcd((sod / 60) % 60),
        toBcd(sod / 3600),
        toBcd(d.day),
        static_cast<uint8_t>(civil::weekdayFromDays(days)),
        static_cast<uint8_t>(toBcd(d.month) | (d.year >= 2100 ? 0x80u : 0u)),
        toBcd(static_cast<unsigned>(d.year % 100)),
    };
    return i2c_master_transmit(dev_, start, sizeof start, kI2cTimeoutMs) == ESP_OK &&
           i2c_master_transmit(dev_, buf, sizeof buf, kI2cTimeoutMs) == ESP_OK;
}

// ---- SNTP ---------------------------------------------------------------------------------

namespace {
TimeService *sTime = nullptr;
bool sSntpStarted = false;

void onSntpSync(struct timeval *)
{
    if (sTime != nullptr) {
        sTime->onNetworkSync();
        ESP_LOGI(kTag, "SNTP sync; RTC updated");
    }
}

void onGotIp(void *, esp_event_base_t, int32_t, void *)
{
    if (sSntpStarted) {
        return;
    }
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.sync_cb = &onSntpSync;
    if (esp_netif_sntp_init(&cfg) == ESP_OK) {
        sSntpStarted = true;
    }
}
} // namespace

esp_err_t startTimeSync(TimeService &time)
{
    sTime = &time;
    return esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &onGotIp, nullptr);
}

} // namespace zf::platform
