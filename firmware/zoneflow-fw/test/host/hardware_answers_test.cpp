// SPDX-License-Identifier: Apache-2.0
// The firmware side of the hardware answers to section 19 (issue #41,
// docs/design/hardware-answers-a2.md section 7): the hold of step 2, step 2b
// before every run, the float-test load, the band and learning limits of
// step 9, commissioning only at 25 to 50 C, the supply monitor of 19.10, the
// rest levels with rules 18 and 19, a trip with no valve bit low (19.8) and
// U13, on the simulated board of test/host/fake_board.hpp. Each test names
// the corner case of the hardware simulation it follows.
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "fake_board.hpp"
#include "window_rig.hpp"
#include "zf/app/controller.hpp"
#include "zf/fault_text.hpp"

using namespace zf;
using namespace zf::test;

namespace {

constexpr uint64_t kStartupMs = 11000;

void startUp(StageRig &r)
{
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, kStartupMs)) << toString(r.fault());
}

void powerUp(StageRig &r, uint8_t firstBit = 0)
{
    r.stage.requestPower(true, firstBit);
    ASSERT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 9000)) << toString(r.fault());
}

void runZone(StageRig &r, uint64_t ms = 300)
{
    powerUp(r);
    r.stage.setZone(0, true);
    r.run(ms);
    r.stage.setZone(0, false);
    r.stage.requestPower(false, 0xFF);
}

// Runs until pred() holds, watching every 5 ms period.
template <typename Watch> void runWatching(StageRig &r, uint64_t ms, Watch watch)
{
    for (uint64_t t = 0; t < ms; t += 5) {
        r.run(5);
        watch();
    }
}

} // namespace

// ---- step 2: the hold -------------------------------------------------------------------------------

TEST(HardwareAnswers, OneEdgeHoldsTheRelayReadBackLowAtTheHotCorner)
{
    // The board model first: at the corner with Q2 at 0.65 V and 70 C the
    // single rising edge of step 2 keeps B_RB low for 1.33 s
    // (hardware/sim/failsafe/sequence.py). With the hold of 1.0 s that
    // review A2 had, step 2 failed this healthy board.
    // The pin is driven here, not the stage.
    StageRig r;
    r.board.setCorner(DetectorCorner::LatestRelease);
    r.guard.setRequest(KeepAliveRequest::HoldHigh);
    const auto hold = [&](uint64_t ms) {
        for (uint64_t t = 0; t < ms; t += 5) {
            r.clock.advanceMs(5);
            r.guard.heartbeat(static_cast<uint32_t>(r.clock.mono));
            r.board.step5ms();
        }
    };
    hold(1000);
    EXPECT_TRUE(r.board.kaHigh());
    EXPECT_FALSE(r.board.relayReadbackHigh()) << "1.0 s after the edge: the old hold would fail here";
    hold(400);
    EXPECT_TRUE(r.board.relayReadbackHigh()) << "1.4 s after the edge";
    EXPECT_FALSE(r.board.driveEnabled());
}

TEST(HardwareAnswers, HealthyHotAndColdCornersPassStepTwoWithTheTwoSecondHold)
{
    for (const DetectorCorner c : {DetectorCorner::LatestRelease, DetectorCorner::SlowCharge}) {
        StageRig r;
        r.board.setCorner(c);
        r.reference.stored =
            c == DetectorCorner::LatestRelease ? DetectorReference{830, 1450} : DetectorReference{680, 200};
        startUp(r);
        EXPECT_EQ(r.fault(), HwCheck::None) << static_cast<int>(c);
        runZone(r);
        ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000)) << toString(r.fault());
        EXPECT_EQ(r.fault(), HwCheck::None) << static_cast<int>(c);
    }
}

TEST(HardwareAnswers, ShortedC51IsStillFoundWithTheLongerHold)
{
    StageRig r;
    r.board.b.onWhilePinHigh = true;
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, kStartupMs));
    EXPECT_EQ(r.fault(), HwCheck::PinHighRelayOn);
}

// ---- step 2b: before every run, and its load ---------------------------------------------------------

