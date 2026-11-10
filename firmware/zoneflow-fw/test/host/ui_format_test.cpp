// SPDX-License-Identifier: Apache-2.0
// Text formatting, the next-run forecast, the time zone table, fault texts.
#include <gtest/gtest.h>

#include <cstring>
#include <set>

#include "fakes.hpp"
#include "zf_ui/faults.hpp"
#include "zf_ui/format.hpp"
#include "zf_ui/next_run.hpp"
#include "zf_ui/tz_table.hpp"

using namespace zf;
using namespace zf::test;
using namespace zf::ui;

namespace {

const TimeZone kBerlin = *TimeZone::parse("CET-1CEST,M3.5.0,M10.5.0/3");

// Local Berlin time to UTC.
UtcSeconds berlin(int y, unsigned mo, unsigned d, int h, int mi)
{
    return kBerlin.fromLocal(civil::daysFromCivil(y, mo, d), h * 3600 + mi * 60);
}

} // namespace

TEST(UiFormat, Durations)
{
    EXPECT_EQ(clockText(6 * 60), "06:00");
    EXPECT_EQ(clockText(23 * 60 + 5), "23:05");
    EXPECT_EQ(countdownText(0), "0:00");
    EXPECT_EQ(countdownText(754), "12:34");
    EXPECT_EQ(countdownText(3900), "1:05:00");
    EXPECT_EQ(durationText(45), "45 s");
    EXPECT_EQ(durationText(600), "10 min");
    EXPECT_EQ(durationText(150), "2:30 min");
    EXPECT_EQ(durationText(3600), "1 h");
    EXPECT_EQ(durationText(5400), "1 h 30 min");
}

TEST(UiFormat, Measurements)
{
    EXPECT_EQ(currentText(320), "320 mA");
    EXPECT_EQ(currentText(-1), "No data");
    EXPECT_EQ(flowText(12.34f), "12.3 L/min");
    EXPECT_EQ(flowText(0.0f), "0.0 L/min");
    EXPECT_EQ(litresText(1234.4f), "1234 L");
    EXPECT_EQ(percentText(100), "100 %");
}

TEST(UiFormat, Weekdays)
{
    EXPECT_EQ(weekdaysText(0x7F), "Every day");
    EXPECT_EQ(weekdaysText(0), "None");
    EXPECT_EQ(weekdaysText(0b0101010), "Mo We Fr");
    EXPECT_EQ(weekdaysText(0b1000001), "Sa Su") << "Monday first, Sunday last";
}

TEST(UiFormat, DayWords)
{
    const UtcSeconds now = berlin(2026, 11, 10, 14, 32); // a Tuesday
    EXPECT_EQ(dateText(kBerlin.toLocal(now)), "Tue 10 Nov");
    EXPECT_EQ(whenText(berlin(2026, 11, 10, 18, 0), now, kBerlin), "Today 18:00");
    EXPECT_EQ(whenText(berlin(2026, 11, 11, 6, 0), now, kBerlin), "Tomorrow 06:00");
    EXPECT_EQ(whenText(berlin(2026, 11, 13, 6, 0), now, kBerlin), "Fri 06:00");
    EXPECT_EQ(whenText(berlin(2026, 11, 19, 6, 0), now, kBerlin), "Thu 19 Nov 06:00");
    EXPECT_EQ(whenShortText(berlin(2026, 11, 10, 18, 0), now, kBerlin), "Today 18:00");
    EXPECT_EQ(whenShortText(berlin(2026, 11, 11, 6, 0), now, kBerlin), "Wed 06:00");
    EXPECT_EQ(whenShortText(berlin(2026, 11, 19, 6, 0), now, kBerlin), "19 Nov 06:00");
    // 00:30 tomorrow is "Tomorrow" even though it is under 24 h away.
    EXPECT_EQ(dayText(berlin(2026, 11, 11, 0, 30), berlin(2026, 11, 10, 23, 50), kBerlin), "Tomorrow");
}

TEST(UiStrings, EveryEntryHasText)
{
    for (unsigned i = 0; i < static_cast<unsigned>(Str::Count); ++i) {
        const char *s = tr(static_cast<Str>(i));
        ASSERT_NE(s, nullptr);
        EXPECT_GT(std::strlen(s), 0u) << "string " << i;
    }
    EXPECT_STREQ(tr(Str::Count), "");
}

