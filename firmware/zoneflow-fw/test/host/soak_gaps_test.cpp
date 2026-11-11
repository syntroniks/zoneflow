// SPDX-License-Identifier: Apache-2.0
// Regression tests for the requirement gaps the system soak simulation
// found (firmware architecture section 22) that firmware closes: the zone
// fault policy shared by both firmwares, a failed-short output device found
// during another zone's run, the fault path's all-off write, the run budget
// kept over a power cycle, the runtime record's own check and second copy,
// and the clock put back by less than a day.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>

#include "fake_board.hpp"
#include "window_rig.hpp"
#include "zf/app/persist.hpp"
#include "zf/config/config_json.hpp"
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

// ---- gap 3: the run budget over a power cut -------------------------------------------------------------------

namespace {

// A simple board (outputs switch directly) and a controller that can be
// powered off and on again over the same flash. RTC memory is lost with the
// power, so only the copy in flash carries the budget.
struct PowerRig {
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    MemoryStorage flash;
    ConfigStore config{flash};
    std::unique_ptr<Controller> ctrl;
    std::vector<Event> events;

    explicit PowerRig(uint32_t maxRunSec = 120)
    {
        sensors.sample.acPresent = true;
        bus.subscribe([](const Event &e, void *ctx) { static_cast<std::vector<Event> *>(ctx)->push_back(e); }, &events);
        Config c = Config::defaults();
        c.master.enabled = false;
        c.driver.interZoneDelayMs = 0;
        c.driver.minRestSec = 1800;
        for (auto &z : c.zones) {
            z.maxRunSec = maxRunSec;
        }
        EXPECT_TRUE(config.update(c).ok);
        boot();
    }
    void boot()
    {
        ctrl.reset();
        config.load();
        events.clear();
        ctrl = std::make_unique<Controller>(ControllerDeps{clock, out, sensors, oe, bus, state, config, &flash});
        ctrl->begin();
        ctrl->setTimeStatus(TimeStatus::Synced);
        for (int i = 0; i < 20; ++i) { // 24 VAC debounced
            clock.advanceMs(50);
            ctrl->tick();
        }
    }
    // The persist task polls every 250 ms.
    void run(uint64_t ms)
    {
        for (uint64_t t = 0; t < ms; t += 50) {
            clock.advanceMs(50);
            ctrl->tick();
            if (clock.mono % 250 == 0) {
                persistWriteOnce(ctrl->persistMailbox(), flash);
            }
        }
    }
    void powerCut(uint64_t offMs = 60000)
    {
        out = FakeOutputs{};
        clock.advanceMs(offMs);
        boot();
    }
    RunError startZone(ZoneIndex z, uint32_t sec)
    {
        ctrl->handle(Command::runZone(z, sec));
        return ctrl->lastRunError();
    }
};

} // namespace

TEST(BudgetOverPowerCut, AZoneThatUsedItsBudgetStillRestsAfterAPowerCut)
{
    PowerRig r(120);
    ASSERT_EQ(r.startZone(0, 120), RunError::Ok);
    r.run(125000);
    ASSERT_FALSE(r.out.zone[0]);
    EXPECT_EQ(r.startZone(0, 60), RunError::ZoneResting);
    r.powerCut();
    EXPECT_EQ(r.ctrl->budgetLoad(), RecordLoad::Certain);
    // Before: the budget lived in RTC memory only, and the zone ran again
    // for its full maximum after the power came back.
    EXPECT_EQ(r.startZone(0, 60), RunError::ZoneResting);
    r.run(1801000);
    EXPECT_EQ(r.startZone(0, 60), RunError::Ok) << "the rest counts from the restart";
}

TEST(BudgetOverPowerCut, APowerCutDuringARunChargesWhatTheLastWriteMissed)
{
    // 100 s of a 120 s maximum ran; the last periodic write was at most 30 s
    // before the cut. After it the zone has at most 20 s left, not 120 s.
    PowerRig r(120);
    ASSERT_EQ(r.startZone(0, 120), RunError::Ok);
    r.run(100000);
    ASSERT_TRUE(r.out.zone[0]);
    r.powerCut(1000);
    const auto budget = r.ctrl->driver().exportBudget();
    EXPECT_GE(budget[0].burstMs, 100000u) << "the zone kept a budget it had used";
    EXPECT_LE(budget[0].closedForMs, 1000u) << "the power cut is no rest: only the second since the restart";
}

TEST(BudgetOverPowerCut, BothCopiesUnreadableIsTheWorstCase)
{
    PowerRig r(120);
    ASSERT_EQ(r.startZone(1, 10), RunError::Ok);
    r.run(15000);
    ASSERT_TRUE(r.flash.data.count(BudgetRecord::kKeyA) + r.flash.data.count(BudgetRecord::kKeyB) > 0);
    for (const char *k : {BudgetRecord::kKeyA, BudgetRecord::kKeyB}) {
        if (r.flash.data.count(k) != 0) {
            r.flash.data[k][20] ^= 0x10; // a bit flipped in each copy
        }
    }
    r.powerCut();
    EXPECT_EQ(r.ctrl->budgetLoad(), RecordLoad::Lost);
    for (ZoneIndex z = 0; z < 8; ++z) {
        EXPECT_EQ(r.startZone(z, 10), RunError::ZoneResting) << "zone " << int(z);
    }
}

