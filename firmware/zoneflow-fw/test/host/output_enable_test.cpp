// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "zf/zone/output_enable.hpp"

using zf::OutputEnableGuard;
using zf::OutputEnableMode;

TEST(OutputEnable, LowWithoutDemandOrHeartbeat)
{
    OutputEnableGuard g(OutputEnableMode::Toggle, 100);
    EXPECT_FALSE(g.nextLevel(0));
    g.setDemand(true);
    EXPECT_FALSE(g.nextLevel(0)); // never beaten
    g.heartbeat(0);
    g.setDemand(false);
    EXPECT_FALSE(g.nextLevel(1));
}

TEST(OutputEnable, TogglesWhileAliveAndStopsWhenStale)
{
    OutputEnableGuard g(OutputEnableMode::Toggle, 100);
    g.setDemand(true);
    g.heartbeat(1000);
    EXPECT_TRUE(g.nextLevel(1005));
    EXPECT_FALSE(g.nextLevel(1010));
    EXPECT_TRUE(g.nextLevel(1015));
    // Control loop stops beating: after the timeout, OE stays low.
    EXPECT_FALSE(g.nextLevel(1101));
    EXPECT_FALSE(g.nextLevel(1106));
    EXPECT_FALSE(g.nextLevel(1111));
}

TEST(OutputEnable, StaticLevelMode)
{
    OutputEnableGuard g(OutputEnableMode::StaticLevel, 100);
    g.setDemand(true);
    g.heartbeat(0);
    EXPECT_TRUE(g.nextLevel(1));
    EXPECT_TRUE(g.nextLevel(2));
    EXPECT_FALSE(g.nextLevel(500));
}

TEST(OutputEnable, TripLatchesUntilReset)
{
    OutputEnableGuard g(OutputEnableMode::StaticLevel, 100);
    g.setDemand(true);
    g.heartbeat(0);
    g.trip();
    EXPECT_FALSE(g.nextLevel(1));
    g.heartbeat(2);
    EXPECT_FALSE(g.nextLevel(3));
    g.reset();
    EXPECT_TRUE(g.nextLevel(4));
}

TEST(OutputEnable, HeartbeatFromOtherCoreSlightlyAhead)
{
    OutputEnableGuard g(OutputEnableMode::StaticLevel, 100);
    g.setDemand(true);
    g.heartbeat(1001);
    EXPECT_TRUE(g.nextLevel(1000));
}

TEST(OutputEnable, MillisecondCounterWrap)
{
    OutputEnableGuard g(OutputEnableMode::StaticLevel, 100);
    g.setDemand(true);
    g.heartbeat(0xFFFFFFF0u);
    EXPECT_TRUE(g.nextLevel(0x20u));
    EXPECT_FALSE(g.nextLevel(0x80u));
}
