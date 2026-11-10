// SPDX-License-Identifier: Apache-2.0
// Setup: network, time, display, about, factory reset, and the first-boot
// wizard.
#include <algorithm>

#include "screen_util.hpp"
#include "zf_ui/screens.hpp"
#include "zf_ui/tz_table.hpp"

namespace zf::ui {
namespace {

Str linkText(display::WifiLink link)
{
    switch (link) {
    case display::WifiLink::Unavailable:
        return Str::LinkUnavailable;
    case display::WifiLink::NotProvisioned:
        return Str::LinkNotProvisioned;
    case display::WifiLink::Provisioning:
        return Str::LinkProvisioning;
    case display::WifiLink::Connecting:
        return Str::LinkConnecting;
    case display::WifiLink::Connected:
        return Str::LinkConnected;
    case display::WifiLink::Disconnected:
        return Str::LinkDisconnected;
    }
    return Str::LinkUnavailable;
}

std::unique_ptr<Picker> makeZoneCountPicker(const Ctx &c)
{
    auto picker = std::make_unique<NumberPicker>(c.config.zoneCount, 1, static_cast<int>(kMaxZones));
    picker->caption = tr(Str::PickZoneCount);
    return picker;
}

bool applyZoneCount(Ctx &c, int64_t value)
{
    const auto count = static_cast<uint8_t>(std::clamp<int64_t>(value, 1, kMaxZones));
    return c.ui.updateConfig([count](Config &cfg) { cfg.zoneCount = count; });
}

// Fills a Detail frame with the provisioning QR code, or says why not.
struct Provisioning {
    bool started = false;
    std::string payload;

    void start(Ctx &c)
    {
        char buf[160] = {};
        started = c.ports.net.startProvisioning(buf, sizeof buf);
        payload = started ? buf : "";
    }
    void stop(Ctx &c)
    {
        if (started && c.ports.net.status().link != display::WifiLink::Connected) {
            c.ports.net.stopProvisioning();
        }
        started = false;
    }
    void build(const Ctx &c, DetailModel &d) const
    {
        const display::NetworkStatus net = c.ports.net.status();
        if (net.link == display::WifiLink::Connected) {
            d.icon = Icon::Wifi;
            d.tone = Tone::Ok;
            d.headline = tr(Str::LinkConnected);
            d.body = std::string(net.ssid.c_str()) + "\n" + net.ip.c_str();
        } else if (!started) {
            // Either the build has no network module, or it has one that
            // cannot start provisioning without a restart.
            const bool none = net.link == display::WifiLink::Unavailable;
            d.icon = Icon::WifiOff;
            d.tone = Tone::Muted;
            d.headline = tr(none ? Str::ProvUnavailableHeadline : Str::ProvNotRunningHeadline);
            d.body = tr(none ? Str::ProvUnavailableBody : Str::ProvNotRunningBody);
        } else {
            d.qr = payload;
            d.headline = tr(Str::ProvHeadline);
            d.body = tr(Str::ProvBody);
        }
    }
};

// ---- Setup ----------------------------------------------------------------------------

class SetupScreen final : public Screen {
public:
    enum : int { kNetwork = 1, kTime, kDisplay, kZoneCount, kAbout, kWizard, kReset };
    enum : int { kAskReset = 1 };

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::MenuSetup);
        auto &rows = out.list.rows;
        const display::NetworkStatus net = c.ports.net.status();
        rows.push_back(itemRow(kNetwork, net.link == display::WifiLink::Connected ? Icon::Wifi : Icon::WifiOff,
                               tr(Str::SetupNetwork), tr(linkText(net.link))));
        rows.push_back(itemRow(kTime, Icon::Clock, tr(Str::SetupTime), out.bar.clock,
                               c.state.timeStatus == TimeStatus::Unset ? Tone::Warn : Tone::Muted));
        rows.push_back(itemRow(kDisplay, Icon::Sun, tr(Str::SetupDisplay), percentText(c.settings.brightnessPercent)));
        rows.push_back(itemRow(kZoneCount, Icon::Sprinkler, tr(Str::SetupZoneCount), std::to_string(c.config.zoneCount),
                               Tone::Normal));
        rows.push_back(itemRow(kAbout, Icon::Info, tr(Str::SetupAbout), c.ports.system.firmwareVersion()));
        rows.push_back(itemRow(kWizard, Icon::Knob, tr(Str::SetupWizard)));
        Row reset = itemRow(kReset, Icon::Trash, tr(Str::SetupReset));
        reset.tone = Tone::Fault;
        rows.push_back(reset);
    }

