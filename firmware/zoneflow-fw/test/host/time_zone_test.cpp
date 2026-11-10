// SPDX-License-Identifier: Apache-2.0
// Expected transition instants are worked out by hand from the calendar:
// 2026-01-01 is a Thursday, so 2026-03-01 and 2026-11-01 are Sundays,
// 2026-03-29 and 2026-10-25 are the last Sundays of March and October,
// 2026-04-05 and 2026-10-04 the first Sundays of April and October.
#include <gtest/gtest.h>

#include "fakes.hpp"
#include "zf/time/time_zone.hpp"

using zf::TimeZone;
using zf::test::utcOf;

namespace {
const char *kBerlin = "CET-1CEST,M3.5.0,M10.5.0/3";
const char *kNewYork = "EST5EDT,M3.2.0,M11.1.0";
const char *kSydney = "AEST-10AEDT,M10.1.0,M4.1.0/3";
} // namespace

TEST(TimeZone, RejectsMalformed)
{
    for (const char *s : {"", "X0", "UTC", "CET-1CEST,M3.5.0", "CET-1CEST,M13.5.0,M10.5.0", "CET-1CEST,M3.6.0,M10.5.0",
                          "UTC0 junk", "CET-1CEST,M3.5.0,M10.5.0/3x", "<AB>1", "CET-25"}) {
        EXPECT_FALSE(TimeZone::parse(s).has_value()) << s;
    }
}

TEST(TimeZone, Utc)
{
    auto tz = TimeZone::parse("UTC0");
    ASSERT_TRUE(tz);
    EXPECT_FALSE(tz->hasDst());
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 7, 1)), 0);
    EXPECT_EQ(tz->fromLocal(zf::civil::daysFromCivil(2026, 7, 1), 3600), utcOf(2026, 7, 1, 1));
}

TEST(TimeZone, CentralEuropeTransitions)
{
    auto tz = TimeZone::parse(kBerlin);
    ASSERT_TRUE(tz);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 3, 29, 0, 59, 59)), 3600);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 3, 29, 1, 0, 0)), 7200);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 10, 25, 0, 59, 59)), 7200);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 10, 25, 1, 0, 0)), 3600);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 1, 15)), 3600);
}

TEST(TimeZone, GapMapsToTransitionInstant)
{
    auto tz = TimeZone::parse(kBerlin);
    ASSERT_TRUE(tz);
    const int64_t day = zf::civil::daysFromCivil(2026, 3, 29);
    // 02:30 local does not exist; first valid instant is 03:00 CEST = 01:00Z.
    EXPECT_EQ(tz->fromLocal(day, 2 * 3600 + 1800), utcOf(2026, 3, 29, 1, 0));
    EXPECT_EQ(tz->fromLocal(day, 2 * 3600), utcOf(2026, 3, 29, 1, 0));
    // Just outside the gap.
    EXPECT_EQ(tz->fromLocal(day, 3600 + 59 * 60), utcOf(2026, 3, 29, 0, 59));
    EXPECT_EQ(tz->fromLocal(day, 3 * 3600), utcOf(2026, 3, 29, 1, 0));
}

TEST(TimeZone, AmbiguousMapsToEarlierInstant)
{
    auto tz = TimeZone::parse(kBerlin);
    ASSERT_TRUE(tz);
    const int64_t day = zf::civil::daysFromCivil(2026, 10, 25);
    // 02:30 local happens at 00:30Z (CEST) and 01:30Z (CET).
    EXPECT_EQ(tz->fromLocal(day, 2 * 3600 + 1800), utcOf(2026, 10, 25, 0, 30));
}

