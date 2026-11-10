// SPDX-License-Identifier: Apache-2.0
// Zero-cross aware switching: outputs turn on at the next mains zero crossing
// when the board provides the signal; without it nothing changes.
#include <gtest/gtest.h>

#include "fakes.hpp"
#include "zf/config/config_json.hpp"
#include "zf/zone/zero_cross.hpp"
#include "zf/zone/zone_driver.hpp"

using namespace zf;
using namespace zf::test;

namespace {

ZeroCrossSample signal(uint32_t lastEdgeUs, uint32_t intervalUs, uint32_t nowUs)
{
    ZeroCrossSample s;
    s.present = true;
    s.lastEdgeUs = lastEdgeUs;
    s.intervalUs = intervalUs;
    s.nowUs = nowUs;
    return s;
}

// Fake zero-cross input: a steady 50 Hz signal (a crossing every 10 ms).
// delayUs() advances its clock and logs into the output log, so tests can
// check the order of waits and output writes.
class FakeZeroCross final : public IZeroCrossHal {
public:
    explicit FakeZeroCross(FakeOutputs &out) : out_(out) {}
    bool present = true;
    uint32_t nowUs = 1'003'000; // 3 ms after a crossing
    std::vector<uint32_t> waits;

    ZeroCrossSample sample() const override
    {
        ZeroCrossSample s;
        s.present = present;
        s.intervalUs = 10'000;
        s.lastEdgeUs = nowUs - nowUs % 10'000;
        s.nowUs = nowUs;
        return s;
    }
    void delayUs(uint32_t us) override
    {
        waits.push_back(us);
        nowUs += us;
        out_.log.push_back("wait");
    }

private:
    FakeOutputs &out_;
};

} // namespace

TEST(ZeroCrossTiming, NoSignalMeansSwitchNow) { EXPECT_EQ(zeroCrossWaitUs(ZeroCrossSample{}, 0), 0u); }

TEST(ZeroCrossTiming, WaitsForTheNextCrossing)
{
    EXPECT_EQ(zeroCrossWaitUs(signal(1'000'000, 10'000, 1'003'000), 0), 7'000u); // 50 Hz
    EXPECT_EQ(zeroCrossWaitUs(signal(1'000'000, 8'333, 1'000'333), 0), 8'000u);  // 60 Hz
    EXPECT_EQ(zeroCrossWaitUs(signal(1'000'000, 10'000, 1'025'000), 0), 5'000u); // edges missed: still in phase
    EXPECT_EQ(zeroCrossWaitUs(signal(1'000'000, 10'000, 1'000'000), 0), 0u);     // on the crossing
    EXPECT_EQ(zeroCrossWaitUs(signal(0xFFFF'FC18u, 10'000, 3'000), 0), 6'000u);  // 32-bit wrap
}

TEST(ZeroCrossTiming, OffsetShiftsTheTarget)
{
    // Detector edge 500 us before the real crossing: target edge + 500 us.
    EXPECT_EQ(zeroCrossWaitUs(signal(1'000'000, 10'000, 1'003'000), 500), 7'500u);
    EXPECT_EQ(zeroCrossWaitUs(signal(1'000'000, 10'000, 1'003'000), -500), 6'500u);
}

TEST(ZeroCrossTiming, ImplausibleOrStaleSignalMeansSwitchNow)
{
    EXPECT_EQ(zeroCrossWaitUs(signal(1'000'000, 3'000, 1'001'000), 0), 0u);  // interval too short
    EXPECT_EQ(zeroCrossWaitUs(signal(1'000'000, 25'000, 1'001'000), 0), 0u); // too long
    EXPECT_EQ(zeroCrossWaitUs(signal(1'000'000, 10'000, 1'031'000), 0), 0u); // no edge for 3 intervals
}

namespace {
class ZeroCrossDriverTest : public ::testing::Test {
protected:
    FakeClock clock;
    FakeOutputs out;
    FakeZeroCross zc{out};
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    ZoneDriver drv{out, oe, clock};

    ZoneDriverConfig cfg(bool zeroCross)
    {
        ZoneDriverConfig c;
        c.zoneEnabled.fill(true);
        c.maxRunSec.fill(600);
        c.masterEnabled = true;
        c.zeroCrossSwitching = zeroCross;
        return c;
    }
};
} // namespace

TEST_F(ZeroCrossDriverTest, TurnOnWaitsForTheCrossingWhenEnabled)
{
    drv.setZeroCross(&zc, 0);
    drv.init(cfg(true));
    out.log.clear();
    ASSERT_TRUE(drv.start(0, 60).ok());
    clock.advanceMs(10);
    drv.tick();
    ASSERT_TRUE(out.zone[0]);
    ASSERT_EQ(zc.waits.size(), 1u);
    EXPECT_EQ(zc.waits[0], 7'000u);
    // The master waits for the crossing; the zone that follows at once is
    // already on it and does not wait a further half cycle.
    const std::vector<std::string> expected = {"wait", "m+", "z0+"};
    EXPECT_EQ(out.log, expected);
}

TEST_F(ZeroCrossDriverTest, TurnOffAndAllOffNeverWait)
{
    drv.setZeroCross(&zc, 0);
    drv.init(cfg(true));
    ASSERT_TRUE(drv.start(0, 60).ok());
    clock.advanceMs(10);
    drv.tick();
    zc.waits.clear();
    drv.stop(0, StopReason::Requested);
    drv.stopAll(StopReason::Requested);
    drv.emergencyOff();
    EXPECT_TRUE(zc.waits.empty());
    EXPECT_FALSE(out.anyOn());
}

TEST_F(ZeroCrossDriverTest, DisabledOrNoSignalSwitchesAsBefore)
{
    drv.setZeroCross(&zc, 0);
    drv.init(cfg(false));
    ASSERT_TRUE(drv.start(0, 60).ok());
    EXPECT_TRUE(zc.waits.empty());
    drv.stopAll(StopReason::Requested);

    drv.reconfigure(cfg(true));
    zc.present = false;
    ASSERT_TRUE(drv.start(1, 60).ok());
    clock.advanceMs(10);
    drv.tick();
    EXPECT_TRUE(out.zone[1]);
    EXPECT_TRUE(zc.waits.empty());
}

TEST_F(ZeroCrossDriverTest, NoHalAttachedSwitchesAsBefore)
{
    drv.init(cfg(true));
    ASSERT_TRUE(drv.start(0, 60).ok());
    clock.advanceMs(10);
    drv.tick();
    EXPECT_TRUE(out.zone[0]);
}

TEST(Config, ZeroCrossSwitchingFlag)
{
    Config c = Config::defaults();
    EXPECT_FALSE(c.driver.zeroCrossSwitching);
    c.driver.zeroCrossSwitching = true;
    Config back;
    ASSERT_TRUE(configFromJson(configToJson(c), back).ok());
    EXPECT_TRUE(back.driver.zeroCrossSwitching);
}
