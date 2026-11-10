// SPDX-License-Identifier: Apache-2.0
// The display's navigation and behaviour, driven with the same input events
// the device produces, against the real controller on fake hardware.
#include <gtest/gtest.h>

#include <random>

#include "ui_harness.hpp"
#include "zf_ui/format.hpp"

using namespace zf;
using namespace zf::test;
using namespace zf::ui;

namespace {

class UiNav : public ::testing::Test {
protected:
    UiHarness h;

    void SetUp() override
    {
        // Tuesday 2026-11-10 14:32 in Berlin.
        h.clock.utc = utcOf(2026, 11, 10, 13, 32);
    }
    void start(bool wizardDone = true) { h.start(demoConfig(), wizardDone); }
    const Frame &f() const { return h.frame(); }
    const HomeModel &home() const { return h.frame().home; }
    bool onHome() const { return f().kind == FrameKind::Home; }
    std::vector<std::string> buttonLabels() const
    {
        std::vector<std::string> out;
        for (const Button &b : (onHome() ? home().buttons : f().detail.buttons)) {
            out.push_back(b.label);
        }
        return out;
    }
    std::string pickerText() const
    {
        std::string out;
        for (const PickerSegment &s : f().picker.segments) {
            out += s.cursor ? "[" + s.text + "]" : s.text;
        }
        return out;
    }
};

using Labels = std::vector<std::string>;

} // namespace

// ---- home ---------------------------------------------------------------------------

TEST_F(UiNav, IdleHomeShowsTheNextRun)
{
    start();
    ASSERT_TRUE(onHome());
    EXPECT_TRUE(f().bar.home);
    EXPECT_EQ(f().bar.clock, "14:32");
    EXPECT_EQ(f().bar.date, "Tue 10 Nov");
    EXPECT_EQ(home().chip, "ALL OFF");
    EXPECT_EQ(home().title, "Next run");
    EXPECT_EQ(home().big, "19:30") << "Beds and roses runs every 2 days from 1 June: today is a run day";
    EXPECT_EQ(home().bigNote, "Today");
    EXPECT_EQ(home().lines[0].text, "Beds and roses, 16 min");
    EXPECT_EQ(home().lines[1].text, "Rain sensor dry");
    EXPECT_EQ(buttonLabels(), (Labels{"Menu", "Manual", "Rain"}));
    EXPECT_EQ(h.focusedButton(), "Menu");
    EXPECT_FALSE(f().bar.fault);
}

TEST_F(UiNav, ManualRunTakesThreeInputsAndStopTakesOne)
{
    start();
    h.home(); // 1: Manual button
    ASSERT_EQ(f().kind, FrameKind::List);
    EXPECT_EQ(f().bar.title, "Manual run");
    EXPECT_EQ(h.focusedRow().label, "Front lawn") << "last used zone is preselected";
    h.press(); // 2: pick the zone
    ASSERT_TRUE(f().pickerOpen);
    EXPECT_EQ(f().picker.caption, "Run Front lawn for");
    EXPECT_EQ(pickerText(), "[10]");
    h.press(); // 3: confirm the minutes
    h.runMs(1500);

    ASSERT_TRUE(onHome());
    EXPECT_TRUE(h.out.zone[0]);
    EXPECT_EQ(home().chip, "WATERING");
    EXPECT_EQ(home().badge, "1");
    EXPECT_EQ(home().title, "Front lawn");
    EXPECT_EQ(home().chipNote, "Manual");
    EXPECT_EQ(home().bigNote, "left");
    EXPECT_GE(home().progressPermille, 0);
    EXPECT_EQ(buttonLabels(), (Labels{"Stop", "Pause", "Menu"}));
    EXPECT_EQ(h.focusedButton(), "Stop");

    h.press(); // one press stops
    EXPECT_FALSE(h.out.anyOn());
    EXPECT_EQ(home().chip, "ALL OFF");
    EXPECT_EQ(h.focusedButton(), "Menu") << "focus returns to the idle default";
}

TEST_F(UiNav, CountdownAndProgressFollowTheCore)
{
    start();
    h.home();
    h.turn(1); // Back lawn
    h.press();
    h.turn(-8); // 2 minutes
    EXPECT_EQ(pickerText(), "[2]");
    h.press();
    h.runSec(31);
    EXPECT_EQ(home().title, "Back lawn");
    const uint32_t left = h.state.snapshot().zones[1].remainingSec;
    EXPECT_EQ(home().big, countdownText(left));
    EXPECT_NEAR(home().progressPermille, 1000 - static_cast<int>(left) * 1000 / 120, 15);
    h.runSec(120);
    EXPECT_FALSE(h.out.anyOn());
    EXPECT_EQ(home().chip, "ALL OFF");
    // The total current seen while it ran alone is remembered for the zone list.
    h.press(); // the screen dimmed meanwhile: this wakes it
    h.press();
    ASSERT_TRUE(h.focusRow("Zones"));
    h.press();
    EXPECT_EQ(f().list.rows[1].value, "260 mA");
    EXPECT_EQ(f().list.rows[0].value, "Off");
}

