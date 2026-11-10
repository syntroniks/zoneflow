// SPDX-License-Identifier: Apache-2.0
#include "zf_ui/ui.hpp"

#include <algorithm>
#include <cstdlib>

#include "zf_ui/faults.hpp"
#include "zf_ui/format.hpp"
#include "zf_ui/screens.hpp"

namespace zf::ui {
namespace {

// A question pushed by Ui::confirm(). The answer goes to the screen below.
class ConfirmScreen final : public Screen {
public:
    ConfirmScreen(int request, DetailModel question) : request_(request), question_(std::move(question)) {}
    FrameKind kind() const override { return FrameKind::Detail; }
    int request() const { return request_; }
    bool holdsScreen() const override { return true; }
    void build(const Ctx & /*ctx*/, Frame &out) override
    {
        out.detail = question_;
        out.bar.title = title_;
    }
    void activate(Ctx &ctx, int id) override { ctx.ui.finishConfirm(request_, id); }
    void setTitle(std::string title) { title_ = std::move(title); }

private:
    int request_;
    DetailModel question_;
    std::string title_;
};

Str skipReasonText(SkipReason r)
{
    switch (r) {
    case SkipReason::RainDelay:
        return Str::SkipRainDelay;
    case SkipReason::SensorInhibit:
        return Str::SkipSensor;
    case SkipReason::NoAc:
        return Str::SkipNoAc;
    case SkipReason::Maintenance:
        return Str::SkipMaintenance;
    case SkipReason::AlreadyQueued:
        return Str::SkipAlreadyQueued;
    case SkipReason::QueueFull:
        return Str::SkipQueueFull;
    case SkipReason::NothingToRun:
        return Str::SkipNothing;
    case SkipReason::MasterLockedOut:
        return Str::SkipMasterLocked;
    }
    return Str::SkipNothing;
}

// Why the core refused a run command.
Str runErrorText(RunError e)
{
    switch (e) {
    case RunError::Ok:
        break;
    case RunError::InvalidZone:
    case RunError::UnknownProgram:
        return Str::WhyNotFound;
    case RunError::ZeroDuration:
    case RunError::NothingToRun:
        return Str::SkipNothing;
    case RunError::QueueFull:
        return Str::SkipQueueFull;
    case RunError::AlreadyQueued:
        return Str::SkipAlreadyQueued;
    case RunError::Inhibited:
        return Str::WhyBlocked;
    case RunError::LockedOut:
        return Str::WhyZoneLocked;
    case RunError::ZoneResting:
        return Str::WhyZoneResting;
    case RunError::MasterLockedOut:
        return Str::SkipMasterLocked;
    }
    return Str::WhyUnknown;
}

// Why a queued run could not open its valve.
Str startErrorText(StartError e)
{
    switch (e) {
    case StartError::Disabled:
        return Str::WhyZoneDisabled;
    case StartError::LockedOut:
        return Str::WhyZoneLocked;
    case StartError::Inhibited:
        return Str::WhyBlocked;
    case StartError::Resting:
        return Str::WhyZoneResting;
    case StartError::MasterLockedOut:
        return Str::SkipMasterLocked;
    case StartError::Ok:
    case StartError::InvalidZone:
    case StartError::TooManyActive:
    case StartError::AlreadyActive:
    case StartError::ZeroDuration:
        break;
    }
    return Str::WhyUnknown;
}

} // namespace

Ui::Ui(const AppServices &svc, const display::Ports &ports) : svc_(svc), ports_(ports) { lastCurrentMa_.fill(-1); }

Ui::~Ui() = default;

Ctx Ui::ctx()
{
    return Ctx{*this,     svc_, ports_, state_,         config_,      tz_,
               settings_, now_, stale_, lastCurrentMa_, headEntryId_, headRemainingSec_};
}

void Ui::begin(MonoMs now)
{
    now_ = now;
    lastInputMs_ = now;
    lastSnapshotMs_ = now;
    ports_.settings.load(settings_);
    settings_.brightnessPercent = std::clamp<uint8_t>(settings_.brightnessPercent, 10, 100);
    refresh(now);
    stack_.clear();
    stack_.push_back(makeHomeScreen());
    if (!settings_.wizardDone) {
        startWizard();
    }
    rebuild();
}

void Ui::startWizard()
{
    Ctx c = ctx();
    stack_.push_back(makeWizardScreen());
    top().entered(c);
    dirty_ = true;
}

// ---- state from the core -----------------------------------------------------------

void Ui::refresh(MonoMs now)
{
    const uint32_t seq = svc_.state.sequence();
    if (seq != lastSequence_ || !haveSnapshot_) {
        if (seq != lastSequence_) {
            lastSnapshotMs_ = now;
        }
        state_ = svc_.state.snapshot();
        lastSequence_ = seq;
        haveSnapshot_ = true;
        trackCurrent(now);
        const QueueStatus &q = state_.queue;
        if (q.state == QueueState::Running && q.shownCount > 0 && q.items[0].zone < kMaxZones &&
            state_.zones[q.items[0].zone].state == ValveState::Open) {
            headEntryId_ = q.items[0].id;
            headRemainingSec_ = state_.zones[q.items[0].zone].remainingSec;
        }
        dirty_ = true;
    }
    const bool stale = now - lastSnapshotMs_ > kStaleMs;
    if (stale != stale_) {
        stale_ = stale;
        dirty_ = true;
    }
    const uint32_t rev = svc_.config.revision();
    if (rev != lastConfigRevision_) {
        lastConfigRevision_ = rev;
        config_ = svc_.config.get();
        tz_ = TimeZone::parse(config_.timezone.view()).value_or(TimeZone::utc());
        dirty_ = true;
    }
}

// The board measures one total current. While exactly one zone is open and
// has been for a few seconds, that total is the zone's (plus the master
// valve's, if one is fitted). Kept in RAM only.
void Ui::trackCurrent(MonoMs now)
{
    int open = -1;
    int count = 0;
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        if (state_.zones[z].state == ValveState::Open) {
            open = static_cast<int>(z);
            ++count;
        }
    }
    if (count != 1) {
        soloZone_ = -1;
        return;
    }
    if (open != soloZone_) {
        soloZone_ = open;
        soloSinceMs_ = now;
        return;
    }
    if (now - soloSinceMs_ >= kCurrentSettleMs && state_.sensors.valveCurrentMa >= 0) {
        lastCurrentMa_[static_cast<std::size_t>(open)] = state_.sensors.valveCurrentMa;
    }
}

