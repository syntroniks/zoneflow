// SPDX-License-Identifier: Apache-2.0
// Value pickers.
#include <gtest/gtest.h>

#include "zf/time/civil.hpp"
#include "zf_ui/picker.hpp"

using namespace zf;
using namespace zf::ui;

namespace {

std::string text(const Picker &p)
{
    PickerModel m;
    p.render(m);
    std::string out;
    for (const PickerSegment &s : m.segments) {
        out += s.cursor ? "[" + s.text + "]" : s.text;
    }
    return m.unit.empty() ? out : out + " " + m.unit;
}

} // namespace

TEST(UiNumberPicker, ClampsAndUsesAcceleratedSteps)
{
    NumberPicker p(10, 1, 60);
    p.unit = "min";
    EXPECT_EQ(text(p), "[10] min");
    p.turn(1, 1);
    EXPECT_EQ(p.value(), 11);
    p.turn(3, 30); // a fast spin
    EXPECT_EQ(p.value(), 41);
    p.turn(5, 50);
    EXPECT_EQ(p.value(), 60);
    p.turn(-9, -90);
    EXPECT_EQ(p.value(), 1);
    EXPECT_TRUE(p.press());
}

TEST(UiNumberPicker, StepGridAndOffWord)
{
    NumberPicker percent(103, 0, 250, 5); // set to an odd value elsewhere
    EXPECT_EQ(percent.value(), 105);
    percent.turn(-1, -1);
    EXPECT_EQ(percent.value(), 100);

    NumberPicker minutes(0, 0, 30);
    minutes.unit = "min";
    minutes.minIsOff = true;
    EXPECT_EQ(text(minutes), "[Off]");
    minutes.turn(1, 1);
    EXPECT_EQ(text(minutes), "[1] min");
}

TEST(UiChoicePicker, StopsAtTheEnds)
{
    ChoicePicker p({"Off", "6 h", "1 day"}, 1);
    EXPECT_EQ(text(p), "[6 h]");
    p.turn(5, 50);
    EXPECT_EQ(p.value(), 2);
    p.turn(-9, -90);
    EXPECT_EQ(p.value(), 0);
    EXPECT_EQ(text(p), "[Off]");
}

TEST(UiTimePicker, HoursThenMinutes)
{
    TimePicker p(6 * 60, false);
    EXPECT_EQ(text(p), "[06]:00");
    p.turn(-7, -70);
    EXPECT_EQ(text(p), "[23]:00") << "hours wrap; acceleration is not used";
    EXPECT_FALSE(p.press());
    EXPECT_EQ(text(p), "23:[00]");
    p.turn(-1, -1);
    EXPECT_EQ(text(p), "23:[55]") << "minutes wrap without changing the hour";
    p.turn(3, 3);
    EXPECT_EQ(text(p), "23:[10]");
    EXPECT_TRUE(p.press());
    EXPECT_EQ(p.value(), 23 * 60 + 10);
}

TEST(UiTimePicker, OffBelowMidnightRemovesAStart)
{
    TimePicker p(60, true);
    p.turn(-1, -1);
    EXPECT_EQ(text(p), "[00]:00");
    p.turn(-1, -1);
    EXPECT_EQ(text(p), "[Off]");
    p.turn(-5, -5);
    EXPECT_EQ(text(p), "[Off]") << "does not wrap to 23";
    EXPECT_TRUE(p.press()) << "Off needs no minutes";
    EXPECT_EQ(p.value(), TimePicker::kOff);

    TimePicker q(60, true);
    q.turn(-2, -2);
    q.turn(3, 3);
    EXPECT_EQ(text(q), "[02]:00");
}

TEST(UiTimePicker, OffGridMinutesSnapInTheTurnDirection)
{
    TimePicker up(6 * 60 + 7, false);
    up.press();
    up.turn(1, 1);
    EXPECT_EQ(up.value(), 6 * 60 + 10);
    TimePicker down(6 * 60 + 7, false);
    down.press();
    down.turn(-1, -1);
    EXPECT_EQ(down.value(), 6 * 60 + 5);

    TimePicker exact(6 * 60 + 7, false, 1); // setting the clock: one-minute steps
    exact.press();
    exact.turn(1, 1);
    EXPECT_EQ(exact.value(), 6 * 60 + 8);
}

TEST(UiWeekdayPicker, TogglesDaysMondayFirst)
{
    WeekdayPicker p(0b0101010); // Mon, Wed, Fri (bit 0 = Sunday)
    PickerModel m;
    p.render(m);
    ASSERT_EQ(m.segments.size(), 8u);
    EXPECT_TRUE(m.cells);
    EXPECT_EQ(m.segments[0].text, "Mo");
    EXPECT_TRUE(m.segments[0].on);
    EXPECT_FALSE(m.segments[1].on);
    EXPECT_EQ(m.segments[6].text, "Su");
    EXPECT_TRUE(m.segments[0].cursor);

    EXPECT_FALSE(p.press()); // Monday off
    p.turn(6, 6);            // Sunday
    EXPECT_FALSE(p.press()); // Sunday on
    EXPECT_EQ(p.value(), 0b0101001);
    p.turn(5, 5); // past the end: stays on OK
    p.render(m);
    EXPECT_TRUE(m.segments[7].cursor);
    EXPECT_TRUE(p.press());
    EXPECT_EQ(p.value(), 0b0101001);
}

TEST(UiDatePicker, ClampsTheDayToTheMonth)
{
    DatePicker p(civil::daysFromCivil(2028, 1, 31));
    EXPECT_EQ(text(p), "[31] Jan 2028");
    EXPECT_FALSE(p.press());
    p.turn(1, 1);
    EXPECT_EQ(text(p), "29 [Feb] 2028") << "leap year";
    EXPECT_FALSE(p.press());
    p.turn(1, 1);
    EXPECT_EQ(text(p), "28 Feb [2029]");
    EXPECT_TRUE(p.press());
    EXPECT_EQ(p.value(), civil::daysFromCivil(2029, 2, 28));
}