TEST_F(UiNav, QueueIsShownAndSkipAdvances)
{
    start();
    h.home();
    ASSERT_TRUE(h.focusRow("Lawn morning"));
    EXPECT_EQ(h.focusedRow().value, "27 min");
    h.press();
    ASSERT_EQ(f().kind, FrameKind::Detail);
    EXPECT_EQ(f().detail.headline, "Run Lawn morning now?");
    EXPECT_EQ(f().detail.body, "3 zones, 27 min in total.");
    EXPECT_EQ(h.focusedButton(), "Run now");
    h.press();
    h.runMs(1500);

    ASSERT_TRUE(onHome());
    EXPECT_EQ(home().chip, "WATERING");
    EXPECT_EQ(home().chipNote, "Lawn morning");
    EXPECT_EQ(home().title, "Front lawn");
    EXPECT_EQ(home().lines[0].text, "Then Back lawn, 12 min  +1");
    EXPECT_EQ(buttonLabels(), (Labels{"Stop", "Pause", "Skip", "Menu"}));

    ASSERT_TRUE(h.focusButton("Skip"));
    h.press();
    h.runSec(4); // inter-zone delay
    EXPECT_TRUE(h.out.zone[1]);
    EXPECT_EQ(home().title, "Back lawn");
    EXPECT_EQ(home().lines[0].text, "Then Hedge, 5 min");

    ASSERT_TRUE(h.focusButton("Pause"));
    h.press();
    EXPECT_FALSE(h.out.anyOn());
    EXPECT_EQ(home().chip, "PAUSED");
    EXPECT_EQ(home().title, "Back lawn");
    const std::string leftWhenPaused = home().big;
    EXPECT_EQ(leftWhenPaused.substr(0, 3), "11:") << "the time left when it was paused";
    h.runSec(20);
    EXPECT_EQ(home().big, leftWhenPaused) << "does not count down while paused";
    EXPECT_EQ(buttonLabels(), (Labels{"Stop", "Resume", "Skip", "Menu"}));
    ASSERT_TRUE(h.focusButton("Resume"));
    h.press();
    h.runSec(3);
    EXPECT_TRUE(h.out.zone[1]);
    EXPECT_EQ(home().chip, "WATERING");
}

TEST_F(UiNav, HoldingHomeStopsFromAnyScreen)
{
    start();
    h.home();
    h.press();
    h.press();
    h.runSec(2);
    ASSERT_TRUE(h.out.zone[0]);
    h.focusButton("Menu");
    h.press();
    ASSERT_TRUE(h.focusRow("Setup"));
    h.press();
    EXPECT_EQ(f().bar.title, "Setup");
    EXPECT_FALSE(f().bar.run.empty()) << "the open valve is shown on every screen";
    EXPECT_EQ(f().bar.run.substr(0, 1), "1");

    h.holdHome();
    EXPECT_FALSE(h.out.anyOn());
    EXPECT_TRUE(f().toastOpen);
    EXPECT_EQ(f().toast.text, "All valves off");
    EXPECT_TRUE(f().bar.run.empty());
    EXPECT_EQ(f().bar.title, "Setup") << "stopping does not move the user";
}

// ---- screen power -------------------------------------------------------------------

TEST_F(UiNav, FirstInputOnlyWakesTheScreen)
{
    start();
    EXPECT_EQ(h.ui->power(), Power::Active);
    EXPECT_EQ(h.ui->backlightPercent(), 80);
    h.runSec(31);
    EXPECT_EQ(h.ui->power(), Power::Dim);
    EXPECT_EQ(h.ui->backlightPercent(), 16);
    h.runSec(300);
    EXPECT_EQ(h.ui->power(), Power::Off);
    EXPECT_EQ(h.ui->backlightPercent(), 0);

    h.press(); // would open the menu if it were not asleep
    EXPECT_EQ(h.ui->power(), Power::Active);
    EXPECT_TRUE(onHome());
    h.press();
    EXPECT_EQ(f().bar.title, "Menu");

    h.runSec(31);
    ASSERT_EQ(h.ui->power(), Power::Dim);
    h.turn(3); // dimmed: also only wakes
    EXPECT_EQ(h.focusedRow().label, "Manual run");
    h.turn(1);
    EXPECT_EQ(h.focusedRow().label, "Zones");
}

