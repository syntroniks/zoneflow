// SPDX-License-Identifier: Apache-2.0
// The self-test as hardware review A2 left it (hardware design 19.1, 19.4 and
// 19.7): the float test (2b), the gate bleed check (5b), the read-back of
// V_DRV on GPIO42, the analogue read of GPIO7, the limits of step 9 with a
// value stored per board, commissioning, the toggling before a stop test, the
// keep-alive phase rule and rule 16.
//
// The second half injects into the simulated board the faults that review A2
// appendix A lists as found by a step of the self-test, and checks that the
// step the review names is the one that finds them. The simulated board is a
// model of the review's results at the level firmware can observe, not of the
// circuit: each row maps one fault to what the review says it does to the
// read-backs, the expander and the current.
#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>

#include "fake_board.hpp"
#include "zf/fault_text.hpp"
#include "zf/hw/reference_store.hpp"

using namespace zf;
using namespace zf::test;

namespace {

constexpr uint64_t kStartupMs = 11000; // 2.5 s low, 2.1 s pin high, 0.6 s float, 1 s toggling, up to 3 s stop test

bool contains(const std::vector<std::string> &log, std::initializer_list<const char *> seq)
{
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

void startUp(StageRig &r)
{
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, kStartupMs)) << toString(r.fault());
}

void powerUp(StageRig &r, uint8_t firstBit = 0)
{
    r.stage.requestPower(true, firstBit);
    ASSERT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 9000)) << toString(r.fault());
}

// One run of zone 1 for `ms`, then power off and the stop test.
void runZone(StageRig &r, uint64_t ms = 300)
{
    powerUp(r);
    r.stage.setZone(0, true);
    r.run(ms);
    r.stage.setZone(0, false);
    r.stage.requestPower(false, 0xFF);
}

} // namespace

// ---- the limits ---------------------------------------------------------------------------------

TEST(SelfTestA2, LimitsAreTheRevisedSection19)
{
    using S = hw::OutputStage;
    EXPECT_EQ(S::kKeepAliveLowBeforeStartMs, 2500u); // step 0
    EXPECT_EQ(S::kFloatTestMs, 500u);                // step 2b
    EXPECT_EQ(S::kDriveReleaseMinMs, 150u);          // step 9
    EXPECT_EQ(S::kDriveReleaseMaxMs, 1000u);
    EXPECT_EQ(S::kRelayReleaseMinMs, 50u);
    EXPECT_EQ(S::kRelayReleaseMaxMs, 1600u);
    // Hardware answers A2 (issue #41): the band 0.40 to 1.70, the hold of
    // step 2 2.0 s, the learning limits tA 0.33 to 0.85 s and tB 0.12 to
    // 1.25 s, the agreement within 1.25.
    EXPECT_EQ(S::kBandLowPercent, 40u);
    EXPECT_EQ(S::kBandHighPercent, 170u);
    EXPECT_EQ(S::kPinHighHoldMs, 2000u);
    EXPECT_EQ(S::kLearnDriveMinMs, 330u);
    EXPECT_EQ(S::kLearnDriveMaxMs, 850u);
    EXPECT_EQ(S::kLearnRelayMinMs, 120u);
    EXPECT_EQ(S::kLearnRelayMaxMs, 1250u);
    EXPECT_EQ(S::kLearnMaxSpreadPercent, 125u);
    // 19.10: VIN_MON and the rest levels.
    EXPECT_EQ(S::kVinMinMv, 600);
    EXPECT_EQ(S::kVinMaxMv, 1500);
    EXPECT_EQ(S::kVinAbsentMv, 100);
    EXPECT_EQ(S::kRectifierFaultMs, 2000u);
    EXPECT_EQ(S::kRestLoMinMv, 700);
    EXPECT_EQ(S::kRestLoMaxMv, 880);
    EXPECT_EQ(S::kRestHiMinMv, 680);
    EXPECT_EQ(S::kRestHiMaxMv, 900);
    EXPECT_EQ(S::kRestOverVoltageMv, 900);
    EXPECT_EQ(S::kCommissionTempMinC, 25.0f);
    EXPECT_EQ(S::kCommissionTempMaxC, 50.0f);
    EXPECT_GE(S::kMinToggleBeforeStopMs, 300u);
    // Review A2 m10: the relay detector alone lets go within 1.45 s with the
    // pin low and 2.19 s with it high. No timeout may assume one second.
    EXPECT_GT(S::kRelayReleaseMaxMs, 1450u);
    EXPECT_GT(S::kStopTestMaxMs, 2190u);
    EXPECT_GT(S::kKeepAliveLowBeforeStartMs, 2190u);
}

TEST(SelfTestA2, StepNamesCarryTheLetters)
{
    EXPECT_STREQ(hwCheckStep(HwCheck::FloatPinHigh), "2b");
    EXPECT_STREQ(hwCheckStep(HwCheck::GateBleed), "5b");
    EXPECT_STREQ(hwCheckStep(HwCheck::ExpanderInputs), "4a");
    EXPECT_STREQ(hwCheckStep(HwCheck::ExpanderConfig), "4c");
    EXPECT_STREQ(hwCheckStep(HwCheck::StopRelayBand), "9");
    EXPECT_STREQ(hwCheckStep(HwCheck::LatchStuck), "");
    EXPECT_STREQ(hwCheckStep(HwCheck::KeepAlivePinRouted), "");
    EXPECT_STREQ(hwCheckStep(HwCheck::RectifierFault), "0");
    EXPECT_STREQ(hwCheckStep(HwCheck::SupplyOverVoltage), "1");
    EXPECT_STREQ(hwCheckStep(HwCheck::LatchInputHeld), "3");
    // 19.7, the fault state: steps 1, 2, 2b, 5b and 9 need a confirmed
    // repair, and so do the rectifier fault of 19.10, the over-voltage of
    // rule 18 and a held latch input of 19.8.
    for (unsigned i = 1; i < static_cast<unsigned>(HwCheck::Count); ++i) {
        const auto c = static_cast<HwCheck>(i);
        const std::string step = hwCheckStep(c);
        const bool repair = step == "1" || step == "2" || step == "2b" || step == "5b" || step == "9" ||
                            c == HwCheck::RectifierFault || c == HwCheck::LatchInputHeld;
        EXPECT_EQ(hwCheckNeedsRepair(c), repair) << toString(c);
        EXPECT_NE(faultCodeFor(c), FaultCode::None) << toString(c);
        EXPECT_STRNE(toString(c), "?");
    }
    EXPECT_EQ(faultCodeFor(HwCheck::FloatDriveEnabled), FaultCode::KeepAliveBridged);
    EXPECT_EQ(faultCodeFor(HwCheck::GateBleed), FaultCode::RelayGateBleed);
    EXPECT_EQ(faultCodeFor(HwCheck::IdleDriveSupplyOn), FaultCode::KeepAliveDetector);
}

