// SPDX-License-Identifier: Apache-2.0
// The screenshot walk: every screen in representative states. Each scene
// drives the display with the same inputs a user would give, so what is
// captured is reachable on the device. The order and names are the file
// names under docs/design/display-ui/.
#include "sim.hpp"

namespace zf::sim {
namespace {

using test::UiHarness;

struct Walk {
    ShotFn shot;
    void *ctx;
    int count = 0;
    void operator()(const char *name, UiHarness &h)
    {
        shot(ctx, name, h);
        ++count;
    }
};

// Home -> Menu -> a row.
// Lets time pass, then lights the screen again (the first input after it
// dimmed only wakes it).
void wait(UiHarness &h, uint32_t sec)
{
    h.runSec(sec);
    h.wake();
}

void openMenu(UiHarness &h, const char *row)
{
    h.focusButton("Menu");
    h.press();
    if (row != nullptr) {
        h.focusRow(row);
        h.press();
    }
}

void runProgram(UiHarness &h, const char *name)
{
    h.home();
    h.focusRow(name);
    h.press();
    h.press(); // Run now
}

void homeScenes(Walk &shot)
{
    {
        World w;
        shot("home-idle", w.h);
        w.h.runSec(31);
        shot("home-dimmed", w.h);
    }
    {
        World w;
        runProgram(w.h, "Lawn morning");
        wait(w.h, 4 * 60 + 9);
        shot("home-watering-program", w.h);
        w.h.focusButton("Pause");
        w.h.press();
        shot("home-paused", w.h);
    }
    {
        World w;
        w.h.home();
        w.h.turn(3); // Vegetable beds
        w.h.press();
        w.h.turn(5); // 15 min
        w.h.press();
        wait(w.h, 2 * 60 + 26);
        shot("home-watering-manual", w.h);
        w.h.press(); // Stop
        w.h.runMs(100);
        shot("home-stopped-toast", w.h);
    }
    {
        World w;
        w.h.focusButton("Rain");
        w.h.press();
        shot("picker-rain-delay", w.h);
        w.h.turn(1);
        w.h.press();
        w.h.runSec(4);
        shot("home-rain-delay", w.h);
    }
    {
        World w;
        w.h.sensors.sample.rainContactClosed = true;
        w.h.runSec(4);
        shot("home-rain-sensor-wet", w.h);
    }
    {
        World w;
        w.h.zoneMa[2] = 0; // wire of zone 3 cut
        w.h.home();
        w.h.turn(2);
        w.h.press();
        w.h.press();
        w.h.runSec(8);
        shot("home-fault", w.h);
        w.h.press(); // Faults
        shot("faults", w.h);
        w.h.press();
        shot("fault-detail", w.h);
        w.h.back();
        w.h.back();
        openMenu(w.h, "Zones");
        w.h.turn(2);
        shot("zones-fault", w.h);
        w.h.press();
        shot("zone-detail-fault", w.h);
    }
    {
        // The rev A board's stop test finds detector A releasing late (an
        // open bleed resistor): a fault that needs the repair confirmed.
        World w(true, true, true, true);
        w.h.board->a.holdMs = 1700;
        w.h.runSec(8);
        shot("home-board-fault", w.h);
        w.h.press(); // Faults
        w.h.press();
        shot("fault-detail-detector", w.h);
        w.h.turn(1);
        w.h.press(); // Repaired
        shot("fault-repair-confirm", w.h);
        w.h.back();
        w.h.back();
        w.h.back();
        openMenu(w.h, "Sensors and flow");
        w.h.turn(6);
        shot("sensors-board-fault", w.h);
    }
    {
        // Review A2's float test (step 2b) finds a 1 kohm bridge from the
        // backlight PWM onto the keep-alive net.
        World w(true, true, true, true);
        w.h.board->kaNet = test::KaNet::ToggleWeak;
        w.h.runSec(8);
        w.h.press(); // Faults
        w.h.press();
        shot("fault-detail-float-test", w.h);
    }
    {
        // A new board learns its detector release times in its first minute
        // with 24 VAC (19.7, the limits of step 9); meanwhile it says so.
        World w(true, true, true, true, false);
        w.h.runSec(4);
        shot("home-not-commissioned", w.h);
    }
    {
        World w;
        w.h.sensors.sample.acPresent = false;
        w.h.runSec(2);
        shot("home-no-ac", w.h);
        w.h.home();
        shot("manual-blocked", w.h);
    }
    {
        World w(true, false);
        shot("home-clock-not-set", w.h);
    }
    {
        World w;
        w.h.controllerFrozen = true;
        w.h.runSec(7);
        shot("home-no-data", w.h);
    }
}

void runScenesList(Walk &shot)
{
    {
        World w;
        openMenu(w.h, nullptr);
        shot("menu", w.h);
        w.h.turn(6);
        shot("menu-end", w.h);
    }
    {
        World w;
        w.h.home();
        shot("manual-pick", w.h);
        w.h.turn(1);
        w.h.press();
        shot("manual-minutes", w.h);
        w.h.back();
        w.h.focusRow("Beds and roses");
        shot("manual-programs", w.h);
        w.h.press();
        shot("manual-program-confirm", w.h);
    }
    {
        World w;
        // Give two zones a measured current, then leave one running.
        for (int zone : {0, 1}) {
            w.h.home();
            w.h.focusRow(zone == 0 ? "Front lawn" : "Back lawn");
            w.h.press();
            w.h.turn(-9);
            w.h.press();
            wait(w.h, 65);
        }
        w.h.home();
        w.h.focusRow("Roses");
        w.h.press();
        w.h.turn(11); // 12 min
        w.h.press();
        wait(w.h, 95);
        openMenu(w.h, "Zones");
        shot("zones", w.h);
        w.h.turn(2);
        w.h.press();
        shot("zone-detail-running", w.h);
        w.h.back();
        w.h.turn(-2);
        w.h.press();
        shot("zone-detail", w.h);
    }
}

void programScenes(Walk &shot)
{
    World w;
    UiHarness &h = w.h;
    openMenu(h, "Programs");
    shot("programs", h);
    h.press();
    shot("program-edit", h);
    h.focusRow("Start 1");
    h.press();
    shot("picker-start-hour", h);
    h.turn(-1);
    h.press();
    h.turn(6);
    shot("picker-start-minute", h);
    h.press();
    h.focusRow("Runs on");
    shot("program-edit-days", h);
    h.press();
    shot("picker-day-rule", h);
    h.back();
    h.focusRow("Days");
    h.press();
    h.turn(1);
    h.press();
    h.turn(1);
    shot("picker-weekdays", h);
    h.turn(9);
    h.press();
    h.focusRow("Roses");
    shot("program-edit-zones", h);
    h.press();
    h.turn(8);
    shot("picker-run-time", h);
    h.press();
    h.focusRow("Delete program");
    shot("program-edit-options", h);
    h.back();
    shot("program-save-question", h);
    h.focusButton("Cancel");
    h.press();
    h.focusRow("Delete program");
    h.press();
    shot("program-delete-question", h);
    h.press(); // Cancel
    h.back();
    h.press(); // Save
    shot("programs-saved", h);
    wait(h, 4); // let the toast go
    h.focusRow("Add program");
    h.press();
    shot("program-new", h);
    h.back();
    h.focusRow("Seasonal adjust");
    h.press();
    h.input(ui::InputEvent::turn(2, 4));
    shot("picker-seasonal", h);
}

void statusAndSetupScenes(Walk &shot)
{
    {
        World w;
        UiHarness &h = w.h;
        // Something for the sensors screen to show.
        h.home();
        h.press();
        h.press();
        wait(h, 20);
        h.sensors.sample.flowPulses += 3200;
        h.runSec(3);
        for (int i = 0; i < 8; ++i) {
            h.sensors.sample.flowPulses += 105;
            h.runSec(1);
        }
        openMenu(h, "Sensors and flow");
        shot("sensors", h);
        h.turn(2);
        shot("sensors-end", h);
        h.holdHome();
        h.runSec(5);
        h.back();
        h.focusRow("Faults");
        h.press();
        shot("faults-none", h);
    }
    {
        World w;
        UiHarness &h = w.h;
        openMenu(h, "Setup");
        shot("setup", h);
        h.press();
        shot("network", h);
        h.press();
        shot("wifi-restart-question", h);
        h.back();
        h.focusRow("Show setup code");
        h.press();
        shot("setup-code-hidden", h);
        h.holdSelect();
        shot("setup-code-shown", h);
        h.back();
        h.back();
        h.focusRow("Time");
        h.press();
        shot("time", h);
        h.press();
        shot("time-zones", h);
        h.back();
        h.focusRow("Set the clock");
        h.press();
        shot("picker-date", h);
        h.back();
        h.back();
        h.focusRow("Display");
        h.press();
        shot("display", h);
        h.press();
        h.turn(-3);
        shot("picker-brightness", h);
        h.back();
        h.back();
        h.focusRow("Zones in use");
        h.press();
        shot("picker-zone-count", h);
        h.back();
        h.focusRow("About");
        h.press();
        shot("about", h);
        h.back();
        h.focusRow("Factory reset");
        shot("setup-end", h);
        h.press();
        shot("factory-reset-question", h);
    }
    {
        World w(true, true, false);
        openMenu(w.h, "Setup");
        w.h.press();
        shot("network-not-set-up", w.h);
        w.h.press();
        shot("wifi-provisioning", w.h);
    }
    {
        // After 10 minutes without a BLE setup, zf_net switches to the setup
        // access point: no QR code, the passphrase is the setup code.
        World w(true, true, false);
        w.h.net.canProvision = false;
        w.h.net.net.link = display::WifiLink::Provisioning;
        w.h.net.net.setupNetwork.assign("ZoneFlow-1A2B");
        openMenu(w.h, "Setup");
        w.h.press();
        w.h.press();
        shot("wifi-setup-ap", w.h);
    }
    {
        // The spare input set to hold programs like the rain sensor.
        World w;
        Config c = w.h.config.get();
        c.spare.enabled = true;
        c.spareFunction = SpareFunction::InhibitLikeRain;
        w.h.config.update(c);
        w.h.commands.post(Command::simple(CommandType::ReloadConfig));
        w.h.sensors.sample.spareContactClosed = true;
        w.h.runSec(2);
        shot("home-spare-input", w.h);
    }
}

void wizardScenes(Walk &shot)
{
    World w(false, true, false);
    UiHarness &h = w.h;
    shot("wizard-welcome", h);
    h.press();
    shot("wizard-time-zone", h);
    h.press();
    shot("wizard-wifi", h);
    h.press();
    shot("wizard-zones", h);
    h.press();
    shot("wizard-done", h);
    h.press();
    shot("wizard-home", h);
}

} // namespace

int runScenes(ShotFn fn, void *ctx)
{
    Walk shot{fn, ctx};
    homeScenes(shot);
    runScenesList(shot);
    programScenes(shot);
    statusAndSetupScenes(shot);
    wizardScenes(shot);
    return shot.count;
}

} // namespace zf::sim