TEST_F(UiNav, StaysLitWhileWateringAndReturnsHome)
{
    start();
    h.home();
    h.press();
    h.turn(20); // 30 minutes
    h.press();
    ASSERT_TRUE(h.focusButton("Menu"));
    h.press();
    EXPECT_EQ(f().bar.title, "Menu");
    h.runSec(400);
    ASSERT_TRUE(h.out.zone[0]);
    EXPECT_EQ(h.ui->power(), Power::Dim) << "not off while a valve is open";
    EXPECT_TRUE(onHome()) << "an idle menu returns to Home";
    EXPECT_EQ(h.focusedButton(), "Stop");
    h.press(); // wakes
    ASSERT_TRUE(h.out.zone[0]);
    h.press(); // stops
    EXPECT_FALSE(h.out.anyOn());
    h.runSec(400);
    EXPECT_EQ(h.ui->power(), Power::Off);
}

// ---- safety: the display never contradicts the valves ---------------------------------

TEST_F(UiNav, NoSnapshotMeansNoClaimThatValvesAreOff)
{
    start();
    h.controllerFrozen = true;
    h.runSec(6);
    EXPECT_EQ(home().chip, "NO DATA");
    EXPECT_EQ(home().title, "Controller not responding");
    EXPECT_TRUE(f().bar.fault);
    EXPECT_EQ(h.focusedButton(), "Stop");
    h.controllerFrozen = false;
    h.runSec(2);
    EXPECT_EQ(home().chip, "ALL OFF");
}

TEST_F(UiNav, RandomInputsNeverShowOffWhileAValveIsOpen)
{
    start();
    std::mt19937 rng(20261110);
    int openSteps = 0;
    for (int i = 0; i < 3000; ++i) {
        const unsigned r = rng() % 100;
        if (r < 45) {
            h.input(InputEvent::turn(static_cast<int>(rng() % 5) - 2, static_cast<int>(rng() % 21) - 10), 50);
        } else if (r < 80) {
            h.input(InputEvent::press(Key::Select), 50);
        } else if (r < 90) {
            h.input(InputEvent::press(Key::Back), 50);
        } else if (r < 98) {
            h.input(InputEvent::press(Key::Home), 50);
        } else {
            h.runMs(1000);
        }
        // Give the display one snapshot period to catch up, then compare.
        h.runMs(100);
        const SystemState s = h.state.snapshot();
        bool coreOpen = false;
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            coreOpen = coreOpen || s.zones[z].state == ValveState::Open;
            ASSERT_EQ(h.out.zone[z], s.zones[z].state == ValveState::Open);
        }
        if (!coreOpen) {
            continue;
        }
        ++openSteps;
        if (f().kind == FrameKind::Home) {
            ASSERT_EQ(home().chip, "WATERING") << "step " << i;
            ASSERT_EQ(home().buttons.front().label, "Stop") << "step " << i;
        } else {
            ASSERT_FALSE(f().bar.run.empty()) << "step " << i << " on " << f().bar.title;
        }
    }
    EXPECT_GT(openSteps, 100) << "the walk did open valves";
}

// ---- faults -------------------------------------------------------------------------

TEST_F(UiNav, OpenCircuitFaultIsExplainedAndCleared)
{
    start();
    h.zoneMa[2] = 0; // the wire to zone 3 is cut
    h.runSec(400);   // screen is off by now
    ASSERT_EQ(h.ui->power(), Power::Off);
    h.press(); // wake
    h.home();
    h.turn(2);
    EXPECT_EQ(h.focusedRow().label, "Roses");
    h.press();
    h.press();
    h.runSec(3);

    EXPECT_FALSE(h.out.anyOn());
    ASSERT_TRUE(onHome());
    EXPECT_EQ(home().chip, "ALL OFF") << "the chip still tells the valve state";
    EXPECT_EQ(home().title, "Zone 3: no current") << "with nothing running the fault is the headline";
    EXPECT_EQ(home().titleTone, Tone::Fault);
    EXPECT_EQ(home().titleIcon, Icon::Fault);
    EXPECT_TRUE(home().big.empty());
    EXPECT_EQ(home().lines[0].text, "Next run Today 19:30");
    EXPECT_TRUE(f().bar.fault);
    EXPECT_EQ(buttonLabels(), (Labels{"Faults", "Menu", "Manual"}));
    EXPECT_EQ(h.focusedButton(), "Faults");

    h.runSec(400);
    EXPECT_EQ(h.ui->power(), Power::Dim) << "a latched fault keeps the screen on, dimmed";
    h.press(); // wake
    h.press(); // Faults
    EXPECT_EQ(f().bar.title, "Faults");
    EXPECT_EQ(h.focusedRow().label, "Zone 3: no current");
    h.press();
    ASSERT_EQ(f().kind, FrameKind::Detail);
    EXPECT_EQ(f().bar.title, "Roses");
    EXPECT_EQ(f().detail.headline, "Zone 3: no current");
    EXPECT_NE(f().detail.body.find("valve wire may be disconnected"), std::string::npos);
    EXPECT_EQ(buttonLabels(), (Labels{"OK", "Clear"}));
    ASSERT_TRUE(h.focusButton("Clear"));
    h.press();
    EXPECT_EQ(f().bar.title, "Faults");
    EXPECT_EQ(f().list.rows[0].label, "No faults");
    EXPECT_FALSE(f().bar.fault);
}

