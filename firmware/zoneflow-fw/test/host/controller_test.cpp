// SPDX-License-Identifier: Apache-2.0
// Controller integration: real driver, scheduler, sensors and diagnostics on
// fake hardware.
#include <gtest/gtest.h>

#include <algorithm>

#include "fakes.hpp"
#include "zf/app/controller.hpp"

using namespace zf;
using namespace zf::test;

namespace {

class ControllerTest : public ::testing::Test {
protected:
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    MemoryStorage storage;
    ConfigStore config{storage};
    std::unique_ptr<Controller> ctrl;
    std::vector<Event> events;

    void SetUp() override
    {
        bus.subscribe([](const Event &e, void *ctx) { static_cast<std::vector<Event> *>(ctx)->push_back(e); }, &events);
        Config c = Config::defaults();
        c.master.enabled = false;
        c.driver.interZoneDelayMs = 0;
        ASSERT_TRUE(config.update(c).ok);
    }

    void start(TimeStatus ts = TimeStatus::Synced)
    {
        ctrl = std::make_unique<Controller>(ControllerDeps{clock, out, sensors, oe, bus, state, config, &storage});
        ctrl->begin();
        ctrl->setTimeStatus(ts);
        tick();
        runMs(1000); // let the AC debounce settle
    }

    void tick(MonoMs ms = 50)
    {
        clock.advanceMs(ms);
        ctrl->tick();
    }
    void runMs(MonoMs ms)
    {
        for (MonoMs t = 0; t < ms; t += 50) {
            tick();
        }
    }
    bool hasFault(FaultCode c) const { return (ctrl->activeFaults() & (1u << static_cast<unsigned>(c))) != 0; }
    int count(EventType t) const
    {
        return static_cast<int>(
            std::count_if(events.begin(), events.end(), [t](const Event &e) { return e.type == t; }));
    }
};

} // namespace

TEST_F(ControllerTest, BootsWithOutputsOffAndPublishesState)
{
    out.zone[0] = true;
    start();
    EXPECT_FALSE(out.anyOn());
    const SystemState s = state.snapshot();
    EXPECT_GT(s.sequence, 0u);
    EXPECT_EQ(s.zoneCount, kMaxZones);
    EXPECT_TRUE(s.sensors.acPresent);
    EXPECT_EQ(s.timeStatus, TimeStatus::Synced);
    EXPECT_FALSE(hasFault(FaultCode::TimeInvalid));
}

