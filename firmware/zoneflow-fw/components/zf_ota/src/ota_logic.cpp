// SPDX-License-Identifier: Apache-2.0
#include "zf_ota/ota_logic.hpp"

#include <algorithm>
#include <cstring>

namespace zf::ota {

const char *toString(OtaPhase p)
{
    switch (p) {
    case OtaPhase::Idle:
        return "idle";
    case OtaPhase::Preparing:
        return "preparing";
    case OtaPhase::Receiving:
        return "receiving";
    case OtaPhase::Verifying:
        return "verifying";
    case OtaPhase::Rebooting:
        return "rebooting";
    case OtaPhase::Failed:
        return "failed";
    }
    return "?";
}

const char *toString(OtaError e)
{
    switch (e) {
    case OtaError::None:
        return "none";
    case OtaError::Busy:
        return "busy";
    case OtaError::ZonesActive:
        return "zones_active";
    case OtaError::NotSafe:
        return "outputs_not_off";
    case OtaError::BadImage:
        return "bad_image";
    case OtaError::WrongChip:
        return "wrong_chip";
    case OtaError::WrongProject:
        return "wrong_project";
    case OtaError::TooLarge:
        return "too_large";
    case OtaError::Truncated:
        return "truncated";
    case OtaError::FlashError:
        return "flash_error";
    case OtaError::VerifyFailed:
        return "verify_failed";
    case OtaError::Download:
        return "download_failed";
    case OtaError::BadUrl:
        return "bad_url";
    case OtaError::Aborted:
        return "aborted";
    }
    return "?";
}

const char *toString(OtaSource s)
{
    switch (s) {
    case OtaSource::None:
        return "none";
    case OtaSource::Upload:
        return "upload";
    case OtaSource::Pull:
        return "pull";
    }
    return "?";
}

const char *toString(SelfTestVerdict v)
{
    switch (v) {
    case SelfTestVerdict::Wait:
        return "wait";
    case SelfTestVerdict::Confirm:
        return "confirm";
    case SelfTestVerdict::Rollback:
        return "rollback";
    }
    return "?";
}

// ---- image header ------------------------------------------------------------------

namespace {

uint32_t le32(const uint8_t *p)
{
    return uint32_t{p[0]} | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}

template <std::size_t N> void copyField(FixedString<N> &dst, const uint8_t *src, std::size_t max)
{
    std::size_t n = 0;
    while (n < max && src[n] != 0) {
        ++n;
    }
    dst.assign(std::string_view(reinterpret_cast<const char *>(src), n));
}

} // namespace

ImageInfo checkImageHeader(const uint8_t *d, std::size_t len, uint16_t expectedChip, std::string_view project)
{
    ImageInfo info;
    if (len < kImageHeaderBytes) {
        info.error = OtaError::Truncated;
        return info;
    }
    constexpr std::size_t kDesc = 24 + 8;
    if (d[0] != kImageMagic || le32(d + kDesc) != kAppDescMagic) {
        info.error = OtaError::BadImage;
        return info;
    }
    info.chipId = static_cast<uint16_t>(d[12] | (d[13] << 8));
    copyField(info.version, d + kDesc + 16, 32);
    copyField(info.project, d + kDesc + 48, 32);
    copyField(info.idfVersion, d + kDesc + 112, 32);
    if (info.chipId != expectedChip) {
        info.error = OtaError::WrongChip;
    } else if (info.project.view() != project) {
        info.error = OtaError::WrongProject;
    }
    return info;
}

bool looksLikeLittlefs(const uint8_t *data, std::size_t len)
{
    constexpr char kMagic[] = "littlefs";
    return len >= kLittlefsMagicOffset + 8 && std::memcmp(data + kLittlefsMagicOffset, kMagic, 8) == 0;
}

// ---- safety gate -----------------------------------------------------------------------

bool wateringActive(const SystemState &s)
{
    if (s.masterOn || s.queue.state == QueueState::Running) {
        return true;
    }
    for (std::size_t i = 0; i < s.zoneCount && i < s.zones.size(); ++i) {
        if (s.zones[i].state != ValveState::Off) {
            return true;
        }
    }
    return false;
}

bool outputsSafe(const SystemState &s)
{
    if (!s.maintenance || s.masterOn) {
        return false;
    }
    return std::all_of(s.zones.begin(), s.zones.end(), [](const ZoneStatus &z) { return z.state == ValveState::Off; });
}

OtaError mayStart(const SystemState &s, bool force, OtaPhase current)
{
    if (current != OtaPhase::Idle && current != OtaPhase::Failed) {
        return OtaError::Busy;
    }
    if (!force && wateringActive(s)) {
        return OtaError::ZonesActive;
    }
    return OtaError::None;
}

// ---- session ------------------------------------------------------------------------------

OtaError OtaSession::fail(OtaError e)
{
    abort();
    return e;
}

OtaError OtaSession::begin(std::size_t total)
{
    headLen_ = 0;
    received_ = 0;
    headerChecked_ = false;
    image_ = ImageInfo{};
    if (total > writer_.slotSize()) {
        return OtaError::TooLarge;
    }
    if (!writer_.begin()) {
        return OtaError::FlashError;
    }
    open_ = true;
    return OtaError::None;
}

OtaError OtaSession::write(const uint8_t *data, std::size_t len)
{
    if (!open_) {
        return OtaError::Aborted;
    }
    if (received_ + len > writer_.slotSize()) {
        return fail(OtaError::TooLarge);
    }
    received_ += len;
    if (!headerChecked_) {
        const std::size_t take = std::min(len, kImageHeaderBytes - headLen_);
        std::memcpy(head_ + headLen_, data, take);
        headLen_ += take;
        data += take;
        len -= take;
        if (headLen_ < kImageHeaderBytes) {
            return OtaError::None;
        }
        image_ = checkImageHeader(head_, headLen_, chip_, project_);
        if (image_.error != OtaError::None) {
            return fail(image_.error);
        }
        headerChecked_ = true;
        if (!writer_.write(head_, headLen_)) {
            return fail(OtaError::FlashError);
        }
    }
    if (len > 0 && !writer_.write(data, len)) {
        return fail(OtaError::FlashError);
    }
    return OtaError::None;
}

OtaError OtaSession::finish()
{
    if (!open_) {
        return OtaError::Aborted;
    }
    if (!headerChecked_) {
        return fail(OtaError::Truncated);
    }
    open_ = false;
    if (!writer_.finishAndSetBoot()) {
        return OtaError::VerifyFailed;
    }
    return OtaError::None;
}

void OtaSession::abort()
{
    if (open_) {
        writer_.abort();
        open_ = false;
    }
}

// ---- self-test -------------------------------------------------------------------------------

SelfTestVerdict evaluateSelfTest(const SelfTestInput &in, const SelfTestLimits &limits)
{
    if (in.configInvalid) {
        return SelfTestVerdict::Rollback;
    }
    if (in.snapshotsSeen >= limits.minSnapshots && in.webStarted) {
        return SelfTestVerdict::Confirm;
    }
    return in.elapsedMs >= limits.deadlineMs ? SelfTestVerdict::Rollback : SelfTestVerdict::Wait;
}

} // namespace zf::ota