// ---- the keep-alive waveform (19.4) ---------------------------------------------------------------

TEST(SelfTestA2, PhaseRuleHoldsForTheTimerConfiguration)
{
    EXPECT_TRUE(keepalive::periodMeetsContract(5000)); // the firmware's 5 ms
    EXPECT_TRUE(keepalive::periodMeetsContract(2500));
    EXPECT_FALSE(keepalive::periodMeetsContract(2000));  // a 1 ms or 2 ms phase: M6
    EXPECT_FALSE(keepalive::periodMeetsContract(25000)); // edges more than 20 ms apart
}

TEST(SelfTestA2, NoPhaseShorterThan2500UsThroughEveryProcedure)
{
    StageRig r;
    startUp(r);
    runZone(r);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000));
    r.stage.requestPoll();
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Polling; }, 5000));
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000));
    // Run, hold high, float, off: the pin changes only at period boundaries.
    EXPECT_GE(r.board.minPhaseMs, 5u);
}

// ---- step 2b, the float test ---------------------------------------------------------------------------

TEST(SelfTestA2, FloatTestRunsAfterThePinHighTestBeforeEveryRun)
{
    StageRig r;
    startUp(r);
    // 0.5 s, judged on the control tick that follows: at most one tick more.
    EXPECT_GE(r.board.floatMs, 500u);
    EXPECT_LE(r.board.floatMs, 560u);
    const uint64_t before = r.board.floatMs;
    // The display, the backlight and the PSRAM are asked to be busy from the
    // 100 ms before the pin floats until it is driven again (rule 17).
    r.stage.requestPower(true, 0);
    bool sawLoad = false;
    bool loadWhileFloating = true;
    while (!r.stage.powerReady() && r.clock.mono < 30000) {
        r.run(5);
        const bool load = r.stage.status().floatTest;
        sawLoad = sawLoad || load;
        if (r.board.pinIsInput() && !load) {
            loadWhileFloating = false;
        }
    }
    EXPECT_TRUE(sawLoad);
    EXPECT_TRUE(loadWhileFloating);
    EXPECT_GE(r.board.floatMs - before, 500u);
    EXPECT_FALSE(r.stage.status().floatTest);
    EXPECT_FALSE(r.board.pinIsInput());
}

TEST(SelfTestA2, FloatTestNotReadIsAFailure)
{
    StageRig r;
    r.board.floatNotRead = true;
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, kStartupMs));
    EXPECT_EQ(r.fault(), HwCheck::FloatPinHigh);
}

// ---- step 1 with V_DRV and the analogue read of GPIO7 ----------------------------------------------

TEST(SelfTestA2, VDrvHighWithNoAcknowledgeIsQ3)
{
    StageRig r;
    r.board.q3Shorted = true;
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, kStartupMs));
    EXPECT_EQ(r.fault(), HwCheck::IdleDriveSupplyOn);
    EXPECT_TRUE(hwCheckNeedsRepair(r.fault()));
}

TEST(SelfTestA2, Q2GateDrainFoundByTheAnalogueReadEvenWhenTheDigitalReadIsHigh)
{
    StageRig r;
    r.board.q2GateDrainMv = 870; // 0.6 x 1.45 V: between the logic thresholds, read as high
    ASSERT_TRUE(r.board.relayReadbackHigh());
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, kStartupMs));
    EXPECT_EQ(r.fault(), HwCheck::IdleRelayGateDrain);
}

TEST(SelfTestA2, WithoutCurrentReadingsTheDigitalReadAloneDecides)
{
    StageRig r;
    r.senseAvailable = false;
    r.board.q2GateDrainMv = 600; // reads low
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, kStartupMs));
    EXPECT_EQ(r.fault(), HwCheck::IdleRelayOn);
}

TEST(SelfTestA2, RegulatorFaultOnUsbIsTheIdleRelayCheck)
{
    // Rule 15: V_BUCK missing, mains present, logic on USB. GPIO7 reads low
    // at rest exactly as with Q2 stuck on; step 1 reports it and nothing runs.
    StageRig r;
    r.board.vBuck = false;
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, kStartupMs));
    EXPECT_EQ(r.fault(), HwCheck::IdleRelayOn);
    EXPECT_STREQ(hwCheckStep(r.fault()), "1");
    r.stage.requestPower(true, 0);
    r.run(5000);
    EXPECT_FALSE(r.stage.powerReady());
}

// ---- step 5b, the gate bleed check ----------------------------------------------------------------------

TEST(SelfTestA2, GateBleedRegisterSequence)
{
    StageRig r;
    startUp(r);
    r.board.log.clear();
    powerUp(r, 2);
    // Step 5 on zone 3 (port 0 bit 2); step 5b: RLY_ON set (0xff), 20 ms,
    // configuration 0 = 0xe0, 10 ms, zone 3 on, one window, zone 3 off,
    // RLY_ON cleared, configuration 0 = 0xc0; then step 6.
    EXPECT_TRUE(contains(r.board.log, {"w02=db", "r02", "r00", "w02=df", "r02", "w02=ff", "r02", "w06=e0", "r06",
                                       "w02=fb", "r02", "w02=ff", "r02", "w02=df", "r02", "w06=c0", "r06", "w02=ff"}));
    EXPECT_EQ(r.fault(), HwCheck::None);
    EXPECT_FALSE(r.board.q1On() && !r.stage.status().relayClosed);
}