// ---- input ---------------------------------------------------------------------------

void Ui::handleInput(const InputEvent &e, MonoMs now)
{
    now_ = now;
    refresh(now);
    const bool asleep = power_ != Power::Active;
    lastInputMs_ = now;
    power_ = Power::Active;
    if (asleep) {
        // The first input only wakes the screen.
        dirty_ = true;
        rebuild();
        return;
    }
    rebuild(); // the ids acted on below are the ones on screen now

    if (e.key == Key::Home && e.type == InputType::LongPress) {
        stopAll();
    } else if (picker_ != nullptr) {
        Ctx c = ctx();
        if (e.type == InputType::Turn) {
            picker_->turn(e.steps, e.accelSteps);
            top().pickerChanged(c, pickerId_, picker_->value());
        } else if (e.key == Key::Select && e.type == InputType::Press) {
            if (picker_->press()) {
                closePicker(true);
            }
        } else if (e.key == Key::Back && e.type != InputType::LongPress) {
            closePicker(false);
        } else if (e.key == Key::Home && e.type == InputType::Press) {
            closePicker(false);
            back(true);
        }
    } else if (e.type == InputType::Turn) {
        moveFocus(e.steps);
    } else if (e.key == Key::Select && e.type == InputType::Press) {
        activateFocused();
    } else if (e.key == Key::Back && (e.type == InputType::Press || e.type == InputType::Repeat)) {
        back(false);
    } else if (e.key == Key::Home && e.type == InputType::Press) {
        if (top().isHome()) {
            push(makeManualScreen());
        } else {
            back(true);
        }
    }
    reap();
    dirty_ = true;
    rebuild();
}

