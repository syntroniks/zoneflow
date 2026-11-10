// SPDX-License-Identifier: Apache-2.0
// zf_ota device glue: esp_ota_* for uploads, esp_https_ota for pulls, and the
// post-boot self-test. Decisions come from ota_logic.hpp.
#include "zf_ota/ota.hpp"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <ctime>
#include <memory>
#include <mutex>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "zf/history/history.hpp"

namespace zf::ota {
namespace {

constexpr const char *kTag = "zf.ota";
constexpr uint32_t kSafeWaitMs = 5000;

class EspOtaWriter final : public IOtaWriter {
public:
    std::size_t slotSize() const override
    {
        const esp_partition_t *p = esp_ota_get_next_update_partition(nullptr);
        return p == nullptr ? 0 : p->size;
    }
    bool begin() override
    {
        part_ = esp_ota_get_next_update_partition(nullptr);
        return part_ != nullptr && esp_ota_begin(part_, OTA_WITH_SEQUENTIAL_WRITES, &handle_) == ESP_OK;
    }
    bool write(const uint8_t *data, std::size_t len) override { return esp_ota_write(handle_, data, len) == ESP_OK; }
    bool finishAndSetBoot() override
    {
        // esp_ota_end checks the image (segments, checksum, appended SHA-256
        // and, with secure boot, the signature).
        const esp_err_t end = esp_ota_end(handle_);
        handle_ = 0;
        if (end != ESP_OK) {
            ESP_LOGE(kTag, "image verification failed: %s", esp_err_to_name(end));
            return false;
        }
        return esp_ota_set_boot_partition(part_) == ESP_OK;
    }
    void abort() override
    {
        if (handle_ != 0) {
            esp_ota_abort(handle_);
            handle_ = 0;
        }
    }

private:
    const esp_partition_t *part_ = nullptr;
    esp_ota_handle_t handle_ = 0;
};

struct Module {
    const AppServices *services = nullptr;
    OtaStatusStore store;
    std::mutex mutex; // one update at a time
    EspOtaWriter writer;
    std::unique_ptr<OtaSession> session;
    std::atomic<bool> webStarted{false};
    std::string pullUrl;
    bool pullForce = false;
};

Module m;

// Firmware updates go into the persistent history (urgent: a restart follows).
void noteHistory(HistoryOtaStage stage, OtaSource source, OtaError error)
{
    HistoryLog *log = historyLog();
    if (log == nullptr) {
        return;
    }
    HistoryRecord r;
    r.utc = static_cast<UtcSeconds>(std::time(nullptr)); // as the controller stamps its events
    r.kind = HistoryKind::Ota;
    r.a = static_cast<uint8_t>(stage);
    r.b = source == OtaSource::Upload ? 1 : source == OtaSource::Pull ? 2 : 0;
    r.c = static_cast<uint16_t>(error);
    log->append(r, static_cast<MonoMs>(esp_timer_get_time() / 1000), true);
}

void setStatus(OtaPhase phase, OtaError error = OtaError::None)
{
    OtaStatus s = m.store.get();
    const OtaPhase before = s.phase;
    s.phase = phase;
    s.error = error;
    m.store.set(s);
    if (phase != before && phase == OtaPhase::Failed) {
        noteHistory(HistoryOtaStage::Failed, s.source, error);
    } else if (phase != before && phase == OtaPhase::Rebooting) {
        noteHistory(HistoryOtaStage::Installed, s.source, OtaError::None);
    }
}

void restartSoon()
{
    static esp_timer_handle_t timer = nullptr;
    if (timer == nullptr) {
        esp_timer_create_args_t args{};
        args.callback = [](void *) { esp_restart(); };
        args.name = "zf_ota_restart";
        esp_timer_create(&args, &timer);
    }
    esp_timer_start_once(timer, 1000 * 1000);
}

// Safety gate, maintenance on, wait for outputs off. Caller holds m.mutex.
OtaError prepare(bool force, OtaSource source, std::size_t total)
{
    const OtaError gate = mayStart(m.services->state.snapshot(), force, m.store.get().phase);
    if (gate != OtaError::None) {
        return gate;
    }
    OtaStatus s;
    s.phase = OtaPhase::Preparing;
    s.source = source;
    s.forced = force;
    s.totalBytes = static_cast<uint32_t>(total);
    m.store.set(s);
    noteHistory(HistoryOtaStage::Started, source, OtaError::None);
    // Maintenance closes every valve, clears the queue and blocks starts.
    m.services->commands.post(Command::simple(CommandType::StopAll));
    if (!m.services->commands.post(Command::maintenance(true))) {
        setStatus(OtaPhase::Failed, OtaError::Busy);
        return OtaError::Busy;
    }
    for (uint32_t waited = 0; !outputsSafe(m.services->state.snapshot()); waited += 50) {
        if (waited >= kSafeWaitMs) {
            m.services->commands.post(Command::maintenance(false));
            setStatus(OtaPhase::Failed, OtaError::NotSafe);
            return OtaError::NotSafe;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGI(kTag, "outputs off, maintenance on; writing the passive slot");
    return OtaError::None;
}

void fail(OtaError e)
{
    ESP_LOGE(kTag, "update failed: %s", toString(e));
    m.services->commands.post(Command::maintenance(false));
    setStatus(OtaPhase::Failed, e);
}

// ---- pull ---------------------------------------------------------------------------

void pullTask(void *)
{
    std::lock_guard<std::mutex> lock(m.mutex);
    esp_http_client_config_t http{};
    http.url = m.pullUrl.c_str();
    http.crt_bundle_attach = esp_crt_bundle_attach;
    http.timeout_ms = 15000;
    http.keep_alive_enable = true;
    http.max_redirection_count = 5;
    http.buffer_size = 4096;
    http.buffer_size_tx = 2048;
    esp_https_ota_config_t cfg{};
    cfg.http_config = &http;
    esp_https_ota_handle_t handle = nullptr;
    OtaError result = OtaError::None;
    if (esp_https_ota_begin(&cfg, &handle) != ESP_OK) {
        result = OtaError::Download;
    } else {
        esp_app_desc_t desc{};
        if (esp_https_ota_get_img_desc(handle, &desc) != ESP_OK) {
            result = OtaError::BadImage;
        } else if (std::string_view(desc.project_name) != kProjectName) {
            result = OtaError::WrongProject;
        } else {
            OtaStatus s = m.store.get();
            s.phase = OtaPhase::Receiving;
            s.newVersion.assign(desc.version);
            const int size = esp_https_ota_get_image_size(handle);
            s.totalBytes = size > 0 ? static_cast<uint32_t>(size) : 0;
            m.store.set(s);
            esp_err_t err = ESP_ERR_HTTPS_OTA_IN_PROGRESS;
            while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
                err = esp_https_ota_perform(handle);
                s.receivedBytes = static_cast<uint32_t>(std::max(0, esp_https_ota_get_image_len_read(handle)));
                m.store.set(s);
            }
            if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
                result = OtaError::Download;
            }
        }
        if (result != OtaError::None) {
            esp_https_ota_abort(handle);
        } else {
            setStatus(OtaPhase::Verifying);
            // Verifies the image and sets the boot partition.
            if (esp_https_ota_finish(handle) != ESP_OK) {
                result = OtaError::VerifyFailed;
            }
        }
    }
    if (result == OtaError::None) {
        setStatus(OtaPhase::Rebooting);
        ESP_LOGI(kTag, "update written; restarting");
        restartSoon();
    } else {
        fail(result);
    }
    vTaskDelete(nullptr);
}

// ---- self-test -----------------------------------------------------------------------

void selfTestTask(void *)
{
    const uint32_t startSeq = m.services->state.sequence();
    const int64_t startUs = esp_timer_get_time();
    for (;;) {
        SelfTestInput in;
        in.elapsedMs = static_cast<MonoMs>((esp_timer_get_time() - startUs) / 1000);
        in.snapshotsSeen = m.services->state.sequence() - startSeq;
        in.configInvalid =
            (m.services->state.snapshot().activeFaults & (1u << static_cast<unsigned>(FaultCode::ConfigInvalid))) != 0;
        in.webStarted = m.webStarted.load();
        const SelfTestVerdict v = evaluateSelfTest(in);
        if (v == SelfTestVerdict::Confirm) {
            ESP_LOGI(kTag, "self-test passed; new image confirmed, rollback cancelled");
            esp_ota_mark_app_valid_cancel_rollback();
            break;
        }
        if (v == SelfTestVerdict::Rollback) {
            ESP_LOGE(kTag, "self-test failed (snapshots %" PRIu32 ", web %d, config invalid %d); rolling back",
                     in.snapshotsSeen, in.webStarted, in.configInvalid);
            esp_ota_mark_app_invalid_rollback_and_reboot();
            break; // not reached when another image exists
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    vTaskDelete(nullptr);
}

const char *imageStateName(esp_ota_img_states_t s)
{
    switch (s) {
    case ESP_OTA_IMG_NEW:
        return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
        return "pending_verify";
    case ESP_OTA_IMG_VALID:
        return "valid";
    case ESP_OTA_IMG_INVALID:
        return "invalid";
    case ESP_OTA_IMG_ABORTED:
        return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
        break;
    }
    return "undefined";
}

} // namespace

esp_err_t start(const AppServices &services)
{
    m.services = &services;
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *running = esp_ota_get_running_partition();
    ESP_LOGI(kTag, "running %s from %s", app->version, running != nullptr ? running->label : "?");
    return ESP_OK;
}

esp_err_t markRunningAppValid()
{
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == nullptr || esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY) {
        return ESP_OK;
    }
    if (m.services == nullptr) {
        // start() was not called: confirm at once rather than roll back a
        // build that simply has no self-test wiring.
        return esp_ota_mark_app_valid_cancel_rollback();
    }
    ESP_LOGI(kTag, "new image: running the self-test before confirming it");
    return xTaskCreate(&selfTestTask, "zf_ota_test", 3072, nullptr, 2, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void notifyWebStarted() { m.webStarted.store(true); }

OtaStatus status() { return m.store.get(); }

Slots slots()
{
    Slots s;
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    if (running != nullptr) {
        s.running = running->label;
        esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
        esp_ota_get_state_partition(running, &st);
        s.runningState = imageStateName(st);
    }
    if (next != nullptr) {
        s.next = next->label;
        esp_app_desc_t desc{};
        if (esp_ota_get_partition_description(next, &desc) == ESP_OK) {
            s.otherVersion = desc.version;
        }
    }
    return s;
}

OtaError uploadBegin(std::size_t totalBytes, bool force)
{
    if (!m.mutex.try_lock()) {
        return OtaError::Busy;
    }
    const OtaError prep = prepare(force, OtaSource::Upload, totalBytes);
    if (prep != OtaError::None) {
        m.mutex.unlock();
        return prep;
    }
    m.session = std::make_unique<OtaSession>(m.writer, kChipIdEsp32s3, kProjectName);
    const OtaError e = m.session->begin(totalBytes);
    if (e != OtaError::None) {
        fail(e);
        m.session.reset();
        m.mutex.unlock();
        return e;
    }
    setStatus(OtaPhase::Receiving);
    return OtaError::None; // mutex stays held until finish or abort
}

OtaError uploadWrite(const uint8_t *data, std::size_t len)
{
    if (!m.session) {
        return OtaError::Aborted;
    }
    const OtaError e = m.session->write(data, len);
    OtaStatus s = m.store.get();
    s.receivedBytes = static_cast<uint32_t>(m.session->received());
    if (m.session->image().version.size() > 0) {
        s.newVersion = m.session->image().version;
    }
    m.store.set(s);
    if (e != OtaError::None) {
        fail(e);
        m.session.reset();
        m.mutex.unlock();
    }
    return e;
}

OtaError uploadFinish()
{
    if (!m.session) {
        return OtaError::Aborted;
    }
    setStatus(OtaPhase::Verifying);
    const OtaError e = m.session->finish();
    m.session.reset();
    if (e != OtaError::None) {
        fail(e);
    } else {
        setStatus(OtaPhase::Rebooting);
        ESP_LOGI(kTag, "upload verified; restarting into the new image");
        restartSoon();
    }
    m.mutex.unlock();
    return e;
}

void uploadAbort()
{
    if (!m.session) {
        return;
    }
    m.session->abort();
    m.session.reset();
    fail(OtaError::Aborted);
    m.mutex.unlock();
}

// ---- web UI image ----------------------------------------------------------------------

namespace {

struct Assets {
    const esp_partition_t *part = nullptr;
    std::size_t written = 0;
    std::size_t erased = 0;
    bool active = false;
};
Assets gAssets;

void assetsEnd(OtaError e)
{
    gAssets.active = false;
    m.services->commands.post(Command::maintenance(false));
    if (e == OtaError::None) {
        setStatus(OtaPhase::Idle);
    } else {
        setStatus(OtaPhase::Failed, e);
    }
    m.mutex.unlock();
}

} // namespace

OtaError assetsBegin(std::size_t totalBytes, bool force)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS, "www");
    if (part == nullptr) {
        return OtaError::FlashError;
    }
    if (totalBytes > part->size) {
        return OtaError::TooLarge;
    }
    if (!m.mutex.try_lock()) {
        return OtaError::Busy;
    }
    const OtaError prep = prepare(force, OtaSource::Upload, totalBytes);
    if (prep != OtaError::None) {
        m.mutex.unlock();
        return prep;
    }
    gAssets = Assets{part, 0, 0, true};
    setStatus(OtaPhase::Receiving);
    return OtaError::None;
}

OtaError assetsWrite(const uint8_t *data, std::size_t len)
{
    if (!gAssets.active) {
        return OtaError::Aborted;
    }
    if (gAssets.written == 0 && !looksLikeLittlefs(data, len)) {
        assetsEnd(OtaError::BadImage);
        return OtaError::BadImage;
    }
    if (gAssets.written + len > gAssets.part->size) {
        assetsEnd(OtaError::TooLarge);
        return OtaError::TooLarge;
    }
    constexpr std::size_t kSector = 4096;
    const std::size_t end = gAssets.written + len;
    while (gAssets.erased < end) {
        if (esp_partition_erase_range(gAssets.part, gAssets.erased, kSector) != ESP_OK) {
            assetsEnd(OtaError::FlashError);
            return OtaError::FlashError;
        }
        gAssets.erased += kSector;
    }
    if (esp_partition_write(gAssets.part, gAssets.written, data, len) != ESP_OK) {
        assetsEnd(OtaError::FlashError);
        return OtaError::FlashError;
    }
    gAssets.written = end;
    OtaStatus s = m.store.get();
    s.receivedBytes = static_cast<uint32_t>(gAssets.written);
    m.store.set(s);
    return OtaError::None;
}

OtaError assetsFinish()
{
    if (!gAssets.active) {
        return OtaError::Aborted;
    }
    const OtaError e = gAssets.written == 0 ? OtaError::Truncated : OtaError::None;
    assetsEnd(e);
    return e;
}

void assetsAbort()
{
    if (gAssets.active) {
        assetsEnd(OtaError::Aborted);
    }
}

OtaError startPull(std::string_view url, bool force)
{
    if (url.rfind("https://", 0) != 0) {
        return OtaError::BadUrl;
    }
    if (!m.mutex.try_lock()) {
        return OtaError::Busy;
    }
    const OtaError prep = prepare(force, OtaSource::Pull, 0);
    if (prep != OtaError::None) {
        m.mutex.unlock();
        return prep;
    }
    m.pullUrl = std::string(url);
    m.pullForce = force;
    m.mutex.unlock(); // the pull task takes it again
    // TLS handshake and HTTP client: 8 KB stack (ESP-IDF https_ota example uses 8 KB).
    if (xTaskCreate(&pullTask, "zf_ota_pull", 8192, nullptr, 4, nullptr) != pdPASS) {
        fail(OtaError::Download);
        return OtaError::Download;
    }
    return OtaError::None;
}

} // namespace zf::ota
