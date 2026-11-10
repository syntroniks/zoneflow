// SPDX-License-Identifier: Apache-2.0
// Watering program model.
#pragma once

#include <array>
#include <cstdint>

#include "zf/fixed_string.hpp"
#include "zf/types.hpp"

namespace zf {

enum class DayRuleType : uint8_t {
    Weekdays, // weekdayMask bit 0 = Sunday ... bit 6 = Saturday
    Interval, // every intervalDays days, counting from intervalAnchorDay
    OddDays,  // odd day of month (1, 3, ... 29, 31)
    EvenDays, // even day of month
};

struct DayRule {
    DayRuleType type = DayRuleType::Weekdays;
    uint8_t weekdayMask = 0x7F;
    uint16_t intervalDays = 1;
    int32_t intervalAnchorDay = 0; // local day serial (days since 1970-01-01)

    // daySerial is the local calendar day of the start time.
    bool matches(int64_t daySerial) const;
};

struct Program {
    uint16_t id = 0; // stable, non-zero, unique; survives reordering
    FixedString<31> name;
    bool enabled = true;
    uint8_t startCount = 0;
    std::array<uint16_t, kMaxStartTimes> startMinutes{}; // local minutes after midnight
    DayRule days;
    std::array<uint32_t, kMaxZones> durationSec{}; // 0 = zone not in program
    bool useSeasonalAdjust = true;
    bool obeySensorInhibit = true;
};

struct ScheduleConfig {
    uint8_t programCount = 0;
    std::array<Program, kMaxPrograms> programs{};
    uint16_t seasonalPercent = 100;    // 0..250
    uint16_t missedStartGraceMin = 60; // see firmware-architecture.md, missed starts
    uint16_t pauseTimeoutMin = 120;    // a paused queue stops after this long
};

} // namespace zf
