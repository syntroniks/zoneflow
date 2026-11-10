// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fakes.hpp"
#include "zf/zone/zone_driver.hpp"

using namespace zf;
using namespace zf::test;

namespace {

struct Listener final : IZoneDriverListener {
    std::vector<std::tuple<ZoneIndex, ValveState, StopReason>> zones;
    std::vector<bool> master;
    void onZoneChanged(ZoneIndex z, ValveState s, StopReason r) override { zones.emplace_back(z, s, r); }
    void onMasterChanged(bool on) override { master.push_back(on); }
};

class ZoneDriverTest : public ::testing::Test {
protected:
    FakeClock clock;
    FakeOutputs out;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    ZoneDriver drv{out, oe, clock};
    Listener listener;

    ZoneDriverConfig cfg()
    {
        ZoneDriverConfig c;
        c.zoneEnabled.fill(true);
        c.maxRunSec.fill(600);
        return c;
    }

    void SetUp() override
    {
        drv.setListener(&listener);
        drv.init(cfg());
    }

    void run(MonoMs ms, MonoMs step = 10)
    {
        for (MonoMs t = 0; t < ms; t += step) {
            clock.advanceMs(step);
            drv.tick();
        }
    }
};

} // namespace

TEST_F(ZoneDriverTest, InitForcesAllOff)
{
    out.zone[3] = true;
    out.master = true;
    drv.init(cfg());
    EXPECT_FALSE(out.anyOn());
    EXPECT_GE(out.allOffCalls, 2);
}

TEST_F(ZoneDriverTest, RunsForRequestedDurationThenCompletes)
{
    auto r = drv.start(2, 5);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.acceptedSec, 5u);
    EXPECT_TRUE(out.zone[2]);
    run(4990);
    EXPECT_TRUE(out.zone[2]);
    run(20);
    EXPECT_FALSE(out.zone[2]);
    EXPECT_EQ(drv.lastStopReason(2), StopReason::Completed);
}

TEST_F(ZoneDriverTest, ClampsToZoneMaximumRunTime)
{
    auto c = cfg();
    c.maxRunSec[0] = 10;
    drv.reconfigure(c);
    auto r = drv.start(0, 3600);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.acceptedSec, 10u);
    run(10'010);
    EXPECT_FALSE(out.zone[0]);
}

TEST_F(ZoneDriverTest, NoCallerCanExceedHardLimit)
{
    auto c = cfg();
    c.maxRunSec[1] = 0;       // "unset" still means the hard limit
    c.maxRunSec[2] = 999'999; // above the hard limit
    drv.reconfigure(c);
    EXPECT_EQ(drv.start(1, 0xFFFFFFFFu).acceptedSec, kHardMaxRunSec);
    EXPECT_EQ(drv.start(2, 0xFFFFFFFFu).acceptedSec, kHardMaxRunSec);
    run(static_cast<MonoMs>(kHardMaxRunSec) * 1000 + 100, 1000);
    EXPECT_FALSE(out.anyOn());
}

TEST_F(ZoneDriverTest, ConcurrencyLimitDefaultsToTwo)
{
    EXPECT_TRUE(drv.start(0, 60).ok());
    EXPECT_TRUE(drv.start(1, 60).ok());
    EXPECT_EQ(drv.start(2, 60).error, StartError::TooManyActive);
    EXPECT_EQ(out.openCount(), 2);
}

TEST_F(ZoneDriverTest, ConcurrencyLimitCannotBeConfiguredAboveSpec)
{
    auto c = cfg();
    c.maxConcurrent = 8;
    drv.reconfigure(c);
    EXPECT_EQ(drv.config().maxConcurrent, kMaxConcurrentZonesLimit);
    c.maxConcurrent = 1;
    drv.reconfigure(c);
    EXPECT_TRUE(drv.start(0, 60).ok());
    EXPECT_EQ(drv.start(1, 60).error, StartError::TooManyActive);
}

TEST_F(ZoneDriverTest, RejectsInvalidDisabledDuplicateAndZero)
{
    auto c = cfg();
    c.zoneCount = 4;
    c.zoneEnabled[1] = false;
    drv.reconfigure(c);
    EXPECT_EQ(drv.start(4, 10).error, StartError::InvalidZone);
    EXPECT_EQ(drv.start(1, 10).error, StartError::Disabled);
    EXPECT_EQ(drv.start(0, 0).error, StartError::ZeroDuration);
    EXPECT_TRUE(drv.start(0, 10).ok());
    EXPECT_EQ(drv.start(0, 10).error, StartError::AlreadyActive);
}

