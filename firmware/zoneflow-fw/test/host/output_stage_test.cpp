// SPDX-License-Identifier: Apache-2.0
// The rev A output stage against a simulated board: the register sequence,
// the self-test of hardware design 19.7 with every check made to fail, relay
// sequencing, the over-current latch of 19.8 and the polling cycle of 19.6.
#include <gtest/gtest.h>

#include <algorithm>

#include "fake_board.hpp"
#include "zf/zone/zone_driver.hpp"

using namespace zf;
using namespace zf::test;

namespace {

constexpr uint64_t kStartupMs = 6000; // 2 s low, 1.1 s pin high, start, stop test: about 4 s

bool contains(const std::vector<std::string> &log, std::initializer_list<const char *> seq)
{
    // The entries of seq appear in log in this order (not necessarily adjacent).
    auto it = log.begin();
    for (const char *want : seq) {
        it = std::find(it, log.end(), std::string(want));
        if (it == log.end()) {
            return false;
        }
        ++it;
    }
    return true;
}

bool hasWrite(const std::vector<std::string> &log)
{
    return std::any_of(log.begin(), log.end(), [](const std::string &e) { return e[0] == 'w'; });
}

// Brings the rig through the start-up self-test.
void startUp(StageRig &r)
{
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, kStartupMs)) << toString(r.fault());
}

// Asks for power and waits for the relay.
void powerUp(StageRig &r, uint8_t firstBit = 0)
{
    r.stage.requestPower(true, firstBit);
    ASSERT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 5000)) << toString(r.fault());
}

// What a single fault can still hold on after the keep-alive stopped.
enum Stuck : uint8_t { kNothingStuck = 0, kDriveStuck = 1, kRelayStuck = 2 };

void expectSafe(StageRig &r, uint8_t stuck = kNothingStuck)
{
    // Keep-alive stopped; within the detectors' hold-up the board is dead,
    // except the part the fault holds on. No valve has power either way
    // (hardware design 8.5): no valve bit is driven, or the relay is open.
    EXPECT_EQ(r.guard.request(), KeepAliveRequest::Off);
    r.run(1500);
    EXPECT_EQ(r.board.driveEnabled(), (stuck & kDriveStuck) != 0);
    EXPECT_EQ(r.board.relayClosed(), (stuck & kRelayStuck) != 0);
    EXPECT_EQ(r.board.valvesLow(), 0);
    EXPECT_EQ(r.board.currentMa(), 0);
}

} // namespace

// ---- start-up -----------------------------------------------------------------------------

TEST(OutputStage, StartupSelfTestPassesWithNoValveAndNoRelay)
{
    StageRig r;
    EXPECT_EQ(r.phase(), StagePhase::NoAc); // before the first tick
    startUp(r);
    const OutputStageStatus s = r.stage.status();
    EXPECT_EQ(s.fault, HwCheck::None);
    EXPECT_EQ(s.zonesFitted, 8);
    EXPECT_EQ(s.selfTestsPassed, 1u);
    EXPECT_EQ(r.board.maxValvesLow, 0);
    EXPECT_FALSE(r.board.relayClosed());
    EXPECT_EQ(r.guard.request(), KeepAliveRequest::Off);
    // Section 19.7 values: 4a reads, 4b outputs, 4c configuration, then 8.
    EXPECT_TRUE(contains(r.board.log, {"probe", "r00", "r01", "w02=df", "r02", "w03=ff", "r03", "w06=c0", "r06",
                                       "w07=f0", "r07", "r04", "r05", "w02=df", "w06=ff", "w07=ff", "probe"}));
    // The stop test timed both detectors from the last edge.
    EXPECT_GE(s.releaseDriveMs, 300);
    EXPECT_LE(s.releaseDriveMs, 1000);
    EXPECT_GE(s.releaseRelayMs, 80);
    EXPECT_LE(s.releaseRelayMs, 1000);
    EXPECT_GT(s.enableDriveMs, 0);
    EXPECT_LE(s.enableDriveMs, 150);
}

