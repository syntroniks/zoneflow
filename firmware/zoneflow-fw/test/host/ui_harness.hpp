// SPDX-License-Identifier: Apache-2.0
// The display logic wired to the real controller on fake hardware. Used by
// the UI host tests and by the desktop simulator (sim/), so both exercise
// the display against what the core really publishes.
#pragma once

#include <array>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>

#include "fake_board.hpp"
#include "fakes.hpp"
#include "zf/app/controller.hpp"
#include "zf/app/services.hpp"
#include "zf_display/stubs.hpp"
#include "zf_ui/ui.hpp"

namespace zf::test {

class FakeNetworkPort final : public display::INetworkPort {
public:
    display::NetworkStatus net{};
    bool canProvision = true;
    bool provisioning = false;
    int forgets = 0;
    bool resetsDevice = false; // true: behaves like zf_net, which erases and restarts itself
    int reprovisionRequests = 0;
    std::string code = "K7M2Q9XR4T"; // the device's setup code
    int codeReads = 0;

    display::NetworkStatus status() const override { return net; }
    bool startProvisioning(char *out, std::size_t outSize) override
    {
        if (!canProvision || net.link == display::WifiLink::Unavailable) {
            if (outSize > 0) {
                out[0] = '\0';
            }
            return false;
        }
        provisioning = true;
        if (net.link != display::WifiLink::Connected) {
            net.link = display::WifiLink::Provisioning;
        }
        std::snprintf(
            out, outSize, "%s",
            R"({"ver":"v1","name":"PROV_ZF_1A2B3C","username":"wifiprov","pop":"abcd1234","transport":"ble"})");
        return true;
    }
    void stopProvisioning() override
    {
        provisioning = false;
        if (net.link == display::WifiLink::Provisioning) {
            net.link = display::WifiLink::NotProvisioned;
        }
    }
    bool restartIntoProvisioning() override
    {
        if (net.link == display::WifiLink::Unavailable) {
            return false;
        }
        ++reprovisionRequests;
        return true;
    }
    bool setupCode(char *out, std::size_t outSize) override
    {
        if (outSize > 0) {
            out[0] = '\0';
        }
        if (net.link == display::WifiLink::Unavailable || code.size() >= outSize) {
            return false;
        }
        ++codeReads;
        std::snprintf(out, outSize, "%s", code.c_str());
        return true;
    }
    bool factoryReset() override
    {
        ++forgets;
        net = display::NetworkStatus{};
        net.link = display::WifiLink::NotProvisioned;
        return resetsDevice;
    }

    void connect(const char *ssid = "Garden-2G", const char *ip = "192.168.1.47", int8_t rssi = -58)
    {
        net.link = display::WifiLink::Connected;
        net.ssid.assign(ssid);
        net.ip.assign(ip);
        net.rssiDbm = rssi;
        net.mac.assign("84:F7:03:1A:2B:3C");
        net.hostname.assign("zoneflow.local");
    }
};

class FakeDiagPort final : public display::IZoneDiagPort {
public:
    std::array<uint16_t, kMaxZones> learned{};
    uint16_t learnedCurrentMa(ZoneIndex zone) const override { return zone < kMaxZones ? learned[zone] : 0; }
};

class FakeSystemPort final : public display::ISystemPort {
public:
    int restarts = 0;
    const char *firmwareVersion() const override { return "0.2.0-sim"; }
    const char *idfVersion() const override { return "host"; }
    const char *boardName() const override { return "simulator"; }
    const char *graphicsVersion() const override { return graphics.c_str(); }
    void restart() override { ++restarts; }
    std::string graphics = "none";
};

class UiHarness {
public:
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    CommandQueue commands;
    MemoryStorage storage;
    ConfigStore config{storage};
    FakeSystemTime systemTime;
    FakeRtc rtc;
    TimeService time{systemTime, &rtc};
    std::unique_ptr<Controller> ctrl;

    FakeNetworkPort net;
    FakeDiagPort diag;
    FakeSystemPort system;
    display::MemorySettingsStore settings;
    std::unique_ptr<ui::Ui> ui;