TEST_F(UiNav, FaultDuringAProgramIsABannerUnderTheRunningZone)
{
    start();
    h.zoneMa[1] = 0; // Back lawn is the second zone of Lawn morning
    h.home();
    ASSERT_TRUE(h.focusRow("Lawn morning"));
    h.press();
    h.press();
    h.runSec(3);
    ASSERT_TRUE(h.focusButton("Skip"));
    h.press();
    h.runSec(12); // Back lawn fails, the queue moves on to Hedge
    ASSERT_TRUE(h.out.zone[4]);
    EXPECT_EQ(home().chip, "WATERING");
    EXPECT_EQ(home().title, "Hedge");
    EXPECT_EQ(home().titleTone, Tone::Normal);
    EXPECT_EQ(home().lines[1].text, "Zone 2: no current");
    EXPECT_TRUE(home().lines[1].banner);
    EXPECT_EQ(h.focusedButton(), "Stop") << "Stop stays first while a valve is open";
}

TEST_F(UiNav, RefusedCommandIsExplained)
{
    start();
    // Something other than the display's own checks got it wrong: the core
    // refuses, and the display says why instead of pretending it started.
    h.commands.post(Command::runZone(7, 60)); // zone 8 is not in use on this 6-zone setup
    h.runMs(200);
    EXPECT_FALSE(h.out.anyOn());
    ASSERT_TRUE(f().toastOpen);
    EXPECT_EQ(f().toast.text.substr(0, 13), "Not started: ");
    EXPECT_EQ(home().chip, "ALL OFF");
}

TEST_F(UiNav, ZoneAtItsLongestRunRestsAndCannotBePicked)
{
    Config cfg = demoConfig();
    cfg.zones[0].maxRunSec = 120;
    h.start(cfg);
    h.home();
    h.press();
    EXPECT_EQ(pickerText(), "[2]") << "the picker stops at the zone's longest run";
    h.press();
    h.runSec(125);
    EXPECT_FALSE(h.out.anyOn());
    ASSERT_GT(h.state.snapshot().zones[0].restRemainingSec, 0u);
    h.wake();
    h.home();
    EXPECT_EQ(f().list.rows[1].label, "Front lawn");
    EXPECT_EQ(f().list.rows[1].value.substr(0, 8), "Resting ");
    EXPECT_TRUE(f().list.rows[1].disabled);
    EXPECT_EQ(h.focusedRow().label, "Back lawn") << "focus skips the resting zone";
}

TEST_F(UiNav, PauseThatLastsTooLongIsReported)
{
    Config cfg = demoConfig();
    cfg.schedule.pauseTimeoutMin = 1;
    h.start(cfg);
    h.home();
    h.press();
    h.press();
    h.runSec(3);
    ASSERT_TRUE(h.focusButton("Pause"));
    h.press();
    EXPECT_EQ(home().chip, "PAUSED");
    h.runSec(58);
    EXPECT_EQ(home().chip, "PAUSED");
    h.runSec(4);
    EXPECT_EQ(home().chip, "ALL OFF");
    ASSERT_TRUE(f().toastOpen);
    EXPECT_EQ(f().toast.text, "Paused too long: stopped");
}

TEST_F(UiNav, MissingSupplyBlocksManualRuns)
{
    start();
    h.sensors.sample.acPresent = false;
    h.runSec(1);
    EXPECT_EQ(home().lines[1].text, "No 24 V AC supply");
    EXPECT_EQ(buttonLabels(), (Labels{"Menu", "Manual", "Rain"})) << "a condition is not a latched fault";
    h.home();
    EXPECT_EQ(f().list.rows[0].label, "No 24 V AC supply");
    EXPECT_EQ(f().list.focus, -1) << "no zone can be picked";
    h.press();
    EXPECT_FALSE(f().pickerOpen);
    h.sensors.sample.acPresent = true;
    h.runSec(1);
    EXPECT_EQ(h.focusedRow().label, "Front lawn");
}