    void activate(Ctx &c, int id) override
    {
        switch (id) {
        case kNetwork:
            c.ui.push(makeNetworkScreen());
            break;
        case kTime:
            c.ui.push(makeTimeScreen());
            break;
        case kDisplay:
            c.ui.push(makeDisplayScreen());
            break;
        case kZoneCount:
            c.ui.openPicker(kZoneCount, makeZoneCountPicker(c));
            break;
        case kAbout:
            c.ui.push(makeAboutScreen());
            break;
        case kWizard:
            c.ui.goHome();
            c.ui.startWizard();
            break;
        case kReset: {
            DetailModel q;
            q.icon = Icon::Trash;
            q.tone = Tone::Fault;
            q.headline = tr(Str::ResetHeadline);
            q.body = tr(Str::ResetBody);
            // Cancel first: a stray press does not erase.
            q.buttons = {button(kChoiceNo, Str::Cancel),
                         button(kChoiceYes, Str::ResetConfirm, Icon::Trash, Tone::Fault)};
            c.ui.confirm(kAskReset, q);
            break;
        }
        default:
            break;
        }
    }

    void picked(Ctx &c, int id, int64_t value) override
    {
        if (id == kZoneCount) {
            applyZoneCount(c, value);
        }
    }

    void confirmed(Ctx &c, int request, int choice) override
    {
        if (request == kAskReset && choice == kChoiceYes) {
            c.ui.factoryReset();
        }
    }
};

// ---- Network ----------------------------------------------------------------------------

class NetworkScreen final : public Screen {
public:
    enum : int { kSetup = 1 };

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::SetupNetwork);
        const display::NetworkStatus net = c.ports.net.status();
        auto &rows = out.list.rows;
        const bool up = net.link == display::WifiLink::Connected;
        rows.push_back(itemRow(kSetup, Icon::Plus, tr(Str::NetSetup)));
        rows.push_back(infoRow(tr(Str::NetWifi), tr(linkText(net.link)), up ? Tone::Ok : Tone::Muted,
                               up ? Icon::Wifi : Icon::WifiOff));
        if (!net.ssid.empty()) {
            rows.push_back(infoRow(tr(Str::NetSsid), net.ssid.c_str()));
        }
        if (up) {
            rows.push_back(infoRow(tr(Str::NetIp), net.ip.c_str()));
            rows.push_back(infoRow(tr(Str::NetSignal), fmt(Str::FmtDbm, static_cast<int>(net.rssiDbm))));
        }
        if (!net.hostname.empty()) {
            rows.push_back(infoRow(tr(Str::NetHostname), net.hostname.c_str()));
        }
        if (!net.mac.empty()) {
            rows.push_back(infoRow(tr(Str::NetMac), net.mac.c_str()));
        }
    }

    void activate(Ctx &c, int id) override
    {
        if (id == kSetup) {
            c.ui.push(makeProvisioningScreen());
        }
    }
};

class ProvisioningScreen final : public Screen {
public:
    FrameKind kind() const override { return FrameKind::Detail; }
    void entered(Ctx &c) override { prov_.start(c); }
    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::NetSetup);
        prov_.build(c, out.detail);
        out.detail.buttons = {button(1, Str::Done, Icon::Check)};
    }
    void activate(Ctx &c, int /*id*/) override
    {
        prov_.stop(c);
        c.ui.pop();
    }
    bool mayLeave(Ctx &c, bool /*toHome*/) override
    {
        prov_.stop(c);
        return true;
    }

private:
    Provisioning prov_;
};

// ---- Time -------------------------------------------------------------------------------