    // Current each open output draws; 0 simulates a broken wire.
    std::array<int32_t, kMaxZones> zoneMa{};
    int32_t masterMa = 180;
    // The control task stopped publishing (to test the stale-state display).
    bool controllerFrozen = false;
    // With useRevABoard() before start(): the simulated rev A output stage
    // (expander, relay, keep-alive detectors) instead of direct outputs, so
    // runs go through its self-test and its faults reach the display.
    std::unique_ptr<FakeBoard> board;
    std::unique_ptr<hw::OutputStage> stage;
    void useRevABoard()
    {
        board = std::make_unique<FakeBoard>(oe, clock);
        stage = std::make_unique<hw::OutputStage>(*board, oe, clock, revALayout());
    }

    UiHarness()
    {
        zoneMa.fill(260);
        net.net.link = display::WifiLink::NotProvisioned;
        bus.subscribe([](const Event &e, void *ctx) { static_cast<std::deque<Event> *>(ctx)->push_back(e); }, &events_);
    }

    // Boots the controller and the display. With wizardDone the display
    // starts on Home.
    void start(const Config &cfg, bool wizardDone = true, bool clockValid = true)
    {
        config.update(cfg);
        rtc.ok = clockValid;
        rtc.t = clock.utc;
        systemTime.t = clockValid ? clock.utc : 0;
        lastSystemTime_ = systemTime.t;
        time.boot();
        IOutputHal &outputs = stage ? static_cast<IOutputHal &>(*stage) : static_cast<IOutputHal &>(out);
        ctrl = std::make_unique<Controller>(ControllerDeps{clock, outputs, sensors, oe, bus, state, config, &storage});
        ctrl->begin();
        ctrl->setTimeStatus(time.status());
        display::UiSettings s;
        s.wizardDone = wizardDone;
        settings.save(s);
        services_ = std::make_unique<AppServices>(AppServices{bus, state, commands, config, time});
        const display::Ports ports{net, diag, system, settings};
        ui = std::make_unique<ui::Ui>(*services_, ports);
        runMs(1200); // 24 VAC debounce
        ui->begin(clock.mono);
        runMs(100);
    }

    // One 50 ms control tick, then the display.
    void step()
    {
        if (board) {
            for (int i = 0; i < 10; ++i) {
                clock.advanceMs(5);
                board->step5ms();
            }
        } else {
            clock.advanceMs(50);
        }
        if (systemTime.t != lastSystemTime_) {
            clock.utc = systemTime.t; // the user set the clock
            lastSystemTime_ = systemTime.t;
        }
        if (!controllerFrozen) {
            Command cmd;
            while (commands.pop(cmd)) {
                ctrl->handle(cmd);
            }
            int32_t ma = out.master ? masterMa : 0;
            for (std::size_t z = 0; z < kMaxZones; ++z) {
                ma += out.zone[z] ? zoneMa[z] : 0;
            }
            sensors.sample.valveCurrentMa = ma;
            if (board) {
                sensors.sample.valveCurrentMa = board->currentMa();
                sensors.sample.acPresent = board->acPresent;
                sensors.sample.sensorSupplyOn = true;
            }
            ctrl->setTimeStatus(time.status());
            ctrl->tick();
        }
        if (ui) {
            while (!events_.empty()) {
                ui->handleEvent(events_.front(), clock.mono);
                events_.pop_front();
            }
            ui->tick(clock.mono);
        }
    }
    void runMs(MonoMs ms)
    {
        for (MonoMs t = 0; t < ms; t += 50) {
            step();
        }
    }
    void runSec(uint32_t sec) { runMs(static_cast<MonoMs>(sec) * 1000); }

    // ---- inputs, each followed by a few ticks so the core has reacted ----
    void input(const ui::InputEvent &e, MonoMs settleMs = 200)
    {
        ui->handleInput(e, clock.mono);
        runMs(settleMs);
    }
    // The first input after the screen dimmed only wakes it. Call this
    // before scripted inputs that follow a wait.
    void wake()
    {
        if (ui->power() != ui::Power::Active) {
            input(ui::InputEvent::turn(0, 0));
        }
    }
    void turn(int steps) { input(ui::InputEvent::turn(steps, steps)); }
    void press() { input(ui::InputEvent::press(ui::Key::Select)); }
    void back() { input(ui::InputEvent::press(ui::Key::Back)); }
    void home() { input(ui::InputEvent::press(ui::Key::Home)); }
    void holdHome() { input(ui::InputEvent::press(ui::Key::Home, ui::InputType::LongPress)); }
    // The encoder switch held down: its Press on the way down, then Hold
    // once it has been down for the hold time.
    void holdSelect()
    {
        input(ui::InputEvent::press(ui::Key::Select), 200);
        runMs(ui::kSelectHoldMs - 200);
        input(ui::InputEvent::press(ui::Key::Select, ui::InputType::Hold));
    }