TEST(OutputStage, FourZoneBuildLeavesPort1AsInputs)
{
    StageRig r;
    r.board.eightZone = false;
    startUp(r);
    EXPECT_EQ(r.stage.status().zonesFitted, 4);
    EXPECT_TRUE(contains(r.board.log, {"w06=c0", "w07=ff"}));
    powerUp(r);
    r.stage.setZone(5, true); // not fitted: ignored
    r.run(100);
    EXPECT_EQ(r.board.valvesLow(), 0);
    EXPECT_EQ(r.fault(), HwCheck::None);
}

TEST(OutputStage, NoSelfTestAndNoRunWithout24Vac)
{
    StageRig r;
    r.board.acPresent = false;
    r.run(5000);
    EXPECT_EQ(r.phase(), StagePhase::NoAc);
    EXPECT_TRUE(r.board.log.empty()); // the expander was not even addressed
    r.stage.requestPower(true, 0);
    r.run(3000);
    EXPECT_FALSE(r.stage.powerReady());
    r.board.acPresent = true;
    startUp(r);
    EXPECT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 5000));
}

TEST(OutputStage, KeepAliveLowTwoSecondsBeforeAnyTest)
{
    StageRig r;
    r.run(1500);
    EXPECT_TRUE(r.board.log.empty()); // step 0 still waiting
    EXPECT_FALSE(r.board.kaHigh());
}

// ---- a run ------------------------------------------------------------------------------------

TEST(OutputStage, RunSequenceRelayLeadsAndTrailsTheValves)
{
    StageRig r;
    startUp(r);
    r.board.log.clear();
    r.stage.requestPower(true, 2);
    uint64_t relayClosedAt = 0;
    while (!r.stage.powerReady()) {
        r.run(5);
        ASSERT_LT(r.clock.mono, 20000u) << toString(r.fault());
        if (r.board.relayClosed() && relayClosedAt == 0) {
            relayClosedAt = r.clock.mono;
        }
    }
    // Weld check on the first output (zone 3: port 0 bit 2) with the relay open.
    EXPECT_TRUE(contains(r.board.log, {"w02=db", "r02", "w02=df", "r02", "w02=ff", "r02", "r00"}));
    r.stage.setZone(2, true);
    EXPECT_TRUE(r.board.valveDriven(2));
    EXPECT_GE(r.clock.mono - relayClosedAt, hw::OutputStage::kRelaySettleMs);
    // Every tick scrubs all six registers.
    r.board.log.clear();
    r.run(50);
    EXPECT_TRUE(contains(r.board.log, {"r00", "r02", "r06", "r01", "r03", "r07"}));

    r.stage.setZone(2, false);
    const uint64_t offAt = r.clock.mono;
    r.stage.requestPower(false, 0xFF);
    uint64_t relayOpenAt = 0;
    while (r.phase() != StagePhase::Idle) {
        r.run(5);
        ASSERT_LT(r.clock.mono - offAt, 10000u) << toString(r.fault());
        if (!r.board.relayClosed() && relayOpenAt == 0) {
            relayOpenAt = r.clock.mono;
        }
    }
    EXPECT_GE(relayOpenAt - offAt, 40u);
    EXPECT_EQ(r.fault(), HwCheck::None);
    EXPECT_EQ(r.stage.status().selfTestsPassed, 2u);
}

TEST(OutputStage, BackToBackZonesKeepThePowerWithoutRetesting)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    const uint32_t passed = r.stage.status().selfTestsPassed;
    r.stage.setZone(0, true);
    r.run(200);
    r.stage.setZone(0, false);
    r.stage.requestPower(false, 0xFF);
    r.run(1000); // shorter than the linger time
    EXPECT_TRUE(r.stage.powerReady());
    r.stage.requestPower(true, 1);
    r.stage.setZone(1, true);
    EXPECT_TRUE(r.board.valveDriven(1));
    EXPECT_EQ(r.stage.status().selfTestsPassed, passed);
}

