// SPDX-License-Identifier: Apache-2.0
// Working copy of one program while it is edited on the device, and the
// mapping back into zf::Config. Nothing reaches the core until applyTo() and
// ConfigStore::update(); the core validates again.
#pragma once

#include <cstdint>

#include "zf/config/config.hpp"
#include "zf_ui/strings.hpp"

namespace zf::ui {

class ProgramEdit {
public:
    // Edit an existing program.
    explicit ProgramEdit(const Program &existing);
    // A new program: free id, default name "Program N", every day, enabled,
    // no start time and no zones yet. Returns false if the schedule is full.
    static bool makeNew(const ScheduleConfig &schedule, ProgramEdit &out);

    const Program &program() const { return work_; }
    bool isNew() const { return isNew_; }
    bool dirty() const;

    void setEnabled(bool on) { work_.enabled = on; }
    void setSeasonal(bool on) { work_.useSeasonalAdjust = on; }
    void setObeySensor(bool on) { work_.obeySensorInhibit = on; }

    // Start times stay sorted and unique. index == startCount() adds one.
    // minuteOfDay < 0 removes the entry. Returns false if nothing changed
    // (index out of range, or no room).
    bool setStart(std::size_t index, int minuteOfDay);
    std::size_t startCount() const { return work_.startCount; }
    bool canAddStart() const { return work_.startCount < kMaxStartTimes; }
    // A sensible time for a new start: 06:00, or two hours after the last one.
    int suggestedStart() const;

    void setRule(DayRuleType type, int64_t todaySerial);
    void setWeekdayMask(uint8_t mask) { work_.days.weekdayMask = mask & 0x7F; }
    // Counting restarts today, so the next run is easy to predict.
    void setIntervalDays(int days, int64_t todaySerial);

    // Whole minutes, 0 = zone not in the program. Clamped to maxRunSec.
    void setZoneMinutes(ZoneIndex zone, int minutes, uint32_t maxRunSec);
    // Rounded to the nearest minute; a 90 s duration set elsewhere reads 2.
    int zoneMinutes(ZoneIndex zone) const;

    // The first thing that keeps this program from ever running by itself,
    // or Str::Count if there is none. Not an error: it can still be saved.
    Str problem(uint8_t zoneCount) const;

    // Writes the working copy into cfg (replace by id, or append).
    // Returns false if there is no room for a new program.
    bool applyTo(Config &cfg) const;
    // Removes the program with this id. Returns false if it was not there.
    static bool removeFrom(Config &cfg, uint16_t programId);

    static uint16_t freeId(const ScheduleConfig &schedule);

private:
    ProgramEdit() = default;

    Program original_{};
    Program work_{};
    bool isNew_ = false;
};

bool programsEqual(const Program &a, const Program &b);

} // namespace zf::ui