TEST(BudgetOverPowerCut, OneDamagedCopyFallsBackToTheOtherAndChargesMore)
{
    PowerRig r(300);
    ASSERT_EQ(r.startZone(0, 60), RunError::Ok);
    r.run(65000);
    // The newest copy is damaged (a write cut by the power cut).
    BudgetRecord a;
    BudgetRecord b;
    std::memcpy(&a, r.flash.data[BudgetRecord::kKeyA].data(), sizeof a);
    std::memcpy(&b, r.flash.data[BudgetRecord::kKeyB].data(), sizeof b);
    const char *newest = a.seq > b.seq ? BudgetRecord::kKeyA : BudgetRecord::kKeyB;
    r.flash.data[newest].resize(10);
    r.powerCut();
    EXPECT_EQ(r.ctrl->budgetLoad(), RecordLoad::Uncertain);
    const auto budget = r.ctrl->driver().exportBudget();
    EXPECT_GE(budget[0].burstMs, 60000u) << "never less than what had run";
}

TEST(BudgetOverPowerCut, ANewBoardHasAnEmptyBudget)
{
    PowerRig r(120);
    r.powerCut();
    EXPECT_EQ(r.ctrl->budgetLoad(), RecordLoad::None);
    EXPECT_EQ(r.startZone(0, 120), RunError::Ok);
}

TEST(BudgetOverPowerCut, WritesAreBoundedWhileAZoneRuns)
{
    // Flash wear: a 20-minute run writes the budget about once every 30 s,
    // and nothing is written while nothing runs.
    PowerRig r(3600);
    ASSERT_EQ(r.startZone(0, 1200), RunError::Ok);
    r.run(1000);
    const int before = r.flash.writes;
    r.run(1200000);
    const int during = r.flash.writes - before;
    EXPECT_LE(during, 1200 / 30 + 6);
    r.run(600000);
    EXPECT_LE(r.flash.writes - before - during, 2) << "a zone that finished resting writes once";
}

// ---- gap 9: the runtime record's own check and second copy --------------------------------------------------

namespace {

Config morningProgram()
{
    Config c = Config::defaults();
    c.master.enabled = false;
    c.driver.interZoneDelayMs = 0;
    c.schedule.programs[0] = makeProgram(1, {360}, {{0, 60}});
    c.schedule.programCount = 1;
    return c;
}

int countEvents(const std::vector<Event> &ev, EventType t)
{
    return static_cast<int>(std::count_if(ev.begin(), ev.end(), [t](const Event &e) { return e.type == t; }));
}

} // namespace

TEST(RuntimeRecord, ADamagedNewestCopyNeverRunsAStartTwice)
{
    // Chaos seed 392 of the soak: a key damaged beyond NVS's guarantee made
    // a start run again after a restart. Now the copy fails its CRC, the
    // older copy is used, and the start it does not cover is reported
    // missed, not run.
    PowerRig r;
    ASSERT_TRUE(r.config.update(morningProgram()).ok);
    r.clock.utc = utcOf(2026, 6, 1, 5, 59, 0);
    r.powerCut(0);
    r.run(70000); // the 06:00 start runs and is stored
    ASSERT_EQ(countEvents(r.events, EventType::ProgramStarted), 1);
    // A bit flips in the copy that holds the 06:00 key.
    SchedulerPersist p;
    uint32_t seq = 0;
    ASSERT_EQ(loadRuntime(r.flash, p, seq), RecordLoad::Certain);
    const char *newest = (seq & 1u) != 0 ? ConfigStore::kRuntimeKey : kRuntimeKeyB;
    r.flash.data[newest].back() ^= 0x01;
    r.powerCut(5000);
    EXPECT_EQ(r.ctrl->runtimeLoad(), RecordLoad::Uncertain);
    r.run(5000);
    EXPECT_EQ(countEvents(r.events, EventType::ProgramStarted), 0) << "the 06:00 start ran a second time";
    EXPECT_EQ(countEvents(r.events, EventType::ProgramMissed), 1);
}

TEST(RuntimeRecord, BothCopiesWrittenAlternatelyWithACheck)
{
    PowerRig r;
    ASSERT_TRUE(r.config.update(morningProgram()).ok);
    r.ctrl->handle(Command::rainDelay(utcOf(2026, 6, 2)));
    r.run(3000);
    r.ctrl->handle(Command::rainDelay(utcOf(2026, 6, 3)));
    r.run(3000);
    ASSERT_EQ(r.flash.data.count(ConfigStore::kRuntimeKey), 1u);
    ASSERT_EQ(r.flash.data.count(kRuntimeKeyB), 1u);
    EXPECT_EQ(r.flash.data[ConfigStore::kRuntimeKey].substr(0, 4), "ZFR1");
    SchedulerPersist p;
    uint32_t seq = 0;
    ASSERT_EQ(loadRuntime(r.flash, p, seq), RecordLoad::Certain);
    EXPECT_EQ(p.rainDelayUntil, utcOf(2026, 6, 3));
    // An older firmware's plain JSON is still read.
    MemoryStorage old;
    old.data[ConfigStore::kRuntimeKey] = persistToJson(p);
    SchedulerPersist q;
    EXPECT_EQ(loadRuntime(old, q, seq), RecordLoad::Certain);
    EXPECT_EQ(q.rainDelayUntil, utcOf(2026, 6, 3));
}