TEST(HardwareAnswers, FloatTestRunsBeforeEveryRunWhenThePinHighTestIsDaily)
{
    hw::OutputStageConfig cfg;
    cfg.pinHighTestEveryRun = false;
    StageRig r(cfg);
    startUp(r);
    for (int run = 0; run < 3; ++run) {
        const uint64_t floatBefore = r.board.floatMs;
        bool heldHigh = false;
        bool loadSeen = false;
        uint64_t loadAt = 0;
        uint64_t floatAt = 0;
        r.stage.requestPower(true, 0);
        for (uint64_t t = 0; t < 9000 && !r.stage.powerReady(); t += 5) {
            r.run(5);
            heldHigh = heldHigh || r.guard.request() == KeepAliveRequest::HoldHigh;
            if (r.stage.status().floatTest && !loadSeen) {
                loadSeen = true;
                loadAt = r.clock.mono;
            }
            if (r.board.pinIsInput() && floatAt == 0) {
                floatAt = r.clock.mono;
            }
        }
        ASSERT_TRUE(r.stage.powerReady()) << toString(r.fault());
        EXPECT_FALSE(heldHigh) << "step 2 is daily here";
        EXPECT_GE(r.board.floatMs - floatBefore, 500u) << "step 2b ran before run " << run;
        ASSERT_TRUE(loadSeen);
        // 19.7, note to step 2b: the load runs from 100 ms before the pin floats.
        EXPECT_GE(floatAt - loadAt, 100u);
        r.stage.requestPower(false, 0xFF);
        ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000));
    }
}

TEST(HardwareAnswers, BridgeIsFoundBeforeARunWhenThePinHighTestIsDaily)
{
    // The one fault that takes both layers at once (hardware answers 2.8).
    hw::OutputStageConfig cfg;
    cfg.pinHighTestEveryRun = false;
    StageRig r(cfg);
    startUp(r);
    r.board.kaNet = KaNet::ToggleWeak;
    r.board.kaWeakReadsHigh = true;
    r.stage.requestPower(true, 0);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 9000));
    EXPECT_EQ(r.fault(), HwCheck::FloatPinHigh);
    EXPECT_EQ(r.board.valvesLow(), 0);
}

// ---- step 9: the band -----------------------------------------------------------------------------------

TEST(HardwareAnswers, BandIsFortyToOneHundredSeventyPercent)
{
    // Stored 670 / 570 (StageRig::kTypicalReference).
    struct Case {
        uint32_t aHold;
        uint32_t bHold;
        HwCheck expected;
    };
    const Case cases[] = {
        {275, 570, HwCheck::None},             // tA 0.41 of stored
        {255, 570, HwCheck::StopDriveBand},    // tA 0.38
        {670, 240, HwCheck::None},             // tB 0.42
        {670, 215, HwCheck::StopRelayBand},    // tB 0.38
        {670, 960, HwCheck::None},             // tB 1.68
        {670, 985, HwCheck::StopRelayBand},    // tB 1.73
        {280 * 670 / 570, 570, HwCheck::None}, // review A2's D22 open (0.49): inside the wider band
    };
    for (const Case &c : cases) {
        StageRig r;
        startUp(r);
        r.board.a.holdMs = c.aHold;
        r.board.b.holdMs = c.bHold;
        runZone(r);
        r.runUntil([&] { return r.phase() == StagePhase::Idle || r.phase() == StagePhase::Fault; }, 9000);
        r.run(50);
        EXPECT_EQ(r.fault(), c.expected) << c.aHold << " / " << c.bHold;
    }
}

// ---- commissioning at 25 to 50 C ----------------------------------------------------------------------

TEST(HardwareAnswers, NoCommissioningSampleOutsideTwentyFiveToFiftyDegrees)
{
    for (const float t : {10.0f, 24.5f, 50.5f, 70.0f}) {
        StageRig r({}, false);
        r.dieTempC = t;
        startUp(r);
        r.run(60000);
        const OutputStageStatus s = r.stage.status();
        EXPECT_FALSE(s.commissioned) << t;
        EXPECT_TRUE(s.commissionWaitingForTemperature) << t;
        EXPECT_EQ(s.selfTestsPassed, 1u) << "only the start-up test ran at " << t;
        EXPECT_EQ(r.reference.saves, 0) << t;
    }
}

TEST(HardwareAnswers, UnknownDieTemperatureNeverCommissions)
{
    StageRig r({}, false);
    r.dieTempKnown = false;
    startUp(r);
    r.run(60000);
    EXPECT_FALSE(r.stage.status().commissioned);
    EXPECT_TRUE(r.stage.status().commissionWaitingForTemperature);
}