TEST(OutputStage, NeverMoreThanThreeValveBitsLow)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setMaster(true);
    r.stage.setZone(0, true);
    r.stage.setZone(1, true);
    EXPECT_EQ(r.board.valvesLow(), 3);
    r.stage.setZone(2, true);
    EXPECT_EQ(r.fault(), HwCheck::TooManyOutputs);
    EXPECT_LE(r.board.maxValvesLow, 3);
    expectSafe(r);
}

TEST(OutputStage, OutputsOnlySwitchWhileReady)
{
    StageRig r;
    startUp(r);
    const std::size_t before = r.board.log.size();
    r.stage.setZone(0, true);
    r.stage.setMaster(true);
    EXPECT_EQ(r.board.log.size(), before); // no bus traffic: the expander is in reset
}

TEST(OutputStage, WeldCheckUsesTheMasterWhenItOpensFirst)
{
    StageRig r;
    startUp(r);
    r.board.log.clear();
    powerUp(r, ZoneDriver::kMasterBit);
    EXPECT_TRUE(contains(r.board.log, {"w02=cf", "r02", "w02=df"})); // port 0 bit 4 low, then high
}

// ---- self-test failures (19.7) ------------------------------------------------------------------

struct FailCase {
    const char *name;
    void (*inject)(FakeBoard &);
    HwCheck expected;
    bool duringRun; // injected after start-up, found by the next run's test
    uint8_t stuck = kNothingStuck;
    bool endRun = false; // found by the stop test after the run
};

class SelfTestFailure : public ::testing::TestWithParam<FailCase> {};

TEST_P(SelfTestFailure, EndsInTheFaultStateWithOutputsOff)
{
    const FailCase &c = GetParam();
    StageRig r;
    if (c.duringRun) {
        startUp(r);
        c.inject(r.board);
        r.stage.requestPower(true, 0);
        if (c.endRun) {
            ASSERT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 6000));
            r.stage.requestPower(false, 0xFF);
        }
    } else {
        c.inject(r.board);
    }
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 12000)) << toString(r.phase());
    EXPECT_EQ(r.fault(), c.expected) << toString(r.fault());
    EXPECT_FALSE(r.stage.powerReady());
    expectSafe(r, c.stuck);
    // Refuses to run until a person acknowledges.
    r.stage.requestPower(true, 0);
    r.run(5000);
    EXPECT_FALSE(r.stage.powerReady());
    EXPECT_EQ(r.phase(), StagePhase::Fault);
}

