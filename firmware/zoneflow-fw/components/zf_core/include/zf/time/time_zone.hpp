// SPDX-License-Identifier: Apache-2.0
// POSIX TZ string time zone ("CET-1CEST,M3.5.0,M10.5.0/3"). Implemented in
// the core instead of using newlib/glibc localtime so the scheduler gives the
// same answers on the host and on the device, and so tests can use any zone
// without touching process-global state.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "zf/time/civil.hpp"
#include "zf/types.hpp"

namespace zf {

class TimeZone {
public:
    // Accepts the POSIX.1-2017 TZ format: std offset [dst [offset] [,rule,rule]].
    // Names may be quoted with <>. Rules may be Jn, n or Mm.w.d, each with an
    // optional /time (signed hours up to 167, as in RFC 8536). With a DST name
    // and no rules, the US rule M3.2.0,M11.1.0 is used, as glibc does.
    static std::optional<TimeZone> parse(std::string_view posix);
    static TimeZone utc() { return TimeZone{}; }

    // Seconds east of UTC in effect at instant t.
    int32_t offsetAt(UtcSeconds t) const;
    bool isDstAt(UtcSeconds t) const;
    civil::LocalDateTime toLocal(UtcSeconds t) const;
    int64_t localDay(UtcSeconds t) const;

    // Converts a local wall-clock time to UTC.
    // - Ambiguous (repeated hour at the end of DST): the earlier instant.
    // - Nonexistent (skipped hour at the start of DST): the transition instant,
    //   i.e. the first valid instant after the gap.
    // This mapping is monotonic (non-decreasing) in local time, which the
    // scheduler relies on.
    UtcSeconds fromLocal(int64_t localDaySerial, int32_t secondOfDay) const;

    bool hasDst() const { return hasDst_; }
    int32_t standardOffset() const { return stdOffset_; }
    int32_t dstOffset() const { return dstOffset_; }

    struct Rule {
        enum class Kind : uint8_t { Julian1, Julian0, MonthWeekDay };
        Kind kind = Kind::MonthWeekDay;
        uint16_t day = 0;    // Jn: 1..365, n: 0..365
        uint8_t month = 0;   // Mm.w.d: 1..12
        uint8_t week = 0;    // 1..5, 5 = last
        uint8_t weekday = 0; // 0 = Sunday
        int32_t timeSec = 7200;
    };

private:
    struct Transitions {
        UtcSeconds dstStart;
        UtcSeconds dstEnd;
    };
    Transitions transitionsForYear(int year) const;
    static int64_t ruleLocalSeconds(int year, const Rule &rule);

    int32_t stdOffset_ = 0;
    int32_t dstOffset_ = 0;
    bool hasDst_ = false;
    Rule start_{};
    Rule end_{};
};

} // namespace zf
