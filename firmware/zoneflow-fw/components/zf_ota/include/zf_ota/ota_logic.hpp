// SPDX-License-Identifier: Apache-2.0
// Pure logic of the OTA module: image header check, the update session state
// machine with its safety gate, and the post-boot self-test that decides
// between confirming the image and rolling back. Built by test/host.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

#include "zf/events/state.hpp"
#include "zf/fixed_string.hpp"
#include "zf/types.hpp"

namespace zf::ota {

enum class OtaPhase : uint8_t {
    Idle,
    Preparing, // maintenance requested, waiting for outputs off
    Receiving, // writing the image to the passive slot
    Verifying, // esp_ota_end: image checksum and hash
    Rebooting, // boot slot switched; restart pending
    Failed,    // last attempt failed; see OtaStatus::error
};

enum class OtaError : uint8_t {
    None,
    Busy,         // another update is running
    ZonesActive,  // a zone is running and force was not given
    NotSafe,      // outputs did not report off after maintenance was requested
    BadImage,     // not an ESP app image
    WrongChip,    // built for another chip
    WrongProject, // not a ZoneFlow firmware image
    TooLarge,     // larger than the OTA slot
    Truncated,    // ended before the header was complete
    FlashError,   // partition write failed
    VerifyFailed, // image check at the end failed
    Download,     // HTTP(S) download failed (pull)
    BadUrl,       // pull URL is not https://
    Aborted,      // client went away
};

const char *toString(OtaPhase p);
const char *toString(OtaError e);

enum class OtaSource : uint8_t { None, Upload, Pull };
const char *toString(OtaSource s);

struct OtaStatus {
    OtaPhase phase = OtaPhase::Idle;
    OtaError error = OtaError::None;
    OtaSource source = OtaSource::None;
    bool forced = false;
    uint32_t receivedBytes = 0;
    uint32_t totalBytes = 0; // 0 when unknown
    FixedString<31> newVersion;
};

// Thread-safe holder; one writer at a time (the OTA path), many readers.
class OtaStatusStore {
public:
    void set(const OtaStatus &s)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status_ = s;
    }
    OtaStatus get() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return status_;
    }

private:
    mutable std::mutex mutex_;
    OtaStatus status_{};
};

// ---- image header -----------------------------------------------------------------

// esp_image_header_t (24 bytes) + first segment header (8) + esp_app_desc_t
// (256). Layouts from ESP-IDF v5.5.6 esp_app_format.h and esp_app_desc.h.
inline constexpr std::size_t kImageHeaderBytes = 24 + 8 + 256;
inline constexpr uint8_t kImageMagic = 0xE9;
inline constexpr uint32_t kAppDescMagic = 0xABCD5432;
inline constexpr uint16_t kChipIdEsp32s3 = 0x0009;
inline constexpr std::string_view kProjectName = "zoneflow-fw";

struct ImageInfo {
    OtaError error = OtaError::None;
    uint16_t chipId = 0;
    FixedString<31> version;
    FixedString<31> project;
    FixedString<31> idfVersion;
};

ImageInfo checkImageHeader(const uint8_t *data, std::size_t len, uint16_t expectedChip, std::string_view project);

// A LittleFS image starts with a superblock whose name field "littlefs"
// sits at byte 8 of block 0 (LittleFS SPEC.md, superblock entry).
inline constexpr std::size_t kLittlefsMagicOffset = 8;
bool looksLikeLittlefs(const uint8_t *data, std::size_t len);

// ---- safety gate ---------------------------------------------------------------------

// A zone open or waiting, the master on, or a queue that is running.
bool wateringActive(const SystemState &s);
// Maintenance is on and every output is off: safe to write and swap.
bool outputsSafe(const SystemState &s);
// May an update start now?
OtaError mayStart(const SystemState &s, bool force, OtaPhase current);

// ---- session ---------------------------------------------------------------------------

// Flash side of an update (esp_ota_* on the device, a fake in tests).
class IOtaWriter {
public:
    virtual ~IOtaWriter() = default;
    virtual std::size_t slotSize() const = 0;
    virtual bool begin() = 0;
    virtual bool write(const uint8_t *data, std::size_t len) = 0;
    // Verifies the image and makes it the boot image.
    virtual bool finishAndSetBoot() = 0;
    virtual void abort() = 0;
};

// Streams an image into the writer. Holds back the first kImageHeaderBytes
// until the header has been checked, so nothing is written to flash for an
// image of the wrong chip or project.
class OtaSession {
public:
    OtaSession(IOtaWriter &writer, uint16_t chip, std::string_view project)
        : writer_(writer), chip_(chip), project_(project)
    {
    }

    // `total` = Content-Length, 0 if unknown.
    OtaError begin(std::size_t total);
    OtaError write(const uint8_t *data, std::size_t len);
    OtaError finish();
    void abort();

    std::size_t received() const { return received_; }
    const ImageInfo &image() const { return image_; }

private:
    OtaError fail(OtaError e);

    IOtaWriter &writer_;
    uint16_t chip_;
    std::string_view project_;
    uint8_t head_[kImageHeaderBytes] = {};
    std::size_t headLen_ = 0;
    std::size_t received_ = 0;
    bool open_ = false;
    bool headerChecked_ = false;
    ImageInfo image_{};
};

// ---- post-boot self-test -------------------------------------------------------------------

struct SelfTestInput {
    MonoMs elapsedMs = 0;       // since the self-test started
    uint32_t snapshotsSeen = 0; // StateStore sequence advance since the start
    bool configInvalid = false; // stored config rejected by this image
    bool webStarted = false;
};

struct SelfTestLimits {
    uint32_t minSnapshots = 5; // the control loop publishes at least once a second
    MonoMs deadlineMs = 60000;
};

enum class SelfTestVerdict : uint8_t { Wait, Confirm, Rollback };
const char *toString(SelfTestVerdict v);

SelfTestVerdict evaluateSelfTest(const SelfTestInput &in, const SelfTestLimits &limits = {});

} // namespace zf::ota