void Ui::closePicker(bool commit)
{
    if (picker_ == nullptr) {
        return;
    }
    const std::unique_ptr<Picker> p = std::move(picker_);
    const int id = pickerId_;
    Ctx c = ctx();
    if (commit) {
        top().picked(c, id, p->value());
    } else {
        top().pickerCancelled(c, id);
    }
    dirty_ = true;
}

void Ui::moveFocus(int steps)
{
    if (steps == 0) {
        return;
    }
    Screen &s = top();
    const int dir = steps > 0 ? 1 : -1;
    if (frame_.kind == FrameKind::List) {
        const auto &rows = frame_.list.rows;
        const int n = static_cast<int>(rows.size());
        int idx = frame_.list.focus;
        if (idx < 0) {
            // Nothing to focus: the knob scrolls.
            s.top = std::clamp(s.top + steps, 0, std::max(0, n - ListModel::kVisibleRows));
            return;
        }
        for (int i = 0; i < std::abs(steps); ++i) {
            int next = idx + dir;
            while (next >= 0 && next < n &&
                   (rows[static_cast<std::size_t>(next)].kind != RowKind::Item ||
                    rows[static_cast<std::size_t>(next)].disabled)) {
                next += dir;
            }
            if (next < 0 || next >= n) {
                // At the end: let the rest of the list scroll into view.
                s.top = dir < 0 ? 0 : std::max(0, n - ListModel::kVisibleRows);
                break;
            }
            idx = next;
        }
        s.focusId = rows[static_cast<std::size_t>(idx)].id;
        s.focusIndex = idx;
        return;
    }
    const std::vector<Button> &buttons = frame_.kind == FrameKind::Home ? frame_.home.buttons : frame_.detail.buttons;
    const int focus = frame_.kind == FrameKind::Home ? frame_.home.focus : frame_.detail.focus;
    if (buttons.empty()) {
        return;
    }
    const int next = std::clamp(focus + steps, 0, static_cast<int>(buttons.size()) - 1);
    s.focusId = buttons[static_cast<std::size_t>(next)].id;
    s.focusIndex = next;
}

void Ui::activateFocused()
{
    Screen &s = top();
    int id = -1;
    if (frame_.kind == FrameKind::List) {
        const int f = frame_.list.focus;
        if (f < 0) {
            return;
        }
        id = frame_.list.rows[static_cast<std::size_t>(f)].id;
    } else {
        const std::vector<Button> &buttons =
            frame_.kind == FrameKind::Home ? frame_.home.buttons : frame_.detail.buttons;
        const int f = frame_.kind == FrameKind::Home ? frame_.home.focus : frame_.detail.focus;
        if (f < 0 || f >= static_cast<int>(buttons.size())) {
            return;
        }
        id = buttons[static_cast<std::size_t>(f)].id;
    }
    Ctx c = ctx();
    s.activate(c, id);
}

void Ui::back(bool toHome)
{
    if (stack_.size() <= 1) {
        return;
    }
    // Going Home asks every screen on the way, so unsaved edits further down
    // the stack are not lost.
    Ctx c = ctx();
    do {
        if (!top().mayLeave(c, toHome)) {
            return;
        }
        pop();
    } while (toHome && stack_.size() > 1);
    if (toHome) {
        top().focusId = -1;
    }
}

// ---- navigation ------------------------------------------------------------------------

void Ui::push(std::unique_ptr<Screen> screen)
{
    stack_.push_back(std::move(screen));
    Ctx c = ctx();
    top().entered(c);
    dirty_ = true;
}

void Ui::pop()
{
    if (stack_.size() <= 1) {
        return;
    }
    graveyard_.push_back(std::move(stack_.back()));
    stack_.pop_back();
    dirty_ = true;
}

void Ui::goHome()
{
    picker_.reset();
    while (stack_.size() > 1) {
        graveyard_.push_back(std::move(stack_.back()));
        stack_.pop_back();
    }
    top().focusId = -1;
    dirty_ = true;
}

void Ui::reap() { graveyard_.clear(); }

void Ui::openPicker(int id, std::unique_ptr<Picker> picker)
{
    picker_ = std::move(picker);
    pickerId_ = id;
    dirty_ = true;
}