TEST(SelfTestA2, GateBleedFindsAnOpenR12)
{
    StageRig r;
    r.board.r12Open = true;
    startUp(r); // the start-up test never sets RLY_ON
    r.stage.requestPower(true, 0);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 9000));
    EXPECT_EQ(r.fault(), HwCheck::GateBleed);
    EXPECT_STREQ(hwCheckStep(r.fault()), "5b");
    EXPECT_TRUE(hwCheckNeedsRepair(r.fault()));
    EXPECT_EQ(faultCodeFor(r.fault()), FaultCode::RelayGateBleed);
}

TEST(SelfTestA2, WeldCheckAloneDoesNotFindAnOpenR12)
{
    // Review A2 m3: at the end of a normal run firmware drives RLY_ON low
    // before it releases the pin, which discharges the gate with or without
    // R12. The run ends cleanly; only 5b finds it.
    StageRig r;
    startUp(r);
    runZone(r);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000));
    r.board.r12Open = true;
    EXPECT_FALSE(r.board.q1On());
}

// ---- step 9: limits, band, commissioning ------------------------------------------------------------------

struct CornerCase {
    const char *name;
    DetectorCorner corner;
    DetectorReference reference; // what commissioning at 27 C stores for this board
};

class HealthyCorner : public ::testing::TestWithParam<CornerCase> {};

TEST_P(HealthyCorner, PassesTheStopTestBeforeAndAfterARun)
{
    // Review A2 4.5: the limits as first merged failed 16 and 18 of 192
    // healthy corner runs. With the revised limits and a reference learned
    // at 27 C (the corner value divided by the review's temperature factor)
    // every corner passes.
    const CornerCase &c = GetParam();
    StageRig r;
    r.reference.stored = c.reference;
    r.board.setCorner(c.corner);
    startUp(r);
    runZone(r);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000)) << toString(r.fault());
    EXPECT_EQ(r.fault(), HwCheck::None) << toString(r.fault());
    EXPECT_EQ(r.stage.status().selfTestsPassed, 2u);
    // And the next run starts: step 0 waited out the slowest release.
    powerUp(r);
}

const CornerCase kCorners[] = {
    {"Typical", DetectorCorner::Typical, {670, 570}},
    {"LatestRelease70C", DetectorCorner::LatestRelease, {805, 1050}},    // 830 / 1.03, 1450 / 1.38
    {"EarliestRelease70C", DetectorCorner::EarliestRelease, {375, 182}}, // 210 / 0.56, 100 / 0.55
    {"SlowChargeMinus20C", DetectorCorner::SlowCharge, {680, 200}},
};
INSTANTIATE_TEST_SUITE_P(Corners, HealthyCorner, ::testing::ValuesIn(kCorners),
                         [](const ::testing::TestParamInfo<CornerCase> &i) { return std::string(i.param.name); });

TEST(SelfTestA2, OneSecondOfTogglingBeforeEveryStopTest)
{
    StageRig r;
    startUp(r);
    EXPECT_GE(r.stage.toggledBeforeLastStopMs(), 1000u); // start-up
    r.stage.requestPoll();
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Polling; }, 5000));
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000));
    EXPECT_GE(r.stage.toggledBeforeLastStopMs(), 1000u); // polling cycle
    // A run cancelled as soon as it was asked for still toggles 1 s.
    r.stage.requestPower(true, 0);
    ASSERT_TRUE(r.runUntil([&] { return r.board.kaHigh(); }, 9000));
    r.stage.requestPower(false, 0xFF);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000));
    EXPECT_EQ(r.fault(), HwCheck::None);
    EXPECT_GE(r.stage.toggledBeforeLastStopMs(), 1000u);
}

TEST(SelfTestA2, ShortToggleAtTheSlowCornerWouldFailTheBand)
{
    // Why 19.7 asks for 0.3 s: at the corner that charges slowest the relay
    // detector holds 40 % of its time after 0.1 s of toggling, the lower
    // edge of the band, and less after a shorter toggle: the band would call
    // a healthy board faulty. The firmware's 1 s gives all of it.
    FakeDetector b;
    b.holdMs = 200;
    b.warmup = Warmup::SlowRelay;
    b.toggledMs = 100;
    EXPECT_LE(b.hold(false) * 100, 200u * hw::OutputStage::kBandLowPercent);
    b.toggledMs = 90;
    EXPECT_LT(b.hold(false) * 100, 200u * hw::OutputStage::kBandLowPercent);
    b.toggledMs = 300;
    EXPECT_GE(b.hold(false), 190u);
    b.toggledMs = hw::OutputStage::kMinToggleBeforeStopMs;
    EXPECT_EQ(b.hold(false), 200u);
}

TEST(SelfTestA2, NewBoardLearnsItsReleaseTimesAtCommissioning)
{
    StageRig r({}, false);
    EXPECT_FALSE(r.stage.status().commissioned);
    startUp(r);
    // The start-up test is the first of five; four commissioning cycles
    // follow at once, with nothing written to the expander's outputs.
    r.board.log.clear();
    ASSERT_TRUE(r.runUntil([&] { return r.stage.status().commissioned; }, 30000));
    EXPECT_FALSE(std::any_of(r.board.log.begin(), r.board.log.end(), [](const std::string &e) { return e[0] == 'w'; }));
    const OutputStageStatus s = r.stage.status();
    EXPECT_NEAR(s.reference.driveMs, 670, 10);
    EXPECT_NEAR(s.reference.relayMs, 570, 10);
    EXPECT_EQ(r.reference.saves, 1);
    EXPECT_TRUE(r.reference.have);
    EXPECT_EQ(s.selfTestsPassed, 5u);
    // Learned once: the next idle stretch runs no more commissioning cycles.
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Idle; }, 9000));
    const uint32_t passed = r.stage.status().selfTestsPassed;
    r.run(20000);
    EXPECT_EQ(r.stage.status().selfTestsPassed, passed);
}