TEST(HardwareAnswers, CommissioningTriesAgainEveryTenMinutes)
{
    // 19.7 item 1: while nothing is stored, again every 10 minutes, not only
    // ten cycles after a start. Hot first, then the room cools down.
    StageRig r({}, false);
    r.dieTempC = 60.0f;
    startUp(r);
    r.run(5000);
    r.dieTempC = 35.0f;
    r.run(9u * 60u * 1000u);
    EXPECT_FALSE(r.stage.status().commissioned) << "the next attempt is 10 minutes after the last";
    ASSERT_TRUE(r.runUntil([&] { return r.stage.status().commissioned; }, 2u * 60u * 1000u));
    EXPECT_EQ(r.reference.saves, 1);
    EXPECT_FALSE(r.stage.status().commissionWaitingForTemperature);
}

TEST(HardwareAnswers, AFailedAttemptIsRetriedAfterTenMinutes)
{
    // Five samples that do not agree within 1.25 store nothing; ten cycles
    // end the attempt, and the next attempt comes 10 minutes later.
    StageRig r({}, false);
    startUp(r);
    // tA alternates between 0.40 and 0.80 s: each inside the learning
    // limits, never five within 1.25 of each other.
    bool slow = false;
    uint32_t passed = r.stage.status().selfTestsPassed;
    const auto alternate = [&] {
        const uint32_t p = r.stage.status().selfTestsPassed;
        if (p != passed) {
            passed = p;
            slow = !slow;
            r.board.a.holdMs = slow ? 800 : 400;
        }
    };
    runWatching(r, 120000, alternate);
    const uint32_t afterFirst = r.stage.status().selfTestsPassed;
    EXPECT_FALSE(r.stage.status().commissioned);
    EXPECT_LE(afterFirst, 1u + hw::OutputStageConfig{}.maxCommissionCycles);
    runWatching(r, 7u * 60u * 1000u, alternate);
    EXPECT_EQ(r.stage.status().selfTestsPassed, afterFirst) << "no cycle before the 10 minutes are up";
    runWatching(r, 4u * 60u * 1000u, alternate);
    EXPECT_GT(r.stage.status().selfTestsPassed, afterFirst) << "a new attempt after 10 minutes";
    EXPECT_FALSE(r.stage.status().commissioned);
}

// ---- 19.10: the supply monitor ----------------------------------------------------------------------------

TEST(HardwareAnswers, SupplyLowWaitsAndClearsItself)
{
    StageRig r;
    startUp(r);
    r.board.vinMonMv = 480; // a sag to 16 VAC: 0.48 to 0.57 V (19.10)
    r.stage.requestPower(true, 0);
    r.run(8000);
    EXPECT_FALSE(r.stage.powerReady());
    EXPECT_TRUE(r.stage.status().supplyLow);
    EXPECT_EQ(r.fault(), HwCheck::None);
    EXPECT_EQ(r.board.valvesLow(), 0);
    r.board.vinMonMv = 630; // the lowest corner of the specified supply
    ASSERT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 9000)) << toString(r.fault());
    EXPECT_FALSE(r.stage.status().supplyLow);
}

TEST(HardwareAnswers, BrownOutOnUsbIsSupplyLowNotARelayFault)
{
    // Hardware answers 2.2: VIN_DC under the 9 V at which U1 starts, mains
    // still on AC_ZC, V_BUCK off, GPIO7 low. Step 1 would have reported a
    // relay fault that needs a repair.
    StageRig r;
    r.board.vinMonMv = 280;
    r.board.vBuck = false;
    r.run(15000);
    EXPECT_EQ(r.fault(), HwCheck::None);
    EXPECT_TRUE(r.stage.status().supplyLow);
    EXPECT_EQ(r.phase(), StagePhase::StartupTest); // waiting in step 0
    r.board.vinMonMv = 960;
    r.board.vBuck = true;
    startUp(r);
    EXPECT_EQ(r.fault(), HwCheck::None);
}

TEST(HardwareAnswers, SupplyHighDoesNotRun)
{
    StageRig r;
    startUp(r);
    r.board.vinMonMv = 1550;
    r.stage.requestPower(true, 0);
    r.run(8000);
    EXPECT_FALSE(r.stage.powerReady());
    EXPECT_TRUE(r.stage.status().supplyHigh);
    EXPECT_EQ(r.fault(), HwCheck::None);
    r.board.vinMonMv = 1430; // 32 VAC with no load: allowed
    ASSERT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 9000));
}

