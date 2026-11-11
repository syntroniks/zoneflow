// SPDX-License-Identifier: Apache-2.0
// The controller on the simulated rev A board: runs go through the output
// stage's self-test and relay, its faults reach the fault codes, state and
// events, a detector fault needs the repair confirmed, and the run budget
// and a latched fault survive a software restart.
#include <gtest/gtest.h>

#include <algorithm>

#include "fake_board.hpp"
#include "zf/app/controller.hpp"

using namespace zf;
using namespace zf::test;

namespace {

class BoardSensors final : public ISensorHal {
public:
    explicit BoardSensors(FakeBoard &b) : board_(b) {}
    SensorSample read() override
    {
        SensorSample s;
        s.acPresent = board_.acPresent;
        s.valveCurrentMa = board_.currentMa();
        s.overCurrentTripped = board_.latched;
        s.mainsMilliHz = mainsMilliHz;
        s.sensorSupplyOn = true;
        s.dieTempKnown = true;
        s.dieTempC = 35.0f;
        return s;
    }
    uint32_t mainsMilliHz = 50000;

private:
    FakeBoard &board_;
};

class RetainedMemory final : public IRetainedStore {
public:
    RetainedRecord rec{};
    bool load(RetainedRecord &out) override
    {
        out = rec;
        return intact(rec);
    }
    void save(const RetainedRecord &r) override { rec = r; }
};

struct HwRig {
    FakeClock clock;
    OutputEnableGuard guard{OutputEnableMode::Toggle, 500};
    FakeBoard board{guard, clock};
    FakeReferenceStore reference = StageRig::storeWith(true);
    hw::OutputStage stage{board, guard, clock, revALayout(), hw::OutputStageConfig{}, &reference};
    BoardSensors sensors{board};
    EventBus bus;
    StateStore state;
    MemoryStorage storage;
    ConfigStore config{storage};
    RetainedMemory &retained;
    std::unique_ptr<Controller> ctrl;
    std::vector<Event> events;

    explicit HwRig(RetainedMemory &mem, uint32_t zone3MaxRunSec = 0) : retained(mem)
    {
        clock.mono = 0;
        bus.subscribe([](const Event &e, void *ctx) { static_cast<std::vector<Event> *>(ctx)->push_back(e); }, &events);
        Config c = Config::defaults();
        c.master.enabled = false;
        c.driver.interZoneDelayMs = 0;
        if (zone3MaxRunSec != 0) {
            c.zones[3].maxRunSec = zone3MaxRunSec;
        }
        EXPECT_TRUE(config.update(c).ok);
        ctrl = std::make_unique<Controller>(
            ControllerDeps{clock, stage, sensors, guard, bus, state, config, &storage, &retained});
        ctrl->begin();
        ctrl->setTimeStatus(TimeStatus::Synced);
    }
    void run(uint64_t ms)
    {
        for (uint64_t t = 0; t < ms; t += 5) {
            clock.advanceMs(5);
            board.step5ms();
            if (clock.mono % 50 == 0) {
                ctrl->tick();
            }
        }
    }
    template <typename Pred> bool runUntil(Pred p, uint64_t limitMs)
    {
        for (uint64_t t = 0; t < limitMs && !p(); t += 5) {
            run(5);
        }
        return p();
    }
    bool hasFault(FaultCode c) const { return (ctrl->activeFaults() & (1u << static_cast<unsigned>(c))) != 0; }
    SystemState snap() const { return state.snapshot(); }
};

} // namespace