TEST(SelfTestA2, StoredReferenceIsUsedAfterARestart)
{
    StageRig r;
    r.reference.stored = {700, 600};
    startUp(r);
    EXPECT_TRUE(r.stage.status().commissioned);
    EXPECT_EQ(r.stage.status().reference.driveMs, 700);
    EXPECT_EQ(r.reference.saves, 0);
}

TEST(SelfTestA2, ImplausibleStoredReferenceIsIgnored)
{
    // A stored value outside what a healthy board gives (a corrupted record,
    // or one written by a faulty board under an older firmware) is not used.
    StageRig r;
    r.reference.stored = {670, 1300};
    r.stage.tick(r.input());
    EXPECT_FALSE(r.stage.status().commissioned);
}

TEST(SelfTestA2, FaultyBoardCannotTeachItselfTheFault)
{
    // An open R30 or R91 on a typical board: tA 1.34 s, doubled. The
    // learning limits refuse every board with a bleed resistor of detector
    // A open (hardware answers A2 2.4: 0 of 600). The start-up test, which
    // has no stored value to compare with, applies the absolute limits:
    // 1.34 s is above them as well, so take the slowest a faulty board gave
    // in the simulation that the absolute limit still passes, 0.95 s.
    StageRig r({}, false);
    r.board.a.holdMs = 950;
    startUp(r);
    r.run(80000);
    EXPECT_FALSE(r.stage.status().commissioned);
    EXPECT_EQ(r.reference.saves, 0);
    EXPECT_EQ(r.fault(), HwCheck::None); // the absolute limits only: reported as not commissioned
    // It stopped trying after the configured number of cycles.
    EXPECT_LE(r.stage.status().selfTestsPassed, 1u + hw::OutputStageConfig{}.maxCommissionCycles);
}

TEST(SelfTestA2, OpenBleedOnDetectorBCanCommissionOnATypicalBoard)
{
    // The limit of what firmware can do, accepted by the hardware answers
    // (2.4): an open R14 or R90 doubles tB, 0.57 to about 1.14 s on a
    // typical board, which is inside the learning limit of 1.25 s. About
    // three boards in four store it; an ohmmeter at bring-up finds it (risk
    // 34). The board stays no slower than the slowest healthy board.
    StageRig r({}, false);
    r.board.b.holdMs = 1140;
    startUp(r);
    ASSERT_TRUE(r.runUntil([&] { return r.stage.status().commissioned; }, 40000));
    EXPECT_NEAR(r.stage.status().reference.relayMs, 1140, 10);
    EXPECT_EQ(r.fault(), HwCheck::None);
}

TEST(SelfTestA2, HotBoardDoesNotCommission)
{
    // Commissioning wants room temperature. The early corner at 70 C gives a
    // tB of 0.10 s, below what is accepted; the same board at 27 C gives
    // about 0.18 s and would be learned.
    StageRig r({}, false);
    r.board.setCorner(DetectorCorner::EarliestRelease);
    startUp(r);
    r.run(60000);
    EXPECT_FALSE(r.stage.status().commissioned);
}

TEST(SelfTestA2, CommissioningGivesWayToARun)
{
    StageRig r({}, false);
    startUp(r);
    r.run(500); // a commissioning cycle has begun
    r.stage.requestPower(true, 0);
    ASSERT_TRUE(r.runUntil([&] { return r.stage.powerReady(); }, 12000));
    r.stage.requestPower(false, 0xFF);
    ASSERT_TRUE(r.runUntil([&] { return r.stage.status().commissioned; }, 40000));
}

TEST(SelfTestA2, BandFindsAnOpenBleedResistorAndARepairRelearns)
{
    StageRig r;
    startUp(r);
    r.board.b.holdMs = 1200; // R14 open
    runZone(r);
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 9000));
    EXPECT_EQ(r.fault(), HwCheck::StopRelayBand);
    EXPECT_FALSE(r.stage.acknowledgeFault(false));
    // Repaired with new capacitors: a healthy board with a longer time.
    r.board.b.holdMs = 800;
    EXPECT_TRUE(r.stage.acknowledgeFault(true));
    EXPECT_EQ(r.reference.clears, 1);
    EXPECT_FALSE(r.stage.status().commissioned);
    ASSERT_TRUE(r.runUntil([&] { return r.stage.status().commissioned; }, 40000));
    EXPECT_NEAR(r.stage.status().reference.relayMs, 800, 10);
    EXPECT_EQ(r.fault(), HwCheck::None);
}

TEST(SelfTestA2, OnePressFaultDoesNotRelearn)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.board.nackNext = 1;
    r.stage.setZone(0, true);
    r.run(100);
    ASSERT_EQ(r.fault(), HwCheck::RunReadback);
    EXPECT_TRUE(r.stage.acknowledgeFault(false));
    EXPECT_EQ(r.reference.clears, 0);
    EXPECT_TRUE(r.stage.status().commissioned);
}

TEST(SelfTestA2, ReferenceSurvivesInBlobStorageAndRejectsDamage)
{
    MemoryStorage nvs;
    hw::BlobReferenceStore store(nvs);
    DetectorReference r;
    EXPECT_FALSE(store.load(r));
    ASSERT_TRUE(store.save({672, 568}));
    ASSERT_TRUE(store.load(r));
    EXPECT_EQ(r.driveMs, 672);
    EXPECT_EQ(r.relayMs, 568);
    // A flipped bit, a short record or another version reads as not stored.
    std::string bad = nvs.data[hw::BlobReferenceStore::kKey];
    bad[7] ^= 0x01;
    nvs.data[hw::BlobReferenceStore::kKey] = bad;
    EXPECT_FALSE(store.load(r));
    nvs.data[hw::BlobReferenceStore::kKey] = hw::BlobReferenceStore::encode({672, 568}).substr(0, 10);
    EXPECT_FALSE(store.load(r));
    EXPECT_FALSE(hw::BlobReferenceStore::decode(hw::BlobReferenceStore::encode({0, 568}), r));
    ASSERT_TRUE(store.save({672, 568}));
    ASSERT_TRUE(store.clear());
    EXPECT_FALSE(store.load(r));
    // Not a configuration key: a factory reset (ConfigStore) leaves it.
    EXPECT_STRNE(hw::BlobReferenceStore::kKey, ConfigStore::kConfigKey);
    EXPECT_STRNE(hw::BlobReferenceStore::kKey, ConfigStore::kRuntimeKey);
}

