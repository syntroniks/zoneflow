// SPDX-License-Identifier: Apache-2.0
#include "zf_ui/picker.hpp"

#include <algorithm>
#include <cstdio>

#include "zf/time/civil.hpp"
#include "zf_ui/format.hpp"

namespace zf::ui {
namespace {

std::string twoDigits(int v)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d", v);
    return buf;
}

PickerSegment separator(const char *text)
{
    PickerSegment s;
    s.text = text;
    s.fixed = true;
    return s;
}

constexpr Str kMonths[12] = {Str::Jan, Str::Feb, Str::Mar, Str::Apr, Str::May, Str::Jun,
                             Str::Jul, Str::Aug, Str::Sep, Str::Oct, Str::Nov, Str::Dec};

} // namespace

// ---- NumberPicker ----------------------------------------------------------------

NumberPicker::NumberPicker(int value, int min, int max, int step) : min_(min), max_(max), step_(step < 1 ? 1 : step)
{
    // Snap a value that is off the step grid (set from the web interface).
    value_ = std::clamp(min_ + ((value - min_ + step_ / 2) / step_) * step_, min_, max_);
}

void NumberPicker::turn(int /*steps*/, int accelSteps)
{
    const int64_t next = static_cast<int64_t>(value_) + static_cast<int64_t>(accelSteps) * step_;
    value_ = static_cast<int>(std::clamp<int64_t>(next, min_, max_));
}

void NumberPicker::render(PickerModel &out) const
{
    out.caption = caption;
    out.cells = false;
    out.hint = tr(Str::HintTurnPress);
    PickerSegment s;
    s.cursor = true;
    if (minIsOff && value_ == min_) {
        s.text = tr(Str::Off);
        out.unit.clear();
    } else {
        s.text = std::to_string(value_);
        out.unit = unit;
    }
    out.segments = {s};
}

// ---- ChoicePicker ----------------------------------------------------------------

ChoicePicker::ChoicePicker(std::vector<std::string> options, int index) : options_(std::move(options))
{
    index_ = options_.empty() ? 0 : std::clamp(index, 0, static_cast<int>(options_.size()) - 1);
}

void ChoicePicker::turn(int steps, int /*accelSteps*/)
{
    if (!options_.empty()) {
        index_ = std::clamp(index_ + steps, 0, static_cast<int>(options_.size()) - 1);
    }
}

void ChoicePicker::render(PickerModel &out) const
{
    out.caption = caption;
    out.cells = false;
    out.unit.clear();
    out.hint = tr(Str::HintTurnPress);
    PickerSegment s;
    s.cursor = true;
    s.text = options_.empty() ? std::string() : options_[static_cast<std::size_t>(index_)];
    out.segments = {s};
}

// ---- TimePicker ------------------------------------------------------------------

TimePicker::TimePicker(int minuteOfDay, bool allowOff, int minuteStep)
    : allowOff_(allowOff), minuteStep_(minuteStep < 1 ? 1 : minuteStep)
{
    if (minuteOfDay < 0 || minuteOfDay >= 1440) {
        off_ = allowOff;
        minuteOfDay = 6 * 60;
    }
    hour_ = minuteOfDay / 60;
    minute_ = minuteOfDay % 60;
}

void TimePicker::turn(int steps, int /*accelSteps*/)
{
    if (field_ == 0) {
        // Hours run -1 (Off) .. 23 with Off allowed, else wrap 0..23.
        if (allowOff_) {
            const int position = std::clamp((off_ ? -1 : hour_) + steps, -1, 23);
            off_ = position < 0;
            hour_ = off_ ? 0 : position;
        } else {
            hour_ = ((hour_ + steps) % 24 + 24) % 24;
        }
        return;
    }
    // Minutes move on the step grid and wrap without carrying into the hour.
    const int slots = 60 / minuteStep_;
    const int slot = minute_ / minuteStep_;
    const bool onGrid = minute_ % minuteStep_ == 0;
    int next = slot + steps;
    if (!onGrid && steps < 0) {
        ++next; // from 07 one step down is 05, not 00
    }
    minute_ = (((next % slots) + slots) % slots) * minuteStep_;
}