class TimeScreen final : public Screen {
public:
    enum : int { kZone = 1, kSet, kPickDate, kPickTime };

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::SetupTime);
        auto &rows = out.list.rows;
        // The first city of the entry is enough to recognise the zone.
        const int index = tzFind(c.config.timezone.view());
        std::string zone = tr(Str::TimeCustomZone);
        if (index >= 0) {
            zone = tzEntry(static_cast<std::size_t>(index)).cities;
            zone = zone.substr(0, zone.find(','));
        }
        rows.push_back(itemRow(kZone, Icon::Info, tr(Str::TimeZoneRow), zone, Tone::Normal));
        const bool unset = c.state.timeStatus == TimeStatus::Unset;
        rows.push_back(itemRow(kSet, Icon::Clock, tr(Str::TimeSet),
                               unset ? std::string(tr(Str::TimeUnset)) : out.bar.clock,
                               unset ? Tone::Warn : Tone::Normal));
        if (!unset) {
            rows.push_back(infoRow(tr(Str::TimeDate), out.bar.date));
        }
        const Str source = c.state.timeStatus == TimeStatus::Synced    ? Str::TimeSynced
                           : c.state.timeStatus == TimeStatus::FromRtc ? Str::TimeRtc
                                                                       : Str::TimeUnset;
        rows.push_back(infoRow(tr(Str::TimeSource), tr(source), unset ? Tone::Warn : Tone::Normal));
        rows.push_back(infoRow(tr(Str::TimeOffset), tzOffsetText(c.config.timezone.view()), Tone::Muted));
    }

    void activate(Ctx &c, int id) override
    {
        if (id == kZone) {
            c.ui.push(makeTimeZoneScreen());
        } else if (id == kSet) {
            // Start from the current clock if it is set, else from the
            // earliest date the core accepts.
            const UtcSeconds base = c.state.timeStatus == TimeStatus::Unset
                                        ? std::max<UtcSeconds>(c.state.utc, TimeService::kMinValidUtc)
                                        : c.state.utc;
            const civil::LocalDateTime local = c.tz.toLocal(base);
            minute_ = local.secondOfDay / 60;
            auto picker = std::make_unique<DatePicker>(local.daySerial);
            picker->caption = tr(Str::PickDate);
            c.ui.openPicker(kPickDate, std::move(picker));
        }
    }

    void picked(Ctx &c, int id, int64_t value) override
    {
        if (id == kPickDate) {
            day_ = value;
            auto picker = std::make_unique<TimePicker>(minute_, false, 1);
            picker->caption = tr(Str::PickTime);
            c.ui.openPicker(kPickTime, std::move(picker));
        } else if (id == kPickTime) {
            const UtcSeconds utc = c.tz.fromLocal(day_, static_cast<int32_t>(value) * 60);
            if (c.svc.time.setManual(utc)) {
                c.ui.toast(tr(Str::ToastSaved), Tone::Ok, Icon::Check);
            } else {
                c.ui.toast(fmt(Str::ToastNotSaved, tr(Str::TimeUnset)), Tone::Fault, Icon::Fault);
            }
        }
    }

private:
    int64_t day_ = 0;
    int minute_ = 0;
};

class TimeZoneScreen final : public Screen {
public:
    void entered(Ctx &c) override
    {
        ListModel scratch;
        focusId = buildTimeZoneRows(c, scratch);
        focusIndex = focusId;
        top = std::max(0, focusId - 2);
    }
    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::TimeZoneRow);
        buildTimeZoneRows(c, out.list);
    }
    void activate(Ctx &c, int id) override
    {
        if (applyTimeZone(c, id)) {
            c.ui.toast(tr(Str::ToastSaved), Tone::Ok, Icon::Check);
            c.ui.pop();
        }
    }
};

// ---- Display ----------------------------------------------------------------------------