    const ui::Frame &frame() const { return ui->frame(); }
    // The focused row of a list frame; a row with id -2 if there is none.
    ui::Row focusedRow() const
    {
        const ui::ListModel &l = frame().list;
        if (frame().kind != ui::FrameKind::List || l.focus < 0) {
            ui::Row none;
            none.id = -2;
            return none;
        }
        return l.rows[static_cast<std::size_t>(l.focus)];
    }
    // Moves the focus to the list row with this label. Returns false if absent.
    bool focusRow(const std::string &label)
    {
        for (int guard = 0; guard < 64; ++guard) {
            if (focusedRow().label == label) {
                return true;
            }
            const ui::ListModel &l = frame().list;
            int target = -1;
            for (std::size_t i = 0; i < l.rows.size(); ++i) {
                if (l.rows[i].label == label) {
                    target = static_cast<int>(i);
                }
            }
            if (frame().kind != ui::FrameKind::List || target < 0) {
                return false;
            }
            turn(target > l.focus ? 1 : -1);
        }
        return false;
    }
    std::string focusedButton() const
    {
        const bool isHome = frame().kind == ui::FrameKind::Home;
        const auto &buttons = isHome ? frame().home.buttons : frame().detail.buttons;
        const int f = isHome ? frame().home.focus : frame().detail.focus;
        return f >= 0 && f < static_cast<int>(buttons.size()) ? buttons[static_cast<std::size_t>(f)].label : "";
    }
    bool focusButton(const std::string &label)
    {
        for (int guard = 0; guard < 8; ++guard) {
            if (focusedButton() == label) {
                return true;
            }
            const bool isHome = frame().kind == ui::FrameKind::Home;
            const auto &buttons = isHome ? frame().home.buttons : frame().detail.buttons;
            const int f = isHome ? frame().home.focus : frame().detail.focus;
            int target = -1;
            for (std::size_t i = 0; i < buttons.size(); ++i) {
                if (buttons[i].label == label) {
                    target = static_cast<int>(i);
                }
            }
            if (target < 0) {
                return false;
            }
            turn(target > f ? 1 : -1);
        }
        return false;
    }

private:
    std::deque<Event> events_;
    std::unique_ptr<AppServices> services_;
    UtcSeconds lastSystemTime_ = 0;
};

// A small garden for tests and screenshots: 6 zones, two programs.
inline Config demoConfig()
{
    Config c = Config::defaults();
    c.timezone.assign("CET-1CEST,M3.5.0,M10.5.0/3");
    c.zoneCount = 6;
    const char *names[] = {"Front lawn", "Back lawn", "Roses", "Vegetable beds", "Hedge", "Greenhouse"};
    for (std::size_t z = 0; z < 6; ++z) {
        c.zones[z].name.assign(names[z]);
    }
    c.master.enabled = false;
    c.driver.interZoneDelayMs = 2000;
    c.rain.enabled = true;
    c.rain.contact = ContactType::NormallyOpen;
    c.flow.enabled = true;

    Program lawn = makeProgram(1, {6 * 60}, {{0, 600}, {1, 720}, {4, 300}});
    lawn.name.assign("Lawn morning");
    lawn.days.type = DayRuleType::Weekdays;
    lawn.days.weekdayMask = 0b0101010; // Mon, Wed, Fri
    Program beds = makeProgram(2, {7 * 60, 19 * 60 + 30}, {{2, 300}, {3, 480}, {5, 180}});
    beds.name.assign("Beds and roses");
    beds.days.type = DayRuleType::Interval;
    beds.days.intervalDays = 2;
    beds.days.intervalAnchorDay = static_cast<int32_t>(civil::daysFromCivil(2026, 6, 1));
    c.schedule.programs[0] = lawn;
    c.schedule.programs[1] = beds;
    c.schedule.programCount = 2;
    return c;
}

} // namespace zf::test
