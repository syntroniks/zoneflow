// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "zf/time/civil.hpp"

using namespace zf::civil;

TEST(Civil, EpochAndKnownDates)
{
    EXPECT_EQ(daysFromCivil(1970, 1, 1), 0);
    EXPECT_EQ(weekdayFromDays(0), 4u); // Thursday
    EXPECT_EQ(daysFromCivil(2000, 3, 1), 11017);
    EXPECT_EQ(daysFromCivil(1969, 12, 31), -1);
    EXPECT_EQ(weekdayFromDays(-1), 3u);
    EXPECT_EQ(weekdayFromDays(daysFromCivil(2026, 11, 10)), 2u); // Tuesday
}

TEST(Civil, RoundTripOverFourCenturies)
{
    for (int64_t z = daysFromCivil(1900, 1, 1); z < daysFromCivil(2300, 1, 1); z += 7) {
        const Date d = civilFromDays(z);
        ASSERT_EQ(daysFromCivil(d.year, d.month, d.day), z);
        ASSERT_GE(d.day, 1u);
        ASSERT_LE(d.day, daysInMonth(d.year, d.month));
    }
}

TEST(Civil, LeapYears)
{
    EXPECT_TRUE(isLeapYear(2000));
    EXPECT_FALSE(isLeapYear(2100));
    EXPECT_TRUE(isLeapYear(2028));
    EXPECT_EQ(daysInMonth(2028, 2), 29u);
    EXPECT_EQ(daysInMonth(2026, 2), 28u);
}

TEST(Civil, FloorDivAndMod)
{
    EXPECT_EQ(floorDiv(-1, 86400), -1);
    EXPECT_EQ(floorDiv(86399, 86400), 0);
    EXPECT_EQ(floorMod(-1, 7), 6);
    EXPECT_EQ(floorMod(14, 7), 0);
}