TEST(HardwareAnswers, NoRectifiedSupplyWithMainsIsARectifierFault)
{
    StageRig r;
    r.board.rectifierOpen = true; // D2, R1 or C1: VIN_DC gone, AC_ZC still toggles
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 4000));
    EXPECT_EQ(r.fault(), HwCheck::RectifierFault);
    EXPECT_STREQ(hwCheckStep(HwCheck::RectifierFault), "0");
    EXPECT_GE(r.clock.mono, 2000u) << "only after 2 s of edges";
    EXPECT_TRUE(hwCheckNeedsRepair(r.fault()));
    EXPECT_FALSE(r.stage.acknowledgeFault(false));
    EXPECT_EQ(faultCodeFor(r.fault()), FaultCode::RectifierFault);
}

TEST(HardwareAnswers, UsbAloneIsNotARectifierFault)
{
    StageRig r;
    r.board.acPresent = false; // no 24 VAC: VIN_MON 0 and no edges
    r.run(20000);
    EXPECT_EQ(r.fault(), HwCheck::None);
    EXPECT_EQ(r.phase(), StagePhase::NoAc);
}

TEST(HardwareAnswers, AShortDropOfTheRectifiedSupplyIsNotAFault)
{
    StageRig r;
    startUp(r);
    r.board.rectifierOpen = true;
    r.run(1500);
    r.board.rectifierOpen = false;
    r.run(5000);
    EXPECT_EQ(r.fault(), HwCheck::None);
}

// ---- 19.10: the rest levels, rules 18 and 19 ------------------------------------------------------------

TEST(HardwareAnswers, BothRestLevelsHighIsTheOverVoltageOfRule18)
{
    // 3V3 at 3.9 V: under U13's 4.04 V, so only firmware sees it.
    StageRig r;
    r.board.v3v3Mv = 3900;
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 3000));
    EXPECT_EQ(r.fault(), HwCheck::SupplyOverVoltage);
    EXPECT_TRUE(hwCheckNeedsRepair(r.fault()));
    EXPECT_EQ(r.guard.request(), KeepAliveRequest::Off);
    r.stage.requestPower(true, 0);
    r.run(9000);
    EXPECT_FALSE(r.stage.powerReady());
}

TEST(HardwareAnswers, HealthyRailCornersAreNotAnOverVoltage)
{
    // 3V3 from 3.20 to 3.40 V (19.10): rest levels 0.76 to 0.81 V.
    for (const int32_t mv : {3200, 3400}) {
        StageRig r;
        r.board.v3v3Mv = mv;
        startUp(r);
        EXPECT_EQ(r.fault(), HwCheck::None) << mv;
        EXPECT_EQ(r.stage.status().currentLost, CurrentLoss::None) << mv;
    }
}

TEST(HardwareAnswers, OneRestLevelOutsideItsLimitsIsALostMeasurementAndZonesStillRun)
{
    // Rule 19: steps 5, 5b and 6 are skipped, zones still run, and the
    // condition is reported for as long as it lasts. A welded relay is then
    // not found: that is what skipping means.
    StageRig r;
    r.board.restLoOffsetMv = 110; // ISENSE_LO at 0.90 V, limit 0.88
    startUp(r);
    EXPECT_EQ(r.stage.status().currentLost, CurrentLoss::RestLevel);
    EXPECT_EQ(r.stage.status().restLoMv, 787 + 110);
    r.board.relayWelded = true;
    powerUp(r);
    EXPECT_EQ(r.fault(), HwCheck::None);
    EXPECT_EQ(r.stage.status().checksSkipped, 1u);
    r.stage.requestPower(false, 0xFF);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000));
    // Back in limits: the condition clears, and the next start checks again.
    r.board.restLoOffsetMv = 0;
    r.run(500);
    EXPECT_EQ(r.stage.status().currentLost, CurrentLoss::None);
    r.stage.requestPower(true, 0);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 9000));
    EXPECT_EQ(r.fault(), HwCheck::RelayWelded);
}

