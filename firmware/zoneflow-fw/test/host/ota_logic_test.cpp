// SPDX-License-Identifier: Apache-2.0
// OTA: image header check, safety gate, session streaming and the post-boot
// self-test.
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "zf_ota/ota_logic.hpp"

using namespace zf;
using namespace zf::ota;

namespace {

// A synthetic app image: header, segment header, app descriptor, payload.
std::vector<uint8_t> image(uint16_t chip = kChipIdEsp32s3, const char *project = "zoneflow-fw",
                           std::size_t payload = 5000)
{
    std::vector<uint8_t> d(kImageHeaderBytes + payload, 0x5A);
    std::fill(d.begin(), d.begin() + kImageHeaderBytes, 0);
    d[0] = kImageMagic;
    d[12] = static_cast<uint8_t>(chip);
    d[13] = static_cast<uint8_t>(chip >> 8);
    const std::size_t desc = 32;
    const uint32_t magic = kAppDescMagic;
    for (int i = 0; i < 4; ++i) {
        d[desc + i] = static_cast<uint8_t>(magic >> (8 * i));
    }
    std::strcpy(reinterpret_cast<char *>(&d[desc + 16]), "v0.2.0");
    std::strcpy(reinterpret_cast<char *>(&d[desc + 48]), project);
    std::strcpy(reinterpret_cast<char *>(&d[desc + 112]), "v5.5.6");
    return d;
}

class FakeWriter final : public IOtaWriter {
public:
    std::size_t slot = 64 * 1024;
    std::vector<uint8_t> written;
    bool began = false;
    bool aborted = false;
    bool finished = false;
    bool failWrite = false;
    bool failFinish = false;

    std::size_t slotSize() const override { return slot; }
    bool begin() override
    {
        began = true;
        return true;
    }
    bool write(const uint8_t *d, std::size_t n) override
    {
        if (failWrite) {
            return false;
        }
        written.insert(written.end(), d, d + n);
        return true;
    }
    bool finishAndSetBoot() override
    {
        finished = !failFinish;
        return !failFinish;
    }
    void abort() override { aborted = true; }
};

OtaError stream(OtaSession &s, const std::vector<uint8_t> &img, std::size_t chunk)
{
    for (std::size_t i = 0; i < img.size(); i += chunk) {
        const OtaError e = s.write(img.data() + i, std::min(chunk, img.size() - i));
        if (e != OtaError::None) {
            return e;
        }
    }
    return s.finish();
}

} // namespace

TEST(ImageHeader, ParsesVersionAndChecksChipAndProject)
{
    const auto img = image();
    const ImageInfo ok = checkImageHeader(img.data(), img.size(), kChipIdEsp32s3, kProjectName);
    EXPECT_EQ(ok.error, OtaError::None);
    EXPECT_EQ(ok.version.view(), "v0.2.0");
    EXPECT_EQ(ok.project.view(), "zoneflow-fw");
    EXPECT_EQ(ok.idfVersion.view(), "v5.5.6");

    const auto c3 = image(0x0005);
    EXPECT_EQ(checkImageHeader(c3.data(), c3.size(), kChipIdEsp32s3, kProjectName).error, OtaError::WrongChip);
    const auto other = image(kChipIdEsp32s3, "esphome");
    EXPECT_EQ(checkImageHeader(other.data(), other.size(), kChipIdEsp32s3, kProjectName).error, OtaError::WrongProject);
    auto bad = image();
    bad[0] = 0;
    EXPECT_EQ(checkImageHeader(bad.data(), bad.size(), kChipIdEsp32s3, kProjectName).error, OtaError::BadImage);
    bad = image();
    bad[32] ^= 1;
    EXPECT_EQ(checkImageHeader(bad.data(), bad.size(), kChipIdEsp32s3, kProjectName).error, OtaError::BadImage);
    EXPECT_EQ(checkImageHeader(img.data(), 100, kChipIdEsp32s3, kProjectName).error, OtaError::Truncated);
}

TEST(SafetyGate, RefusesWhileWateringUnlessForced)
{
    SystemState s;
    s.zoneCount = 8;
    EXPECT_EQ(mayStart(s, false, OtaPhase::Idle), OtaError::None);
    s.zones[3].state = ValveState::Waiting;
    EXPECT_EQ(mayStart(s, false, OtaPhase::Idle), OtaError::ZonesActive);
    EXPECT_EQ(mayStart(s, true, OtaPhase::Idle), OtaError::None);
    s.zones[3].state = ValveState::Off;
    s.queue.state = QueueState::Running;
    EXPECT_EQ(mayStart(s, false, OtaPhase::Idle), OtaError::ZonesActive);
    s.queue.state = QueueState::Paused; // nothing open while paused
    EXPECT_EQ(mayStart(s, false, OtaPhase::Failed), OtaError::None);
    EXPECT_EQ(mayStart(s, true, OtaPhase::Receiving), OtaError::Busy);
}

