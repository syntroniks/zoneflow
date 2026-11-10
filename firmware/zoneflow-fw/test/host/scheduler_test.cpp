// SPDX-License-Identifier: Apache-2.0
// Scheduler tests run the real ZoneDriver against a fake HAL, so they also
// check that queue entries reach the outputs.
//
// Calendar facts used below: 2026-06-01 is a Monday; Berlin DST 2026 starts
// 2026-03-29 01:00Z and ends 2026-10-25 01:00Z.
#include <gtest/gtest.h>

#include <memory>

#include "fakes.hpp"
#include "zf/sched/scheduler.hpp"
#include "zf/zone/zone_driver.hpp"

using namespace zf;
using namespace zf::test;

namespace {

constexpr uint16_t at(int h, int m) { return static_cast<uint16_t>(h * 60 + m); }

class SchedulerTest : public ::testing::Test {
protected:
    FakeClock clock;
    FakeOutputs out;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 1000};
    ZoneDriver drv{out, oe, clock};
    std::unique_ptr<Scheduler> sched;
    SchedulerRecorder rec;
    SchedulerConfig cfg;
    TimeZone tz = TimeZone::utc();
    ZoneDriverConfig dcfg;

    void SetUp() override
    {
        dcfg.zoneEnabled.fill(true);
        dcfg.maxRunSec.fill(3600);
        drv.init(dcfg);
        cfg.zoneEnabled.fill(true);
        rec.clock = &clock;
        makeScheduler();
        apply();
    }

    void makeScheduler()
    {
        sched = std::make_unique<Scheduler>(clock, drv);
        sched->setListener(&rec);
    }

    void addProgram(const Program &p) { cfg.schedule.programs[cfg.schedule.programCount++] = p; }
    void apply() { sched->configure(cfg, tz); }

    void begin(UtcSeconds t, bool valid = true)
    {
        clock.utc = t;
        sched->setTimeValid(valid);
    }

    void step(MonoMs ms = 1000)
    {
        clock.advanceMs(ms);
        drv.tick();
        sched->tick();
    }
    void runFor(int64_t seconds)
    {
        for (int64_t i = 0; i < seconds; ++i) {
            step();
        }
    }
    void runUntil(UtcSeconds t)
    {
        while (clock.utc < t) {
            step();
        }
    }
    void setBerlin() { tz = *TimeZone::parse("CET-1CEST,M3.5.0,M10.5.0/3"); }
};

} // namespace

// ---- day rules ----------------------------------------------------------

TEST(DayRule, Interval)
{
    DayRule r;
    r.type = DayRuleType::Interval;
    r.intervalDays = 3;
    r.intervalAnchorDay = static_cast<int32_t>(civil::daysFromCivil(2026, 6, 1));
    const int64_t a = r.intervalAnchorDay;
    EXPECT_TRUE(r.matches(a));
    EXPECT_FALSE(r.matches(a + 1));
    EXPECT_FALSE(r.matches(a + 2));
    EXPECT_TRUE(r.matches(a + 3));
    EXPECT_TRUE(r.matches(a - 3)); // before the anchor too
    EXPECT_FALSE(r.matches(a - 1));
}

TEST(DayRule, OddEvenAcrossMonthEnds)
{
    DayRule odd;
    odd.type = DayRuleType::OddDays;
    DayRule even;
    even.type = DayRuleType::EvenDays;
    EXPECT_TRUE(odd.matches(civil::daysFromCivil(2026, 1, 31)));
    EXPECT_TRUE(odd.matches(civil::daysFromCivil(2026, 2, 1))); // two odd days in a row
    EXPECT_FALSE(odd.matches(civil::daysFromCivil(2026, 2, 2)));
    EXPECT_TRUE(even.matches(civil::daysFromCivil(2026, 2, 28)));
    EXPECT_FALSE(even.matches(civil::daysFromCivil(2026, 3, 1)));
    EXPECT_TRUE(odd.matches(civil::daysFromCivil(2028, 2, 29)));
    EXPECT_TRUE(even.matches(civil::daysFromCivil(2028, 2, 28)));
}