void Ui::confirm(int request, DetailModel question)
{
    auto screen = std::make_unique<ConfirmScreen>(request, std::move(question));
    screen->setTitle(frame_.bar.title); // keep the title of the screen that asks
    stack_.push_back(std::move(screen));
    dirty_ = true;
}

void Ui::finishConfirm(int request, int choice)
{
    pop();
    Ctx c = ctx();
    top().confirmed(c, request, choice);
}

void Ui::toast(std::string text, Tone tone, Icon icon)
{
    toast_.text = std::move(text);
    toast_.tone = tone;
    toast_.icon = icon;
    toastOpen_ = true;
    toastUntilMs_ = now_ + kToastMs;
    dirty_ = true;
}

bool Ui::post(const Command &cmd)
{
    if (svc_.commands.post(cmd)) {
        return true;
    }
    toast(tr(Str::ToastBusy), Tone::Warn, Icon::Fault);
    return false;
}

void Ui::stopAll()
{
    if (post(Command::simple(CommandType::StopAll))) {
        toast(tr(Str::ToastStopped), Tone::Normal, Icon::Stop);
    }
}

bool Ui::updateConfig(const std::function<void(Config &)> &change)
{
    Config cfg = svc_.config.get();
    change(cfg);
    const ValidationResult r = svc_.config.update(cfg);
    if (!r.ok) {
        toast(fmt(Str::ToastNotSaved, r.message.c_str()), Tone::Fault, Icon::Fault);
        return false;
    }
    post(Command::simple(CommandType::ReloadConfig));
    refresh(now_);
    return true;
}

void Ui::saveSettings() { ports_.settings.save(settings_); }

void Ui::factoryReset()
{
    post(Command::simple(CommandType::StopAll));
    if (ports_.net.factoryReset()) {
        // The network module erases everything, this display's settings
        // included, and restarts the device.
        goHome();
        toast(tr(Str::ToastErasing), Tone::Warn, Icon::Trash);
        return;
    }
    svc_.config.factoryReset();
    post(Command::simple(CommandType::ReloadConfig));
    ports_.settings.erase();
    ports_.system.restart();
    // Reached only where restart() returns (simulator, tests): behave like a
    // fresh boot.
    settings_ = display::UiSettings{};
    lastCurrentMa_.fill(-1);
    goHome();
    refresh(now_);
    startWizard();
}

// ---- events ----------------------------------------------------------------------------

void Ui::handleEvent(const Event &e, MonoMs now)
{
    now_ = now;
    refresh(now);
    auto programName = [this](uint16_t id) -> std::string {
        for (std::size_t i = 0; i < config_.schedule.programCount; ++i) {
            if (config_.schedule.programs[i].id == id) {
                return config_.schedule.programs[i].name.c_str();
            }
        }
        return tr(Str::MenuPrograms);
    };
    switch (e.type) {
    case EventType::ProgramStarted:
        if (e.program.source == RunSource::Schedule) {
            toast(fmt(Str::ToastProgramStarted, programName(e.program.programId).c_str()), Tone::Running, Icon::Drop);
        }
        break;
    case EventType::ProgramSkipped:
        toast(fmt(Str::ToastProgramSkipped, programName(e.program.programId).c_str(),
                  tr(skipReasonText(e.program.skipReason))),
              Tone::Warn, Icon::Rain);
        break;
    case EventType::ProgramMissed:
        toast(fmt(Str::ToastProgramMissed, programName(e.program.programId).c_str()), Tone::Warn, Icon::Clock);
        break;
    case EventType::CommandRejected:
        // The core refused a run the display asked for a moment ago: say why
        // in place of the "started" toast.
        if (e.rejected.command == CommandType::RunZone || e.rejected.command == CommandType::RunProgram) {
            toast(fmt(Str::ToastNotStarted, tr(runErrorText(e.rejected.error))), Tone::Warn, Icon::Fault);
        }
        break;
    case EventType::RunDropped: {
        const std::string zone = e.dropped.zone < kMaxZones && !config_.zones[e.dropped.zone].name.empty()
                                     ? std::string(config_.zones[e.dropped.zone].name.c_str())
                                     : fmt(Str::FmtZone, static_cast<int>(e.dropped.zone) + 1);
        toast(fmt(Str::ToastRunDropped, zone.c_str(), tr(startErrorText(e.dropped.reason))), Tone::Warn, Icon::Fault);
        break;
    }
    case EventType::PauseExpired:
        toast(tr(Str::ToastPauseExpired), Tone::Warn, Icon::Stop);
        break;
    case EventType::RunEnded:
        break; // for the history log; the zone change already updated the screen
    case EventType::FaultRaised:
        if (!faultClearsItself(e.fault.code)) {
            // A new latched fault lights the screen so it gets seen.
            power_ = Power::Active;
            lastInputMs_ = now;
            if (!top().isHome()) {
                toast(faultTitle(FaultItem{e.fault.code, e.fault.zone}), Tone::Fault, Icon::Fault);
            }
        }
        break;
    default:
        break;
    }
    dirty_ = true;
    rebuild();
}