TEST(SafetyGate, OutputsSafeNeedsMaintenanceAndEverythingOff)
{
    SystemState s;
    EXPECT_FALSE(outputsSafe(s));
    s.maintenance = true;
    EXPECT_TRUE(outputsSafe(s));
    s.masterOn = true;
    EXPECT_FALSE(outputsSafe(s));
    s.masterOn = false;
    s.zones[7].state = ValveState::Open;
    EXPECT_FALSE(outputsSafe(s));
}

TEST(OtaSession, StreamsInSmallChunksAndHoldsHeaderBack)
{
    FakeWriter w;
    OtaSession s(w, kChipIdEsp32s3, kProjectName);
    const auto img = image();
    ASSERT_EQ(s.begin(img.size()), OtaError::None);
    // Nothing reaches flash until the header is complete and checked.
    ASSERT_EQ(s.write(img.data(), 100), OtaError::None);
    EXPECT_TRUE(w.written.empty());
    ASSERT_EQ(stream(s, std::vector<uint8_t>(img.begin() + 100, img.end()), 7), OtaError::None);
    EXPECT_EQ(w.written, img);
    EXPECT_TRUE(w.finished);
    EXPECT_EQ(s.image().version.view(), "v0.2.0");
    EXPECT_EQ(s.received(), img.size());
}

TEST(OtaSession, WrongImageNeverWritten)
{
    FakeWriter w;
    OtaSession s(w, kChipIdEsp32s3, kProjectName);
    const auto img = image(0x0005);
    ASSERT_EQ(s.begin(0), OtaError::None);
    EXPECT_EQ(stream(s, img, 1024), OtaError::WrongChip);
    EXPECT_TRUE(w.written.empty());
    EXPECT_TRUE(w.aborted);
    EXPECT_FALSE(w.finished);
}

TEST(OtaSession, TooLargeAndTruncated)
{
    FakeWriter w;
    w.slot = 4096;
    OtaSession s(w, kChipIdEsp32s3, kProjectName);
    EXPECT_EQ(s.begin(5000), OtaError::TooLarge);
    ASSERT_EQ(s.begin(0), OtaError::None); // unknown length: checked while streaming
    EXPECT_EQ(stream(s, image(), 1000), OtaError::TooLarge);
    EXPECT_TRUE(w.aborted);

    FakeWriter w2;
    OtaSession s2(w2, kChipIdEsp32s3, kProjectName);
    ASSERT_EQ(s2.begin(0), OtaError::None);
    const auto img = image();
    ASSERT_EQ(s2.write(img.data(), 50), OtaError::None);
    EXPECT_EQ(s2.finish(), OtaError::Truncated);
    EXPECT_TRUE(w2.aborted);
}

TEST(OtaSession, FlashAndVerifyFailures)
{
    FakeWriter w;
    w.failWrite = true;
    OtaSession s(w, kChipIdEsp32s3, kProjectName);
    ASSERT_EQ(s.begin(0), OtaError::None);
    EXPECT_EQ(stream(s, image(), 4096), OtaError::FlashError);
    EXPECT_TRUE(w.aborted);

    FakeWriter w2;
    w2.failFinish = true;
    OtaSession s2(w2, kChipIdEsp32s3, kProjectName);
    ASSERT_EQ(s2.begin(0), OtaError::None);
    EXPECT_EQ(stream(s2, image(), 4096), OtaError::VerifyFailed);
    // Writing after a failure is refused.
    const uint8_t b = 0;
    EXPECT_EQ(s2.write(&b, 1), OtaError::Aborted);
}

TEST(SelfTest, ConfirmsWhenControlLoopAndWebRun)
{
    SelfTestInput in;
    EXPECT_EQ(evaluateSelfTest(in), SelfTestVerdict::Wait);
    in.elapsedMs = 6000;
    in.snapshotsSeen = 6;
    EXPECT_EQ(evaluateSelfTest(in), SelfTestVerdict::Wait); // web not started
    in.webStarted = true;
    EXPECT_EQ(evaluateSelfTest(in), SelfTestVerdict::Confirm);
}

TEST(SelfTest, RollsBackOnDeadlineOrRejectedConfig)
{
    SelfTestInput in;
    in.elapsedMs = 59999;
    in.snapshotsSeen = 2;
    in.webStarted = true;
    EXPECT_EQ(evaluateSelfTest(in), SelfTestVerdict::Wait);
    in.elapsedMs = 60000;
    EXPECT_EQ(evaluateSelfTest(in), SelfTestVerdict::Rollback);
    SelfTestInput bad;
    bad.configInvalid = true;
    bad.snapshotsSeen = 10;
    bad.webStarted = true;
    EXPECT_EQ(evaluateSelfTest(bad), SelfTestVerdict::Rollback);
}

TEST(Assets, LittlefsMagic)
{
    std::vector<uint8_t> block(4096, 0xFF);
    EXPECT_FALSE(looksLikeLittlefs(block.data(), block.size()));
    std::memcpy(block.data() + 8, "littlefs", 8);
    EXPECT_TRUE(looksLikeLittlefs(block.data(), block.size()));
    EXPECT_FALSE(looksLikeLittlefs(block.data(), 15));
}
