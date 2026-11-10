// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "fakes.hpp"
#include "zf/time/time_service.hpp"

using namespace zf;
using namespace zf::test;

TEST(TimeService, BootFromValidRtc)
{
    FakeSystemTime sys;
    FakeRtc rtc;
    rtc.t = utcOf(2026, 11, 10, 8, 0);
    TimeService ts(sys, &rtc);
    EXPECT_EQ(ts.boot(), TimeStatus::FromRtc);
    EXPECT_EQ(sys.t, rtc.t);
    EXPECT_TRUE(ts.rtcPresent());
}

TEST(TimeService, RtcLostTimeLeavesClockUnset)
{
    FakeSystemTime sys;
    FakeRtc rtc;
    rtc.ok = false; // VL flag set or chip missing
    TimeService ts(sys, &rtc);
    EXPECT_EQ(ts.boot(), TimeStatus::Unset);
    EXPECT_FALSE(ts.rtcPresent());
}

TEST(TimeService, ImplausibleRtcValueIsIgnored)
{
    FakeSystemTime sys;
    FakeRtc rtc;
    rtc.t = utcOf(2000, 1, 1);
    TimeService ts(sys, &rtc);
    EXPECT_EQ(ts.boot(), TimeStatus::Unset);
    EXPECT_EQ(sys.t, 0);
}

TEST(TimeService, SystemClockSurvivingSoftResetIsUsed)
{
    FakeSystemTime sys;
    sys.t = utcOf(2026, 11, 10);
    TimeService ts(sys, nullptr);
    EXPECT_EQ(ts.boot(), TimeStatus::FromRtc);
}

TEST(TimeService, NetworkSyncWritesRtc)
{
    FakeSystemTime sys;
    FakeRtc rtc;
    rtc.ok = false;
    TimeService ts(sys, &rtc);
    ts.boot();
    rtc.ok = true;
    sys.t = utcOf(2026, 11, 10, 9, 0);
    ts.onNetworkSync();
    EXPECT_EQ(ts.status(), TimeStatus::Synced);
    EXPECT_EQ(rtc.t, sys.t);
    EXPECT_EQ(rtc.writes, 1);
}

TEST(TimeService, ManualSet)
{
    FakeSystemTime sys;
    FakeRtc rtc;
    TimeService ts(sys, &rtc);
    EXPECT_FALSE(ts.setManual(100));
    EXPECT_TRUE(ts.setManual(utcOf(2026, 11, 10)));
    EXPECT_EQ(sys.t, utcOf(2026, 11, 10));
    EXPECT_EQ(rtc.t, utcOf(2026, 11, 10));
}