const FailCase kFailCases[] = {
    {"IdleDriveStuckOn", [](FakeBoard &b) { b.a.stuckOn = true; }, HwCheck::IdleDriveEnabled, false, kDriveStuck},
    {"IdleRelayStuckOn", [](FakeBoard &b) { b.b.stuckOn = true; }, HwCheck::IdleRelayOn, false},
    {"ExpanderIgnoresReset", [](FakeBoard &b) { b.expanderIgnoresReset = true; }, HwCheck::IdleExpanderAnswers, false},
    {"ShortedC22", [](FakeBoard &b) { b.a.onWhilePinHigh = true; }, HwCheck::PinHighDriveEnabled, false},
    {"ShortedC51", [](FakeBoard &b) { b.b.onWhilePinHigh = true; }, HwCheck::PinHighRelayOn, false},
    {"DriveNeverEnables", [](FakeBoard &b) { b.a.neverOn = true; }, HwCheck::StartDriveTimeout, false},
    {"DriveTooSlow", [](FakeBoard &b) { b.a.onMs = 300; }, HwCheck::StartDriveTimeout, false},
    {"RelayTooSlow", [](FakeBoard &b) { b.b.onMs = 300; }, HwCheck::StartRelayTimeout, false},
    {"DriveBleedOpen", [](FakeBoard &b) { b.a.holdMs = 1700; }, HwCheck::StopDriveRelease, false},
    {"DriveReleasesTooSoon", [](FakeBoard &b) { b.a.holdMs = 150; }, HwCheck::StopDriveRelease, false},
    {"RelayBleedOpen", [](FakeBoard &b) { b.b.holdMs = 1450; }, HwCheck::StopRelayRelease, false},
    {"InputsWrong", [](FakeBoard &b) { b.corruptReadOf = 0x00; }, HwCheck::ExpanderInputs, false},
    {"OutputsDoNotReadBack", [](FakeBoard &b) { b.corruptReadOf = 0x02; }, HwCheck::ExpanderOutputs, false},
    {"ConfigDoesNotReadBack", [](FakeBoard &b) { b.corruptReadOf = 0x06; }, HwCheck::ExpanderConfig, false},
    {"PolarityNotZero", [](FakeBoard &b) { b.corruptReadOf = 0x04; }, HwCheck::ExpanderConfig, false},
    {"BusStuck", [](FakeBoard &b) { b.busStuck = true; }, HwCheck::BusError, false},
    {"WeldedRelay", [](FakeBoard &b) { b.relayWelded = true; }, HwCheck::RelayWelded, true, kRelayStuck},
    {"ShortedOutputDevice", [](FakeBoard &b) { b.shortedOutputMa = 400; }, HwCheck::OutputShorted, true},
    {"DriveStuckOnAfterRun", [](FakeBoard &b) { b.a.holdMs = 5000; }, HwCheck::StopDriveRelease, true, kDriveStuck,
     true},
    {"RelayStuckOnAfterRun", [](FakeBoard &b) { b.b.holdMs = 5000; }, HwCheck::StopRelayRelease, true, kNothingStuck,
     true},
};

INSTANTIATE_TEST_SUITE_P(Checks, SelfTestFailure, ::testing::ValuesIn(kFailCases),
                         [](const ::testing::TestParamInfo<FailCase> &i) { return std::string(i.param.name); });

TEST(OutputStage, DetectorStuckOnFaultKeepsEveryPinAnInputEveryTick)
{
    StageRig r;
    startUp(r);
    r.board.a.stuckOn = true; // detector A now holds DRV_EN: the expander answers
    r.stage.requestPower(true, 0);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 8000));
    EXPECT_EQ(r.fault(), HwCheck::IdleDriveEnabled);
    // Something writes the expander behind the firmware's back: the next
    // tick puts every pin back to input.
    r.board.cfg = {0x00, 0x00};
    r.run(50);
    EXPECT_EQ(r.board.cfg[0], 0xFF);
    EXPECT_EQ(r.board.cfg[1], 0xFF);
    const int writes = r.board.writes;
    r.run(500);
    EXPECT_GE(r.board.writes - writes, 18); // two per tick
}

// ---- during a run (step 7) -------------------------------------------------------------------------

TEST(OutputStage, MissingAcknowledgeDuringARunStopsTheKeepAlive)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true);
    r.board.nackNext = 1;
    r.run(100);
    EXPECT_EQ(r.fault(), HwCheck::RunReadback);
    expectSafe(r);
}

TEST(OutputStage, StuckBusDuringARunIsRecoveredAndStopsTheKeepAlive)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true);
    r.board.busStuck = true;
    r.run(50);
    EXPECT_EQ(r.fault(), HwCheck::BusError);
    EXPECT_GE(r.board.recovers, 1);
    // Bounded: in the fault state a tick makes at most three transactions
    // (probe, and the two configuration writes while the expander answers).
    r.board.busStuck = true;
    const std::size_t before = r.board.log.size();
    r.run(500);
    EXPECT_LE(r.board.log.size() - before, 30u);
    expectSafe(r);
}

TEST(OutputStage, WrongReadBackDuringARunIsAFault)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true);
    r.board.out[0] ^= 0x02; // zone 2 switched on by a glitch
    r.run(50);
    EXPECT_EQ(r.fault(), HwCheck::RunReadback);
    expectSafe(r);
}