bool TimePicker::press()
{
    if (off_ || field_ == 1) {
        return true;
    }
    field_ = 1;
    return false;
}

void TimePicker::render(PickerModel &out) const
{
    out.caption = caption;
    out.cells = false;
    out.unit.clear();
    out.segments.clear();
    if (off_) {
        PickerSegment s;
        s.text = tr(Str::Off);
        s.cursor = true;
        out.segments.push_back(s);
        out.hint = tr(Str::HintTurnPress);
        return;
    }
    PickerSegment h;
    h.text = twoDigits(hour_);
    h.cursor = field_ == 0;
    PickerSegment m;
    m.text = twoDigits(minute_);
    m.cursor = field_ == 1;
    out.segments = {h, separator(":"), m};
    out.hint = tr(field_ == 0 ? Str::HintTurnNext : Str::HintTurnPress);
}

// ---- WeekdayPicker ---------------------------------------------------------------

WeekdayPicker::WeekdayPicker(uint8_t mask) : mask_(mask & 0x7F) {}

void WeekdayPicker::turn(int steps, int /*accelSteps*/) { cursor_ = std::clamp(cursor_ + steps, 0, 7); }

bool WeekdayPicker::press()
{
    if (cursor_ == 7) {
        return true;
    }
    const unsigned weekday = static_cast<unsigned>(cursor_ + 1) % 7; // cell 0 = Monday = bit 1
    mask_ = static_cast<uint8_t>(mask_ ^ (1u << weekday));
    return false;
}

void WeekdayPicker::render(PickerModel &out) const
{
    out.caption = caption;
    out.cells = true;
    out.unit.clear();
    out.hint = tr(Str::HintWeekdays);
    out.segments.clear();
    for (int i = 0; i < 7; ++i) {
        const unsigned weekday = static_cast<unsigned>(i + 1) % 7;
        PickerSegment s;
        s.text = tr(weekdayTwo(weekday));
        s.on = (mask_ & (1u << weekday)) != 0;
        s.cursor = cursor_ == i;
        out.segments.push_back(s);
    }
    PickerSegment ok;
    ok.text = tr(Str::Ok);
    ok.cursor = cursor_ == 7;
    ok.fixed = true; // drawn as a button, not as a day
    out.segments.push_back(ok);
}

// ---- DatePicker ------------------------------------------------------------------

DatePicker::DatePicker(int64_t daySerial)
{
    const civil::Date d = civil::civilFromDays(daySerial);
    year_ = std::clamp(d.year, 2025, 2099);
    month_ = static_cast<int>(d.month);
    day_ = static_cast<int>(d.day);
}

void DatePicker::turn(int steps, int /*accelSteps*/)
{
    if (field_ == 0) {
        const int dim = static_cast<int>(civil::daysInMonth(year_, static_cast<unsigned>(month_)));
        day_ = ((day_ - 1 + steps) % dim + dim) % dim + 1;
    } else if (field_ == 1) {
        month_ = ((month_ - 1 + steps) % 12 + 12) % 12 + 1;
    } else {
        year_ = std::clamp(year_ + steps, 2025, 2099);
    }
    const int dim = static_cast<int>(civil::daysInMonth(year_, static_cast<unsigned>(month_)));
    day_ = std::min(day_, dim);
}

bool DatePicker::press()
{
    if (field_ == 2) {
        return true;
    }
    ++field_;
    return false;
}

int64_t DatePicker::value() const
{
    return civil::daysFromCivil(year_, static_cast<unsigned>(month_), static_cast<unsigned>(day_));
}

void DatePicker::render(PickerModel &out) const
{
    out.caption = caption;
    out.cells = false;
    out.unit.clear();
    PickerSegment d;
    d.text = std::to_string(day_);
    d.cursor = field_ == 0;
    PickerSegment m;
    m.text = tr(kMonths[month_ - 1]);
    m.cursor = field_ == 1;
    PickerSegment y;
    y.text = std::to_string(year_);
    y.cursor = field_ == 2;
    out.segments = {d, separator(" "), m, separator(" "), y};
    out.hint = tr(field_ == 2 ? Str::HintTurnPress : Str::HintTurnNext);
}

} // namespace zf::ui
