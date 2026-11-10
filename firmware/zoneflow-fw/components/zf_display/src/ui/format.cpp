// SPDX-License-Identifier: Apache-2.0
#include "zf_ui/format.hpp"

#include <cstdarg>
#include <cstdio>

namespace zf::ui {
namespace {

constexpr Str kWeekdayShort[7] = {Str::Sun, Str::Mon, Str::Tue, Str::Wed, Str::Thu, Str::Fri, Str::Sat};
constexpr Str kWeekdayTwo[7] = {Str::Su, Str::Mo, Str::Tu, Str::We, Str::Th, Str::Fr, Str::Sa};
constexpr Str kMonth[12] = {Str::Jan, Str::Feb, Str::Mar, Str::Apr, Str::May, Str::Jun,
                            Str::Jul, Str::Aug, Str::Sep, Str::Oct, Str::Nov, Str::Dec};

} // namespace

std::string fmt(Str id, ...)
{
    char buf[160];
    va_list args;
    va_start(args, id);
    std::vsnprintf(buf, sizeof buf, tr(id), args);
    va_end(args);
    return buf;
}

Str weekdayShort(unsigned weekday) { return kWeekdayShort[weekday % 7]; }
Str weekdayTwo(unsigned weekday) { return kWeekdayTwo[weekday % 7]; }

std::string clockText(int minuteOfDay)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", (minuteOfDay / 60) % 24, minuteOfDay % 60);
    return buf;
}

std::string countdownText(uint32_t sec)
{
    char buf[16];
    if (sec >= 3600) {
        std::snprintf(buf, sizeof buf, "%u:%02u:%02u", static_cast<unsigned>(sec / 3600),
                      static_cast<unsigned>((sec / 60) % 60), static_cast<unsigned>(sec % 60));
    } else {
        std::snprintf(buf, sizeof buf, "%u:%02u", static_cast<unsigned>(sec / 60), static_cast<unsigned>(sec % 60));
    }
    return buf;
}

std::string durationText(uint32_t sec)
{
    const int s = static_cast<int>(sec);
    if (s < 60) {
        return fmt(Str::FmtSeconds, s);
    }
    if (s >= 3600) {
        return s % 3600 < 60 ? fmt(Str::FmtHours, s / 3600) : fmt(Str::FmtHoursMinutes, s / 3600, (s / 60) % 60);
    }
    return s % 60 == 0 ? fmt(Str::FmtMinutes, s / 60) : fmt(Str::FmtMinSec, s / 60, s % 60);
}

std::string dateText(const civil::LocalDateTime &local)
{
    char buf[24];
    std::snprintf(buf, sizeof buf, "%s %u %s", tr(weekdayShort(local.weekday)), local.date.day,
                  tr(kMonth[(local.date.month + 11) % 12]));
    return buf;
}

std::string dayText(UtcSeconds at, UtcSeconds now, const TimeZone &tz)
{
    const civil::LocalDateTime target = tz.toLocal(at);
    const int64_t days = target.daySerial - tz.localDay(now);
    if (days == 0) {
        return tr(Str::Today);
    }
    if (days == 1) {
        return tr(Str::Tomorrow);
    }
    if (days > 1 && days < 7) {
        return tr(weekdayShort(target.weekday));
    }
    return dateText(target);
}

std::string whenText(UtcSeconds at, UtcSeconds now, const TimeZone &tz)
{
    const civil::LocalDateTime target = tz.toLocal(at);
    return dayText(at, now, tz) + " " + clockText(target.secondOfDay / 60);
}

std::string whenShortText(UtcSeconds at, UtcSeconds now, const TimeZone &tz)
{
    const civil::LocalDateTime target = tz.toLocal(at);
    const int64_t days = target.daySerial - tz.localDay(now);
    const std::string time = clockText(target.secondOfDay / 60);
    if (days == 0) {
        return std::string(tr(Str::Today)) + " " + time;
    }
    if (days > 0 && days < 7) {
        return std::string(tr(weekdayShort(target.weekday))) + " " + time;
    }
    char buf[24];
    std::snprintf(buf, sizeof buf, "%u %s ", target.date.day, tr(kMonth[(target.date.month + 11) % 12]));
    return buf + time;
}

std::string weekdaysText(uint8_t mask)
{
    mask &= 0x7F;
    if (mask == 0x7F) {
        return tr(Str::EveryDay);
    }
    if (mask == 0) {
        return tr(Str::None);
    }
    std::string out;
    for (unsigned i = 0; i < 7; ++i) {
        const unsigned weekday = (i + 1) % 7; // Monday first
        if ((mask & (1u << weekday)) != 0) {
            if (!out.empty()) {
                out += ' ';
            }
            out += tr(weekdayTwo(weekday));
        }
    }
    return out;
}

std::string currentText(int32_t milliamp)
{
    return milliamp < 0 ? std::string(tr(Str::NotMeasured)) : fmt(Str::FmtMilliamp, static_cast<int>(milliamp));
}

std::string flowText(float litresPerMinute)
{
    const int tenths = static_cast<int>(litresPerMinute * 10.0f + 0.5f);
    return fmt(Str::FmtFlow, tenths / 10, tenths % 10);
}

std::string litresText(float litres) { return fmt(Str::FmtLitres, static_cast<long>(litres + 0.5f)); }

std::string percentText(int percent) { return fmt(Str::FmtPercent, percent); }

} // namespace zf::ui