TEST(DayRule, Weekdays)
{
    DayRule r;
    r.weekdayMask = (1u << 1) | (1u << 5); // Mon, Fri
    EXPECT_TRUE(r.matches(civil::daysFromCivil(2026, 6, 1)));
    EXPECT_FALSE(r.matches(civil::daysFromCivil(2026, 6, 2)));
    EXPECT_TRUE(r.matches(civil::daysFromCivil(2026, 6, 5)));
    r.weekdayMask = 0;
    EXPECT_FALSE(r.matches(civil::daysFromCivil(2026, 6, 5)));
}

TEST_F(SchedulerTest, WeekdayProgramRunsOnSelectedDaysOnly)
{
    Program p = makeProgram(1, {at(6, 0)}, {{0, 60}});
    p.days.weekdayMask = (1u << 1) | (1u << 3) | (1u << 5);
    addProgram(p);
    apply();
    begin(utcOf(2026, 6, 1, 0, 0));
    runUntil(utcOf(2026, 6, 8, 0, 0));
    EXPECT_EQ(rec.startedAt,
              (std::vector<UtcSeconds>{utcOf(2026, 6, 1, 6), utcOf(2026, 6, 3, 6), utcOf(2026, 6, 5, 6)}));
}

TEST_F(SchedulerTest, IntervalProgram)
{
    Program p = makeProgram(1, {at(6, 0)}, {{0, 60}});
    p.days.type = DayRuleType::Interval;
    p.days.intervalDays = 3;
    p.days.intervalAnchorDay = static_cast<int32_t>(civil::daysFromCivil(2026, 6, 1));
    addProgram(p);
    apply();
    begin(utcOf(2026, 6, 1, 0, 0));
    runUntil(utcOf(2026, 6, 8, 0, 0));
    EXPECT_EQ(rec.startedAt,
              (std::vector<UtcSeconds>{utcOf(2026, 6, 1, 6), utcOf(2026, 6, 4, 6), utcOf(2026, 6, 7, 6)}));
}

TEST_F(SchedulerTest, MultipleStartTimesPerDayAndLocalTime)
{
    setBerlin();
    addProgram(makeProgram(1, {at(20, 0), at(6, 0)}, {{0, 60}})); // unsorted on purpose
    apply();
    begin(utcOf(2026, 6, 1, 0, 0));
    runUntil(utcOf(2026, 6, 2, 0, 0));
    // CEST = UTC+2.
    EXPECT_EQ(rec.startedAt, (std::vector<UtcSeconds>{utcOf(2026, 6, 1, 4), utcOf(2026, 6, 1, 18)}));
}

TEST_F(SchedulerTest, DisabledProgramDoesNotRun)
{
    Program p = makeProgram(1, {at(6, 0)}, {{0, 60}});
    p.enabled = false;
    addProgram(p);
    apply();
    begin(utcOf(2026, 6, 1, 5, 0));
    runUntil(utcOf(2026, 6, 1, 7, 0));
    EXPECT_TRUE(rec.started.empty());
}

// ---- DST ------------------------------------------------------------------

TEST_F(SchedulerTest, SpringForwardStartInGapRunsAtEndOfGap)
{
    setBerlin();
    addProgram(makeProgram(1, {at(2, 30)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 3, 28, 12, 0));
    runUntil(utcOf(2026, 3, 30, 12, 0));
    EXPECT_EQ(rec.startedAt, (std::vector<UtcSeconds>{utcOf(2026, 3, 29, 1, 0), utcOf(2026, 3, 30, 0, 30)}));
}

TEST_F(SchedulerTest, FallBackStartInRepeatedHourRunsOnce)
{
    setBerlin();
    addProgram(makeProgram(1, {at(2, 30)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 10, 24, 12, 0));
    runUntil(utcOf(2026, 10, 25, 12, 0));
    EXPECT_EQ(rec.startedAt, (std::vector<UtcSeconds>{utcOf(2026, 10, 25, 0, 30)}));
}

TEST_F(SchedulerTest, FallBackDayStartsAfterTheRepeatedHourKeepLocalTime)
{
    setBerlin();
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 10, 24, 12, 0));
    runUntil(utcOf(2026, 10, 26, 12, 0));
    // 06:00 CEST = 04:00Z on the 24th has passed; 25th and 26th are CET.
    EXPECT_EQ(rec.startedAt, (std::vector<UtcSeconds>{utcOf(2026, 10, 25, 5, 0), utcOf(2026, 10, 26, 5, 0)}));
}

