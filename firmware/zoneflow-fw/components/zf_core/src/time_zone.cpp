// SPDX-License-Identifier: Apache-2.0
#include "zf/time/time_zone.hpp"

#include <cstdlib>

namespace zf {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

class Cursor {
public:
    explicit Cursor(std::string_view s) : s_(s) {}

    bool done() const { return i_ >= s_.size(); }
    char peek() const { return done() ? '\0' : s_[i_]; }
    bool consume(char c)
    {
        if (peek() != c || done()) {
            return false;
        }
        ++i_;
        return true;
    }

    // A zone abbreviation: three or more letters, or <...> with letters,
    // digits and signs.
    bool name()
    {
        std::size_t n = 0;
        if (consume('<')) {
            while (!done() && peek() != '>') {
                const char c = peek();
                if (!(isAlpha(c) || isDigit(c) || c == '+' || c == '-')) {
                    return false;
                }
                ++i_;
                ++n;
            }
            return consume('>') && n >= 3;
        }
        while (isAlpha(peek())) {
            ++i_;
            ++n;
        }
        return n >= 3;
    }

    bool number(int max, int &out)
    {
        if (!isDigit(peek())) {
            return false;
        }
        int v = 0;
        int digits = 0;
        while (isDigit(peek())) {
            v = v * 10 + (peek() - '0');
            ++i_;
            if (++digits > 3 || v > max) {
                return false;
            }
        }
        out = v;
        return true;
    }

    // [+-]hh[:mm[:ss]]
    bool hms(int maxHours, int32_t &out)
    {
        int sign = 1;
        if (peek() == '+' || peek() == '-') {
            sign = peek() == '-' ? -1 : 1;
            ++i_;
        }
        int h = 0;
        int m = 0;
        int s = 0;
        if (!number(maxHours, h)) {
            return false;
        }
        if (consume(':')) {
            if (!number(59, m)) {
                return false;
            }
            if (consume(':') && !number(59, s)) {
                return false;
            }
        }
        out = sign * (h * 3600 + m * 60 + s);
        return true;
    }

    bool atOffset() const
    {
        const char c = peek();
        return c == '+' || c == '-' || isDigit(c);
    }

private:
    std::string_view s_;
    std::size_t i_ = 0;
};

bool parseRule(Cursor &c, TimeZone::Rule &rule)
{
    int v = 0;
    if (c.consume('J')) {
        if (!c.number(365, v) || v < 1) {
            return false;
        }
        rule.kind = TimeZone::Rule::Kind::Julian1;
        rule.day = static_cast<uint16_t>(v);
    } else if (c.consume('M')) {
        int m = 0;
        int w = 0;
        int d = 0;
        if (!c.number(12, m) || m < 1 || !c.consume('.') || !c.number(5, w) || w < 1 || !c.consume('.') ||
            !c.number(6, d)) {
            return false;
        }
        rule.kind = TimeZone::Rule::Kind::MonthWeekDay;
        rule.month = static_cast<uint8_t>(m);
        rule.week = static_cast<uint8_t>(w);
        rule.weekday = static_cast<uint8_t>(d);
    } else if (isDigit(c.peek())) {
        if (!c.number(365, v)) {
            return false;
        }
        rule.kind = TimeZone::Rule::Kind::Julian0;
        rule.day = static_cast<uint16_t>(v);
    } else {
        return false;
    }
    rule.timeSec = 7200;
    if (c.consume('/')) {
        return c.hms(167, rule.timeSec);
    }
    return true;
}

} // namespace

std::optional<TimeZone> TimeZone::parse(std::string_view posix)
{
    Cursor c(posix);
    TimeZone tz;
    int32_t off = 0;
    if (!c.name() || !c.hms(24, off)) {
        return std::nullopt;
    }
    // POSIX offsets are positive west of Greenwich; store seconds east.
    tz.stdOffset_ = -off;
    tz.dstOffset_ = tz.stdOffset_;
    if (c.done()) {
        return tz;
    }
    if (!c.name()) {
        return std::nullopt;
    }
    tz.hasDst_ = true;
    tz.dstOffset_ = tz.stdOffset_ + 3600;
    if (c.atOffset()) {
        if (!c.hms(24, off)) {
            return std::nullopt;
        }
        tz.dstOffset_ = -off;
    }
    if (c.done()) {
        tz.start_ = Rule{Rule::Kind::MonthWeekDay, 0, 3, 2, 0, 7200};
        tz.end_ = Rule{Rule::Kind::MonthWeekDay, 0, 11, 1, 0, 7200};
        return tz;
    }
    if (!c.consume(',') || !parseRule(c, tz.start_) || !c.consume(',') || !parseRule(c, tz.end_) || !c.done()) {
        return std::nullopt;
    }
    return tz;
}