TEST(HardwareAnswers, WindowsThatStopArrivingAreALostMeasurement)
{
    StageRig r;
    startUp(r);
    r.board.windowsStopped = true;
    powerUp(r);
    EXPECT_EQ(r.stage.status().currentLost, CurrentLoss::NoWindow);
    EXPECT_EQ(r.stage.status().checksSkipped, 1u);
    EXPECT_EQ(r.fault(), HwCheck::None);
    r.board.windowsStopped = false;
    r.run(300);
    EXPECT_EQ(r.stage.status().currentLost, CurrentLoss::None);
}

// ---- 19.8 and U13: a trip with no valve bit low --------------------------------------------------------

TEST(HardwareAnswers, HeldLatchInputIsItsOwnFaultAndNotAnOverCurrent)
{
    // U13's output shorted low (or R13 shorted, U6B or U6C stuck): the set
    // input of the latch is held low with a healthy rail. OC reads high at
    // rest, low at the first edges of step 3; A_RB never goes high.
    StageRig r;
    r.board.ocSetHeld = true;
    bool driveSeen = false;
    bool latchedAtRest = false;
    for (uint64_t t = 0; t < 20000 && r.phase() != StagePhase::Fault; t += 5) {
        r.run(5);
        driveSeen = driveSeen || r.board.driveEnabled();
        latchedAtRest = latchedAtRest || (r.board.latched && r.guard.request() == KeepAliveRequest::Off &&
                                          r.phase() == StagePhase::StartupTest && !r.board.a.level(false));
    }
    ASSERT_EQ(r.phase(), StagePhase::Fault);
    EXPECT_EQ(r.fault(), HwCheck::LatchInputHeld);
    EXPECT_FALSE(driveSeen);
    EXPECT_FALSE(latchedAtRest);
    const OutputStageStatus s = r.stage.status();
    EXPECT_EQ(s.tripSeq, 0u) << "not counted against an output";
    EXPECT_EQ(s.idleTrips, 2u) << "tried once more after 1.5 s";
    EXPECT_TRUE(hwCheckNeedsRepair(s.fault));
    EXPECT_EQ(faultCodeFor(s.fault), FaultCode::LatchInputHeld);
    r.run(1500); // detector A lets go
    EXPECT_FALSE(r.board.latched) << "OC reads high with the keep-alive stopped";
}

TEST(HardwareAnswers, OneTripWithNoValveBitLowIsRetriedAndForgotten)
{
    StageRig r;
    startUp(r);
    // A single spurious set during step 3 of a run: tried again, then the
    // run goes ahead.
    r.stage.requestPower(true, 0);
    ASSERT_TRUE(r.runUntil([&] { return r.board.kaHigh() && r.board.driveEnabled(); }, 9000));
    r.board.trip();
    ASSERT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 12000)) << toString(r.fault());
    EXPECT_EQ(r.stage.status().idleTrips, 1u);
    EXPECT_EQ(r.stage.status().tripSeq, 0u);
    EXPECT_EQ(r.fault(), HwCheck::None);
}

TEST(HardwareAnswers, ShortedRegulatorIsFoundByTheRestLevelsAndU13KeepsDriveOff)
{
    // M5: U3 shorted on 24 VAC puts 4.66 to 5.07 V on 3V3. The rest levels
    // read it first (rule 18); U13 holds DRV_EN low whatever the keep-alive
    // does, so A_RB never goes high and no valve can be driven.
    StageRig r;
    r.board.v3v3Mv = 4820;
    bool driveSeen = false;
    for (uint64_t t = 0; t < 12000; t += 5) {
        r.run(5);
        driveSeen = driveSeen || r.board.driveEnabled();
    }
    EXPECT_EQ(r.fault(), HwCheck::SupplyOverVoltage);
    EXPECT_FALSE(driveSeen);
    // Even if firmware ran the keep-alive (an erratic processor), U13 holds
    // drive off: the latch sets with the first edges.
    r.guard.setRequest(KeepAliveRequest::Run);
    for (uint64_t t = 0; t < 2000; t += 5) {
        r.run(5);
        driveSeen = driveSeen || r.board.driveEnabled() || r.board.expanderActive();
    }
    EXPECT_FALSE(driveSeen);
}

// ---- the fault path: one pair write -----------------------------------------------------------------------

