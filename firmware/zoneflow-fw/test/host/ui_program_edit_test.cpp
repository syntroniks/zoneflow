// SPDX-License-Identifier: Apache-2.0
// Program editor model and its mapping into the core configuration.
#include <gtest/gtest.h>

#include "fakes.hpp"
#include "zf/config/config_json.hpp"
#include "zf_ui/program_edit.hpp"

using namespace zf;
using namespace zf::test;
using namespace zf::ui;

namespace {

std::vector<int> starts(const ProgramEdit &e)
{
    std::vector<int> out;
    for (std::size_t i = 0; i < e.startCount(); ++i) {
        out.push_back(e.program().startMinutes[i]);
    }
    return out;
}

} // namespace

TEST(UiProgramEdit, StartTimesStaySortedAndUnique)
{
    ProgramEdit e(makeProgram(1, {360}, {{0, 600}}));
    EXPECT_FALSE(e.dirty());
    EXPECT_TRUE(e.setStart(1, 1200));
    EXPECT_TRUE(e.setStart(2, 300));
    EXPECT_EQ(starts(e), (std::vector<int>{300, 360, 1200}));
    EXPECT_TRUE(e.dirty());

    EXPECT_TRUE(e.setStart(0, 1200)) << "moving a start onto another merges them";
    EXPECT_EQ(starts(e), (std::vector<int>{360, 1200}));

    EXPECT_TRUE(e.setStart(0, -1)) << "negative removes";
    EXPECT_EQ(starts(e), (std::vector<int>{1200}));
    EXPECT_FALSE(e.setStart(3, 100)) << "index past the end";
    EXPECT_FALSE(e.setStart(1, -1)) << "nothing to remove";
    EXPECT_FALSE(e.setStart(0, 1440));
}

TEST(UiProgramEdit, AtMostFourStartTimes)
{
    ProgramEdit e(makeProgram(1, {60, 120, 180}, {}));
    EXPECT_TRUE(e.canAddStart());
    EXPECT_EQ(e.suggestedStart(), 300);
    EXPECT_TRUE(e.setStart(e.startCount(), e.suggestedStart()));
    EXPECT_FALSE(e.canAddStart());
    EXPECT_FALSE(e.setStart(e.startCount(), 600));
    EXPECT_EQ(e.startCount(), kMaxStartTimes);
}

TEST(UiProgramEdit, DayRules)
{
    ProgramEdit e(makeProgram(1, {360}, {{0, 600}}));
    e.setWeekdayMask(0);
    EXPECT_EQ(e.problem(8), Str::ProgWarnNoDays);

    e.setRule(DayRuleType::Interval, 20000);
    EXPECT_EQ(e.program().days.type, DayRuleType::Interval);
    EXPECT_EQ(e.program().days.intervalAnchorDay, 20000) << "counting starts today";
    e.setIntervalDays(3, 20005);
    EXPECT_EQ(e.program().days.intervalDays, 3);
    EXPECT_EQ(e.program().days.intervalAnchorDay, 20005);
    EXPECT_TRUE(e.program().days.matches(20005));
    EXPECT_FALSE(e.program().days.matches(20006));
    EXPECT_TRUE(e.program().days.matches(20008));
    e.setIntervalDays(999, 20005);
    EXPECT_EQ(e.program().days.intervalDays, 365);

    e.setRule(DayRuleType::Weekdays, 20005);
    EXPECT_EQ(e.program().days.weekdayMask, 0x7F) << "an empty week becomes every day";
    EXPECT_EQ(e.problem(8), Str::Count);
}

TEST(UiProgramEdit, ZoneMinutesMapToSecondsWithinTheZoneLimit)
{
    ProgramEdit e(makeProgram(1, {360}, {{2, 90}}));
    EXPECT_EQ(e.zoneMinutes(2), 2) << "90 s set elsewhere reads as 2 min";
    e.setZoneMinutes(0, 15, 3600);
    EXPECT_EQ(e.program().durationSec[0], 900u);
    e.setZoneMinutes(1, 500, 3600);
    EXPECT_EQ(e.program().durationSec[1], 3600u) << "clamped to the zone's longest run";
    e.setZoneMinutes(2, 0, 3600);
    EXPECT_EQ(e.program().durationSec[2], 0u);
    e.setZoneMinutes(9, 5, 3600); // out of range: ignored
    e.setZoneMinutes(0, 0, 3600);
    e.setZoneMinutes(1, 0, 3600);
    EXPECT_EQ(e.problem(8), Str::ProgWarnNoZones);
}

TEST(UiProgramEdit, ProblemsAreReportedInOrder)
{
    ProgramEdit e(Program{});
    ScheduleConfig empty;
    ASSERT_TRUE(ProgramEdit::makeNew(empty, e));
    EXPECT_EQ(e.problem(8), Str::ProgWarnNoStart);
    e.setStart(0, 360);
    EXPECT_EQ(e.problem(8), Str::ProgWarnNoZones);
    e.setZoneMinutes(5, 10, 3600);
    EXPECT_EQ(e.problem(4), Str::ProgWarnNoZones) << "zone 6 is not in use on a 4-zone setup";
    EXPECT_EQ(e.problem(8), Str::Count);
}