int64_t TimeZone::ruleLocalSeconds(int year, const Rule &rule)
{
    const int64_t jan1 = civil::daysFromCivil(year, 1, 1);
    int64_t day = jan1;
    switch (rule.kind) {
    case Rule::Kind::Julian1:
        // Day 1..365, February 29 is never counted.
        day = jan1 + rule.day - 1 + ((civil::isLeapYear(year) && rule.day >= 60) ? 1 : 0);
        break;
    case Rule::Kind::Julian0:
        day = jan1 + rule.day;
        break;
    case Rule::Kind::MonthWeekDay: {
        const int64_t first = civil::daysFromCivil(year, rule.month, 1);
        const unsigned firstWd = civil::weekdayFromDays(first);
        unsigned dom = 1 + (rule.weekday + 7 - firstWd) % 7 + (rule.week - 1u) * 7u;
        const unsigned dim = civil::daysInMonth(year, rule.month);
        while (dom > dim) {
            dom -= 7;
        }
        day = first + dom - 1;
        break;
    }
    }
    return day * civil::kSecondsPerDay + rule.timeSec;
}

TimeZone::Transitions TimeZone::transitionsForYear(int year) const
{
    // The start rule is expressed in standard time, the end rule in DST.
    return Transitions{ruleLocalSeconds(year, start_) - stdOffset_, ruleLocalSeconds(year, end_) - dstOffset_};
}

bool TimeZone::isDstAt(UtcSeconds t) const
{
    if (!hasDst_) {
        return false;
    }
    const int year = civil::civilFromDays(civil::floorDiv(t + stdOffset_, civil::kSecondsPerDay)).year;
    const Transitions tr = transitionsForYear(year);
    if (tr.dstStart < tr.dstEnd) {
        return t >= tr.dstStart && t < tr.dstEnd; // northern hemisphere
    }
    return !(t >= tr.dstEnd && t < tr.dstStart); // southern hemisphere
}

int32_t TimeZone::offsetAt(UtcSeconds t) const { return isDstAt(t) ? dstOffset_ : stdOffset_; }

int64_t TimeZone::localDay(UtcSeconds t) const { return civil::floorDiv(t + offsetAt(t), civil::kSecondsPerDay); }

civil::LocalDateTime TimeZone::toLocal(UtcSeconds t) const
{
    const bool dst = isDstAt(t);
    const int32_t off = dst ? dstOffset_ : stdOffset_;
    const int64_t local = t + off;
    const int64_t day = civil::floorDiv(local, civil::kSecondsPerDay);
    civil::LocalDateTime out{};
    out.date = civil::civilFromDays(day);
    out.daySerial = day;
    out.secondOfDay = static_cast<int32_t>(local - day * civil::kSecondsPerDay);
    out.weekday = civil::weekdayFromDays(day);
    out.utcOffsetSec = off;
    out.dst = dst;
    return out;
}

UtcSeconds TimeZone::fromLocal(int64_t localDaySerial, int32_t secondOfDay) const
{
    const int64_t local = localDaySerial * civil::kSecondsPerDay + secondOfDay;
    const UtcSeconds viaStd = local - stdOffset_;
    if (!hasDst_) {
        return viaStd;
    }
    const UtcSeconds viaDst = local - dstOffset_;
    const bool stdValid = offsetAt(viaStd) == stdOffset_;
    const bool dstValid = offsetAt(viaDst) == dstOffset_;
    if (stdValid && dstValid) {
        return viaStd < viaDst ? viaStd : viaDst;
    }
    if (stdValid) {
        return viaStd;
    }
    if (dstValid) {
        return viaDst;
    }
    // In a gap: return the transition that created it.
    const int year = civil::civilFromDays(localDaySerial).year;
    const Transitions tr = transitionsForYear(year);
    const auto dist = [viaStd](UtcSeconds t) { return t > viaStd ? t - viaStd : viaStd - t; };
    return dist(tr.dstStart) <= dist(tr.dstEnd) ? tr.dstStart : tr.dstEnd;
}

} // namespace zf