TEST(HardwareAnswers, FaultPathSwitchesBothPortsOffInOnePairWrite)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true); // port 0
    r.run(200);
    r.stage.setZone(4, true); // port 1 (zone 5)
    r.run(200);
    ASSERT_EQ(r.board.valvesLow(), 2);
    r.board.log.clear();
    r.board.corruptReadOf = hw::reg::kOutput1; // the next scrub fails its read-back
    r.run(50);
    ASSERT_EQ(r.phase(), StagePhase::Fault);
    EXPECT_EQ(r.fault(), HwCheck::RunReadback);
    // In the same tick as the fault: outputs off in one transaction, then
    // every pin an input in one transaction; no single-port write between.
    const auto off = std::find(r.board.log.begin(), r.board.log.end(), "w02=df,ff");
    ASSERT_NE(off, r.board.log.end());
    EXPECT_NE(std::find(off, r.board.log.end(), "w06=ff,ff"), r.board.log.end());
    EXPECT_TRUE(std::none_of(r.board.log.begin(), off,
                             [](const std::string &e) { return e.rfind("w02=", 0) == 0 || e.rfind("w03=", 0) == 0; }));
    EXPECT_EQ(r.board.valvesLow(), 0);
    EXPECT_FALSE(r.board.relayClosed());
    // The bound: zones 5 to 8 follow the relay and zones 1 to 4 by nine SCL
    // periods at 400 kHz plus t_pv (TI SCPS202C), far inside the 10 ms in
    // which K1 opens (hardware design 9.2).
    EXPECT_EQ(hw::kPairWriteSkewNs, 22700u);
    EXPECT_LT(hw::kPairWriteSkewNs, 10u * 1000u * 1000u);
}

// ---- the controller: every new condition reaches the fault codes ------------------------------------------

TEST(HardwareAnswers, ControllerReportsSupplyLowAndHighAsConditions)
{
    WindowRig r;
    r.run(12000);
    ASSERT_EQ(r.stage.status().phase, StagePhase::Idle) << toString(r.stage.status().fault);
    r.board.vinMonMv = 500;
    r.run(300);
    EXPECT_TRUE(r.has(FaultCode::SupplyLow));
    r.board.vinMonMv = 1600;
    r.run(300);
    EXPECT_FALSE(r.has(FaultCode::SupplyLow));
    EXPECT_TRUE(r.has(FaultCode::SupplyHigh));
    r.board.vinMonMv = 960;
    r.run(300);
    EXPECT_FALSE(r.has(FaultCode::SupplyHigh));
    EXPECT_TRUE(r.raised(FaultCode::SupplyLow));
    EXPECT_TRUE(r.raised(FaultCode::SupplyHigh));
}

TEST(HardwareAnswers, ControllerReportsALostMeasurementForAsLongAsItLasts)
{
    WindowRig r;
    r.run(12000);
    EXPECT_FALSE(r.has(FaultCode::CurrentSenseUnavailable));
    r.board.restHiOffsetMv = -150; // ISENSE_HI at 0.64 V, limit 0.68
    r.run(300);
    EXPECT_TRUE(r.has(FaultCode::CurrentSenseUnavailable));
    r.board.restHiOffsetMv = 0;
    r.run(300);
    EXPECT_FALSE(r.has(FaultCode::CurrentSenseUnavailable));
}

TEST(HardwareAnswers, ControllerLatchesTheRepairFaultsOfTheSupply)
{
    WindowRig r;
    r.board.v3v3Mv = 3950;
    r.run(3000);
    EXPECT_TRUE(r.has(FaultCode::SupplyOverVoltage));
    const SystemState st = r.state.snapshot();
    EXPECT_EQ(st.hardware.check, HwCheck::SupplyOverVoltage);
    EXPECT_TRUE(st.hardware.repairNeeded);
    // One press does not clear it; the confirmed repair does.
    r.board.v3v3Mv = 3300;
    r.ctrl->handle(Command::simple(CommandType::ClearFaults));
    EXPECT_TRUE(r.has(FaultCode::SupplyOverVoltage));
}

// ---- the text table -------------------------------------------------------------------------------------

TEST(HardwareAnswers, NewFaultsHaveTheirOwnWords)
{
    for (const FaultCode c : {FaultCode::SupplyLow, FaultCode::SupplyHigh, FaultCode::RectifierFault,
                              FaultCode::SupplyOverVoltage, FaultCode::LatchInputHeld}) {
        const FaultText *t = faultText(c);
        ASSERT_NE(t, nullptr) << toString(c);
        EXPECT_EQ(t->id, std::string(toString(c)));
        EXPECT_FALSE(t->title.empty());
        EXPECT_TRUE(t->zoneTitle.empty());
    }
}