// ---- overlaps and queueing ------------------------------------------------

TEST_F(SchedulerTest, ProgramsStartingTogetherRunSequentially)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    addProgram(makeProgram(2, {at(6, 0)}, {{1, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 50));
    runUntil(utcOf(2026, 6, 1, 6, 0, 0));
    EXPECT_TRUE(out.zone[0]);
    EXPECT_FALSE(out.zone[1]);
    EXPECT_EQ(sched->queueSize(), 2u);
    runFor(60);
    EXPECT_FALSE(out.zone[0]);
    EXPECT_TRUE(out.zone[1]);
    runFor(60);
    EXPECT_FALSE(out.anyOn());
    EXPECT_EQ(sched->queueState(), QueueState::Idle);
}

TEST_F(SchedulerTest, ProgramZonesRunInOrderOneAtATime)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{2, 30}, {0, 30}, {5, 30}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 50));
    out.log.clear();
    runUntil(utcOf(2026, 6, 1, 6, 2, 0));
    EXPECT_EQ(out.log, (std::vector<std::string>{"z0+", "z0-", "z2+", "z2-", "z5+", "z5-"}));
}

TEST_F(SchedulerTest, SameProgramStillRunningSkipsNextStart)
{
    addProgram(makeProgram(1, {at(6, 0), at(6, 1)}, {{0, 600}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 50));
    runUntil(utcOf(2026, 6, 1, 6, 2, 0));
    ASSERT_EQ(rec.started.size(), 1u);
    ASSERT_EQ(rec.skipped.size(), 1u);
    EXPECT_EQ(rec.skipped[0].second, SkipReason::AlreadyQueued);
}

TEST_F(SchedulerTest, QueueFull)
{
    for (std::size_t i = 0; i < kRunQueueCapacity; ++i) {
        ASSERT_EQ(sched->runZone(0, 10), RunError::Ok);
    }
    EXPECT_EQ(sched->runZone(0, 10), RunError::QueueFull);
}

TEST_F(SchedulerTest, PauseResumeKeepsRemainingTime)
{
    ASSERT_EQ(sched->runZone(0, 100), RunError::Ok);
    ASSERT_EQ(sched->runZone(1, 100), RunError::Ok);
    EXPECT_TRUE(out.zone[0]);
    runFor(30);
    sched->pause();
    EXPECT_FALSE(out.anyOn());
    EXPECT_EQ(sched->queueState(), QueueState::Paused);
    EXPECT_NEAR(sched->entry(0)->remainingMs, 70000, 1000);
    runFor(20);
    EXPECT_FALSE(out.anyOn());
    sched->resume();
    EXPECT_TRUE(out.zone[0]);
    runFor(69);
    EXPECT_TRUE(out.zone[0]);
    runFor(2);
    EXPECT_FALSE(out.zone[0]);
    EXPECT_TRUE(out.zone[1]);
}

TEST_F(SchedulerTest, SkipMovesToNextEntry)
{
    ASSERT_EQ(sched->runZone(0, 100), RunError::Ok);
    ASSERT_EQ(sched->runZone(1, 100), RunError::Ok);
    sched->skip();
    EXPECT_FALSE(out.zone[0]);
    EXPECT_TRUE(out.zone[1]);
    EXPECT_EQ(sched->queueSize(), 1u);
}

TEST_F(SchedulerTest, StopClearsQueueAndOutputs)
{
    ASSERT_EQ(sched->runZone(0, 100), RunError::Ok);
    ASSERT_EQ(sched->runZone(1, 100), RunError::Ok);
    sched->stop();
    EXPECT_FALSE(out.anyOn());
    EXPECT_EQ(sched->queueSize(), 0u);
    runFor(5);
    EXPECT_FALSE(out.anyOn());
}

TEST_F(SchedulerTest, ManualRunValidation)
{
    cfg.zoneCount = 4;
    cfg.zoneEnabled[2] = false;
    apply();
    EXPECT_EQ(sched->runZone(4, 10), RunError::InvalidZone);
    EXPECT_EQ(sched->runZone(2, 10), RunError::InvalidZone);
    EXPECT_EQ(sched->runZone(0, 0), RunError::ZeroDuration);
    EXPECT_EQ(sched->runProgram(42), RunError::UnknownProgram);
}

TEST_F(SchedulerTest, ManualProgramRunIgnoresDayRuleAndRainDelay)
{
    Program p = makeProgram(7, {at(6, 0)}, {{0, 600}});
    p.days.weekdayMask = 0;
    addProgram(p);
    cfg.schedule.seasonalPercent = 50;
    apply();
    begin(utcOf(2026, 6, 1, 12, 0));
    sched->setRainDelayUntil(utcOf(2026, 6, 3));
    ASSERT_EQ(sched->runProgram(7), RunError::Ok);
    EXPECT_TRUE(out.zone[0]);
    EXPECT_EQ(sched->entry(0)->durationSec, 300u); // seasonal applies
    ASSERT_EQ(rec.started.size(), 1u);
    EXPECT_EQ(rec.started[0].source, RunSource::ManualProgram);
    EXPECT_EQ(sched->runProgram(7), RunError::AlreadyQueued);
}

// ---- adjustments and inhibits -------------------------------------------

TEST_F(SchedulerTest, SeasonalAdjustment)
{
    Program a = makeProgram(1, {at(6, 0)}, {{0, 601}});
    Program b = makeProgram(2, {at(6, 0)}, {{1, 600}});
    b.useSeasonalAdjust = false;
    addProgram(a);
    addProgram(b);
    cfg.schedule.seasonalPercent = 50;
    apply();
    EXPECT_EQ(sched->adjustedDurationSec(a, 0), 301u); // 300.5 rounds up
    EXPECT_EQ(sched->adjustedDurationSec(b, 1), 600u);
    cfg.schedule.seasonalPercent = 0;
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 50));
    runUntil(utcOf(2026, 6, 1, 6, 0, 5));
    ASSERT_EQ(rec.skipped.size(), 1u);
    EXPECT_EQ(rec.skipped[0], std::make_pair(uint16_t{1}, SkipReason::NothingToRun));
    EXPECT_EQ(rec.started.size(), 1u);
}