TEST(HardwareController, ManualRunWaitsForTheSelfTestThenOpens)
{
    RetainedMemory mem;
    HwRig r(mem);
    ASSERT_TRUE(r.runUntil([&] { return r.snap().hardware.phase == StagePhase::Idle; }, 9000));
    EXPECT_EQ(r.snap().hardware.zonesFitted, 8);
    ASSERT_TRUE(r.ctrl->handle(Command::runZone(1, 30)));
    r.run(100);
    EXPECT_EQ(r.snap().zones[1].state, ValveState::Waiting); // self-test before the run
    EXPECT_FALSE(r.board.valveDriven(1));
    // The weld check drives zone 2's output for one window with the relay
    // open; the run itself opens it with the relay closed.
    ASSERT_TRUE(r.runUntil([&] { return r.snap().zones[1].state == ValveState::Open; }, 9000));
    EXPECT_TRUE(r.board.valveDriven(1));
    EXPECT_TRUE(r.board.relayClosed());
    EXPECT_EQ(r.snap().hardware.phase, StagePhase::Ready);
    EXPECT_EQ(r.snap().zones[1].state, ValveState::Open);
    // The run ends, the relay opens, the stop test passes.
    ASSERT_TRUE(r.runUntil([&] { return r.snap().hardware.phase == StagePhase::Idle; }, 40'000));
    EXPECT_FALSE(r.board.relayClosed());
    EXPECT_EQ(r.snap().hardware.selfTestsPassed, 2u);
    EXPECT_EQ(r.ctrl->activeFaults(), 0u);
}

TEST(HardwareController, FourZoneBoardLimitsTheZoneCount)
{
    RetainedMemory mem;
    HwRig r(mem);
    r.board.eightZone = false;
    ASSERT_TRUE(r.runUntil([&] { return r.snap().hardware.zonesFitted == 4; }, 9000));
    r.run(100);
    EXPECT_EQ(r.snap().zoneCount, 4);
    EXPECT_FALSE(r.ctrl->handle(Command::runZone(5, 30)));
    EXPECT_EQ(r.ctrl->lastRunError(), RunError::InvalidZone);
}

TEST(HardwareController, DetectorFaultStopsEverythingAndNeedsTheRepairConfirmed)
{
    RetainedMemory mem;
    HwRig r(mem);
    r.board.a.holdMs = 1700; // open bleed resistor: detector A releases late
    ASSERT_TRUE(r.runUntil([&] { return r.hasFault(FaultCode::KeepAliveDetector); }, 10000));
    const SystemState s = r.snap();
    EXPECT_EQ(s.hardware.phase, StagePhase::Fault);
    EXPECT_EQ(s.hardware.check, HwCheck::StopDriveRelease);
    EXPECT_STREQ(s.hardware.step, "9");
    EXPECT_TRUE(s.hardware.repairNeeded);
    EXPECT_NE(s.inhibitFlags & kInhibitHardware, 0);
    const auto raised = std::find_if(r.events.begin(), r.events.end(), [](const Event &e) {
        return e.type == EventType::FaultRaised && e.fault.code == FaultCode::KeepAliveDetector;
    });
    ASSERT_NE(raised, r.events.end());
    EXPECT_EQ(raised->fault.detail, static_cast<uint8_t>(HwCheck::StopDriveRelease));
    EXPECT_STREQ(toString(FaultCode::KeepAliveDetector), "keepalive_detector");

    EXPECT_FALSE(r.ctrl->handle(Command::runZone(0, 30)));
    EXPECT_EQ(r.ctrl->lastRunError(), RunError::HardwareFault);
    // One press does not clear it.
    r.ctrl->handle(Command::simple(CommandType::ClearFaults));
    EXPECT_TRUE(r.hasFault(FaultCode::KeepAliveDetector));
    r.run(100);
    EXPECT_EQ(r.snap().hardware.phase, StagePhase::Fault);
    // The repair, confirmed.
    r.board.a.holdMs = 600;
    Command ack = Command::acknowledgeRepair();
    ack.flag = false;
    EXPECT_FALSE(r.ctrl->handle(ack)); // flag missing: refused
    EXPECT_TRUE(r.ctrl->handle(Command::acknowledgeRepair()));
    EXPECT_FALSE(r.hasFault(FaultCode::KeepAliveDetector));
    r.run(50);
    EXPECT_EQ(r.snap().inhibitFlags & kInhibitHardware, 0);
    ASSERT_TRUE(r.ctrl->handle(Command::runZone(0, 30)));
    EXPECT_TRUE(r.runUntil([&] { return r.board.valveDriven(0); }, 16000));
}

TEST(HardwareController, WeldedRelayFaultClearsWithOnePressButIsFoundAgain)
{
    RetainedMemory mem;
    HwRig r(mem);
    ASSERT_TRUE(r.runUntil([&] { return r.snap().hardware.phase == StagePhase::Idle; }, 9000));
    r.board.relayWelded = true;
    ASSERT_TRUE(r.ctrl->handle(Command::runZone(2, 30)));
    ASSERT_TRUE(r.runUntil([&] { return r.hasFault(FaultCode::RelayWelded); }, 10000));
    EXPECT_EQ(r.snap().zones[2].state, ValveState::Off);
    EXPECT_EQ(r.snap().queue.length, 0);
    EXPECT_FALSE(r.snap().hardware.repairNeeded);
    r.ctrl->handle(Command::simple(CommandType::ClearFaults));
    EXPECT_FALSE(r.hasFault(FaultCode::RelayWelded));
    ASSERT_TRUE(r.ctrl->handle(Command::runZone(2, 30)));
    EXPECT_TRUE(r.runUntil([&] { return r.hasFault(FaultCode::RelayWelded); }, 10000));
}

TEST(HardwareController, TripOnTheBoardIsBlamedAndTheQueueCarriesOn)
{
    RetainedMemory mem;
    HwRig r(mem);
    ASSERT_TRUE(r.runUntil([&] { return r.snap().hardware.phase == StagePhase::Idle; }, 9000));
    ASSERT_TRUE(r.ctrl->handle(Command::runZone(0, 60)));
    ASSERT_TRUE(r.ctrl->handle(Command::runZone(1, 60)));
    ASSERT_TRUE(r.runUntil([&] { return r.snap().zones[0].state == ValveState::Open; }, 10000));
    r.run(50);
    r.board.trip(); // within 500 ms of switching zone 1 on: it is the faulted output
    r.run(100);
    EXPECT_TRUE(r.hasFault(FaultCode::OverCurrentTrip));
    EXPECT_EQ(r.snap().zones[0].fault, FaultCode::OverCurrentTrip);
    EXPECT_FALSE(r.ctrl->driver().lockedOut(0));
    // Latch cleared by the stage, self-test again, the next zone runs.
    EXPECT_TRUE(r.runUntil([&] { return r.snap().zones[1].state == ValveState::Open; }, 10'000));
    EXPECT_FALSE(r.board.latched);
}

TEST(HardwareController, SensorSupplyAndMainsSignalAreConditions)
{
    RetainedMemory mem;
    HwRig r(mem);
    r.board.sensorFault = true;
    r.sensors.mainsMilliHz = 100'000; // AC_ZC following the keep-alive
    ASSERT_TRUE(r.runUntil([&] { return r.snap().hardware.phase == StagePhase::Idle; }, 9000));
    EXPECT_TRUE(r.hasFault(FaultCode::SensorSupply));
    EXPECT_TRUE(r.hasFault(FaultCode::MainsSignal));
    EXPECT_TRUE(r.snap().hardware.sensorSupplyFault);
    r.sensors.mainsMilliHz = 60'000;
    r.run(100);
    EXPECT_FALSE(r.hasFault(FaultCode::MainsSignal));
}

TEST(HardwareController, RestartKeepsTheRunBudgetAndTheLatchedFault)
{
    RetainedMemory mem;
    {
        HwRig r(mem, 60);
        ASSERT_TRUE(r.runUntil([&] { return r.snap().hardware.phase == StagePhase::Idle; }, 9000));
        ASSERT_TRUE(r.ctrl->handle(Command::runZone(3, 60)));
        ASSERT_TRUE(r.runUntil([&] { return r.snap().zones[3].restRemainingSec > 0; }, 80'000));
        r.board.b.holdMs = 1700; // the stop test after this run fails: beyond 1.6 s
        ASSERT_TRUE(r.runUntil([&] { return r.hasFault(FaultCode::KeepAliveDetector); }, 10000));
        r.run(100);
        ASSERT_TRUE(intact(mem.rec));
    }
    // Software restart: new objects, the clock starts again, RTC memory stays.
    HwRig r(mem, 60);
    r.run(100);
    EXPECT_GT(r.snap().zones[3].restRemainingSec, 1700u);
    EXPECT_EQ(r.snap().hardware.phase, StagePhase::Fault);
    EXPECT_EQ(r.snap().hardware.check, HwCheck::StopRelayRelease);
    EXPECT_TRUE(r.hasFault(FaultCode::KeepAliveDetector));
}

TEST(HardwareController, NewBoardReportsNotCommissionedUntilItHasLearned)
{
    RetainedMemory mem;
    HwRig r(mem);
    r.reference.have = false; // a new board: nothing stored
    r.run(100);
    EXPECT_TRUE(r.hasFault(FaultCode::NotCommissioned));
    EXPECT_FALSE(r.snap().hardware.commissioned);
    // A condition: it does not stop a run and it does not latch.
    ASSERT_TRUE(r.runUntil([&] { return !r.hasFault(FaultCode::NotCommissioned); }, 40'000));
    const SystemState s = r.snap();
    EXPECT_TRUE(s.hardware.commissioned);
    EXPECT_NEAR(s.hardware.referenceDriveMs, 670, 10);
    EXPECT_NEAR(s.hardware.referenceRelayMs, 570, 10);
    const auto cleared = std::find_if(r.events.begin(), r.events.end(), [](const Event &e) {
        return e.type == EventType::FaultCleared && e.fault.code == FaultCode::NotCommissioned;
    });
    EXPECT_NE(cleared, r.events.end());
    EXPECT_EQ(r.ctrl->activeFaults(), 0u);
}

TEST(HardwareController, FloatTestFaultIsKeepAliveBridgedAndNeedsTheRepair)
{
    RetainedMemory mem;
    HwRig r(mem);
    r.board.kaNet = KaNet::ToggleWeak; // a 1 kohm bridge to the backlight PWM
    ASSERT_TRUE(r.runUntil([&] { return r.hasFault(FaultCode::KeepAliveBridged); }, 10000));
    const SystemState s = r.snap();
    EXPECT_STREQ(s.hardware.step, "2b");
    EXPECT_EQ(s.hardware.check, HwCheck::FloatDriveEnabled);
    EXPECT_TRUE(s.hardware.repairNeeded);
    r.ctrl->handle(Command::simple(CommandType::ClearFaults));
    r.run(100);
    EXPECT_TRUE(r.hasFault(FaultCode::KeepAliveBridged)); // one press does not clear it
}

TEST(HardwareController, CorruptRetainedMemoryStartsClean)
{
    RetainedMemory mem;
    std::fill_n(reinterpret_cast<uint8_t *>(&mem.rec), sizeof mem.rec, 0x5A);
    HwRig r(mem);
    r.run(100);
    EXPECT_EQ(r.snap().zones[0].restRemainingSec, 0u);
    EXPECT_NE(r.snap().hardware.phase, StagePhase::Fault);
}