TEST(UiNextRun, WeekdayProgram)
{
    Program p = makeProgram(1, {6 * 60, 18 * 60}, {{0, 600}});
    p.days.weekdayMask = 0b0101010; // Mon, Wed, Fri
    const UtcSeconds tuesdayAfternoon = berlin(2026, 11, 10, 14, 32);
    NextRun n = nextStart(p, kBerlin, tuesdayAfternoon, 0);
    ASSERT_TRUE(n.valid);
    EXPECT_EQ(n.at, berlin(2026, 11, 11, 6, 0));
    // On a run day, the later start of the same day comes first.
    n = nextStart(p, kBerlin, berlin(2026, 11, 11, 6, 0), 0);
    EXPECT_EQ(n.at, berlin(2026, 11, 11, 18, 0)) << "a start at exactly now is in the past";
    n = nextStart(p, kBerlin, berlin(2026, 11, 13, 18, 0), 0);
    EXPECT_EQ(n.at, berlin(2026, 11, 16, 6, 0));
}

TEST(UiNextRun, IntervalOddAndEven)
{
    Program p = makeProgram(1, {7 * 60}, {{0, 600}});
    p.days.type = DayRuleType::Interval;
    p.days.intervalDays = 3;
    p.days.intervalAnchorDay = static_cast<int32_t>(civil::daysFromCivil(2026, 11, 9));
    NextRun n = nextStart(p, kBerlin, berlin(2026, 11, 10, 14, 32), 0);
    EXPECT_EQ(n.at, berlin(2026, 11, 12, 7, 0));

    p.days.type = DayRuleType::OddDays;
    n = nextStart(p, kBerlin, berlin(2026, 11, 30, 8, 0), 0);
    EXPECT_EQ(n.at, berlin(2026, 12, 1, 7, 0));
    p.days.type = DayRuleType::EvenDays;
    n = nextStart(p, kBerlin, berlin(2026, 11, 30, 8, 0), 0);
    EXPECT_EQ(n.at, berlin(2026, 12, 2, 7, 0));
}

TEST(UiNextRun, NothingPlanned)
{
    Program p = makeProgram(1, {6 * 60}, {{0, 600}});
    const UtcSeconds now = berlin(2026, 11, 10, 14, 32);
    p.enabled = false;
    EXPECT_FALSE(nextStart(p, kBerlin, now, 0).valid);
    p.enabled = true;
    p.days.weekdayMask = 0;
    EXPECT_FALSE(nextStart(p, kBerlin, now, 0).valid);
    p.days.weekdayMask = 0x7F;
    p.startCount = 0;
    EXPECT_FALSE(nextStart(p, kBerlin, now, 0).valid);
}

TEST(UiNextRun, RainDelayPushesTheForecast)
{
    ScheduleConfig s;
    s.programs[0] = makeProgram(1, {6 * 60}, {{0, 600}});
    s.programs[1] = makeProgram(2, {5 * 60}, {}); // no zones: never runs
    s.programs[2] = makeProgram(3, {20 * 60}, {{1, 300}});
    s.programCount = 3;
    const UtcSeconds now = berlin(2026, 11, 10, 14, 32);

    NextRun n = nextRun(s, kBerlin, now, 0);
    ASSERT_TRUE(n.valid);
    EXPECT_EQ(n.programId, 3);
    EXPECT_EQ(n.at, berlin(2026, 11, 10, 20, 0));

    // Delay until Thursday 07:00: Tuesday 20:00, Wednesday and Thursday 06:00 are skipped.
    n = nextRun(s, kBerlin, now, berlin(2026, 11, 12, 7, 0));
    EXPECT_EQ(n.programId, 3);
    EXPECT_EQ(n.at, berlin(2026, 11, 12, 20, 0));
    // A start exactly when the delay ends does run.
    n = nextRun(s, kBerlin, now, berlin(2026, 11, 12, 6, 0));
    EXPECT_EQ(n.programId, 1);
    EXPECT_EQ(n.at, berlin(2026, 11, 12, 6, 0));
}

