// SPDX-License-Identifier: Apache-2.0
// Regression tests for the requirement gaps the system soak simulation
// found (firmware architecture section 22) that firmware closes: the zone
// fault policy shared by both firmwares, a failed-short output device found
// during another zone's run, the fault path's all-off write, the run budget
// kept over a power cycle, the runtime record's own check and second copy,
// and the clock put back by less than a day.
#include <gtest/gtest.h>

#include <algorithm>

#include "fake_board.hpp"
#include "window_rig.hpp"
#include "zf/diag/zone_fault_policy.hpp"

using namespace zf;
using namespace zf::test;

namespace {

bool lockedOut(WindowRig &r, ZoneIndex z) { return r.state.snapshot().zones[z].lockedOut; }

// Starts zone `z` for `sec` and waits until it is driven.
void runZone(WindowRig &r, ZoneIndex z, uint32_t sec)
{
    ASSERT_TRUE(r.ctrl->handle(Command::runZone(z, sec)));
    ASSERT_TRUE(r.runUntil([&] { return r.board.valveDriven(z); }, 12000)) << toString(r.stage.status().fault);
}

void bootIdle(WindowRig &r)
{
    ASSERT_TRUE(r.runUntil([&] { return r.stage.status().phase == StagePhase::Idle; }, 12000))
        << toString(r.stage.status().fault);
}

} // namespace

// ---- gap 7: one zone fault policy for both firmwares ---------------------------------------------------------

TEST(ZoneFaultPolicy, OpenCircuitRetriesAndDcComponentLocksOut)
{
    // zf/diag/zone_fault_policy.hpp. The ESPHome supervisor takes the same
    // table (firmware/esphome/test/host/supervisor_test.cpp checks it there).
    EXPECT_FALSE(zoneFaultPolicy(CurrentFault::OpenCircuit).lockOut);
    EXPECT_FALSE(zoneFaultPolicy(CurrentFault::OpenCircuit).stopAll);
    EXPECT_TRUE(zoneFaultPolicy(CurrentFault::OpenCircuit).clearsOnGoodRun);
    EXPECT_TRUE(zoneFaultPolicy(CurrentFault::OverCurrent).lockOut);
    EXPECT_TRUE(zoneFaultPolicy(CurrentFault::ShortCircuit).lockOut);
    EXPECT_TRUE(zoneFaultPolicy(CurrentFault::ShortCircuit).stopAll);
    EXPECT_TRUE(zoneFaultPolicy(CurrentFault::CurrentRose).stopAll);
    EXPECT_TRUE(kDcComponentPolicy.lockOut);
    EXPECT_FALSE(kDcComponentPolicy.stopAll);
}

TEST(ZoneFaultPolicy, OpenWireIsRetriedAndTheReportClearsWhenItIsFixed)
{
    WindowRig r;
    bootIdle(r);
    r.board.valveMa[2] = 10; // the wire to zone 3 is cut
    runZone(r, 2, 120);
    ASSERT_TRUE(r.runUntil([&] { return r.has(FaultCode::ZoneOpenCircuit); }, 5000));
    r.run(200);
    EXPECT_FALSE(r.board.valveDriven(2));
    EXPECT_FALSE(lockedOut(r, 2)) << "an open wire is not locked out";
    EXPECT_EQ(r.state.snapshot().zones[2].fault, FaultCode::ZoneOpenCircuit);
    // Fixed in the field: the next start runs it, and the report clears.
    r.board.valveMa[2] = 300;
    runZone(r, 2, 3);
    ASSERT_TRUE(r.runUntil([&] { return !r.board.valveDriven(2); }, 6000));
    r.run(100);
    EXPECT_FALSE(r.has(FaultCode::ZoneOpenCircuit));
    EXPECT_EQ(r.state.snapshot().zones[2].fault, FaultCode::None);
}

TEST(ZoneFaultPolicy, DcComponentLocksTheZoneOut)
{
    WindowRig r;
    bootIdle(r);
    r.board.halfWaveOutput = 1; // zone 2's triac conducts one way only
    runZone(r, 1, 120);
    ASSERT_TRUE(r.runUntil([&] { return r.has(FaultCode::ZoneDcCurrent); }, 5000));
    r.run(200);
    EXPECT_FALSE(r.board.valveDriven(1));
    EXPECT_TRUE(lockedOut(r, 1)) << "a half-dead output on the board";
    EXPECT_FALSE(r.ctrl->handle(Command::runZone(1, 60)));
    EXPECT_EQ(r.ctrl->lastRunError(), RunError::LockedOut);
}

// ---- gap 2: a failed-short output device during another zone's run -------------------------------------------

TEST(ShortedOutput, FoundDuringAnotherZonesRunWithinASecond)
{
    // Zone 1 runs. Another output's triac fails short and adds its valve's
    // 250 mA: the current rises with no output switched. Every output goes
    // off, and rule 6 (current with the relay closed and no valve bit low)
    // finds the device and opens the relay. Before, it watered until zone 1
    // ended, up to its maximum run time.
    WindowRig r;
    bootIdle(r);
    runZone(r, 0, 600);
    r.run(3000); // settled, level known
    r.board.shortedOutputMa = 250;
    const uint64_t at = r.clock.mono;
    ASSERT_TRUE(r.runUntil([&] { return !r.board.relayClosed(); }, 3000));
    const uint64_t detectMs = r.clock.mono - at;
    EXPECT_LE(detectMs, 1000u) << "the shorted output watered for " << detectMs << " ms";
    r.run(200);
    EXPECT_TRUE(r.has(FaultCode::OutputShorted));
    EXPECT_FALSE(r.has(FaultCode::ZoneOverCurrent)) << "the zone that was on is not to blame";
    EXPECT_FALSE(lockedOut(r, 0));
    EXPECT_EQ(r.board.currentMa(), 0);
}

TEST(ShortedOutput, ARiseThroughTheZoneItselfIsItsOverCurrent)
{
    // The zone's own current rises inside its window (80 to 700 mA): with
    // every valve off the idle check finds nothing, so after 1.5 s the zone
    // is blamed and locked out like an over-current.
    WindowRig r;
    bootIdle(r);
    runZone(r, 0, 600);
    r.run(3000);
    r.board.valveMa[0] = 560;
    ASSERT_TRUE(r.runUntil([&] { return r.has(FaultCode::ZoneOverCurrent); }, 4000));
    EXPECT_FALSE(r.has(FaultCode::OutputShorted));
    EXPECT_TRUE(lockedOut(r, 0));
    EXPECT_EQ(r.state.snapshot().zones[0].fault, FaultCode::ZoneOverCurrent);
}

TEST(ShortedOutput, ASteadyRunIsNotARise)
{
    WindowRig r([](Config &c) { c.zones[0].maxRunSec = 900; });
    bootIdle(r);
    r.board.noiseMa = 0;
    runZone(r, 0, 600);
    r.run(60000);
    EXPECT_TRUE(r.board.valveDriven(0));
    EXPECT_EQ(r.ctrl->activeFaults() & ~(1u << static_cast<unsigned>(FaultCode::TimeInvalid)), 0u);
}