TEST(UiProgramEdit, NewProgramGetsAFreeIdAndIsValidForTheCore)
{
    Config cfg = Config::defaults();
    cfg.schedule.programs[0] = makeProgram(4, {360}, {{0, 600}});
    cfg.schedule.programs[1] = makeProgram(9, {420}, {{1, 600}});
    cfg.schedule.programCount = 2;

    ProgramEdit e(Program{});
    ASSERT_TRUE(ProgramEdit::makeNew(cfg.schedule, e));
    EXPECT_TRUE(e.isNew());
    EXPECT_FALSE(e.dirty()) << "an untouched new program is not saved";
    EXPECT_EQ(e.program().id, 10);
    EXPECT_STREQ(e.program().name.c_str(), "Program 3");
    e.setStart(0, 5 * 60 + 30);
    e.setZoneMinutes(3, 12, 3600);
    EXPECT_TRUE(e.dirty());

    ASSERT_TRUE(e.applyTo(cfg));
    EXPECT_EQ(cfg.schedule.programCount, 3);
    EXPECT_TRUE(validate(cfg).ok) << validate(cfg).path << ": " << validate(cfg).message;
    const Program &stored = cfg.schedule.programs[2];
    EXPECT_EQ(stored.id, 10);
    EXPECT_EQ(stored.startCount, 1);
    EXPECT_EQ(stored.startMinutes[0], 330);
    EXPECT_EQ(stored.durationSec[3], 720u);
    EXPECT_TRUE(stored.enabled);
}

TEST(UiProgramEdit, EditReplacesByIdAndSurvivesTheJsonRoundTrip)
{
    MemoryStorage storage;
    ConfigStore store(storage);
    Config cfg = Config::defaults();
    cfg.schedule.programs[0] = makeProgram(1, {360}, {{0, 600}});
    cfg.schedule.programs[1] = makeProgram(2, {420}, {{1, 600}});
    cfg.schedule.programCount = 2;
    ASSERT_TRUE(store.update(cfg).ok);

    ProgramEdit e(store.get().schedule.programs[1]);
    e.setStart(0, 21 * 60 + 15);
    e.setStart(1, 5 * 60);
    e.setRule(DayRuleType::EvenDays, 0);
    e.setZoneMinutes(1, 0, 3600);
    e.setZoneMinutes(6, 25, 3600);
    e.setSeasonal(false);
    e.setObeySensor(false);
    e.setEnabled(false);

    Config next = store.get();
    ASSERT_TRUE(e.applyTo(next));
    ASSERT_TRUE(store.update(next).ok);

    // Read back through the stored JSON, as after a reboot.
    ConfigStore reloaded(storage);
    reloaded.load();
    const ScheduleConfig &s = reloaded.get().schedule;
    ASSERT_EQ(s.programCount, 2);
    EXPECT_EQ(s.programs[0].startMinutes[0], 360) << "the other program is untouched";
    const Program &p = s.programs[1];
    EXPECT_EQ(p.id, 2);
    EXPECT_EQ(p.startCount, 2);
    EXPECT_EQ(p.startMinutes[0], 300);
    EXPECT_EQ(p.startMinutes[1], 1275);
    EXPECT_EQ(p.days.type, DayRuleType::EvenDays);
    EXPECT_EQ(p.durationSec[1], 0u);
    EXPECT_EQ(p.durationSec[6], 1500u);
    EXPECT_FALSE(p.useSeasonalAdjust);
    EXPECT_FALSE(p.obeySensorInhibit);
    EXPECT_FALSE(p.enabled);
    EXPECT_TRUE(programsEqual(p, e.program()));
}

TEST(UiProgramEdit, RemoveAndFullSchedule)
{
    Config cfg = Config::defaults();
    for (std::size_t i = 0; i < kMaxPrograms; ++i) {
        cfg.schedule.programs[i] = makeProgram(static_cast<uint16_t>(i + 1), {360}, {{0, 60}});
    }
    cfg.schedule.programCount = kMaxPrograms;

    ProgramEdit e(Program{});
    EXPECT_FALSE(ProgramEdit::makeNew(cfg.schedule, e));

    EXPECT_TRUE(ProgramEdit::removeFrom(cfg, 3));
    EXPECT_FALSE(ProgramEdit::removeFrom(cfg, 3));
    EXPECT_EQ(cfg.schedule.programCount, kMaxPrograms - 1);
    EXPECT_EQ(cfg.schedule.programs[2].id, 4) << "later programs move up";
    EXPECT_TRUE(validate(cfg).ok);
    EXPECT_TRUE(ProgramEdit::makeNew(cfg.schedule, e));
    EXPECT_EQ(e.program().id, kMaxPrograms + 1);
}