TEST_F(SchedulerTest, DurationsAreClampedToZoneMaximum)
{
    dcfg.maxRunSec[0] = 120;
    drv.reconfigure(dcfg);
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 600}, {1, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 59));
    step();
    ASSERT_TRUE(out.zone[0]);
    runFor(119);
    EXPECT_TRUE(out.zone[0]);
    runFor(2);
    EXPECT_FALSE(out.zone[0]);
    EXPECT_TRUE(out.zone[1]);
}

TEST_F(SchedulerTest, RainDelaySkipsUntilExpiry)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 0));
    sched->setRainDelayUntil(utcOf(2026, 6, 2, 0, 0));
    EXPECT_TRUE(sched->rainDelayActive());
    runUntil(utcOf(2026, 6, 1, 6, 1));
    ASSERT_EQ(rec.skipped.size(), 1u);
    EXPECT_EQ(rec.skipped[0].second, SkipReason::RainDelay);
    runUntil(utcOf(2026, 6, 2, 0, 0, 5));
    EXPECT_FALSE(sched->rainDelayActive());
    ASSERT_FALSE(rec.rainDelays.empty());
    EXPECT_EQ(rec.rainDelays.back(), 0);
    runUntil(utcOf(2026, 6, 2, 6, 1));
    EXPECT_EQ(rec.startedAt, (std::vector<UtcSeconds>{utcOf(2026, 6, 2, 6, 0)}));
}

