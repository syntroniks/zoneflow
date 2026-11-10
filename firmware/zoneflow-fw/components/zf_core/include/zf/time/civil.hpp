// SPDX-License-Identifier: Apache-2.0
// Proleptic Gregorian calendar arithmetic on day serials (days since
// 1970-01-01). Algorithms after H. Hinnant, "chrono-Compatible Low-Level Date
// Algorithms".
#pragma once

#include <cstdint>

namespace zf::civil {

inline constexpr int64_t kSecondsPerDay = 86400;

struct Date {
    int year;
    unsigned month; // 1..12
    unsigned day;   // 1..31
};

constexpr int64_t floorDiv(int64_t a, int64_t b)
{
    const int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

constexpr int64_t floorMod(int64_t a, int64_t b) { return a - floorDiv(a, b) * b; }

constexpr int64_t daysFromCivil(int y, unsigned m, unsigned d)
{
    y -= m <= 2 ? 1 : 0;
    const int64_t era = floorDiv(y, 400);
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

constexpr Date civilFromDays(int64_t z)
{
    z += 719468;
    const int64_t era = floorDiv(z, 146097);
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t y = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return Date{static_cast<int>(y + (m <= 2 ? 1 : 0)), m, d};
}

// 0 = Sunday ... 6 = Saturday.
constexpr unsigned weekdayFromDays(int64_t z) { return static_cast<unsigned>(floorMod(z + 4, 7)); }

constexpr bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

constexpr unsigned daysInMonth(int y, unsigned m)
{
    constexpr unsigned kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && isLeapYear(y)) ? 29 : kDays[m - 1];
}

// Local wall-clock time as produced by TimeZone::toLocal.
struct LocalDateTime {
    Date date;
    int64_t daySerial;    // local days since 1970-01-01
    int32_t secondOfDay;  // 0..86399
    unsigned weekday;     // 0 = Sunday
    int32_t utcOffsetSec; // local = UTC + offset
    bool dst;

    int hour() const { return secondOfDay / 3600; }
    int minute() const { return (secondOfDay / 60) % 60; }
    int second() const { return secondOfDay % 60; }
};

} // namespace zf::civil
