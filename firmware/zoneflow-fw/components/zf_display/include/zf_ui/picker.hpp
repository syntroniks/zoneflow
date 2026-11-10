// SPDX-License-Identifier: Apache-2.0
// Value pickers: what the knob edits after a row is pressed. A picker owns
// the value while it is open; the screen that opened it gets the result when
// the last part is confirmed. Pure logic, drawn from a PickerModel.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "zf_ui/model.hpp"
#include "zf_ui/strings.hpp"

namespace zf::ui {

class Picker {
public:
    virtual ~Picker() = default;
    // steps: detents. accelSteps: detents after acceleration.
    virtual void turn(int steps, int accelSteps) = 0;
    // Returns true when the value is complete.
    virtual bool press() = 0;
    // Number: the value. Time: minute of day, or TimePicker::kOff.
    // Weekdays: mask. Date: day serial. Choice: index.
    virtual int64_t value() const = 0;
    virtual void render(PickerModel &out) const = 0;

    std::string caption;
};

// One number with a unit. Accelerated. Optionally the lowest value reads as
// a word ("Off").
class NumberPicker final : public Picker {
public:
    NumberPicker(int value, int min, int max, int step = 1);
    void turn(int steps, int accelSteps) override;
    bool press() override { return true; }
    int64_t value() const override { return value_; }
    void render(PickerModel &out) const override;

    std::string unit;
    bool minIsOff = false; // show Str::Off at the minimum

private:
    int value_;
    int min_;
    int max_;
    int step_;
};

// One of a short list of words. Not accelerated, does not wrap.
class ChoicePicker final : public Picker {
public:
    ChoicePicker(std::vector<std::string> options, int index);
    void turn(int steps, int accelSteps) override;
    bool press() override { return true; }
    int64_t value() const override { return index_; }
    void render(PickerModel &out) const override;

private:
    std::vector<std::string> options_;
    int index_;
};

// Hours, then minutes. With allowOff, turning the hour below 00 gives "Off"
// (used to remove a start time).
class TimePicker final : public Picker {
public:
    static constexpr int64_t kOff = -1;

    TimePicker(int minuteOfDay, bool allowOff, int minuteStep = 5);
    void turn(int steps, int accelSteps) override;
    bool press() override;
    int64_t value() const override { return off_ ? kOff : hour_ * 60 + minute_; }
    void render(PickerModel &out) const override;

private:
    bool allowOff_;
    int minuteStep_;
    bool off_ = false;
    int hour_ = 0;
    int minute_ = 0;
    int field_ = 0; // 0 hour, 1 minute
};

// Seven day cells, Monday first, and an OK cell. Turn moves, press toggles;
// press on OK finishes. Mask bit 0 = Sunday, as in zf::DayRule.
class WeekdayPicker final : public Picker {
public:
    explicit WeekdayPicker(uint8_t mask);
    void turn(int steps, int accelSteps) override;
    bool press() override;
    int64_t value() const override { return mask_; }
    void render(PickerModel &out) const override;

private:
    uint8_t mask_;
    int cursor_ = 0; // 0..6 = Monday..Sunday, 7 = OK
};

// Year, month, day. The day is clamped when the month changes.
class DatePicker final : public Picker {
public:
    explicit DatePicker(int64_t daySerial);
    void turn(int steps, int accelSteps) override;
    bool press() override;
    int64_t value() const override;
    void render(PickerModel &out) const override;

private:
    int year_;
    int month_;
    int day_;
    int field_ = 0; // 0 day, 1 month, 2 year
};

} // namespace zf::ui
