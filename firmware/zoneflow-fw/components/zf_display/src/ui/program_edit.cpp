// SPDX-License-Identifier: Apache-2.0
#include "zf_ui/program_edit.hpp"

#include <algorithm>

#include "zf_ui/format.hpp"

namespace zf::ui {

bool programsEqual(const Program &a, const Program &b)
{
    if (a.id != b.id || !(a.name == b.name) || a.enabled != b.enabled || a.startCount != b.startCount ||
        a.useSeasonalAdjust != b.useSeasonalAdjust || a.obeySensorInhibit != b.obeySensorInhibit ||
        a.days.type != b.days.type || a.durationSec != b.durationSec) {
        return false;
    }
    for (std::size_t i = 0; i < a.startCount && i < kMaxStartTimes; ++i) {
        if (a.startMinutes[i] != b.startMinutes[i]) {
            return false;
        }
    }
    switch (a.days.type) {
    case DayRuleType::Weekdays:
        return a.days.weekdayMask == b.days.weekdayMask;
    case DayRuleType::Interval:
        return a.days.intervalDays == b.days.intervalDays && a.days.intervalAnchorDay == b.days.intervalAnchorDay;
    case DayRuleType::OddDays:
    case DayRuleType::EvenDays:
        break;
    }
    return true;
}

ProgramEdit::ProgramEdit(const Program &existing) : original_(existing), work_(existing)
{
    work_.startCount = std::min<uint8_t>(work_.startCount, kMaxStartTimes);
}

uint16_t ProgramEdit::freeId(const ScheduleConfig &schedule)
{
    uint16_t id = 1;
    for (std::size_t i = 0; i < schedule.programCount && i < kMaxPrograms; ++i) {
        if (schedule.programs[i].id >= id) {
            id = static_cast<uint16_t>(schedule.programs[i].id + 1);
        }
    }
    return id == 0 ? 1 : id; // 65535 + 1 wrapped; ids that high do not occur in practice
}

bool ProgramEdit::makeNew(const ScheduleConfig &schedule, ProgramEdit &out)
{
    if (schedule.programCount >= kMaxPrograms) {
        return false;
    }
    ProgramEdit e;
    e.isNew_ = true;
    e.work_.id = freeId(schedule);
    e.work_.name.assign(fmt(Str::ProgDefaultName, static_cast<int>(schedule.programCount) + 1));
    e.work_.enabled = true;
    e.original_ = e.work_;
    out = e;
    return true;
}

bool ProgramEdit::dirty() const { return !programsEqual(original_, work_); }

bool ProgramEdit::setStart(std::size_t index, int minuteOfDay)
{
    const std::size_t count = std::min<std::size_t>(work_.startCount, kMaxStartTimes);
    if (index > count || (index == count && (count >= kMaxStartTimes || minuteOfDay < 0))) {
        return false;
    }
    if (minuteOfDay >= 1440) {
        return false;
    }
    // Rebuild the list without the edited entry, then insert the new value
    // in order unless it is already there.
    std::array<uint16_t, kMaxStartTimes> list{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != index) {
            list[n++] = work_.startMinutes[i];
        }
    }
    if (minuteOfDay >= 0) {
        const auto value = static_cast<uint16_t>(minuteOfDay);
        std::size_t at = 0;
        while (at < n && list[at] < value) {
            ++at;
        }
        if (at == n || list[at] != value) {
            for (std::size_t i = n; i > at; --i) {
                list[i] = list[i - 1];
            }
            list[at] = value;
            ++n;
        }
    }
    work_.startMinutes = list;
    work_.startCount = static_cast<uint8_t>(n);
    return true;
}

int ProgramEdit::suggestedStart() const
{
    if (work_.startCount == 0) {
        return 6 * 60;
    }
    const int last = work_.startMinutes[work_.startCount - 1];
    return last + 120 < 1440 ? last + 120 : 1435;
}

void ProgramEdit::setRule(DayRuleType type, int64_t todaySerial)
{
    if (type == work_.days.type) {
        return;
    }
    work_.days.type = type;
    if (type == DayRuleType::Interval) {
        if (work_.days.intervalDays < 1) {
            work_.days.intervalDays = 2;
        }
        work_.days.intervalAnchorDay = static_cast<int32_t>(todaySerial);
    }
    if (type == DayRuleType::Weekdays && (work_.days.weekdayMask & 0x7F) == 0) {
        work_.days.weekdayMask = 0x7F;
    }
}

void ProgramEdit::setIntervalDays(int days, int64_t todaySerial)
{
    work_.days.intervalDays = static_cast<uint16_t>(std::clamp(days, 1, 365));
    work_.days.intervalAnchorDay = static_cast<int32_t>(todaySerial);
}

void ProgramEdit::setZoneMinutes(ZoneIndex zone, int minutes, uint32_t maxRunSec)
{
    if (zone >= kMaxZones) {
        return;
    }
    const uint32_t limit = std::min(maxRunSec, kHardMaxRunSec);
    const uint32_t sec = minutes <= 0 ? 0u : static_cast<uint32_t>(minutes) * 60u;
    work_.durationSec[zone] = std::min(sec, limit);
}

int ProgramEdit::zoneMinutes(ZoneIndex zone) const
{
    return zone < kMaxZones ? static_cast<int>((work_.durationSec[zone] + 30) / 60) : 0;
}

Str ProgramEdit::problem(uint8_t zoneCount) const
{
    if (work_.startCount == 0) {
        return Str::ProgWarnNoStart;
    }
    bool anyZone = false;
    for (std::size_t z = 0; z < zoneCount && z < kMaxZones; ++z) {
        anyZone = anyZone || work_.durationSec[z] > 0;
    }
    if (!anyZone) {
        return Str::ProgWarnNoZones;
    }
    if (work_.days.type == DayRuleType::Weekdays && (work_.days.weekdayMask & 0x7F) == 0) {
        return Str::ProgWarnNoDays;
    }
    return Str::Count;
}

bool ProgramEdit::applyTo(Config &cfg) const
{
    ScheduleConfig &s = cfg.schedule;
    for (std::size_t i = 0; i < s.programCount && i < kMaxPrograms; ++i) {
        if (s.programs[i].id == work_.id) {
            s.programs[i] = work_;
            return true;
        }
    }
    if (s.programCount >= kMaxPrograms) {
        return false;
    }
    s.programs[s.programCount++] = work_;
    return true;
}

bool ProgramEdit::removeFrom(Config &cfg, uint16_t programId)
{
    ScheduleConfig &s = cfg.schedule;
    for (std::size_t i = 0; i < s.programCount && i < kMaxPrograms; ++i) {
        if (s.programs[i].id == programId) {
            for (std::size_t j = i + 1; j < s.programCount; ++j) {
                s.programs[j - 1] = s.programs[j];
            }
            s.programs[--s.programCount] = Program{};
            return true;
        }
    }
    return false;
}

} // namespace zf::ui