TEST_F(SchedulerTest, RainDelayRemovesScheduledRunsButKeepsManual)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 600}, {1, 600}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 59));
    step();
    ASSERT_TRUE(out.zone[0]);
    ASSERT_EQ(sched->runZone(2, 100), RunError::Ok);
    sched->setRainDelayUntil(utcOf(2026, 6, 3));
    step();
    EXPECT_FALSE(out.zone[0]);
    EXPECT_TRUE(out.zone[2]);
    EXPECT_EQ(sched->queueSize(), 1u);
}

TEST_F(SchedulerTest, SensorInhibitRespectsPerProgramFlag)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    Program b = makeProgram(2, {at(6, 0)}, {{1, 60}});
    b.obeySensorInhibit = false;
    addProgram(b);
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 50));
    sched->setInhibit(kInhibitSensor);
    runUntil(utcOf(2026, 6, 1, 6, 0, 5));
    ASSERT_EQ(rec.skipped.size(), 1u);
    EXPECT_EQ(rec.skipped[0], std::make_pair(uint16_t{1}, SkipReason::SensorInhibit));
    ASSERT_EQ(rec.started.size(), 1u);
    EXPECT_EQ(rec.started[0].id, 2);
}

TEST_F(SchedulerTest, SensorGoingActiveStopsScheduledRun)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 600}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 59));
    step();
    ASSERT_TRUE(out.zone[0]);
    sched->setInhibit(kInhibitSensor);
    EXPECT_FALSE(out.zone[0]);
    EXPECT_EQ(sched->queueSize(), 0u);
}

TEST_F(SchedulerTest, NoAcBlocksEverything)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 50));
    ASSERT_EQ(sched->runZone(1, 600), RunError::Ok);
    sched->setInhibit(kInhibitNoAc);
    EXPECT_FALSE(out.anyOn());
    EXPECT_EQ(sched->runZone(1, 10), RunError::Inhibited);
    runUntil(utcOf(2026, 6, 1, 6, 0, 5));
    ASSERT_EQ(rec.skipped.size(), 1u);
    EXPECT_EQ(rec.skipped[0].second, SkipReason::NoAc);
}

// ---- time validity, jumps and missed starts --------------------------------

TEST_F(SchedulerTest, NothingRunsWhileTimeInvalid)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    SchedulerPersist ranYesterday;
    ranYesterday.count = 1;
    ranYesterday.fired[0] = {1, Scheduler::makeKey(civil::daysFromCivil(2026, 5, 31), at(6, 0))};
    sched->restore(ranYesterday);
    apply();
    begin(utcOf(2026, 6, 1, 5, 0), false);
    runUntil(utcOf(2026, 6, 1, 6, 30));
    EXPECT_TRUE(rec.started.empty());
    // Time becomes valid (SNTP): the start 30 minutes ago is within grace.
    // This is the RTC-failed-at-boot case.
    sched->setTimeValid(true);
    step();
    ASSERT_EQ(rec.started.size(), 1u);
    EXPECT_TRUE(rec.started[0].late);
}

TEST_F(SchedulerTest, PowerCutWithinGraceRunsLate)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 0));
    runUntil(utcOf(2026, 6, 1, 6, 0, 1));
    ASSERT_EQ(rec.started.size(), 1u);
    const SchedulerPersist saved = sched->persist();

    // Power off 07:00 on day 1, back at 06:20 on day 2.
    drv.init(dcfg);
    makeScheduler();
    sched->restore(saved);
    apply();
    begin(utcOf(2026, 6, 2, 6, 20));
    step();
    ASSERT_EQ(rec.started.size(), 2u);
    EXPECT_TRUE(rec.started[1].late);
    EXPECT_TRUE(rec.missed.empty());
}

TEST_F(SchedulerTest, PowerCutBeyondGraceIsReportedMissed)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    cfg.schedule.missedStartGraceMin = 60;
    apply();
    begin(utcOf(2026, 6, 1, 5, 0));
    runUntil(utcOf(2026, 6, 1, 6, 0, 1));
    const SchedulerPersist saved = sched->persist();

    makeScheduler();
    sched->restore(saved);
    apply();
    begin(utcOf(2026, 6, 2, 7, 30));
    step();
    EXPECT_EQ(rec.started.size(), 1u);
    ASSERT_EQ(rec.missed.size(), 1u);
    EXPECT_EQ(rec.missed[0].second, utcOf(2026, 6, 2, 6, 0));
}

