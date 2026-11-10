// SPDX-License-Identifier: Apache-2.0
#include "zf_ui/next_run.hpp"

namespace zf::ui {
namespace {

// Interval rules reach at most 365 days ahead.
constexpr int kSearchDays = 367;

} // namespace

ProgramTotals programTotals(const Program &p, const ScheduleConfig &schedule, uint8_t zoneCount,
                            const bool *zoneEnabled)
{
    ProgramTotals t;
    for (std::size_t z = 0; z < zoneCount && z < kMaxZones; ++z) {
        if (zoneEnabled != nullptr && !zoneEnabled[z]) {
            continue;
        }
        uint32_t sec = p.durationSec[z];
        if (p.useSeasonalAdjust) {
            sec = static_cast<uint32_t>((static_cast<uint64_t>(sec) * schedule.seasonalPercent + 50) / 100);
        }
        if (sec > 0) {
            ++t.zones;
            t.totalSec += sec;
        }
    }
    return t;
}

bool programSchedulable(const Program &p)
{
    if (!p.enabled || p.startCount == 0) {
        return false;
    }
    return !(p.days.type == DayRuleType::Weekdays && (p.days.weekdayMask & 0x7F) == 0);
}

NextRun nextStart(const Program &p, const TimeZone &tz, UtcSeconds now, UtcSeconds notBefore)
{
    NextRun best;
    if (!programSchedulable(p)) {
        return best;
    }
    const UtcSeconds floor = notBefore > now ? notBefore - 1 : now;
    const int64_t firstDay = tz.localDay(floor);
    for (int d = 0; d < kSearchDays; ++d) {
        const int64_t day = firstDay + d;
        if (!p.days.matches(day)) {
            continue;
        }
        for (std::size_t i = 0; i < p.startCount && i < kMaxStartTimes; ++i) {
            const UtcSeconds at = tz.fromLocal(day, static_cast<int32_t>(p.startMinutes[i]) * 60);
            if (at > floor && (!best.valid || at < best.at)) {
                best.valid = true;
                best.programId = p.id;
                best.at = at;
            }
        }
        if (best.valid) {
            break;
        }
    }
    return best;
}

NextRun nextRun(const ScheduleConfig &schedule, const TimeZone &tz, UtcSeconds now, UtcSeconds notBefore)
{
    NextRun best;
    for (std::size_t i = 0; i < schedule.programCount && i < kMaxPrograms; ++i) {
        const Program &p = schedule.programs[i];
        bool anyZone = false;
        for (uint32_t d : p.durationSec) {
            anyZone = anyZone || d > 0;
        }
        if (!anyZone) {
            continue;
        }
        const NextRun n = nextStart(p, tz, now, notBefore);
        if (n.valid && (!best.valid || n.at < best.at)) {
            best = n;
        }
    }
    return best;
}

} // namespace zf::ui