// ---- time ------------------------------------------------------------------------------

bool Ui::keepLit() const
{
    if (stale_) {
        return true;
    }
    for (const ZoneStatus &z : state_.zones) {
        if (z.state != ValveState::Off) {
            return true;
        }
    }
    const uint32_t latched = state_.activeFaults & ~(1u << static_cast<unsigned>(FaultCode::TimeInvalid));
    return state_.masterOn || latched != 0;
}

void Ui::updatePower(MonoMs now)
{
    const MonoMs idle = now - lastInputMs_;
    Power next = Power::Active;
    if (settings_.offAfterSec != 0 && idle >= static_cast<MonoMs>(settings_.offAfterSec) * 1000 && !keepLit()) {
        next = Power::Off;
    } else if (settings_.dimAfterSec != 0 && idle >= static_cast<MonoMs>(settings_.dimAfterSec) * 1000) {
        next = Power::Dim;
    } else if (settings_.offAfterSec != 0 && idle >= static_cast<MonoMs>(settings_.offAfterSec) * 1000) {
        next = Power::Dim; // would be off, but something needs to stay visible
    }
    if (next != power_) {
        if (next != Power::Active && stack_.size() == 1) {
            top().focusId = -1; // Home: back to the default button (Stop while running)
        }
        power_ = next;
        dirty_ = true;
    }
    if (stack_.size() > 1 && idle >= kHomeReturnMs && !top().holdsScreen()) {
        goHome();
        reap();
    }
}

void Ui::tick(MonoMs now)
{
    now_ = now;
    refresh(now);
    updatePower(now);
    if (toastOpen_ && now >= toastUntilMs_) {
        toastOpen_ = false;
        dirty_ = true;
    }
    rebuild();
}

uint8_t Ui::backlightPercent() const
{
    switch (power_) {
    case Power::Active:
        return settings_.brightnessPercent;
    case Power::Dim:
        return static_cast<uint8_t>(std::max(4, settings_.brightnessPercent / 5));
    case Power::Off:
        break;
    }
    return 0;
}

// ---- frame -----------------------------------------------------------------------------

void Ui::buildBar(StatusBarModel &bar)
{
    bar = StatusBarModel{};
    bar.back = stack_.size() > 1;
    if (state_.timeStatus == TimeStatus::Unset) {
        bar.clock = "--:--";
    } else {
        const civil::LocalDateTime local = tz_.toLocal(state_.utc);
        bar.clock = clockText(local.secondOfDay / 60);
        bar.date = dateText(local);
    }
    if (!stale_) {
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            if (state_.zones[z].state == ValveState::Open) {
                bar.run = std::to_string(z + 1) + "  " + countdownText(state_.zones[z].remainingSec);
                bar.runTone = Tone::Running;
                break;
            }
        }
        if (bar.run.empty() && state_.queue.state == QueueState::Paused) {
            bar.run = tr(Str::ChipPaused);
            bar.runTone = Tone::Warn;
        }
    }
    switch (ports_.net.status().link) {
    case display::WifiLink::Unavailable:
        break;
    case display::WifiLink::Connected:
        bar.wifi = Icon::Wifi;
        break;
    case display::WifiLink::Connecting:
    case display::WifiLink::Provisioning:
        bar.wifi = Icon::Wifi;
        bar.wifiTone = Tone::Muted;
        break;
    case display::WifiLink::NotProvisioned:
    case display::WifiLink::Disconnected:
        bar.wifi = Icon::WifiOff;
        bar.wifiTone = Tone::Muted;
        break;
    }
    bar.rainHold = state_.rainDelayUntil != 0 || (state_.inhibitFlags & kInhibitSensor) != 0;
    bar.fault = state_.activeFaults != 0 || stale_;
}