class DisplayScreen final : public Screen {
public:
    enum : int { kBrightness = 1, kDim, kOff };

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::SetupDisplay);
        auto &rows = out.list.rows;
        rows.push_back(itemRow(kBrightness, Icon::Sun, tr(Str::DispBrightness),
                               percentText(c.settings.brightnessPercent), Tone::Normal));
        rows.push_back(itemRow(kDim, Icon::Clock, tr(Str::DispDim), secondsText(c.settings.dimAfterSec), Tone::Normal));
        rows.push_back(itemRow(kOff, Icon::Clock, tr(Str::DispOff), secondsText(c.settings.offAfterSec), Tone::Normal));
    }

    void activate(Ctx &c, int id) override
    {
        if (id == kBrightness) {
            before_ = c.settings.brightnessPercent;
            auto picker = std::make_unique<NumberPicker>(c.settings.brightnessPercent, 10, 100, 5);
            picker->caption = tr(Str::DispBrightness);
            picker->unit = tr(Str::UnitPercent);
            c.ui.openPicker(kBrightness, std::move(picker));
            return;
        }
        const bool dim = id == kDim;
        const uint16_t *values = dim ? kDimChoices : kOffChoices;
        const int count = dim ? kDimCount : kOffCount;
        const uint16_t current = dim ? c.settings.dimAfterSec : c.settings.offAfterSec;
        std::vector<std::string> options;
        int index = 0;
        for (int i = 0; i < count; ++i) {
            options.push_back(secondsText(values[i]));
            if (values[i] == current) {
                index = i;
            }
        }
        auto picker = std::make_unique<ChoicePicker>(std::move(options), index);
        picker->caption = tr(dim ? Str::DispDim : Str::DispOff);
        c.ui.openPicker(id, std::move(picker));
    }

    // Brightness follows the knob while the picker is open.
    void pickerChanged(Ctx &c, int id, int64_t value) override
    {
        if (id == kBrightness) {
            c.settings.brightnessPercent = static_cast<uint8_t>(std::clamp<int64_t>(value, 10, 100));
        }
    }
    void pickerCancelled(Ctx &c, int id) override
    {
        if (id == kBrightness) {
            c.settings.brightnessPercent = before_;
        }
    }
    void picked(Ctx &c, int id, int64_t value) override
    {
        if (id == kBrightness) {
            c.settings.brightnessPercent = static_cast<uint8_t>(std::clamp<int64_t>(value, 10, 100));
        } else if (id == kDim) {
            c.settings.dimAfterSec = kDimChoices[std::clamp<int64_t>(value, 0, kDimCount - 1)];
        } else if (id == kOff) {
            c.settings.offAfterSec = kOffChoices[std::clamp<int64_t>(value, 0, kOffCount - 1)];
        }
        c.ui.saveSettings();
    }

private:
    static constexpr uint16_t kDimChoices[] = {15, 30, 60, 120, 300, 0};
    static constexpr int kDimCount = 6;
    static constexpr uint16_t kOffChoices[] = {60, 300, 900, 1800, 0};
    static constexpr int kOffCount = 5;

    static std::string secondsText(uint16_t sec) { return sec == 0 ? std::string(tr(Str::Never)) : durationText(sec); }

    uint8_t before_ = 80;
};

class AboutScreen final : public Screen {
public:
    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::SetupAbout);
        auto &rows = out.list.rows;
        rows.push_back(infoRow(tr(Str::AboutFirmware), c.ports.system.firmwareVersion()));
        rows.push_back(infoRow(tr(Str::AboutBoard), c.ports.system.boardName()));
        rows.push_back(infoRow(tr(Str::AboutDevice), c.config.deviceName.c_str()));
        rows.push_back(infoRow(tr(Str::AboutIdf), c.ports.system.idfVersion()));
        rows.push_back(infoRow(tr(Str::AboutGraphics), c.ports.system.graphicsVersion()));
        rows.push_back(infoRow(tr(Str::AboutLicence), tr(Str::AboutLicenceValue)));
    }
};

// ---- First-boot wizard --------------------------------------------------------------------
//
// Welcome, time zone, WiFi (skipped when the build has no network module),
// zones in use, done. Back goes one step back; Home does nothing until the
// wizard has finished.

class WizardScreen final : public Screen {
public:
    enum Step : int { kWelcome, kTimeZone, kWifi, kZones, kDone };
    enum : int { kNext = 1, kZonePicker = 2 };