// ---- rain delay -----------------------------------------------------------------------

TEST_F(UiNav, RainDelayQuickSet)
{
    start();
    ASSERT_TRUE(h.focusButton("Rain"));
    h.press();
    ASSERT_TRUE(f().pickerOpen);
    EXPECT_EQ(pickerText(), "[1 day]");
    h.turn(1);
    EXPECT_EQ(pickerText(), "[2 days]");
    h.press();
    const SystemState s = h.state.snapshot();
    EXPECT_NEAR(static_cast<double>(s.rainDelayUntil), static_cast<double>(s.utc + 2 * 86400), 2.0);
    EXPECT_EQ(home().lines[1].text, "Rain delay until Thu 14:32");
    EXPECT_TRUE(f().bar.rainHold);
    EXPECT_EQ(home().big, "19:30");
    EXPECT_EQ(home().bigNote, "Thu") << "the forecast skips starts inside the delay";

    ASSERT_TRUE(h.focusButton("Rain"));
    h.press();
    EXPECT_EQ(pickerText(), "[2 days]") << "opens on the running delay";
    h.turn(-9);
    EXPECT_EQ(pickerText(), "[Off]");
    h.press();
    EXPECT_EQ(h.state.snapshot().rainDelayUntil, 0);
    EXPECT_EQ(home().bigNote, "Today");
}

// ---- zones ----------------------------------------------------------------------------

TEST_F(UiNav, ZoneDetailTogglesAndTests)
{
    start();
    h.diag.learned[3] = 245;
    h.press();
    ASSERT_TRUE(h.focusRow("Zones"));
    h.press();
    ASSERT_TRUE(h.focusRow("Vegetable beds"));
    h.press();
    EXPECT_EQ(f().bar.title, "4  Vegetable beds");
    ASSERT_TRUE(h.focusRow("Enabled"));
    EXPECT_EQ(h.focusedRow().value, "On");
    h.press();
    EXPECT_EQ(h.focusedRow().value, "Off");
    EXPECT_FALSE(h.config.get().zones[3].enabled);
    EXPECT_FALSE(h.state.snapshot().zones[3].enabled) << "the core reloaded the configuration";
    h.press();
    EXPECT_TRUE(h.config.get().zones[3].enabled);

    ASSERT_TRUE(h.focusRow("Test run"));
    h.press();
    EXPECT_EQ(pickerText(), "[2]");
    h.press();
    h.runSec(5);
    EXPECT_TRUE(h.out.zone[3]);
    EXPECT_EQ(h.focusedRow().label, "Stop") << "the row became Stop";
    bool sawLearned = false;
    for (const Row &r : f().list.rows) {
        sawLearned = sawLearned || (r.label == "Learned current" && r.value == "245 mA");
    }
    EXPECT_TRUE(sawLearned);
    h.press();
    EXPECT_FALSE(h.out.anyOn());
}

// ---- programs ---------------------------------------------------------------------------

TEST_F(UiNav, EditProgramAsksBeforeSaving)
{
    start();
    h.press();
    ASSERT_TRUE(h.focusRow("Programs"));
    h.press();
    EXPECT_EQ(f().list.rows[0].label, "Lawn morning");
    EXPECT_EQ(f().list.rows[0].value, "Wed 06:00");
    EXPECT_EQ(f().list.rows[1].value, "Today 19:30");
    h.press();
    EXPECT_EQ(f().bar.title, "Lawn morning");

    ASSERT_TRUE(h.focusRow("Start 1"));
    h.press();
    EXPECT_EQ(pickerText(), "[06]:00");
    h.turn(-1);
    h.press();
    h.turn(6);
    EXPECT_EQ(pickerText(), "05:[30]");
    h.press();
    EXPECT_EQ(h.focusedRow().value, "05:30");

    ASSERT_TRUE(h.focusRow("Days"));
    EXPECT_EQ(h.focusedRow().value, "Mo We Fr");
    h.press();
    h.turn(1);
    h.press(); // Tuesday on
    h.turn(9);
    h.press(); // OK
    EXPECT_EQ(h.focusedRow().value, "Mo Tu We Fr");

    ASSERT_TRUE(h.focusRow("Roses"));
    EXPECT_EQ(h.focusedRow().value, "Off");
    h.press();
    h.turn(8);
    h.press();
    EXPECT_EQ(h.focusedRow().value, "8 min");
    EXPECT_EQ(h.config.get().schedule.programs[0].startMinutes[0], 360) << "nothing is stored yet";

    h.back();
    ASSERT_EQ(f().kind, FrameKind::Detail);
    EXPECT_EQ(f().detail.headline, "Save changes to Lawn morning?");
    EXPECT_EQ(buttonLabels(), (Labels{"Save", "Discard", "Cancel"}));
    ASSERT_TRUE(h.focusButton("Cancel"));
    h.press();
    EXPECT_EQ(f().bar.title, "Lawn morning") << "Cancel keeps editing";
    EXPECT_EQ(h.focusedRow().label, "Roses");

    h.home(); // Home also asks
    ASSERT_EQ(f().kind, FrameKind::Detail);
    h.press(); // Save
    EXPECT_TRUE(onHome());
    const Program p = h.config.get().schedule.programs[0];
    EXPECT_EQ(p.startMinutes[0], 330);
    EXPECT_EQ(p.days.weekdayMask, 0b0101110);
    EXPECT_EQ(p.durationSec[2], 480u);
    EXPECT_EQ(home().big, "19:30");
}