TEST(TimeZone, FromLocalIsMonotonicAcrossTransitions)
{
    for (const char *s : {kBerlin, kNewYork, kSydney}) {
        auto tz = TimeZone::parse(s);
        ASSERT_TRUE(tz);
        for (int y = 2025; y <= 2027; ++y) {
            for (unsigned m = 1; m <= 12; ++m) {
                const int64_t first = zf::civil::daysFromCivil(y, m, 1);
                for (int64_t d = first; d < first + 28; ++d) {
                    zf::UtcSeconds prev = tz->fromLocal(d, 0);
                    for (int32_t sec = 60; sec < 86400; sec += 60) {
                        const zf::UtcSeconds cur = tz->fromLocal(d, sec);
                        ASSERT_GE(cur, prev) << s << " day " << d << " sec " << sec;
                        prev = cur;
                    }
                }
            }
        }
    }
}

TEST(TimeZone, UsRulesAndDefaultRule)
{
    auto tz = TimeZone::parse(kNewYork);
    auto dflt = TimeZone::parse("EST5EDT");
    ASSERT_TRUE(tz);
    ASSERT_TRUE(dflt);
    // 2026-03-08 02:00 EST = 07:00Z; 2026-11-01 02:00 EDT = 06:00Z.
    for (const auto &z : {*tz, *dflt}) {
        EXPECT_EQ(z.offsetAt(utcOf(2026, 3, 8, 6, 59, 59)), -5 * 3600);
        EXPECT_EQ(z.offsetAt(utcOf(2026, 3, 8, 7, 0, 0)), -4 * 3600);
        EXPECT_EQ(z.offsetAt(utcOf(2026, 11, 1, 5, 59, 59)), -4 * 3600);
        EXPECT_EQ(z.offsetAt(utcOf(2026, 11, 1, 6, 0, 0)), -5 * 3600);
    }
}

TEST(TimeZone, SouthernHemisphere)
{
    auto tz = TimeZone::parse(kSydney);
    ASSERT_TRUE(tz);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 1, 15)), 11 * 3600);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 7, 15)), 10 * 3600);
    // Ends 2026-04-05 03:00 AEDT = 2026-04-04 16:00Z.
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 4, 4, 15, 59, 59)), 11 * 3600);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 4, 4, 16, 0, 0)), 10 * 3600);
    // Starts 2026-10-04 02:00 AEST = 2026-10-03 16:00Z.
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 10, 3, 15, 59, 59)), 10 * 3600);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 10, 3, 16, 0, 0)), 11 * 3600);
}

TEST(TimeZone, QuotedNamesAndHalfHourOffsets)
{
    auto tz = TimeZone::parse("<+1030>-10:30<+11>-11,M10.1.0,M4.1.0");
    ASSERT_TRUE(tz);
    EXPECT_EQ(tz->standardOffset(), 37800);
    EXPECT_EQ(tz->dstOffset(), 39600);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 7, 1)), 37800);
    EXPECT_EQ(tz->offsetAt(utcOf(2026, 1, 1)), 39600);
}

TEST(TimeZone, JulianRuleSkipsFebruary29)
{
    auto tz = TimeZone::parse("AAA0BBB,J60/0,J300/0");
    ASSERT_TRUE(tz);
    // J60 is always March 1.
    EXPECT_EQ(tz->offsetAt(utcOf(2028, 2, 29, 23, 59, 59)), 0);
    EXPECT_EQ(tz->offsetAt(utcOf(2028, 3, 1)), 3600);
    EXPECT_EQ(tz->offsetAt(utcOf(2027, 2, 28, 23, 59, 59)), 0);
    EXPECT_EQ(tz->offsetAt(utcOf(2027, 3, 1)), 3600);
}

TEST(TimeZone, ToLocal)
{
    auto tz = TimeZone::parse(kBerlin);
    ASSERT_TRUE(tz);
    const auto l = tz->toLocal(utcOf(2026, 7, 1, 12, 0, 5));
    EXPECT_EQ(l.hour(), 14);
    EXPECT_EQ(l.second(), 5);
    EXPECT_TRUE(l.dst);
    EXPECT_EQ(l.weekday, 3u); // Wednesday
    EXPECT_EQ(l.date.month, 7u);
    // Late evening UTC is already the next local day.
    EXPECT_EQ(tz->localDay(utcOf(2026, 7, 1, 23, 30)), zf::civil::daysFromCivil(2026, 7, 2));
}