    FrameKind kind() const override { return step_ == kTimeZone ? FrameKind::List : FrameKind::Detail; }
    bool holdsScreen() const override { return true; }

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.back = step_ != kWelcome;
        out.bar.title = tr(Str::AppName);
        const int steps = wifi_ ? 3 : 2;
        DetailModel &d = out.detail;
        switch (step_) {
        case kWelcome:
            d.icon = Icon::Knob;
            d.tone = Tone::Running;
            d.headline = tr(Str::WizWelcome);
            d.body = tr(Str::WizWelcomeBody);
            d.buttons = {button(kNext, Str::WizStart, Icon::ChevronRight)};
            break;
        case kTimeZone:
            out.bar.title = tr(Str::TimeZoneRow);
            out.bar.step = "1/" + std::to_string(steps);
            buildTimeZoneRows(c, out.list);
            break;
        case kWifi:
            out.bar.title = tr(Str::NetWifi);
            out.bar.step = "2/" + std::to_string(steps);
            prov_.build(c, d);
            d.buttons = {button(kNext,
                                c.ports.net.status().link == display::WifiLink::Connected ? Str::Next : Str::Later,
                                Icon::ChevronRight)};
            break;
        case kZones:
            out.bar.title = tr(Str::SetupZoneCount);
            out.bar.step = std::to_string(steps) + "/" + std::to_string(steps);
            d.icon = Icon::Sprinkler;
            d.headline = tr(Str::PickZoneCount);
            d.buttons = {button(kNext, Str::Next, Icon::ChevronRight)};
            break;
        case kDone:
            d.icon = Icon::Check;
            d.tone = Tone::Ok;
            d.headline = tr(Str::WizDoneHeadline);
            d.body = tr(Str::WizDoneBody);
            d.buttons = {button(kNext, Str::Done, Icon::Check)};
            break;
        }
    }

    void entered(Ctx &c) override { wifi_ = c.ports.net.status().link != display::WifiLink::Unavailable; }

    void activate(Ctx &c, int id) override
    {
        switch (step_) {
        case kWelcome:
            enter(c, kTimeZone);
            break;
        case kTimeZone:
            if (applyTimeZone(c, id)) {
                enter(c, wifi_ ? kWifi : kZones);
            }
            break;
        case kWifi:
            prov_.stop(c);
            enter(c, kZones);
            break;
        case kZones:
            c.ui.openPicker(kZonePicker, makeZoneCountPicker(c));
            break;
        case kDone:
            c.settings.wizardDone = true;
            c.ui.saveSettings();
            c.ui.goHome();
            break;
        }
    }

    void picked(Ctx &c, int id, int64_t value) override
    {
        if (id == kZonePicker && applyZoneCount(c, value)) {
            enter(c, kDone);
        }
    }

    void pickerCancelled(Ctx &c, int id) override
    {
        if (id == kZonePicker) {
            enter(c, wifi_ ? kWifi : kTimeZone);
        }
    }

    bool mayLeave(Ctx &c, bool toHome) override
    {
        if (toHome) {
            return false;
        }
        switch (step_) {
        case kWelcome:
            break;
        case kTimeZone:
            enter(c, kWelcome);
            break;
        case kWifi:
            prov_.stop(c);
            enter(c, kTimeZone);
            break;
        case kZones:
            enter(c, wifi_ ? kWifi : kTimeZone);
            break;
        case kDone:
            enter(c, kZones);
            break;
        }
        return false;
    }

private:
    void enter(Ctx &c, Step step)
    {
        step_ = step;
        focusId = -1;
        focusIndex = -1;
        top = 0;
        if (step == kTimeZone) {
            ListModel scratch;
            focusId = buildTimeZoneRows(c, scratch);
            focusIndex = focusId;
            top = std::max(0, focusId - 2);
        } else if (step == kWifi) {
            prov_.start(c);
        } else if (step == kZones) {
            c.ui.openPicker(kZonePicker, makeZoneCountPicker(c));
        }
    }

    Step step_ = kWelcome;
    bool wifi_ = false;
    Provisioning prov_;
};

} // namespace

std::unique_ptr<Screen> makeSetupScreen() { return std::make_unique<SetupScreen>(); }
std::unique_ptr<Screen> makeNetworkScreen() { return std::make_unique<NetworkScreen>(); }
std::unique_ptr<Screen> makeProvisioningScreen() { return std::make_unique<ProvisioningScreen>(); }
std::unique_ptr<Screen> makeTimeScreen() { return std::make_unique<TimeScreen>(); }
std::unique_ptr<Screen> makeTimeZoneScreen() { return std::make_unique<TimeZoneScreen>(); }
std::unique_ptr<Screen> makeDisplayScreen() { return std::make_unique<DisplayScreen>(); }
std::unique_ptr<Screen> makeAboutScreen() { return std::make_unique<AboutScreen>(); }
std::unique_ptr<Screen> makeWizardScreen() { return std::make_unique<WizardScreen>(); }

} // namespace zf::ui