TEST(OutputStage, ShortedOutputWithRelayClosedAndNoValveOn)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true);
    r.run(300);
    r.stage.setZone(0, false);
    r.board.shortedOutputMa = 500; // rule 6: current with no valve bit low
    r.run(400);
    EXPECT_EQ(r.fault(), HwCheck::OutputShorted);
    expectSafe(r);
}

TEST(OutputStage, ValveCurrentWhileAValveIsOnIsNotRule6)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true);
    r.run(1000);
    EXPECT_EQ(r.fault(), HwCheck::None);
    r.stage.setZone(0, false);
    r.run(300); // the window that saw the valve current ended before
    EXPECT_EQ(r.fault(), HwCheck::None);
}

TEST(OutputStage, SensorSupplyFaultIsReportedNotAStop)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true);
    r.board.sensorFault = true;
    r.run(100);
    EXPECT_TRUE(r.stage.status().sensorSupplyFault);
    EXPECT_TRUE(r.stage.powerReady());
    EXPECT_EQ(r.fault(), HwCheck::None);
}

TEST(OutputStage, AcLostDuringARunStopsWithoutATest)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true);
    r.board.acPresent = false;
    r.run(50);
    EXPECT_EQ(r.phase(), StagePhase::NoAc);
    EXPECT_EQ(r.fault(), HwCheck::None);
    EXPECT_EQ(r.guard.request(), KeepAliveRequest::Off);
}

// ---- over-current latch (19.8) --------------------------------------------------------------------

TEST(OutputStage, TripHoldsTheKeepAliveLowUntilTheLatchClears)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true);
    r.run(200);
    r.board.trip();
    r.run(50);
    EXPECT_EQ(r.stage.status().tripSeq, 1u);
    EXPECT_EQ(r.phase(), StagePhase::TripClear);
    EXPECT_EQ(r.guard.request(), KeepAliveRequest::Off);
    EXPECT_EQ(r.fault(), HwCheck::None); // a trip is not a self-test fault
    r.run(1400);
    EXPECT_EQ(r.phase(), StagePhase::TripClear); // 1.5 s at least
    EXPECT_TRUE(r.runUntil([&] { return r.phase() != StagePhase::TripClear; }, 1000));
    EXPECT_FALSE(r.board.latched);
    // The next run starts from step 0, self-test included.
    EXPECT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 6000));
}

TEST(OutputStage, LatchThatDoesNotClearIsAFault)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.board.latchStuck = true;
    r.board.trip();
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 4000));
    EXPECT_EQ(r.fault(), HwCheck::LatchStuck);
}

TEST(OutputStage, DriveLostWithoutTripIsADetectorFault)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.board.a.neverOn = true;
    r.board.a.on = false; // detector A drops out while the keep-alive runs
    r.run(100);
    EXPECT_EQ(r.fault(), HwCheck::RunDriveLost);
    EXPECT_TRUE(hwCheckNeedsRepair(HwCheck::StopDriveRelease));
    EXPECT_EQ(hwCheckStep(HwCheck::RunDriveLost), 7);
}

// ---- polling between runs (19.6) ----------------------------------------------------------------------

TEST(OutputStage, PollingCycleEveryTenMinutesTouchesNoOutput)
{
    hw::OutputStageConfig cfg;
    cfg.pollIntervalMs = 60'000;
    StageRig r(cfg);
    startUp(r);
    r.board.log.clear();
    r.board.sensorFault = true;
    r.run(55'000);
    EXPECT_TRUE(r.board.log.empty());
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Polling; }, 10'000));
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 5'000));
    EXPECT_FALSE(hasWrite(r.board.log));
    EXPECT_TRUE(contains(r.board.log, {"probe", "probe", "r00", "r01", "r01", "probe"}));
    EXPECT_TRUE(r.stage.status().sensorSupplyFault);
    EXPECT_EQ(r.board.maxValvesLow, 0);
    EXPECT_FALSE(r.board.relayClosed());
}