TEST_F(SchedulerTest, ZeroGraceNeverCatchesUp)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    cfg.schedule.missedStartGraceMin = 0;
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 59));
    step();
    ASSERT_EQ(rec.started.size(), 1u); // on time still runs
    const SchedulerPersist saved = sched->persist();
    makeScheduler();
    sched->restore(saved);
    apply();
    begin(utcOf(2026, 6, 2, 6, 1));
    step();
    EXPECT_EQ(rec.started.size(), 1u);
    EXPECT_EQ(rec.missed.size(), 1u);
}

TEST_F(SchedulerTest, RebootJustAfterStartDoesNotRepeatIt)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 600}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 59));
    step();
    ASSERT_EQ(rec.started.size(), 1u);
    const SchedulerPersist saved = sched->persist();

    drv.init(dcfg);
    makeScheduler();
    sched->restore(saved);
    apply();
    begin(utcOf(2026, 6, 1, 6, 5));
    runFor(5);
    EXPECT_EQ(rec.started.size(), 1u);
    EXPECT_TRUE(rec.missed.empty());
}

TEST_F(SchedulerTest, ForwardJumpOverStartWithinGraceRunsLate)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 50));
    step();
    clock.utc = utcOf(2026, 6, 1, 6, 30); // SNTP step
    step();
    ASSERT_EQ(rec.started.size(), 1u);
    EXPECT_TRUE(rec.started[0].late);
}

TEST_F(SchedulerTest, ForwardJumpBeyondGraceReportsMissed)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 50));
    step();
    clock.utc = utcOf(2026, 6, 1, 9, 0);
    step();
    EXPECT_TRUE(rec.started.empty());
    ASSERT_EQ(rec.missed.size(), 1u);
}

TEST_F(SchedulerTest, OnlyNewestOfSeveralMissedStartsRuns)
{
    addProgram(makeProgram(1, {at(6, 0), at(18, 0)}, {{0, 60}}));
    cfg.schedule.missedStartGraceMin = 720;
    apply();
    begin(utcOf(2026, 6, 1, 5, 0));
    step();
    clock.utc = utcOf(2026, 6, 2, 5, 0);
    step();
    ASSERT_EQ(rec.started.size(), 1u);
    ASSERT_EQ(rec.missed.size(), 1u);
    EXPECT_EQ(rec.missed[0].second, utcOf(2026, 6, 1, 6, 0));
}

TEST_F(SchedulerTest, ClockSetBackDoesNotRepeatStart)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 59));
    runUntil(utcOf(2026, 6, 1, 6, 10));
    ASSERT_EQ(rec.started.size(), 1u);
    clock.utc = utcOf(2026, 6, 1, 5, 30);
    runUntil(utcOf(2026, 6, 1, 6, 10));
    EXPECT_EQ(rec.started.size(), 1u);
    runUntil(utcOf(2026, 6, 2, 6, 1));
    EXPECT_EQ(rec.started.size(), 2u);
}

TEST_F(SchedulerTest, LargeBackwardJumpFromBogusFutureClockRecovers)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2031, 6, 1, 5, 59, 59)); // RTC was wrong
    step();
    ASSERT_EQ(rec.started.size(), 1u);
    clock.utc = utcOf(2026, 6, 1, 5, 59, 0); // SNTP corrects it
    runUntil(utcOf(2026, 6, 1, 6, 0, 1));
    EXPECT_EQ(rec.started.size(), 2u);
}

TEST_F(SchedulerTest, EditingConfigDoesNotTriggerPastStarts)
{
    apply();
    begin(utcOf(2026, 6, 1, 6, 10));
    runFor(3);
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    runFor(3);
    EXPECT_TRUE(rec.started.empty());
    EXPECT_TRUE(rec.missed.empty());
    runUntil(utcOf(2026, 6, 2, 6, 0, 1));
    EXPECT_EQ(rec.started.size(), 1u);
}