TEST_F(UiNav, DiscardLeavesTheProgramAlone)
{
    start();
    h.press();
    h.focusRow("Programs");
    h.press();
    h.press();
    ASSERT_TRUE(h.focusRow("Enabled"));
    h.press();
    EXPECT_EQ(h.focusedRow().value, "Off");
    h.back();
    ASSERT_TRUE(h.focusButton("Discard"));
    h.press();
    EXPECT_EQ(f().bar.title, "Programs");
    EXPECT_TRUE(h.config.get().schedule.programs[0].enabled);
    // An idle screen with unsaved edits is not abandoned by the timeout.
    h.press();
    h.focusRow("Enabled");
    h.press();
    h.runSec(200);
    EXPECT_EQ(f().bar.title, "Lawn morning");
}

TEST_F(UiNav, AddDayRuleAndDeleteProgram)
{
    start();
    h.press();
    h.focusRow("Programs");
    h.press();
    ASSERT_TRUE(h.focusRow("Add program"));
    h.press();
    EXPECT_EQ(f().bar.title, "Program 3");
    EXPECT_EQ(f().list.rows[0].label, "No start time yet");
    h.back();
    EXPECT_EQ(f().bar.title, "Programs") << "an untouched new program is dropped without asking";
    h.press();

    ASSERT_TRUE(h.focusRow("Add start time"));
    h.press();
    EXPECT_EQ(pickerText(), "[06]:00");
    h.press();
    h.press();
    ASSERT_TRUE(h.focusRow("Runs on"));
    h.press();
    h.turn(1);
    EXPECT_EQ(pickerText(), "[Interval]");
    h.press();
    ASSERT_TRUE(h.focusRow("Every"));
    EXPECT_EQ(h.focusedRow().value, "1 day");
    h.press();
    h.turn(2);
    h.press();
    EXPECT_EQ(h.focusedRow().value, "3 days");
    ASSERT_TRUE(h.focusRow("Greenhouse"));
    h.press();
    h.turn(4);
    h.press();
    h.back();
    h.press(); // Save
    EXPECT_EQ(f().bar.title, "Programs");
    Config cfg = h.config.get();
    ASSERT_EQ(cfg.schedule.programCount, 3);
    EXPECT_EQ(cfg.schedule.programs[2].days.type, DayRuleType::Interval);
    EXPECT_EQ(cfg.schedule.programs[2].days.intervalDays, 3);
    EXPECT_EQ(cfg.schedule.programs[2].durationSec[5], 240u);
    EXPECT_EQ(f().list.rows[2].value, "Fri 06:00") << "06:00 today has passed; next is in 3 days";

    ASSERT_TRUE(h.focusRow("Program 3"));
    h.press();
    ASSERT_TRUE(h.focusRow("Delete program"));
    h.press();
    EXPECT_EQ(h.focusedButton(), "Cancel") << "the safe answer is focused";
    h.press();
    EXPECT_EQ(h.config.get().schedule.programCount, 3);
    h.press();
    ASSERT_TRUE(h.focusButton("Delete"));
    h.press();
    EXPECT_EQ(f().bar.title, "Programs");
    EXPECT_EQ(h.config.get().schedule.programCount, 2);
}

TEST_F(UiNav, SeasonalAdjustChangesTotals)
{
    start();
    h.press();
    h.focusRow("Programs");
    h.press();
    ASSERT_TRUE(h.focusRow("Seasonal adjust"));
    EXPECT_EQ(h.focusedRow().value, "100 %");
    h.press();
    h.input(InputEvent::turn(2, 10)); // a quick flick: accelerated
    EXPECT_EQ(pickerText(), "[150]");
    h.press();
    EXPECT_EQ(h.config.get().schedule.seasonalPercent, 150);
    h.home();
    EXPECT_EQ(home().lines[0].text, "Beds and roses, 24 min");
}