TEST(OutputStage, PollOnRequestOnlyWithTheSensorSupplyOn)
{
    StageRig r;
    startUp(r);
    r.sensorSupplyOn = false;
    r.stage.requestPoll();
    r.run(3000);
    EXPECT_EQ(r.stage.status().selfTestsPassed, 1u);
    r.sensorSupplyOn = true;
    r.run(4000);
    EXPECT_EQ(r.stage.status().selfTestsPassed, 2u);
}

// ---- acknowledging --------------------------------------------------------------------------------------

TEST(OutputStage, DetectorFaultNeedsTheRepairConfirmed)
{
    StageRig r;
    r.board.b.onWhilePinHigh = true;
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, kStartupMs));
    EXPECT_TRUE(hwCheckNeedsRepair(r.fault()));
    EXPECT_FALSE(r.stage.acknowledgeFault(false));
    EXPECT_EQ(r.phase(), StagePhase::Fault);
    r.board.b.onWhilePinHigh = false; // repaired
    EXPECT_TRUE(r.stage.acknowledgeFault(true));
    // The start-up test runs again.
    EXPECT_TRUE(r.runUntil([&] { return r.stage.status().selfTestsPassed == 1; }, kStartupMs));
    EXPECT_EQ(r.stage.status().zonesFitted, 8);
}

TEST(OutputStage, ExpanderFaultTakesOnePress)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.board.nackNext = 1;
    r.stage.setZone(0, true);
    r.run(100);
    ASSERT_EQ(r.fault(), HwCheck::RunReadback);
    EXPECT_FALSE(hwCheckNeedsRepair(r.fault()));
    EXPECT_TRUE(r.stage.acknowledgeFault(false));
    EXPECT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 6000));
}

TEST(OutputStage, FaultFromBeforeARestartIsRestored)
{
    StageRig r;
    r.stage.restoreFault(HwCheck::StopRelayRelease);
    r.run(6000);
    EXPECT_EQ(r.phase(), StagePhase::Fault);
    EXPECT_EQ(r.fault(), HwCheck::StopRelayRelease);
    EXPECT_FALSE(r.board.kaHigh());
}

TEST(OutputStage, TrippedGuardHaltsWithoutBusTraffic)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.guard.trip();
    r.stage.emergencyAllOff();
    const std::size_t before = r.board.log.size();
    r.run(1500);
    EXPECT_EQ(r.phase(), StagePhase::Halted);
    EXPECT_EQ(r.board.log.size(), before);
    EXPECT_FALSE(r.board.driveEnabled());
    EXPECT_FALSE(r.board.relayClosed());
}

TEST(OutputStage, LayoutGivesTheSection19Values)
{
    const hw::ExpanderLayout l = revALayout();
    EXPECT_EQ(l.idleInputExpected(0), 0x1F);
    EXPECT_EQ(l.idleInputExpected(1) | 0x10, 0x3F); // 4-zone build: variant high
    EXPECT_EQ(l.idleInputExpected(1) & l.idleInputChecked(1), 0x0F);
    EXPECT_EQ(l.outputsOff(0), 0xDF);
    EXPECT_EQ(l.outputsOff(1), 0xFF);
    EXPECT_EQ(l.configRun(0, 8), 0xC0);
    EXPECT_EQ(l.configRun(1, 8), 0xF0);
    EXPECT_EQ(l.configRun(1, 4), 0xFF);
}

TEST(OutputStage, StartupTogglesLongEnoughBeforeTheStopTest)
{
    StageRig r;
    uint64_t runFrom = 0;
    uint64_t runTo = 0;
    while (r.phase() != StagePhase::Idle && r.clock.mono < kStartupMs) {
        const bool before = r.guard.request() == KeepAliveRequest::Run;
        r.run(5);
        const bool after = r.guard.request() == KeepAliveRequest::Run;
        if (!before && after) {
            runFrom = r.clock.mono;
        } else if (before && !after) {
            runTo = r.clock.mono;
        }
    }
    ASSERT_EQ(r.phase(), StagePhase::Idle);
    EXPECT_GE(runTo - runFrom, hw::OutputStage::kMinToggleBeforeStopMs);
}
