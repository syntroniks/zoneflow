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

// Review finding: a heartbeat that stopped 2^31 ms (24.8 days) ago has a
// negative 32-bit age and used to count as fresh again; after 2^32 ms it
// reads as a few milliseconds old. Once stale, OE stays low until the control
// loop beats again.
TEST(OutputEnable, StaleHeartbeatNeverBecomesFreshAgain)
{
    OutputEnableGuard g(OutputEnableMode::StaticLevel, 100);
    g.setDemand(true);
    g.heartbeat(0);
    EXPECT_TRUE(g.nextLevel(50));
    EXPECT_FALSE(g.nextLevel(1000));
    EXPECT_FALSE(g.allowed(0x80000000u));
    EXPECT_FALSE(g.nextLevel(0x80000000u));
    EXPECT_FALSE(g.nextLevel(0xFFFFFFF6u)); // age reads as -10 ms
    EXPECT_FALSE(g.nextLevel(0x00000005u)); // age reads as +5 ms
    g.heartbeat(0x10u);                     // the control loop is back
    EXPECT_TRUE(g.nextLevel(0x12u));
}

// ---- keep-alive from a hardware timer: step() -----------------------------------------
// The device calls step(period) from an IRAM-safe timer interrupt that keeps
// running while the flash cache is off. step() needs no clock: it counts its
// own periods since the heartbeat counter last changed.

namespace {
// Runs the timer for durationMs with a 5 ms period; the control loop beats
// every beatEveryMs (0 = never). Returns the number of edges seen.
int runTimer(OutputEnableGuard &g, uint32_t durationMs, uint32_t beatEveryMs, uint32_t &t, bool &level)
{
    int edges = 0;
    for (uint32_t i = 0; i < durationMs / 5; ++i) {
        t += 5;
        if (beatEveryMs != 0 && t % beatEveryMs == 0) {
            g.heartbeat(t);
        }
        const bool next = g.step(5);
        edges += next != level ? 1 : 0;
        level = next;
    }
    return edges;
}
} // namespace

TEST(OutputEnableTimer, TogglesEveryPeriodWhileTheLoopBeats)
{
    OutputEnableGuard g(OutputEnableMode::Toggle, 500);
    g.setDemand(true);
    uint32_t t = 0;
    bool level = false;
    EXPECT_EQ(runTimer(g, 100, 0, t, level), 0); // never beaten: low
    // First beat at t = 150, then an edge on every 5 ms step up to t = 1100.
    EXPECT_EQ(runTimer(g, 1000, 50, t, level), 191);
}

// The control loop dies: the toggling stops once the heartbeat is older than
// the timeout, measured in timer periods, and OE then stays low.
TEST(OutputEnableTimer, DeadControlLoopStopsTheToggleWithinTheTimeout)
{
    OutputEnableGuard g(OutputEnableMode::Toggle, 500);
    g.setDemand(true);
    uint32_t t = 0;
    bool level = false;
    runTimer(g, 1000, 50, t, level);
    const uint32_t lastBeat = t - t % 50;
    uint32_t lastEdge = t;
    for (int i = 0; i < 400; ++i) { // 2 s with no heartbeat
        t += 5;
        const bool next = g.step(5);
        if (next != level) {
            lastEdge = t;
        }
        level = next;
    }
    EXPECT_FALSE(level);
    EXPECT_LE(lastEdge - lastBeat, 500u + 5u);
    EXPECT_GE(lastEdge - lastBeat, 500u - 5u);
}

// A flash erase stalls the control task for a while: OE survives a stall
// shorter than the timeout, and drops for a longer one until the next beat.
TEST(OutputEnableTimer, SurvivesAStallShorterThanTheTimeout)
{
    OutputEnableGuard g(OutputEnableMode::Toggle, 500);
    g.setDemand(true);
    uint32_t t = 0;
    bool level = false;
    runTimer(g, 200, 50, t, level);
    EXPECT_EQ(runTimer(g, 400, 0, t, level), 80); // 400 ms stall: keeps toggling
    runTimer(g, 200, 50, t, level);
    EXPECT_LT(runTimer(g, 800, 0, t, level), 160); // 800 ms stall: stops part way
    EXPECT_FALSE(level);
    g.heartbeat(t);
    EXPECT_TRUE(g.step(5)); // back with the next beat
}

TEST(OutputEnableTimer, StaysLowWhenTrippedOrWithoutDemand)
{
    OutputEnableGuard g(OutputEnableMode::StaticLevel, 500);
    g.heartbeat(0);
    EXPECT_FALSE(g.step(5)); // no demand
    g.setDemand(true);
    EXPECT_TRUE(g.step(5));
    EXPECT_TRUE(g.step(5)); // static level: held high
    g.trip();
    EXPECT_FALSE(g.step(5));
    g.heartbeat(10);
    EXPECT_FALSE(g.step(5));
    g.reset();
    EXPECT_TRUE(g.step(5));
}

// The elapsed-time count saturates: however long the loop stays dead, it
// never wraps round to "fresh".
TEST(OutputEnableTimer, LongDeadLoopNeverReadsFresh)
{
    OutputEnableGuard g(OutputEnableMode::StaticLevel, 500);
    g.setDemand(true);
    g.heartbeat(0);
    EXPECT_TRUE(g.step(5));
    for (int i = 0; i < 10; ++i) {
        EXPECT_FALSE(g.step(0x7FFFFFFFu));
    }
    g.heartbeat(1);
    EXPECT_TRUE(g.step(5));
}