// ---- lists ------------------------------------------------------------------------------

TEST_F(UiNav, ListsScrollWithTheFocusAndBackReturns)
{
    start();
    h.press();
    EXPECT_EQ(f().bar.title, "Menu");
    EXPECT_TRUE(f().bar.back);
    EXPECT_EQ(f().list.rows.size(), 7u);
    EXPECT_EQ(f().list.top, 0);
    h.turn(6);
    EXPECT_EQ(h.focusedRow().label, "Setup");
    EXPECT_EQ(f().list.top, 2);
    h.turn(5);
    EXPECT_EQ(h.focusedRow().label, "Setup") << "no wrap at the end";
    h.turn(-20);
    EXPECT_EQ(h.focusedRow().label, "Manual run");
    EXPECT_EQ(f().list.top, 0);

    h.turn(4);
    h.press();
    EXPECT_EQ(f().bar.title, "Sensors and flow");
    EXPECT_EQ(f().list.focus, -1) << "read-only list: the knob scrolls";
    h.turn(1);
    EXPECT_EQ(f().list.top, 1);
    h.back();
    EXPECT_EQ(f().bar.title, "Menu");
    EXPECT_EQ(h.focusedRow().label, "Sensors and flow") << "focus is where it was";
    h.back();
    EXPECT_TRUE(onHome());
    h.back();
    EXPECT_TRUE(onHome());
}

// ---- setup ------------------------------------------------------------------------------

TEST_F(UiNav, DisplaySettingsPreviewAndPersist)
{
    start();
    h.press();
    h.focusRow("Setup");
    h.press();
    ASSERT_TRUE(h.focusRow("Display"));
    h.press();
    h.press();
    EXPECT_EQ(pickerText(), "[80]");
    h.turn(-4);
    EXPECT_EQ(h.ui->backlightPercent(), 60) << "brightness follows the knob";
    h.back();
    EXPECT_EQ(h.ui->backlightPercent(), 80) << "Back restores it";
    h.press();
    h.turn(-6);
    h.press();
    EXPECT_EQ(h.ui->backlightPercent(), 50);

    ASSERT_TRUE(h.focusRow("Dim after"));
    EXPECT_EQ(h.focusedRow().value, "30 s");
    h.press();
    h.turn(9);
    EXPECT_EQ(pickerText(), "[Never]");
    h.press();
    display::UiSettings stored;
    ASSERT_TRUE(h.settings.load(stored));
    EXPECT_EQ(stored.brightnessPercent, 50);
    EXPECT_EQ(stored.dimAfterSec, 0);
}

TEST_F(UiNav, SetClockByHand)
{
    h.clock.utc = 1000;
    h.start(demoConfig(), true, false);
    EXPECT_EQ(f().bar.clock, "--:--");
    EXPECT_EQ(home().title, "Clock not set");
    EXPECT_EQ(home().titleTone, Tone::Warn);
    EXPECT_EQ(home().lines[0].text, "Programs are off until it is set");
    EXPECT_EQ(home().lines[1].text, "Rain sensor dry") << "the clock is mentioned once, not twice";
    EXPECT_EQ(buttonLabels(), (Labels{"Menu", "Manual", "Rain"}));

    h.press();
    h.focusRow("Setup");
    h.press();
    ASSERT_TRUE(h.focusRow("Time"));
    h.press();
    ASSERT_TRUE(h.focusRow("Set the clock"));
    h.press();
    EXPECT_EQ(pickerText(), "[1] Jan 2025") << "starts at the earliest date the core accepts";
    h.turn(9);
    h.press();
    h.turn(10);
    h.press();
    h.turn(1);
    EXPECT_EQ(pickerText(), "10 Nov [2026]");
    h.press();
    EXPECT_EQ(f().picker.caption, "Time");
    h.turn(13);
    h.press();
    h.turn(32);
    EXPECT_EQ(pickerText(), "14:[32]");
    h.press();
    h.runSec(1);
    EXPECT_EQ(h.time.status(), TimeStatus::Synced);
    EXPECT_EQ(h.systemTime.t, utcOf(2026, 11, 10, 13, 32));
    EXPECT_EQ(f().bar.clock, "14:32");
    h.home();
    EXPECT_EQ(home().title, "Next run");
}