TEST_F(ControllerTest, ManualRunReachesOutputsAndState)
{
    start();
    ASSERT_TRUE(ctrl->handle(Command::runZone(2, 10)));
    tick();
    EXPECT_TRUE(out.zone[2]);
    const SystemState s = state.snapshot();
    EXPECT_EQ(s.zones[2].state, ValveState::Open);
    EXPECT_EQ(s.queue.state, QueueState::Running);
    EXPECT_EQ(s.queue.length, 1);
    EXPECT_GE(count(EventType::ZoneChanged), 1);
    runMs(10'100);
    EXPECT_FALSE(out.zone[2]);
}

TEST_F(ControllerTest, OutputEnableAllowedOnlyWhileRunning)
{
    start();
    EXPECT_FALSE(oe.allowed(static_cast<uint32_t>(clock.mono)));
    ctrl->handle(Command::runZone(0, 5));
    tick();
    EXPECT_TRUE(oe.allowed(static_cast<uint32_t>(clock.mono)));
    // Control loop stalls: OE must drop after the heartbeat timeout.
    clock.advanceMs(600);
    EXPECT_FALSE(oe.allowed(static_cast<uint32_t>(clock.mono)));
}

TEST_F(ControllerTest, NoAcAtBootBlocksRuns)
{
    sensors.sample.acPresent = false;
    start();
    EXPECT_TRUE(hasFault(FaultCode::AcLost));
    EXPECT_FALSE(ctrl->handle(Command::runZone(0, 10)));
    EXPECT_EQ(ctrl->lastRunError(), RunError::Inhibited);
    EXPECT_FALSE(out.anyOn());
}

TEST_F(ControllerTest, AcLossShutsOutputsAndRecovers)
{
    start();
    ctrl->handle(Command::runZone(0, 600));
    tick();
    ASSERT_TRUE(out.zone[0]);
    sensors.sample.acPresent = false;
    runMs(150);
    EXPECT_FALSE(out.anyOn());
    EXPECT_TRUE(hasFault(FaultCode::AcLost));
    EXPECT_EQ(state.snapshot().queue.length, 0);
    sensors.sample.acPresent = true;
    runMs(600);
    EXPECT_FALSE(hasFault(FaultCode::AcLost));
    EXPECT_TRUE(ctrl->handle(Command::runZone(0, 10)));
}

TEST_F(ControllerTest, OverCurrentShutsEverythingAndLocksZone)
{
    start();
    ctrl->handle(Command::runZone(3, 600));
    ctrl->handle(Command::runZone(4, 600));
    tick();
    ASSERT_TRUE(out.zone[3]);
    sensors.sample.valveCurrentMa = 1500; // window for one zone is 80..700 mA
    runMs(1000);
    EXPECT_FALSE(out.anyOn());
    EXPECT_TRUE(hasFault(FaultCode::ZoneOverCurrent));
    EXPECT_TRUE(ctrl->driver().lockedOut(3));
    EXPECT_EQ(state.snapshot().zones[3].fault, FaultCode::ZoneOverCurrent);
    EXPECT_EQ(state.snapshot().queue.length, 0); // whole queue stopped

    sensors.sample.valveCurrentMa = -1;
    ctrl->handle(Command::runZone(3, 60));
    tick();
    EXPECT_FALSE(out.zone[3]); // locked out: entry dropped
    ctrl->handle(Command::simple(CommandType::ClearFaults));
    EXPECT_FALSE(hasFault(FaultCode::ZoneOverCurrent));
    ctrl->handle(Command::runZone(3, 60));
    tick();
    EXPECT_TRUE(out.zone[3]);
}

TEST_F(ControllerTest, OpenCircuitStopsZoneAndQueueContinues)
{
    start();
    ctrl->handle(Command::runZone(0, 600));
    ctrl->handle(Command::runZone(1, 600));
    tick();
    sensors.sample.valveCurrentMa = 5; // nothing flows
    runMs(1000);
    EXPECT_FALSE(out.zone[0]);
    EXPECT_TRUE(hasFault(FaultCode::ZoneOpenCircuit));
    EXPECT_EQ(state.snapshot().zones[0].fault, FaultCode::ZoneOpenCircuit);
    sensors.sample.valveCurrentMa = 250;
    tick();
    EXPECT_TRUE(out.zone[1]);
}

TEST_F(ControllerTest, ShortCircuitDuringInrushWindow)
{
    start();
    ctrl->handle(Command::runZone(5, 600));
    tick();
    sensors.sample.valveCurrentMa = 5000;
    tick();
    tick();
    EXPECT_FALSE(out.anyOn());
    EXPECT_TRUE(hasFault(FaultCode::ShortCircuit));
}

TEST_F(ControllerTest, RainSensorInhibitsScheduledRun)
{
    Config c = config.get();
    c.rain.enabled = true;
    c.rain.contact = ContactType::NormallyClosed;
    c.rain.debounceMs = 1000;
    c.schedule.programs[0] = makeProgram(1, {360}, {{0, 60}});
    c.schedule.programCount = 1;
    ASSERT_TRUE(config.update(c).ok);
    clock.utc = utcOf(2026, 6, 1, 5, 59, 0);
    sensors.sample.rainContactClosed = false; // NC open = wet
    start();
    EXPECT_TRUE(state.snapshot().sensors.rainActive);
    runMs(61'000);
    EXPECT_FALSE(out.anyOn());
    ASSERT_EQ(count(EventType::ProgramSkipped), 1);
    const auto it =
        std::find_if(events.begin(), events.end(), [](const Event &e) { return e.type == EventType::ProgramSkipped; });
    EXPECT_EQ(it->program.skipReason, SkipReason::SensorInhibit);
}

TEST_F(ControllerTest, ScheduledRunPersistsRuntimeState)
{
    Config c = config.get();
    c.schedule.programs[0] = makeProgram(1, {360}, {{0, 60}});
    c.schedule.programCount = 1;
    ASSERT_TRUE(config.update(c).ok);
    clock.utc = utcOf(2026, 6, 1, 5, 59, 50);
    start();
    runMs(15'000);
    EXPECT_TRUE(out.zone[0]);
    EXPECT_EQ(count(EventType::ProgramStarted), 1);
    ASSERT_EQ(storage.data.count("runtime"), 1u);
    SchedulerPersist p;
    ASSERT_TRUE(persistFromJson(storage.data["runtime"], p));
    ASSERT_EQ(p.count, 1);
    EXPECT_EQ(p.fired[0].key, Scheduler::makeKey(civil::daysFromCivil(2026, 6, 1), 360));
}

TEST_F(ControllerTest, NoScheduleWhileTimeUnset)
{
    Config c = config.get();
    c.schedule.programs[0] = makeProgram(1, {360}, {{0, 60}});
    c.schedule.programCount = 1;
    ASSERT_TRUE(config.update(c).ok);
    clock.utc = utcOf(2026, 6, 1, 5, 59, 50);
    start(TimeStatus::Unset);
    EXPECT_TRUE(hasFault(FaultCode::TimeInvalid));
    runMs(20'000);
    EXPECT_FALSE(out.anyOn());
    // Manual runs still work without a clock.
    EXPECT_TRUE(ctrl->handle(Command::runZone(1, 5)));
}

TEST_F(ControllerTest, ConfigReloadKeepsRunningZoneWhenDriverSettingsUnchanged)
{
    start();
    ctrl->handle(Command::runZone(0, 600));
    tick();
    Config c = config.get();
    c.deviceName.assign("renamed");
    ASSERT_TRUE(config.update(c).ok);
    ctrl->handle(Command::simple(CommandType::ReloadConfig));
    tick();
    EXPECT_TRUE(out.zone[0]);
    c.zones[0].maxRunSec = 30; // driver setting: everything stops
    ASSERT_TRUE(config.update(c).ok);
    ctrl->handle(Command::simple(CommandType::ReloadConfig));
    tick();
    EXPECT_FALSE(out.anyOn());
    EXPECT_GE(count(EventType::ConfigApplied), 3);
}

TEST_F(ControllerTest, MaintenanceStopsAndBlocks)
{
    start();
    ctrl->handle(Command::runZone(0, 600));
    tick();
    ctrl->handle(Command::maintenance(true));
    EXPECT_FALSE(out.anyOn());
    EXPECT_FALSE(ctrl->handle(Command::runZone(0, 10)));
    ctrl->handle(Command::maintenance(false));
    EXPECT_TRUE(ctrl->handle(Command::runZone(0, 10)));
}

TEST_F(ControllerTest, EmergencyOffFromAnotherContext)
{
    start();
    ctrl->handle(Command::runZone(0, 600));
    tick();
    ctrl->emergencyOff();
    EXPECT_FALSE(out.anyOn());
    EXPECT_TRUE(oe.tripped());
    tick();
    EXPECT_FALSE(out.anyOn());
    EXPECT_EQ(state.snapshot().zones[0].state, ValveState::Off);
}

TEST_F(ControllerTest, FlowWithAllValvesShutRaisesLeakFault)
{
    Config c = config.get();
    c.flow.enabled = true;
    c.flow.pulsesPerLitre = 100.0f;
    c.flow.leakThresholdLpm = 0.5f;
    c.flow.leakDetectSec = 10;
    ASSERT_TRUE(config.update(c).ok);
    start();
    // 5 pulses/s = 3 L/min.
    for (int i = 0; i < 20 * 20; ++i) {
        if (i % 4 == 0) {
            sensors.sample.flowPulses += 1;
        }
        tick();
    }
    EXPECT_TRUE(hasFault(FaultCode::FlowWithoutZones));
}

TEST_F(ControllerTest, RainDelayCommand)
{
    clock.utc = utcOf(2026, 6, 1, 5, 0);
    start();
    ctrl->handle(Command::rainDelay(utcOf(2026, 6, 2)));
    tick();
    EXPECT_EQ(state.snapshot().rainDelayUntil, utcOf(2026, 6, 2));
    EXPECT_EQ(count(EventType::RainDelayChanged), 1);
}

// ---- review findings ---------------------------------------------------------

// A shorted wire whose current straddles short_circuit_ma (3000 mA) reads
// 3100, 2900, 3100, ... on successive ticks. The zone must still be shut off.
TEST_F(ControllerTest, FaultCurrentAroundShortCircuitLimitShutsZoneOff)
{
    start();
    ctrl->handle(Command::runZone(2, 600));
    tick();
    ASSERT_TRUE(out.zone[2]);
    for (int i = 0; i < 40; ++i) {
        sensors.sample.valveCurrentMa = (i % 2 == 0) ? 3100 : 2900;
        tick();
    }
    EXPECT_FALSE(out.anyOn());
    EXPECT_TRUE(hasFault(FaultCode::ShortCircuit) || hasFault(FaultCode::ZoneOverCurrent));
    EXPECT_TRUE(ctrl->driver().lockedOut(2));
}

// Every config reload restarted the current monitor's confirmation count, so
// a client writing the config every tick kept an over-current zone open.
TEST_F(ControllerTest, ConfigChurnDoesNotDelayOverCurrentShutOff)
{
    start();
    ctrl->handle(Command::runZone(0, 600));
    tick();
    ASSERT_TRUE(out.zone[0]);
    runMs(600);                           // past the settle time
    sensors.sample.valveCurrentMa = 1500; // window is 80..700 mA
    Config c = config.get();
    for (int i = 0; i < 20; ++i) {
        c.deviceName.assign(i % 2 == 0 ? "a" : "b");
        ASSERT_TRUE(config.update(c).ok);
        ctrl->handle(Command::simple(CommandType::ReloadConfig));
        tick();
    }
    EXPECT_FALSE(out.anyOn());
    EXPECT_TRUE(hasFault(FaultCode::ZoneOverCurrent));
}

// Current with every output off means an output stage is stuck on. Clearing
// the fault while the current still flows must not hide it.
TEST_F(ControllerTest, ClearingIdleCurrentFaultWhileItPersistsRaisesItAgain)
{
    start();
    sensors.sample.valveCurrentMa = 300;
    runMs(1000);
    ASSERT_TRUE(hasFault(FaultCode::CurrentWhileIdle));
    ctrl->handle(Command::simple(CommandType::ClearFaults));
    EXPECT_FALSE(hasFault(FaultCode::CurrentWhileIdle));
    runMs(1000);
    EXPECT_TRUE(hasFault(FaultCode::CurrentWhileIdle));
}

// ---- phase 2 hardening ---------------------------------------------------------

TEST_F(ControllerTest, IdleCurrentRisingToShortRaisesShortCircuit)
{
    start();
    sensors.sample.valveCurrentMa = 300;
    runMs(1000);
    ASSERT_TRUE(hasFault(FaultCode::CurrentWhileIdle));
    sensors.sample.valveCurrentMa = 5000;
    runMs(200);
    EXPECT_TRUE(hasFault(FaultCode::ShortCircuit));
}

// The caller of a rejected command learns why: handle() returns false and a
// command_rejected event carries the error for the API layer.
TEST_F(ControllerTest, RunZoneOnLockedOutZoneReturnsError)
{
    start();
    ctrl->handle(Command::runZone(3, 600));
    tick();
    sensors.sample.valveCurrentMa = 5000;
    runMs(200);
    ASSERT_TRUE(ctrl->driver().lockedOut(3));
    sensors.sample.valveCurrentMa = -1;
    events.clear();
    EXPECT_FALSE(ctrl->handle(Command::runZone(3, 60)));
    EXPECT_EQ(ctrl->lastRunError(), RunError::LockedOut);
    ASSERT_EQ(count(EventType::CommandRejected), 1);
    const auto it =
        std::find_if(events.begin(), events.end(), [](const Event &e) { return e.type == EventType::CommandRejected; });
    EXPECT_EQ(it->rejected.command, CommandType::RunZone);
    EXPECT_EQ(it->rejected.zone, 3);
    EXPECT_EQ(it->rejected.error, RunError::LockedOut);
    EXPECT_STREQ(toString(EventType::CommandRejected), "command_rejected");
}

TEST_F(ControllerTest, DroppedRunIsPublished)
{
    Config c = config.get();
    c.zones[0].maxRunSec = 60;
    ASSERT_TRUE(config.update(c).ok);
    start();
    ASSERT_TRUE(ctrl->handle(Command::runZone(0, 60)));
    ASSERT_TRUE(ctrl->handle(Command::runZone(0, 60)));
    runMs(61'000);
    ASSERT_EQ(count(EventType::RunDropped), 1);
    const auto it =
        std::find_if(events.begin(), events.end(), [](const Event &e) { return e.type == EventType::RunDropped; });
    EXPECT_EQ(it->dropped.zone, 0);
    EXPECT_EQ(it->dropped.reason, StartError::Resting);
    EXPECT_GE(state.snapshot().zones[0].restRemainingSec, 1798u);
    EXPECT_LE(state.snapshot().zones[0].restRemainingSec, 1800u);
    EXPECT_FALSE(ctrl->handle(Command::runZone(0, 60)));
    EXPECT_EQ(ctrl->lastRunError(), RunError::ZoneResting);
}

// ---- master valve lockout ------------------------------------------------------------

namespace {
class MasterTest : public ControllerTest {
protected:
    void SetUp() override
    {
        ControllerTest::SetUp();
        Config c = config.get();
        c.master.enabled = true;
        c.master.preOpenMs = 1000;
        c.master.postCloseMs = 0;
        ASSERT_TRUE(config.update(c).ok);
    }
    // Starts zone 0; the master opens first and shorts during pre-open.
    void masterShort()
    {
        sensors.sample.valveCurrentMa = -1;
        ASSERT_TRUE(ctrl->handle(Command::runZone(0, 60)));
        tick();
        ASSERT_TRUE(out.master);
        sensors.sample.valveCurrentMa = 5000;
        tick();
        tick();
        ASSERT_FALSE(out.anyOn());
        sensors.sample.valveCurrentMa = -1;
        tick();
    }
};
} // namespace

TEST_F(MasterTest, RepeatedMasterShortsLockTheMasterOut)
{
    start();
    masterShort();
    masterShort();
    EXPECT_FALSE(hasFault(FaultCode::MasterLockedOut));
    EXPECT_EQ(state.snapshot().masterFaultCount, 2);
    masterShort();
    EXPECT_TRUE(hasFault(FaultCode::MasterLockedOut));
    EXPECT_TRUE(ctrl->driver().masterLockedOut());
    EXPECT_TRUE(state.snapshot().masterLockedOut);
    // No more retries into the short: manual runs refused, nothing switches.
    EXPECT_FALSE(ctrl->handle(Command::runZone(1, 60)));
    EXPECT_EQ(ctrl->lastRunError(), RunError::MasterLockedOut);
    runMs(2000);
    EXPECT_FALSE(out.anyOn());
    // ClearFaults releases it.
    ctrl->handle(Command::simple(CommandType::ClearFaults));
    tick();
    EXPECT_FALSE(ctrl->driver().masterLockedOut());
    EXPECT_EQ(state.snapshot().masterFaultCount, 0);
    EXPECT_TRUE(ctrl->handle(Command::runZone(1, 60)));
    tick();
    EXPECT_TRUE(out.master);
}

TEST_F(MasterTest, LockedMasterSkipsScheduledPrograms)
{
    Config c = config.get();
    c.schedule.programs[0] = makeProgram(1, {360}, {{0, 60}});
    c.schedule.programCount = 1;
    ASSERT_TRUE(config.update(c).ok);
    clock.utc = utcOf(2026, 6, 1, 5, 50, 0);
    start();
    masterShort();
    masterShort();
    masterShort();
    ASSERT_TRUE(ctrl->driver().masterLockedOut());
    runMs(11 * 60 * 1000);
    EXPECT_FALSE(out.anyOn());
    const auto it =
        std::find_if(events.begin(), events.end(), [](const Event &e) { return e.type == EventType::ProgramSkipped; });
    ASSERT_NE(it, events.end());
    EXPECT_EQ(it->program.skipReason, SkipReason::MasterLockedOut);
}

TEST_F(MasterTest, CleanMasterRunResetsTheCount)
{
    start();
    masterShort();
    masterShort();
    sensors.sample.valveCurrentMa = 300; // master alone, then master + zone: in window
    ASSERT_TRUE(ctrl->handle(Command::runZone(0, 5)));
    runMs(7000);
    ASSERT_FALSE(out.anyOn());
    EXPECT_EQ(state.snapshot().masterFaultCount, 0);
    masterShort();
    masterShort();
    EXPECT_FALSE(ctrl->driver().masterLockedOut());
}

TEST(Config, MasterFaultLockoutCount)
{
    Config c = Config::defaults();
    EXPECT_EQ(c.master.faultLockoutCount, 3);
    c.master.faultLockoutCount = 5;
    Config back;
    ASSERT_TRUE(configFromJson(configToJson(c), back).ok());
    EXPECT_EQ(back.master.faultLockoutCount, 5);
    c.master.faultLockoutCount = 0;
    EXPECT_EQ(validate(c).path, "master.fault_lockout_count");
}

// An over-current found after the settle time also counts: closing the master
// on that fault is not a clean session.
TEST_F(MasterTest, MasterOverCurrentAfterSettleCounts)
{
    start();
    for (int i = 0; i < 3; ++i) {
        sensors.sample.valveCurrentMa = -1;
        ASSERT_TRUE(ctrl->handle(Command::runZone(0, 60)));
        tick();
        sensors.sample.valveCurrentMa = 1000; // master window 80..700 mA
        runMs(900);
        ASSERT_FALSE(out.anyOn());
    }
    EXPECT_TRUE(ctrl->driver().masterLockedOut());
}
