// SPDX-License-Identifier: Apache-2.0
// The next scheduled start, for the home screen and the program list.
//
// The scheduler in zf_core decides what runs; it does not publish what it
// will do next. This is a read-only forecast from the same configuration and
// the same TimeZone code. It knows about the rain delay (starts inside the
// delay are skipped by the scheduler) but cannot know a future sensor state.
#pragma once

#include "zf/sched/program.hpp"
#include "zf/time/time_zone.hpp"
#include "zf/types.hpp"

namespace zf::ui {

struct NextRun {
    bool valid = false;
    uint16_t programId = 0;
    UtcSeconds at = 0;
};

// How many zones a program would run and for how long, with the seasonal
// adjustment applied the way Scheduler::adjustedDurationSec does.
struct ProgramTotals {
    int zones = 0;
    uint32_t totalSec = 0;
};
ProgramTotals programTotals(const Program &p, const ScheduleConfig &schedule, uint8_t zoneCount,
                            const bool *zoneEnabled);

// True if the program can start by itself: enabled, has a start time and a
// day rule that can match.
bool programSchedulable(const Program &p);

// First start of this program after `now` and not before `notBefore`
// (the end of a rain delay, or 0).
NextRun nextStart(const Program &p, const TimeZone &tz, UtcSeconds now, UtcSeconds notBefore);

// Earliest nextStart over all programs. Ties go to the first program.
NextRun nextRun(const ScheduleConfig &schedule, const TimeZone &tz, UtcSeconds now, UtcSeconds notBefore);

} // namespace zf::ui