TEST_F(UiNav, FactoryResetNeedsADeliberateAnswer)
{
    start();
    h.press();
    h.focusRow("Setup");
    h.press();
    ASSERT_TRUE(h.focusRow("Factory reset"));
    h.press();
    EXPECT_EQ(f().detail.headline, "Erase all settings?");
    EXPECT_EQ(h.focusedButton(), "Cancel");
    h.press();
    EXPECT_EQ(h.system.restarts, 0);
    EXPECT_EQ(h.config.get().schedule.programCount, 2);

    h.press();
    ASSERT_TRUE(h.focusButton("Erase"));
    h.press();
    EXPECT_EQ(h.system.restarts, 1);
    EXPECT_EQ(h.net.forgets, 1);
    EXPECT_EQ(h.config.get().schedule.programCount, 0);
    display::UiSettings stored;
    EXPECT_FALSE(h.settings.load(stored));
}

TEST_F(UiNav, FactoryResetIsLeftToTheNetworkModuleWhenItDoesTheWholeJob)
{
    start();
    h.net.resetsDevice = true; // as zf_net: stops watering, erases NVS, restarts
    h.press();
    h.focusRow("Setup");
    h.press();
    ASSERT_TRUE(h.focusRow("Factory reset"));
    h.press();
    ASSERT_TRUE(h.focusButton("Erase"));
    h.press();
    EXPECT_EQ(h.net.forgets, 1);
    EXPECT_EQ(h.system.restarts, 0) << "the display does not restart on top of it";
    EXPECT_EQ(h.config.get().schedule.programCount, 2) << "nor erase the configuration a second time";
    EXPECT_TRUE(onHome());
    EXPECT_EQ(f().toast.text, "Erasing, then restarting");
}

TEST_F(UiNav, WifiSetupThatNeedsARestartSaysSo)
{
    start();
    h.net.connect();
    h.net.canProvision = false; // zf_net: provisioning only runs after a restart
    h.net.net.link = display::WifiLink::Disconnected;
    h.press();
    h.focusRow("Setup");
    h.press();
    h.press(); // Network
    h.press(); // Set up WiFi
    ASSERT_EQ(f().kind, FrameKind::Detail);
    EXPECT_EQ(f().detail.headline, "WiFi setup is not running");
    EXPECT_TRUE(f().detail.qr.empty());
}

// ---- wizard -------------------------------------------------------------------------------

TEST_F(UiNav, FirstBootWizard)
{
    start(false);
    ASSERT_EQ(f().kind, FrameKind::Detail);
    EXPECT_EQ(f().detail.headline, "Welcome");
    EXPECT_EQ(buttonLabels(), (Labels{"Start"}));
    h.home();
    EXPECT_EQ(f().detail.headline, "Welcome") << "Home is ignored until setup is done";
    h.press();

    EXPECT_EQ(f().bar.title, "Time zone");
    EXPECT_EQ(f().bar.step, "1/3");
    EXPECT_EQ(h.focusedRow().label, "Berlin, Paris") << "the configured zone is preselected";
    EXPECT_EQ(h.focusedRow().value, "+01:00");
    h.turn(-1);
    EXPECT_EQ(h.focusedRow().label, "London, Lisbon");
    h.press();
    EXPECT_STREQ(h.config.get().timezone.c_str(), "GMT0BST,M3.5.0/1,M10.5.0");

    EXPECT_EQ(f().bar.step, "2/3");
    EXPECT_FALSE(f().detail.qr.empty());
    EXPECT_TRUE(h.net.provisioning);
    EXPECT_EQ(buttonLabels(), (Labels{"Later"}));
    h.net.connect();
    h.runSec(1);
    h.back(); // any input refreshes; Back goes one step back
    EXPECT_EQ(f().bar.step, "1/3");
    h.press();
    EXPECT_EQ(f().detail.headline, "Connected");
    EXPECT_EQ(buttonLabels(), (Labels{"Next"}));
    h.press();

    EXPECT_EQ(f().bar.step, "3/3");
    ASSERT_TRUE(f().pickerOpen);
    EXPECT_EQ(pickerText(), "[6]");
    h.turn(-2);
    h.press();
    EXPECT_EQ(h.config.get().zoneCount, 4);
    EXPECT_EQ(f().detail.headline, "Ready");
    h.press();
    EXPECT_TRUE(onHome());
    display::UiSettings stored;
    ASSERT_TRUE(h.settings.load(stored));
    EXPECT_TRUE(stored.wizardDone);
    EXPECT_EQ(f().bar.wifi, Icon::Wifi);
}

TEST_F(UiNav, WizardSkipsWifiWithoutANetworkModule)
{
    h.net.net.link = display::WifiLink::Unavailable;
    start(false);
    h.press();
    EXPECT_EQ(f().bar.step, "1/2");
    h.press();
    EXPECT_EQ(f().bar.step, "2/2");
    ASSERT_TRUE(f().pickerOpen);
    h.back(); // cancelling the picker steps back
    EXPECT_EQ(f().bar.step, "1/2");
    EXPECT_EQ(f().bar.wifi, Icon::None);
}