TEST_F(ZoneDriverTest, MasterPreOpenAndPostClose)
{
    auto c = cfg();
    c.masterEnabled = true;
    c.masterPreOpenMs = 1000;
    c.masterPostCloseMs = 3000;
    drv.reconfigure(c);
    out.log.clear();

    ASSERT_TRUE(drv.start(0, 2).ok());
    EXPECT_TRUE(out.master);
    EXPECT_FALSE(out.zone[0]);
    EXPECT_EQ(drv.state(0), ValveState::Waiting);
    run(990);
    EXPECT_FALSE(out.zone[0]);
    run(20);
    EXPECT_TRUE(out.zone[0]);
    run(2000);
    EXPECT_FALSE(out.zone[0]);
    EXPECT_TRUE(out.master);
    run(2950);
    EXPECT_TRUE(out.master);
    run(100);
    EXPECT_FALSE(out.master);
    EXPECT_EQ(out.log, (std::vector<std::string>{"m+", "z0+", "z0-", "m-"}));
}

TEST_F(ZoneDriverTest, MasterStaysOnBetweenSequentialZones)
{
    auto c = cfg();
    c.masterEnabled = true;
    c.masterPreOpenMs = 500;
    c.masterPostCloseMs = 5000;
    c.interZoneDelayMs = 2000;
    drv.reconfigure(c);
    ASSERT_TRUE(drv.start(0, 1).ok());
    run(1600);
    EXPECT_FALSE(out.zone[0]);
    out.log.clear();
    ASSERT_TRUE(drv.start(1, 1).ok());
    // Master already on: no pre-open delay, but the inter-zone delay applies.
    EXPECT_EQ(drv.state(1), ValveState::Waiting);
    run(1950);
    EXPECT_TRUE(out.zone[1]);
    EXPECT_EQ(out.log, (std::vector<std::string>{"z1+"}));
}

TEST_F(ZoneDriverTest, InterZoneDelay)
{
    auto c = cfg();
    c.interZoneDelayMs = 3000;
    drv.reconfigure(c);
    ASSERT_TRUE(drv.start(0, 1).ok());
    run(1010);
    ASSERT_FALSE(out.zone[0]);
    ASSERT_TRUE(drv.start(1, 1).ok());
    EXPECT_FALSE(out.zone[1]);
    run(1900);
    EXPECT_FALSE(out.zone[1]);
    run(1100); // opens 3000 ms after zone 0 closed
    EXPECT_TRUE(out.zone[1]);
}

TEST_F(ZoneDriverTest, StopAllTurnsEverythingOffImmediately)
{
    auto c = cfg();
    c.masterEnabled = true;
    c.masterPostCloseMs = 60000;
    drv.reconfigure(c);
    ASSERT_TRUE(drv.start(0, 60).ok());
    ASSERT_TRUE(drv.start(1, 60).ok());
    drv.stopAll(StopReason::Fault);
    EXPECT_FALSE(out.anyOn());
    EXPECT_EQ(drv.lastStopReason(0), StopReason::Fault);
    EXPECT_EQ(drv.activeCount(), 0);
}

TEST_F(ZoneDriverTest, EmergencyOffLatchesAndTripsOutputEnable)
{
    ASSERT_TRUE(drv.start(0, 60).ok());
    drv.emergencyOff();
    EXPECT_FALSE(out.anyOn());
    EXPECT_TRUE(oe.tripped());
    EXPECT_EQ(drv.start(1, 10).error, StartError::Inhibited);
    drv.tick();
    EXPECT_EQ(drv.state(0), ValveState::Off);
    EXPECT_EQ(drv.lastStopReason(0), StopReason::Shutdown);
}

TEST_F(ZoneDriverTest, LockoutAndInhibit)
{
    drv.setLockout(3, true);
    EXPECT_EQ(drv.start(3, 10).error, StartError::LockedOut);
    drv.clearLockouts();
    drv.setInhibit(true);
    EXPECT_EQ(drv.start(3, 10).error, StartError::Inhibited);
    drv.setInhibit(false);
    EXPECT_TRUE(drv.start(3, 10).ok());
}

TEST_F(ZoneDriverTest, OutputEnableDemandFollowsOutputs)
{
    drv.tick();
    EXPECT_FALSE(oe.allowed(static_cast<uint32_t>(clock.mono)));
    ASSERT_TRUE(drv.start(0, 1).ok());
    drv.tick();
    EXPECT_TRUE(oe.allowed(static_cast<uint32_t>(clock.mono)));
    run(1100);
    EXPECT_FALSE(oe.allowed(static_cast<uint32_t>(clock.mono)));
}

TEST_F(ZoneDriverTest, RemainingTime)
{
    ASSERT_TRUE(drv.start(0, 10).ok());
    run(4000);
    EXPECT_NEAR(drv.remainingMs(0), 6000, 20);
    EXPECT_EQ(drv.remainingMs(1), 0u);
}

TEST_F(ZoneDriverTest, ListenerSeesEveryTransition)
{
    ASSERT_TRUE(drv.start(0, 1).ok());
    run(1100);
    ASSERT_EQ(listener.zones.size(), 2u);
    EXPECT_EQ(std::get<1>(listener.zones[0]), ValveState::Open);
    EXPECT_EQ(std::get<1>(listener.zones[1]), ValveState::Off);
    EXPECT_EQ(std::get<2>(listener.zones[1]), StopReason::Completed);
}