TEST_F(SchedulerTest, RemovedProgramEntriesLeaveQueue)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 600}}));
    apply();
    ASSERT_EQ(sched->runProgram(1), RunError::Ok);
    ASSERT_EQ(sched->runZone(3, 60), RunError::Ok);
    cfg.schedule.programCount = 0;
    apply();
    EXPECT_FALSE(out.zone[0]);
    EXPECT_EQ(sched->queueSize(), 1u);
    step();
    EXPECT_TRUE(out.zone[3]);
}

TEST_F(SchedulerTest, PersistRoundTripKeepsRainDelay)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 0));
    sched->setRainDelayUntil(utcOf(2026, 6, 5));
    const SchedulerPersist saved = sched->persist();
    makeScheduler();
    sched->restore(saved);
    apply();
    begin(utcOf(2026, 6, 1, 5, 0));
    EXPECT_TRUE(sched->rainDelayActive());
    EXPECT_EQ(sched->rainDelayUntil(), utcOf(2026, 6, 5));
}

TEST_F(SchedulerTest, ProgramWithoutStoredKeyGetsNoCatchUp)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 6, 20));
    step();
    EXPECT_TRUE(rec.started.empty());
    EXPECT_TRUE(rec.missed.empty());
}

// ---- review findings ---------------------------------------------------------

// A bogus future RTC time stored keys years ahead. After a reboot the cursor
// is new, so the "clock set back more than 24 h" clamp never ran and the
// program stayed silent until the stored date came round again.
TEST_F(SchedulerTest, FutureKeysFromBogusClockAreClampedAfterReboot)
{
    addProgram(makeProgram(1, {at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2031, 6, 1, 5, 59, 59)); // RTC wrong, network down
    step();
    ASSERT_EQ(rec.started.size(), 1u);
    const SchedulerPersist saved = sched->persist();

    // Power cut. At the next boot the clock is right (RTC reset, then SNTP).
    drv.init(dcfg);
    makeScheduler();
    sched->restore(saved);
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 0));
    runUntil(utcOf(2026, 6, 1, 6, 0, 1));
    EXPECT_EQ(rec.started.size(), 2u);
}

// A reboot inside the repeated hour must still not run a start twice: keys
// less than 24 h ahead of the clock are kept.
TEST_F(SchedulerTest, KeysSlightlyAheadAreKeptAfterReboot)
{
    setBerlin();
    addProgram(makeProgram(1, {at(2, 30)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 10, 25, 0, 29, 59)); // 02:29:59 CEST
    step();
    ASSERT_EQ(rec.started.size(), 1u);
    const SchedulerPersist saved = sched->persist();

    drv.init(dcfg);
    makeScheduler();
    sched->restore(saved);
    apply();
    begin(utcOf(2026, 10, 25, 1, 29, 0)); // 02:29 CET, the repeated hour
    runFor(120);
    EXPECT_EQ(rec.started.size(), 1u);
}

// RunZone(z, s) multiplied s by 1000 in 32 bits: 4294968 s wrapped to 704 ms
// and the zone ran for one second instead of its maximum run time.
TEST_F(SchedulerTest, HugeManualDurationIsClampedNotWrapped)
{
    ASSERT_EQ(sched->runZone(0, 4'294'968u), RunError::Ok);
    step();
    ASSERT_TRUE(out.zone[0]);
    runFor(5);
    EXPECT_TRUE(out.zone[0]);
    EXPECT_GT(drv.remainingMs(0), 3000u * 1000u);
}

// The config accepts the same start time twice. Both copies became candidates
// for the same instant, so every day one ran and the other was reported as a
// missed start.
TEST_F(SchedulerTest, DuplicateStartTimesRunOnceAndAreNotReportedMissed)
{
    addProgram(makeProgram(1, {at(6, 0), at(6, 0)}, {{0, 60}}));
    apply();
    begin(utcOf(2026, 6, 1, 5, 59, 50));
    runUntil(utcOf(2026, 6, 2, 6, 0, 10));
    EXPECT_EQ(rec.started.size(), 2u);
    EXPECT_TRUE(rec.missed.empty());
}