TEST(UiNextRun, StartInsideTheDstGapRunsAtItsEnd)
{
    Program p = makeProgram(1, {2 * 60 + 30}, {{0, 600}});
    // 2027-03-28: 02:00 CET jumps to 03:00 CEST. Same answer as the scheduler:
    // the first valid instant after the gap.
    const NextRun n = nextStart(p, kBerlin, berlin(2027, 3, 27, 12, 0), 0);
    ASSERT_TRUE(n.valid);
    EXPECT_EQ(n.at, berlin(2027, 3, 28, 3, 0));
    EXPECT_EQ(clockText(kBerlin.toLocal(n.at).secondOfDay / 60), "03:00");
}

TEST(UiNextRun, ProgramTotalsFollowTheSchedulerRounding)
{
    ScheduleConfig s;
    s.seasonalPercent = 150;
    Program p = makeProgram(1, {360}, {{0, 600}, {1, 101}, {3, 300}, {7, 60}});
    bool enabled[kMaxZones] = {true, true, true, false, true, true, true, true};
    ProgramTotals t = programTotals(p, s, 6, enabled);
    EXPECT_EQ(t.zones, 2) << "zone 4 is disabled, zone 8 is not in use";
    EXPECT_EQ(t.totalSec, 900u + 152u); // 101 * 1.5 = 151.5 rounds to 152
    p.useSeasonalAdjust = false;
    t = programTotals(p, s, 6, enabled);
    EXPECT_EQ(t.totalSec, 701u);
}

TEST(UiTimeZones, EveryEntryParsesAndIsUnique)
{
    std::set<std::string> seen;
    int lastOffset = -100000;
    for (std::size_t i = 0; i < tzCount(); ++i) {
        const TzEntry &e = tzEntry(i);
        const auto tz = TimeZone::parse(e.posix);
        ASSERT_TRUE(tz.has_value()) << e.posix;
        EXPECT_TRUE(seen.insert(e.posix).second) << "duplicate " << e.posix;
        EXPECT_LE(std::strlen(e.posix), 63u) << "fits Config::timezone";
        EXPECT_GE(tz->standardOffset(), lastOffset) << "ordered west to east: " << e.cities;
        lastOffset = tz->standardOffset();
        EXPECT_EQ(tzFind(e.posix), static_cast<int>(i));
    }
    EXPECT_EQ(tzFind("Mars/Olympus"), -1);
    EXPECT_EQ(tzOffsetText("CET-1CEST,M3.5.0,M10.5.0/3"), "+01:00");
    EXPECT_EQ(tzOffsetText("<+0330>-3:30"), "+03:30");
    EXPECT_EQ(tzOffsetText("PST8PDT,M3.2.0,M11.1.0"), "-08:00");
    EXPECT_EQ(tzOffsetText("nonsense"), "?");
}

TEST(UiFaults, ListAndTexts)
{
    SystemState s;
    EXPECT_TRUE(listFaults(s).empty());

    auto bit = [](FaultCode c) { return 1u << static_cast<unsigned>(c); };
    s.zones[2].fault = FaultCode::ZoneOpenCircuit;
    s.zones[5].fault = FaultCode::ShortCircuit;
    s.activeFaults = bit(FaultCode::ZoneOpenCircuit) | bit(FaultCode::ShortCircuit) | bit(FaultCode::AcLost) |
                     bit(FaultCode::ZoneOverCurrent);
    const std::vector<FaultItem> faults = listFaults(s);
    ASSERT_EQ(faults.size(), 4u);
    EXPECT_EQ(faultTitle(faults[0]), "Zone 3: no current");
    EXPECT_EQ(faultTitle(faults[1]), "Zone 6: short circuit");
    // Over-current that no zone carries belongs to the master valve.
    EXPECT_EQ(faultTitle(faults[2]), "Master valve: current too high");
    EXPECT_EQ(faultTitle(faults[3]), "No 24 V AC supply");
    EXPECT_TRUE(faultClearsItself(FaultCode::AcLost));
    EXPECT_FALSE(faultClearsItself(FaultCode::ShortCircuit));

    for (unsigned c = 1; c < static_cast<unsigned>(FaultCode::Count); ++c) {
        const auto code = static_cast<FaultCode>(c);
        EXPECT_FALSE(faultTitle(FaultItem{code, 0}).empty()) << toString(code);
        EXPECT_NE(faultBody(code), Str::Count) << toString(code);
    }
}