void Ui::resolveButtonFocus(Screen &screen, const std::vector<Button> &buttons, int &focus)
{
    focus = 0;
    if (buttons.empty()) {
        screen.focusIndex = -1;
        return;
    }
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (buttons[i].id == screen.focusId) {
            focus = static_cast<int>(i);
            screen.focusIndex = focus;
            return;
        }
    }
    // The focused button is gone (or none was chosen): the first one, which
    // screens make the safe default.
    screen.focusId = buttons[0].id;
    screen.focusIndex = 0;
}

void Ui::resolveListFocus(Screen &screen, ListModel &list)
{
    const int n = static_cast<int>(list.rows.size());
    auto focusable = [&list](int i) {
        const Row &r = list.rows[static_cast<std::size_t>(i)];
        return r.kind == RowKind::Item && !r.disabled;
    };
    int focus = -1;
    for (int i = 0; i < n; ++i) {
        if (focusable(i) && list.rows[static_cast<std::size_t>(i)].id == screen.focusId) {
            focus = i;
            break;
        }
    }
    if (focus < 0) {
        // Nearest focusable row to where the focus was, searching down first.
        const int from = std::clamp(screen.focusIndex, 0, std::max(0, n - 1));
        for (int d = 0; d < n && focus < 0; ++d) {
            if (from + d < n && focusable(from + d)) {
                focus = from + d;
            } else if (from - d >= 0 && focusable(from - d)) {
                focus = from - d;
            }
        }
    }
    list.focus = focus;
    int topRow = screen.top;
    if (focus >= 0) {
        screen.focusId = list.rows[static_cast<std::size_t>(focus)].id;
        screen.focusIndex = focus;
        if (focus < topRow) {
            topRow = focus;
        }
        if (focus >= topRow + ListModel::kVisibleRows) {
            topRow = focus - ListModel::kVisibleRows + 1;
        }
        // Keep the heading above the focused row in view.
        if (topRow > 0 && focus == topRow && !focusable(topRow - 1)) {
            --topRow;
        }
        // Nothing to focus above: show the list from its start if it fits.
        bool anyAbove = false;
        for (int i = 0; i < focus; ++i) {
            anyAbove = anyAbove || focusable(i);
        }
        if (!anyAbove && focus < ListModel::kVisibleRows) {
            topRow = 0;
        }
    }
    topRow = std::clamp(topRow, 0, std::max(0, n - ListModel::kVisibleRows));
    screen.top = topRow;
    list.top = topRow;
}

void Ui::rebuild()
{
    if (!dirty_ || stack_.empty()) {
        return;
    }
    dirty_ = false;
    Screen &s = top();
    Frame f;
    buildBar(f.bar);
    f.kind = s.kind();
    const Ctx c = ctx();
    s.build(c, f);
    f.kind = s.kind();
    switch (f.kind) {
    case FrameKind::Home:
        resolveButtonFocus(s, f.home.buttons, f.home.focus);
        break;
    case FrameKind::Detail:
        resolveButtonFocus(s, f.detail.buttons, f.detail.focus);
        break;
    case FrameKind::List:
        resolveListFocus(s, f.list);
        break;
    }
    f.pickerOpen = picker_ != nullptr;
    if (picker_ != nullptr) {
        picker_->render(f.picker);
    }
    f.toastOpen = toastOpen_;
    f.toast = toast_;
    frame_ = std::move(f);
    ++frameSerial_;
}

} // namespace zf::ui
