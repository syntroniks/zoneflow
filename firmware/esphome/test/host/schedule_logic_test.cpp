// SPDX-License-Identifier: Apache-2.0
// When a program of the ESPHome package's scheduler is due.
#include <gtest/gtest.h>

#include "schedule_logic.h"

namespace {

using namespace zfe::sched;

LocalMinute at(int y, unsigned mo, unsigned d, int h, int mi) { return local_minute(y, mo, d, h, mi); }

ProgramSpec program(int16_t start, DayChoice days = DayChoice::EveryDay, uint8_t mask = 0) {
  ProgramSpec p;
  p.enabled = true;
  p.start_minute[0] = start;
  p.days = day_rule(days, mask);
  return p;
}

TEST(ScheduleLogic, DayChoiceLabelsRoundTrip) {
  for (std::size_t i = 0; i < kDayChoiceCount; ++i) {
    const auto c = static_cast<DayChoice>(i);
    EXPECT_EQ(day_choice_from_label(day_choice_label(c)), static_cast<int>(i));
  }
  EXPECT_EQ(day_choice_from_label("Fortnightly"), -1);
  EXPECT_EQ(day_choice_from_label(nullptr), -1);
}

TEST(ScheduleLogic, DayRules) {
  // 2026-11-09 is a Monday, 2026-11-08 a Sunday.
  const int64_t mon = at(2026, 11, 9, 0, 0).day;
  const int64_t sun = at(2026, 11, 8, 0, 0).day;
  const int64_t sat = at(2026, 11, 7, 0, 0).day;
  EXPECT_TRUE(day_rule(DayChoice::Weekdays, 0).matches(mon));
  EXPECT_FALSE(day_rule(DayChoice::Weekdays, 0).matches(sun));
  EXPECT_TRUE(day_rule(DayChoice::Weekends, 0).matches(sun));
  EXPECT_TRUE(day_rule(DayChoice::Weekends, 0).matches(sat));
  EXPECT_FALSE(day_rule(DayChoice::Weekends, 0).matches(mon));
  EXPECT_TRUE(day_rule(DayChoice::OddDays, 0).matches(mon));  // the 9th
  EXPECT_FALSE(day_rule(DayChoice::EvenDays, 0).matches(mon));
  EXPECT_TRUE(day_rule(DayChoice::Custom, 0x02).matches(mon));  // bit 1 = Monday
  EXPECT_FALSE(day_rule(DayChoice::Custom, 0x02).matches(sun));
  const auto every3 = day_rule(DayChoice::Every3Days, 0);
  int hits = 0;
  for (int64_t d = mon; d < mon + 30; ++d)
    hits += every3.matches(d) ? 1 : 0;
  EXPECT_EQ(hits, 10);
}

TEST(ScheduleLogic, NoStoredKeyMeansNoCatchUp) {
  int64_t last = kNoKey;
  const Decision d = evaluate(program(6 * 60), at(2026, 11, 10, 6, 30), 60, last);
  EXPECT_FALSE(d.run);
  EXPECT_EQ(d.missed, 0);
  EXPECT_EQ(last, at(2026, 11, 10, 6, 30).key());
}

TEST(ScheduleLogic, RunsOnTimeOnceOnly) {
  const ProgramSpec p = program(6 * 60);
  int64_t last = at(2026, 11, 10, 5, 0).key();
  EXPECT_FALSE(evaluate(p, at(2026, 11, 10, 5, 59), 60, last).run);
  Decision d = evaluate(p, at(2026, 11, 10, 6, 0), 60, last);
  EXPECT_TRUE(d.run);
  EXPECT_FALSE(d.late);
  // Evaluated every second: the same minute again, and a reboot that reads the key back.
  EXPECT_FALSE(evaluate(p, at(2026, 11, 10, 6, 0), 60, last).run);
  EXPECT_FALSE(evaluate(p, at(2026, 11, 10, 6, 1), 60, last).run);
  // Next day.
  EXPECT_TRUE(evaluate(p, at(2026, 11, 11, 6, 0), 60, last).run);
}

TEST(ScheduleLogic, PowerCutWithinGraceRunsLate) {
  const ProgramSpec p = program(6 * 60);
  int64_t last = at(2026, 11, 10, 5, 30).key();
  const Decision d = evaluate(p, at(2026, 11, 10, 6, 20), 60, last);
  EXPECT_TRUE(d.run);
  EXPECT_TRUE(d.late);
  EXPECT_EQ(d.start_key, at(2026, 11, 10, 6, 0).key());
}

TEST(ScheduleLogic, PowerCutBeyondGraceIsMissedOnce) {
  const ProgramSpec p = program(6 * 60);
  int64_t last = at(2026, 11, 10, 5, 30).key();
  Decision d = evaluate(p, at(2026, 11, 10, 7, 30), 60, last);
  EXPECT_FALSE(d.run);
  EXPECT_EQ(d.missed, 1);
  d = evaluate(p, at(2026, 11, 10, 7, 31), 60, last);
  EXPECT_EQ(d.missed, 0) << "reported once";
}

TEST(ScheduleLogic, GraceZeroNeverCatchesUp) {
  int64_t last = at(2026, 11, 10, 5, 30).key();
  EXPECT_FALSE(evaluate(program(6 * 60), at(2026, 11, 10, 6, 1), 0, last).run);
}

TEST(ScheduleLogic, OnlyTheNewestOfSeveralMissedStartsRuns) {
  ProgramSpec p = program(5 * 60);
  p.start_minute[1] = 6 * 60;
  p.start_minute[2] = 6 * 60;  // listed twice: counts once
  int64_t last = at(2026, 11, 10, 4, 0).key();
  const Decision d = evaluate(p, at(2026, 11, 10, 6, 10), 60, last);
  EXPECT_TRUE(d.run);
  EXPECT_EQ(d.start_key, at(2026, 11, 10, 6, 0).key());
  EXPECT_EQ(d.missed, 1);
}

TEST(ScheduleLogic, WrongDayDoesNotRun) {
  const ProgramSpec p = program(6 * 60, DayChoice::Weekends);
  int64_t last = at(2026, 11, 10, 5, 0).key();  // a Tuesday
  EXPECT_FALSE(evaluate(p, at(2026, 11, 10, 6, 0), 60, last).run);
}

TEST(ScheduleLogic, StartJustBeforeMidnightCaughtUpAfterMidnight) {
  const ProgramSpec p = program(23 * 60 + 50);
  int64_t last = at(2026, 11, 10, 23, 0).key();
  const Decision d = evaluate(p, at(2026, 11, 11, 0, 20), 60, last);
  EXPECT_TRUE(d.run);
  EXPECT_EQ(d.start_key, at(2026, 11, 10, 23, 50).key());
}

TEST(ScheduleLogic, SpringForwardGapRunsAtTheEndOfTheGap) {
  // Local time jumps from 01:59 to 03:00; a 02:30 start never appears.
  const ProgramSpec p = program(2 * 60 + 30);
  int64_t last = at(2026, 3, 29, 1, 59).key();
  const Decision d = evaluate(p, at(2026, 3, 29, 3, 0), 60, last);
  EXPECT_TRUE(d.run);
  EXPECT_TRUE(d.late);
}

TEST(ScheduleLogic, FallBackRepeatedHourRunsOnce) {
  const ProgramSpec p = program(1 * 60 + 30);
  int64_t last = at(2026, 10, 25, 1, 0).key();
  EXPECT_TRUE(evaluate(p, at(2026, 10, 25, 1, 30), 60, last).run);
  // 02:00 summer time becomes 01:00 winter time: the 01:30 comes round again.
  EXPECT_FALSE(evaluate(p, at(2026, 10, 25, 1, 0), 60, last).run);
  EXPECT_FALSE(evaluate(p, at(2026, 10, 25, 1, 30), 60, last).run);
  EXPECT_FALSE(evaluate(p, at(2026, 10, 25, 1, 45), 60, last).run);
}

TEST(ScheduleLogic, ClockSetBackMoreThanADayRearms) {
  const ProgramSpec p = program(6 * 60);
  int64_t last = at(2027, 1, 1, 6, 0).key();  // an RTC that was in the future
  EXPECT_FALSE(evaluate(p, at(2026, 11, 10, 5, 0), 60, last).run);
  EXPECT_EQ(last, at(2026, 11, 10, 5, 0).key());
  EXPECT_TRUE(evaluate(p, at(2026, 11, 10, 6, 0), 60, last).run);
}

TEST(ScheduleLogic, DisabledProgramDoesNotRunAndEnablingDoesNotCatchUp) {
  ProgramSpec p = program(6 * 60);
  int64_t last = at(2026, 11, 10, 5, 0).key();
  p.enabled = false;
  EXPECT_FALSE(evaluate(p, at(2026, 11, 10, 6, 0), 60, last).run);
  p.enabled = true;
  EXPECT_FALSE(evaluate(p, at(2026, 11, 10, 6, 10), 60, last).run);
  EXPECT_TRUE(evaluate(p, at(2026, 11, 11, 6, 0), 60, last).run);
}

TEST(ScheduleLogic, EditingAStartTimeToThePastDoesNotStartIt) {
  ProgramSpec p = program(7 * 60);
  int64_t last = at(2026, 11, 9, 7, 0).key();  // yesterday's run
  const LocalMinute now = at(2026, 11, 10, 6, 0);
  p.start_minute[0] = 5 * 60 + 30;  // edited at 06:00 to 05:30
  rearm(now, last);
  EXPECT_FALSE(evaluate(p, now, 60, last).run);
  EXPECT_TRUE(evaluate(p, at(2026, 11, 11, 5, 30), 60, last).run);
}

TEST(ScheduleLogic, NextStart) {
  ProgramSpec p = program(6 * 60, DayChoice::Weekends);
  p.start_minute[1] = 18 * 60;
  // Tuesday 2026-11-10 07:00: next is Saturday 06:00.
  int64_t k = next_start(p, at(2026, 11, 10, 7, 0));
  EXPECT_EQ(k, at(2026, 11, 14, 6, 0).key());
  KeyParts parts = key_parts(k);
  EXPECT_EQ(parts.weekday, 6u);
  EXPECT_EQ(parts.hour, 6);
  EXPECT_EQ(parts.minute, 0);
  // Saturday 06:00 exactly: the next one is 18:00, strictly after now.
  EXPECT_EQ(next_start(p, at(2026, 11, 14, 6, 0)), at(2026, 11, 14, 18, 0).key());
  p.enabled = false;
  EXPECT_EQ(next_start(p, at(2026, 11, 10, 7, 0)), kNoKey);
  ProgramSpec none = program(-1);
  EXPECT_EQ(next_start(none, at(2026, 11, 10, 7, 0)), kNoKey);
}

TEST(ScheduleLogic, ZoneLists) {
  EXPECT_EQ(parse_zone_list("all", 8), 0xFF);
  EXPECT_EQ(parse_zone_list("", 4), 0x0F);
  EXPECT_EQ(parse_zone_list(nullptr, 4), 0x0F);
  EXPECT_EQ(parse_zone_list("1,2,5", 8), 0x13);
  EXPECT_EQ(parse_zone_list(" 3 4 ", 8), 0x0C);
  EXPECT_EQ(parse_zone_list("5, 6", 4), 0x00) << "zones the board does not have are ignored";
  EXPECT_EQ(parse_zone_list("8", 8), 0x80);
  EXPECT_EQ(parse_zone_list("12, x, 2", 8), 0x02);
}

TEST(ScheduleLogic, RainDelay) {
  EXPECT_TRUE(rain_delay_active(1000, 999));
  EXPECT_FALSE(rain_delay_active(1000, 1000));
  EXPECT_FALSE(rain_delay_active(0, 5));
}

}  // namespace