// ---- rule 16 ------------------------------------------------------------------------------------------------

TEST(SelfTestA2, PeripheralOnTheKeepAlivePinStopsEverything)
{
    StageRig r;
    startUp(r);
    powerUp(r);
    r.stage.setZone(0, true);
    StageTickInput in = r.input();
    in.keepAlivePinPlain = false; // a UART on its default pins, for example
    r.stage.tick(in);
    EXPECT_EQ(r.fault(), HwCheck::KeepAlivePinRouted);
    EXPECT_EQ(faultCodeFor(r.fault()), FaultCode::KeepAliveBridged);
    EXPECT_EQ(r.guard.request(), KeepAliveRequest::Off);
    r.run(2500);
    EXPECT_EQ(r.board.valvesLow(), 0);
    EXPECT_FALSE(r.board.relayClosed());
}

// ---- review A2 appendix A: each fault found by the step the review names ----------------------------------

namespace {

enum class When : uint8_t {
    AtStart,    // there from power-up: "found at the next start"
    DuringARun, // appears while zone 1 runs: "found while the valve runs" (step 9 at the end)
};

struct ReviewFault {
    const char *row; // the fault as appendix A names it
    std::function<void(FakeBoard &)> inject;
    const char *step; // the step the review names
    HwCheck check;    // the firmware's check for it; None: any check of that step
    When when = When::AtStart;
};

void aNeverOn(FakeBoard &b) { b.a.neverOn = true; }
void aStuckOn(FakeBoard &b) { b.a.stuckOn = true; }
void bNeverOn(FakeBoard &b) { b.b.neverOn = true; }
void bStuckOn(FakeBoard &b) { b.b.stuckOn = true; }
void rbLow(FakeBoard &b) { b.relayReadbackLow = true; }
void drvEnHigh(FakeBoard &b) { b.expanderIgnoresReset = true; }
void drvEnLow(FakeBoard &b) { b.expanderNeverActive = true; }
void q3On(FakeBoard &b) { b.q3Shorted = true; }
void vDrvOff(FakeBoard &b) { b.vDrvNeverOn = true; }
void aOnPinHigh(FakeBoard &b) { b.a.onWhilePinHigh = true; }
void bOnPinHigh(FakeBoard &b) { b.b.onWhilePinHigh = true; }
void welded(FakeBoard &b) { b.relayWelded = true; }
void z1ReadsLow(FakeBoard &b) { b.pinStuckLow[0] |= 0x01; }
void z1ReadsHigh(FakeBoard &b) { b.pinStuckHigh[0] |= 0x01; }
void mvReadsLow(FakeBoard &b) { b.pinStuckLow[0] |= 0x10; }
void rlyOnHigh(FakeBoard &b) { b.pinStuckHigh[0] |= 0x20; }
void rlyOnLow(FakeBoard &b) { b.pinStuckLow[0] |= 0x20; }
std::function<void(FakeBoard &)> aHold(uint32_t ms)
{
    return [ms](FakeBoard &b) { b.a.holdMs = ms; };
}
std::function<void(FakeBoard &)> bHold(uint32_t ms)
{
    return [ms](FakeBoard &b) { b.b.holdMs = ms; };
}
std::function<void(FakeBoard &)> ka(KaNet n)
{
    return [n](FakeBoard &b) { b.kaNet = n; };
}

const HwCheck kAny = HwCheck::None;
const HwCheck kStartA = HwCheck::StartDriveTimeout;
const HwCheck kStartB = HwCheck::StartRelayTimeout;
const HwCheck kNoAck = HwCheck::StartNoAcknowledge;

const ReviewFault kReviewFaults[] = {
    // detector A
    {"R33 short", ka(KaNet::Disconnected), "3", kStartA},
    {"R29 open", aNeverOn, "3", kStartA},
    {"C22 open", aNeverOn, "3", kStartA},
    {"C22 short", aOnPinHigh, "2", HwCheck::PinHighDriveEnabled},
    {"D7 pin 1 A (GND) open", aNeverOn, "3", kStartA},
    {"D7 pin 2 K (KA_N2) open", aNeverOn, "3", kStartA},
    {"D7 pin 3 common (KA_N1) open", aNeverOn, "3", kStartA},
    {"D7 pins 1 A and 2 K bridged", aNeverOn, "3", kStartA},
    {"D7 pins 1 A and 3 common bridged", aNeverOn, "3", kStartA},
    {"D7 pins 2 K and 3 common bridged", aNeverOn, "3", kStartA},
    {"C23 open (valve pin Z1 does not read high as an input)", z1ReadsLow, "4a", HwCheck::ExpanderInputs},
    {"C23 short", aNeverOn, "3", kStartA},
    {"R30 open", aHold(1560), "9", HwCheck::StopDriveRelease},
    {"R30 short", aNeverOn, "3", kStartA},
    {"R91 open", aHold(1560), "9", HwCheck::StopDriveRelease},
    {"R91 short", aNeverOn, "3", kStartA},
    {"R31 open", aNeverOn, "3", kStartA},
    {"R32 short", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"C24 short", aNeverOn, "3", kStartA},
    {"R34 open", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"R34 short", aNeverOn, "3", kStartA},
    {"R35 open", aNeverOn, "3", kStartA},
    {"R35 short", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"R36 short", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    // logic
    {"U6 pin 1 1OUT (KA_OK) open", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"U6 pin 3 VCC (V5) open", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"U6 pin 4 2IN- (OC_CMP) open", aNeverOn, "3", kStartA},
    {"U6 pin 6 1IN- (KA_REF) open", aNeverOn, "3", kStartA},
    {"U6 pin 7 1IN+ (KA_INP) open", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"U6 output 1 (KA_OK) never pulls low", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"U6 output 1 (KA_OK) shorted low", aNeverOn, "3", kStartA},
    {"C19 short", [](FakeBoard &b) { b.a.neverOn = b.b.neverOn = true; }, "3", kAny},
    {"U7 pin 7 GND open", vDrvOff, "3", kStartA},
    {"U7 pin 9 3A (KA_OK) open, leaks high", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"U7 pin 11 4Y (DRV_EN) open", drvEnLow, "3", kNoAck},
    {"U7 pin 12 4A (DRV_EN_N) open, leaks low", drvEnHigh, "1", HwCheck::IdleExpanderAnswers},
    {"U7 pin 13 4B (DRV_EN_N) open, holds its last level", drvEnHigh, "1", HwCheck::IdleExpanderAnswers},
    {"U7 pins 9 3A and 10 3B bridged", aHold(5000), "9", HwCheck::StopDriveRelease},
    {"U7 pins 10 3B and 11 4Y bridged", drvEnHigh, "1", HwCheck::IdleExpanderAnswers},
    {"U7 pins 11 4Y and 12 4A bridged", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"U7 output 3 (DRV_EN_N) stuck low", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"U7 output 4 (DRV_EN) stuck high", drvEnHigh, "1", HwCheck::IdleExpanderAnswers},
    {"U7 output 4 (DRV_EN) stuck low", drvEnLow, "3", kNoAck},
    // LED supply switch: GPIO42 reads V_DRV since review A2
    {"Q3 pin G open, holds its last level", vDrvOff, "3", kStartA},
    {"Q3 pin G open, leaks high", vDrvOff, "3", kStartA},
    {"Q3 pin G open, leaks low", q3On, "1", HwCheck::IdleDriveSupplyOn},
    {"Q3 pin S (3V3) open", vDrvOff, "3", kStartA},
    {"Q3 pin D (V_DRV) open", vDrvOff, "3", kStartA},
    {"Q3 pins G and D bridged", q3On, "1", HwCheck::IdleDriveSupplyOn},
    {"Q3 pins S and D bridged", q3On, "1", HwCheck::IdleDriveSupplyOn},
    {"R26 short", vDrvOff, "3", kStartA},
    {"C21 short", vDrvOff, "3", kStartA},
    // read-back
    {"R92 open", vDrvOff, "3", kStartA},
    {"R93 open", rbLow, "1", HwCheck::IdleRelayOn},
    {"R93 short (valve pin does not read low when driven low)", z1ReadsHigh, "5", HwCheck::WeldNoReadback},
    {"R94 open", rbLow, "1", HwCheck::IdleRelayOn},
    {"R95 short", rbLow, "1", HwCheck::IdleRelayOn},
    // expander
    {"U5 pin 3 RESET open, leaks high", drvEnHigh, "1", HwCheck::IdleExpanderAnswers},
    {"U5 pin 3 RESET open, leaks low", drvEnLow, "3", kNoAck},
    {"U5 pin 4 P00 (ZD_Z1_N) open", z1ReadsHigh, "5", HwCheck::WeldNoReadback},
    {"U5 pin 9 P05 (RLY_ON) open", rlyOnLow, "6", HwCheck::RelayNoReadback},
    {"U5 pin 24 VCC open", drvEnLow, "3", kNoAck},
    {"U5 pins 3 RESET and 4 P00 bridged", z1ReadsHigh, "5", HwCheck::WeldNoReadback},
    {"U5 pins 8 P04 and 9 P05 bridged", rlyOnHigh, "4a", HwCheck::ExpanderInputs},
    {"U5 RESET input ignored, bits kept", drvEnHigh, "1", HwCheck::IdleExpanderAnswers},
    {"U5 pin P00 (Z1) stuck low", z1ReadsLow, "4a", HwCheck::ExpanderInputs},
    {"U5 pin P04 (MV) stuck low", mvReadsLow, "4a", HwCheck::ExpanderInputs},
    {"U5 pin P05 (RLY_ON) stuck high", rlyOnHigh, "4a", HwCheck::ExpanderInputs},
    {"RN1 element 1-8 (ZD_Z1_N) short", z1ReadsHigh, "5", HwCheck::WeldNoReadback},
    // relay drive
    {"K1 contact welded, or JP1 bridged", welded, "5", HwCheck::RelayWelded},
    {"Q1 pin G open, leaks high", welded, "5", HwCheck::RelayWelded},
    {"Q1 pins G and D bridged", welded, "5", HwCheck::RelayWelded},
    {"Q1 pins S and D bridged", welded, "5", HwCheck::RelayWelded},
    {"R11 open (RLY_ON does not read low as an input)", rlyOnHigh, "4a", HwCheck::ExpanderInputs},
    {"R12 open", [](FakeBoard &b) { b.r12Open = true; }, "5b", HwCheck::GateBleed},
    {"Q2 pin G open, holds its last level", bNeverOn, "3", kStartB},
    {"Q2 pin G open, leaks high", bStuckOn, "1", HwCheck::IdleRelayOn},
    {"Q2 pin G open, leaks low", bNeverOn, "3", kStartB},
    {"Q2 pin S (GND) open", bNeverOn, "3", kStartB},
    {"Q2 pin D (RLY_B) open", bNeverOn, "3", kStartB},
    {"Q2 pins G and S bridged", bNeverOn, "3", kStartB},
    {"Q2 pins G and D bridged", bStuckOn, "1", HwCheck::IdleRelayOn},
    {"Q2 pins S and D bridged", bStuckOn, "1", HwCheck::IdleRelayOn},
    {"Q2 shorted gate to drain, high-threshold part (review A2 4.6 question 7)",
     [](FakeBoard &b) { b.q2GateDrainMv = 870; }, "1", HwCheck::IdleRelayGateDrain},
    // detector B
    {"R89 open", bNeverOn, "3", kStartB},
    {"C51 open", bNeverOn, "3", kStartB},
    {"C51 short", bOnPinHigh, "2", HwCheck::PinHighRelayOn},
    {"D22 pin 1 A (GND) open", bNeverOn, "3", kStartB},
    {"D22 pins 2 K and 3 common bridged", bNeverOn, "3", kStartB},
    {"R14 open", bHold(1200), "9", HwCheck::StopRelayBand},
    {"R14 short", bNeverOn, "3", kStartB},
    {"R90 open", bHold(1200), "9", HwCheck::StopRelayBand},
    {"R90 short", bNeverOn, "3", kStartB},
    {"C50 short", bNeverOn, "3", kStartB},
    // output devices
    {"triac, TVS, opto-triac output or snubber capacitor shorted", [](FakeBoard &b) { b.shortedOutputMa = 400; }, "6",
     HwCheck::OutputShorted},
    // module
    {"U4 pin 10 (KA) not connected to the board", ka(KaNet::Disconnected), "3", kAny},
    {"GPIO of the gate-drive read-back driven high as an output", q3On, "1", HwCheck::IdleDriveSupplyOn},
    {"GPIO of the gate-drive read-back driven low as an output", vDrvOff, "3", kStartA},
    {"U4 pin 35 (V_DRV_RB) not connected to the board", vDrvOff, "3", kStartA},
    {"GPIO of the relay read-back driven high as an output", bNeverOn, "3", kStartB},
    {"GPIO of the relay read-back driven low as an output", rbLow, "1", HwCheck::IdleRelayOn},
    {"U4 pin 7 (RLY_B_RB) not connected to the board", rbLow, "1", HwCheck::IdleRelayOn},
    // board: the keep-alive net driven by something else
    {"KA bridged to TFT_BL (backlight PWM), 0.01 ohm", ka(KaNet::ToggleStrong), "1", kAny},
    {"KA bridged to TFT_BL (backlight PWM), 1000 ohm", ka(KaNet::ToggleWeak), "2b", HwCheck::FloatDriveEnabled},
    {"KA bridged to TFT_BL, 1000 ohm, the swing read as high",
     [](FakeBoard &b) { b.kaNet = KaNet::ToggleWeak, b.kaWeakReadsHigh = true; }, "2b", HwCheck::FloatPinHigh},
    {"KA bridged to U0TXD (ROM boot log)", ka(KaNet::ToggleStrong), "1", kAny},
    {"net DRV_EN shorted to GND", drvEnLow, "3", kNoAck},
    {"net DRV_EN shorted to 3V3", drvEnHigh, "1", HwCheck::IdleExpanderAnswers},
    {"net DRV_EN_N shorted to GND", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"net DRV_EN_N shorted to 3V3", aNeverOn, "3", kStartA},
    {"net KA shorted to GND", ka(KaNet::Disconnected), "3", kAny},
    {"net KA shorted to 3V3", ka(KaNet::HighStrong), "2b", HwCheck::FloatPinHigh},
    {"net KA_INP shorted to 3V3", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"net KA_N0 shorted to 3V3", ka(KaNet::HighWeak), "2b", HwCheck::FloatPinHigh},
    {"net KA_N1 shorted to 3V3", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"net KA_N2 shorted to 3V3", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"net KA_OK shorted to 3V3", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"net KA_REF shorted to GND", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"net KB_N1 shorted to 3V3", bStuckOn, "1", HwCheck::IdleRelayOn},
    {"net LEDA_Z1 shorted to 3V3", q3On, "1", HwCheck::IdleDriveSupplyOn},
    {"net OLA_MV shorted to 3V3", q3On, "1", HwCheck::IdleDriveSupplyOn},
    {"net RLY_A shorted to GND", bStuckOn, "1", HwCheck::IdleRelayOn},
    {"net RLY_B shorted to GND", bStuckOn, "1", HwCheck::IdleRelayOn},
    {"net RLY_B shorted to 3V3", bNeverOn, "3", kStartB},
    {"net RLY_B_RB shorted to GND", rbLow, "1", HwCheck::IdleRelayOn},
    {"net RLY_G1 shorted to 3V3", rlyOnHigh, "4a", HwCheck::ExpanderInputs},
    {"net RLY_G2 shorted to 3V3", bStuckOn, "1", HwCheck::IdleRelayOn},
    {"net RLY_ON shorted to GND", rlyOnLow, "6", HwCheck::RelayNoReadback},
    {"net RLY_ON shorted to 3V3", rlyOnHigh, "4a", HwCheck::ExpanderInputs},
    {"net V_DRV shorted to GND", vDrvOff, "3", kStartA},
    {"net V_DRV shorted to 3V3", q3On, "1", HwCheck::IdleDriveSupplyOn},
    {"net V_DRV_RB shorted to GND", vDrvOff, "3", kStartA},
    {"net V_DRV_RB shorted to 3V3", q3On, "1", HwCheck::IdleDriveSupplyOn},
    {"net ZD_MV_N shorted to GND", mvReadsLow, "4a", HwCheck::ExpanderInputs},
    {"net ZD_Z1_N shorted to GND", z1ReadsLow, "4a", HwCheck::ExpanderInputs},
    {"net ZD_Z1_N shorted to 3V3", z1ReadsHigh, "5", HwCheck::WeldNoReadback},
    {"leak of 47 kohm from 3V3 to KA_N2", aOnPinHigh, "2", HwCheck::PinHighDriveEnabled},
    {"leak of 100 kohm from 3V3 to KA_N2", aHold(5000), "9", HwCheck::StopDriveRelease},
    {"leak of 220 kohm from 3V3 to KA_N2", aHold(1190), "9", HwCheck::StopDriveRelease},
    {"leak of 100 kohm from 3V3 to KA_INP", aStuckOn, "1", HwCheck::IdleDriveEnabled},
    {"leak of 330 kohm from 3V3 to KA_INP", aOnPinHigh, "2", HwCheck::PinHighDriveEnabled},
    {"leak of 47 kohm from 3V3 to RLY_G2", bStuckOn, "1", HwCheck::IdleRelayOn},
    {"leak of 100 kohm from 3V3 to RLY_G2", bOnPinHigh, "2", HwCheck::PinHighRelayOn},
    {"leak of 100 kohm from V_BUCK to RLY_G2", bStuckOn, "1", HwCheck::IdleRelayOn},
    {"leak of 330 kohm from V_BUCK to RLY_G2", bHold(1130), "9", HwCheck::StopRelayBand},
    {"leak of 100 kohm from 3V3 to KB_N1", bOnPinHigh, "2", HwCheck::PinHighRelayOn},
    {"leak of 100 kohm from 3V3 to KA_N1", aHold(5000), "9", HwCheck::StopDriveRelease},
    // supplies
    // Review A2 found it in step 9 (Q2 held on by 5 V). With U13 and rule 18
    // (hardware answers A2, section 4) the rest levels of the current
    // channels read 3V3 first: both above 0.90 V.
    {"U3 shorted input to output", [](FakeBoard &b) { b.b.holdMs = 5000, b.v3v3Mv = 4820; }, "1",
     HwCheck::SupplyOverVoltage},
    {"V_BUCK falls to 0 V with USB attached (rule 15)", [](FakeBoard &b) { b.vBuck = false; }, "1",
     HwCheck::IdleRelayOn},

    // Found while the valve runs, by the stop test at the end of the run,
    // against this board's own value (670 / 570 ms).
    {"R33 short, appearing during a run", aHold(210), "9", HwCheck::StopDriveBand, When::DuringARun},
    {"R29 open, during a run", aHold(210), "9", HwCheck::StopDriveBand, When::DuringARun},
    {"D7 pin 2 K open, during a run", aHold(220), "9", HwCheck::StopDriveBand, When::DuringARun},
    {"net KA_N0 shorted to GND, during a run", aHold(210), "9", HwCheck::StopDriveBand, When::DuringARun},
    {"U4 pin 10 (KA) not connected, during a run", aHold(210), "9", HwCheck::StopDriveBand, When::DuringARun},
    {"net RLY_A shorted to 3V3, during a run", aHold(180), "9", HwCheck::StopDriveBand, When::DuringARun},
    {"R30 open, during a run", aHold(1560), "9", HwCheck::StopDriveRelease, When::DuringARun},
    {"R31 open, during a run", aHold(5000), "9", HwCheck::StopDriveRelease, When::DuringARun},
    {"R89 open, during a run", bHold(110), "9", HwCheck::StopRelayBand, When::DuringARun},
    {"C51 open, during a run", bHold(110), "9", HwCheck::StopRelayBand, When::DuringARun},
    {"D22 pins 2 K and 3 common bridged, during a run", bHold(0), "9", HwCheck::StopRelayRelease, When::DuringARun},
    {"R14 open, during a run", bHold(1200), "9", HwCheck::StopRelayBand, When::DuringARun},
    {"Q2 pin G open, during a run", bHold(5000), "9", HwCheck::StopRelayRelease, When::DuringARun},
    {"3V3 falls to 2.0 V, during a run", bHold(90), "9", HwCheck::StopRelayBand, When::DuringARun},
    {"V_BUCK falls to 3.0 V, during a run", bHold(180), "9", HwCheck::StopRelayBand, When::DuringARun},
    {"U7 pin 11 4Y (DRV_EN) open, during a run", drvEnHigh, "9", HwCheck::StopExpanderAnswers, When::DuringARun},
};

class ReviewA2Fault : public ::testing::TestWithParam<ReviewFault> {};

std::string testName(const char *row)
{
    std::string n;
    for (const char *p = row; *p != '\0'; ++p) {
        n += std::isalnum(static_cast<unsigned char>(*p)) != 0 ? *p : '_';
    }
    return n;
}

} // namespace

TEST_P(ReviewA2Fault, FoundByTheStepTheReviewNames)
{
    const ReviewFault &f = GetParam();
    StageRig r;
    if (f.when == When::DuringARun) {
        startUp(r);
        powerUp(r);
        r.stage.setZone(0, true);
        r.run(200);
        f.inject(r.board);
        r.run(200);
        r.stage.setZone(0, false);
        r.stage.requestPower(false, 0xFF);
    } else {
        f.inject(r.board);
        // Start-up first; the steps that only run before a run (5, 5b, 6)
        // are reached by asking for one.
        ASSERT_TRUE(
            r.runUntil([&] { return r.phase() == StagePhase::Fault || r.phase() == StagePhase::Idle; }, kStartupMs));
        if (r.phase() == StagePhase::Idle) {
            r.stage.requestPower(true, 0);
        }
    }
    ASSERT_TRUE(r.runUntil([&] { return r.phase() == StagePhase::Fault; }, 15000)) << f.row;
    EXPECT_STREQ(hwCheckStep(r.fault()), f.step) << f.row << ": " << toString(r.fault());
    if (f.check != HwCheck::None) {
        EXPECT_EQ(r.fault(), f.check) << f.row << ": " << toString(r.fault());
    }
    // Whatever was found, the fault state leaves no valve driven.
    EXPECT_EQ(r.guard.request(), KeepAliveRequest::Off);
    r.run(2500);
    EXPECT_EQ(r.board.valvesLow(), 0) << f.row;
    EXPECT_EQ(r.board.currentMa(), 0) << f.row;
}

INSTANTIATE_TEST_SUITE_P(AppendixA, ReviewA2Fault, ::testing::ValuesIn(kReviewFaults),
                         [](const ::testing::TestParamInfo<ReviewFault> &i) { return testName(i.param.row); });
