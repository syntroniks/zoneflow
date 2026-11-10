// SPDX-License-Identifier: Apache-2.0
// Home, menu, manual run, zones.
#include "screen_util.hpp"
#include "zf_ui/faults.hpp"
#include "zf_ui/next_run.hpp"
#include "zf_ui/screens.hpp"

namespace zf::ui {
namespace {

constexpr int kRainPickerId = 900;

// ---- Home ---------------------------------------------------------------------------

class HomeScreen final : public Screen {
public:
    enum : int { kStop = 1, kPause, kResume, kSkip, kMenu, kManual, kRain, kFaults };

    FrameKind kind() const override { return FrameKind::Home; }
    bool isHome() const override { return true; }

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.home = true;
        HomeModel &h = out.home;
        const SystemState &s = c.state;
        const QueueStatus &q = s.queue;

        int open = -1;
        int waiting = -1;
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            if (s.zones[z].state == ValveState::Open && open < 0) {
                open = static_cast<int>(z);
            }
            if (s.zones[z].state == ValveState::Waiting && waiting < 0) {
                waiting = static_cast<int>(z);
            }
        }
        const bool queued = q.length > 0 && q.shownCount > 0;
        const bool paused = q.state == QueueState::Paused && queued;
        const bool active = open >= 0 || waiting >= 0 || s.masterOn || queued;
        const std::vector<FaultItem> faults = listFaults(s);

        // Latched faults need a person; conditions (no 24 V AC, clock not
        // set) clear by themselves.
        std::vector<FaultItem> latchedFaults;
        bool acLost = false;
        for (const FaultItem &f : faults) {
            if (!faultClearsItself(f.code)) {
                latchedFaults.push_back(f);
            }
            acLost = acLost || f.code == FaultCode::AcLost;
        }
        const bool latched = !latchedFaults.empty();
        std::size_t bannerFault = 0; // first latched fault not shown elsewhere

        if (c.stale) {
            h.chip = tr(Str::ChipNoData);
            h.chipTone = Tone::Fault;
            h.chipIcon = Icon::Fault;
            h.title = tr(Str::HomeNoData);
            h.lines[0] = InfoLine{Icon::None, Tone::Muted, tr(Str::HomeNoDataHint), false};
        } else if (open >= 0) {
            buildRun(c, h, static_cast<ZoneIndex>(open), Str::ChipWatering, Tone::Running, Icon::Drop);
            const uint32_t left = s.zones[static_cast<std::size_t>(open)].remainingSec;
            h.big = countdownText(left);
            h.bigNote = tr(Str::HomeLeft);
            if (queued && q.items[0].zone == open) {
                h.progressPermille = progress(q.items[0].durationSec, left);
            }
        } else if (paused) {
            buildRun(c, h, q.items[0].zone, Str::ChipPaused, Tone::Warn, Icon::Pause);
            if (q.items[0].id == c.headEntryId) {
                h.big = countdownText(c.headRemainingSec);
                h.bigNote = tr(Str::HomeLeft);
                h.progressPermille = progress(q.items[0].durationSec, c.headRemainingSec);
            }
        } else if (waiting >= 0 || queued) {
            const ZoneIndex zone = waiting >= 0 ? static_cast<ZoneIndex>(waiting) : q.items[0].zone;
            buildRun(c, h, zone, Str::ChipStarting, Tone::Running, Icon::Drop);
            if (queued && q.items[0].zone == zone) {
                h.big = countdownText(q.items[0].durationSec);
            }
        } else {
            buildIdle(c, h);
            if (latched) {
                // Nothing is running: the fault is the headline, the next
                // run moves down a line.
                const NextRun next = s.timeStatus == TimeStatus::Unset
                                         ? NextRun{}
                                         : nextRun(c.config.schedule, c.tz, s.utc, s.rainDelayUntil);
                h.title = faultTitle(latchedFaults.front());
                h.titleTone = Tone::Fault;
                h.titleIcon = Icon::Fault;
                h.big.clear();
                h.bigNote.clear();
                h.lines[0] = next.valid
                                 ? InfoLine{Icon::Calendar, Tone::Normal,
                                            fmt(Str::HomeNextRunAt, whenText(next.at, s.utc, c.tz).c_str()), false}
                                 : InfoLine{};
                bannerFault = 1;
            }
        }

        // Second line: the most important thing left to say.
        Str blocked = Str::Count;
        if (c.stale) {
            // nothing: the headline says it
        } else if (bannerFault < latchedFaults.size()) {
            const std::string title = faultTitle(latchedFaults[bannerFault]);
            const int more = static_cast<int>(latchedFaults.size() - bannerFault) - 1;
            h.lines[1] = InfoLine{Icon::Fault, Tone::Fault,
                                  more > 0 ? fmt(Str::HomeFaultMore, title.c_str(), more) : title, true};
        } else if (runBlocked(c, blocked)) {
            h.lines[1] = InfoLine{Icon::Fault, acLost ? Tone::Fault : Tone::Warn, tr(blocked), acLost};
        } else if (s.rainDelayUntil != 0) {
            h.lines[1] = InfoLine{Icon::Rain, Tone::Warn,
                                  fmt(Str::HomeRainDelayUntil, whenText(s.rainDelayUntil, s.utc, c.tz).c_str()), false};
        } else if ((s.inhibitFlags & kInhibitSensor) != 0) {
            h.lines[1] = InfoLine{Icon::Rain, Tone::Warn, tr(Str::HomeSensorWet), false};
        } else if (s.sensors.rainEnabled) {
            h.lines[1] = InfoLine{Icon::Rain, Tone::Muted, tr(Str::HomeSensorDry), false};
        }

        // Buttons. Stop comes first whenever a valve is open or a run is
        // queued, so one press of the knob stops it.
        if (active || c.stale) {
            h.buttons.push_back(button(kStop, Str::BtnStop, Icon::Stop, Tone::Fault));
        }
        if (paused) {
            h.buttons.push_back(button(kResume, Str::BtnResume, Icon::Play));
        } else if (open >= 0 || waiting >= 0) {
            h.buttons.push_back(button(kPause, Str::BtnPause, Icon::Pause));
        }
        if (q.length > 1 && !c.stale) {
            h.buttons.push_back(button(kSkip, Str::BtnSkip, Icon::Skip));
        }
        if (latched && !active) {
            h.buttons.push_back(button(kFaults, Str::BtnFaults, Icon::Fault, Tone::Fault));
        }
        h.buttons.push_back(button(kMenu, Str::BtnMenu, Icon::Menu));
        if (!active && !c.stale) {
            h.buttons.push_back(button(kManual, Str::BtnManual, Icon::Play));
            if (!latched) {
                h.buttons.push_back(button(kRain, Str::BtnRain, Icon::Rain));
            }
        }

        // When the first button changes (a run starts or ends, a fault
        // appears), focus returns to it.
        const int first = h.buttons.front().id;
        if (first != lastFirst_) {
            lastFirst_ = first;
            focusId = -1;
        }
    }

    void activate(Ctx &c, int id) override
    {
        switch (id) {
        case kStop:
            c.ui.stopAll();
            break;
        case kPause:
            c.ui.post(Command::simple(CommandType::Pause));
            break;
        case kResume:
            c.ui.post(Command::simple(CommandType::Resume));
            break;
        case kSkip:
            c.ui.post(Command::simple(CommandType::Skip));
            break;
        case kMenu:
            c.ui.push(makeMenuScreen());
            break;
        case kManual:
            c.ui.push(makeManualScreen());
            break;
        case kRain:
            c.ui.openPicker(kRainPickerId, makeRainDelayPicker(c));
            break;
        case kFaults:
            c.ui.push(makeFaultsScreen());
            break;
        default:
            break;
        }
    }

    void picked(Ctx &c, int id, int64_t value) override
    {
        if (id == kRainPickerId) {
            applyRainDelay(c, value);
        }
    }

private:
    static int progress(uint32_t totalSec, uint32_t leftSec)
    {
        if (totalSec == 0) {
            return -1;
        }
        const uint32_t done = leftSec >= totalSec ? 0 : totalSec - leftSec;
        return static_cast<int>(static_cast<uint64_t>(done) * 1000 / totalSec);
    }

    static void buildRun(const Ctx &c, HomeModel &h, ZoneIndex zone, Str chip, Tone tone, Icon icon)
    {
        const QueueStatus &q = c.state.queue;
        h.chip = tr(chip);
        h.chipTone = tone;
        h.chipIcon = icon;
        h.badge = std::to_string(static_cast<int>(zone) + 1);
        h.title = zoneName(c.config, zone);
        if (q.shownCount > 0) {
            const Program *p = q.items[0].programId != 0 ? findProgram(c.config, q.items[0].programId) : nullptr;
            h.chipNote = p != nullptr ? std::string(p->name.c_str()) : std::string(tr(Str::HomeManual));
        }
        if (q.shownCount > 1) {
            const QueueItem &next = q.items[1];
            const std::string name = zoneName(c.config, next.zone);
            const std::string dur = durationText(next.durationSec);
            const int more = static_cast<int>(q.length) - 2;
            h.lines[0] = InfoLine{Icon::Skip, Tone::Normal,
                                  more > 0 ? fmt(Str::HomeThenMore, name.c_str(), dur.c_str(), more)
                                           : fmt(Str::HomeThen, name.c_str(), dur.c_str()),
                                  false};
        } else if (q.shownCount == 1) {
            h.lines[0] = InfoLine{Icon::Skip, Tone::Muted, tr(Str::HomeLastZone), false};
        }
    }

    static void buildIdle(const Ctx &c, HomeModel &h)
    {
        const SystemState &s = c.state;
        if (s.masterOn) {
            // Zones are off but the master valve is still open (post-close delay).
            h.chip = tr(Str::HomeMasterOpen);
            h.chipTone = Tone::Running;
            h.chipIcon = Icon::Drop;
        } else {
            h.chip = tr(Str::ChipAllOff);
            h.chipTone = Tone::Muted;
        }
        if (s.timeStatus == TimeStatus::Unset) {
            h.title = tr(Str::FaultTimeTitle);
            h.titleTone = Tone::Warn;
            h.titleIcon = Icon::Clock;
            h.lines[0] = InfoLine{Icon::None, Tone::Normal, tr(Str::HomeClockNotSet), false};
            return;
        }
        const NextRun next = nextRun(c.config.schedule, c.tz, s.utc, s.rainDelayUntil);
        if (!next.valid) {
            h.title = tr(Str::HomeNoSchedule);
            if (c.config.schedule.programCount == 0) {
                h.lines[0] = InfoLine{Icon::Calendar, Tone::Muted, tr(Str::HomeNoScheduleHint), false};
            }
            return;
        }
        h.title = tr(Str::HomeNextRun);
        h.big = clockText(c.tz.toLocal(next.at).secondOfDay / 60);
        h.bigNote = dayText(next.at, s.utc, c.tz);
        if (const Program *p = findProgram(c.config, next.programId)) {
            bool enabled[kMaxZones];
            for (std::size_t z = 0; z < kMaxZones; ++z) {
                enabled[z] = c.config.zones[z].enabled;
            }
            const ProgramTotals t = programTotals(*p, c.config.schedule, c.config.zoneCount, enabled);
            h.lines[0] =
                InfoLine{Icon::Calendar, Tone::Normal,
                         fmt(Str::HomeProgramSummary, p->name.c_str(), durationText(t.totalSec).c_str()), false};
        }
    }

    int lastFirst_ = -1;
};

// ---- Menu ---------------------------------------------------------------------------

class MenuScreen final : public Screen {
public:
    enum : int { kManual = 1, kZones, kPrograms, kRain, kSensors, kFaults, kSetup };

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::MenuTitle);
        auto &rows = out.list.rows;
        rows.push_back(itemRow(kManual, Icon::Play, tr(Str::MenuManual)));
        rows.push_back(itemRow(kZones, Icon::Sprinkler, tr(Str::MenuZones)));
        rows.push_back(
            itemRow(kPrograms, Icon::Calendar, tr(Str::MenuPrograms), std::to_string(c.config.schedule.programCount)));
        if (c.state.rainDelayUntil != 0) {
            rows.push_back(itemRow(kRain, Icon::Rain, tr(Str::MenuRainDelay),
                                   whenShortText(c.state.rainDelayUntil, c.state.utc, c.tz), Tone::Warn));
        } else {
            rows.push_back(itemRow(kRain, Icon::Rain, tr(Str::MenuRainDelay), tr(Str::Off)));
        }
        rows.push_back(itemRow(kSensors, Icon::Gauge, tr(Str::MenuSensors)));
        const std::size_t faults = listFaults(c.state).size();
        Row f = itemRow(kFaults, Icon::Fault, tr(Str::MenuFaults),
                        faults == 0 ? std::string(tr(Str::None)) : std::to_string(faults),
                        faults == 0 ? Tone::Muted : Tone::Fault);
        rows.push_back(f);
        rows.push_back(itemRow(kSetup, Icon::Sliders, tr(Str::MenuSetup)));
    }

    void activate(Ctx &c, int id) override
    {
        switch (id) {
        case kManual:
            c.ui.push(makeManualScreen());
            break;
        case kZones:
            c.ui.push(makeZonesScreen());
            break;
        case kPrograms:
            c.ui.push(makeProgramsScreen());
            break;
        case kRain:
            c.ui.openPicker(kRainPickerId, makeRainDelayPicker(c));
            break;
        case kSensors:
            c.ui.push(makeSensorsScreen());
            break;
        case kFaults:
            c.ui.push(makeFaultsScreen());
            break;
        case kSetup:
            c.ui.push(makeSetupScreen());
            break;
        default:
            break;
        }
    }

    void picked(Ctx &c, int id, int64_t value) override
    {
        if (id == kRainPickerId) {
            applyRainDelay(c, value);
        }
    }
};

// ---- zone rows shared by Manual run and Zones -------------------------------------------

constexpr int kZoneIdBase = 100;
constexpr int kProgramIdBase = 200;

Row zoneRow(const Ctx &c, ZoneIndex z, bool showCurrent)
{
    const ZoneStatus &zs = c.state.zones[z];
    Row r = itemRow(kZoneIdBase + z, Icon::None, zoneName(c.config, z));
    r.badge = std::to_string(static_cast<int>(z) + 1);
    if (zs.state == ValveState::Open) {
        r.value = countdownText(zs.remainingSec);
        r.valueTone = Tone::Running;
        r.valueIcon = Icon::Drop;
    } else if (zs.state == ValveState::Waiting) {
        r.value = tr(Str::ZoneStarting);
        r.valueTone = Tone::Running;
        r.valueIcon = Icon::Drop;
    } else if (zs.fault != FaultCode::None || zs.lockedOut) {
        r.value = tr(zs.lockedOut ? Str::ZoneLocked : Str::ZoneFault);
        r.valueTone = Tone::Fault;
        r.valueIcon = zs.lockedOut ? Icon::Lock : Icon::Fault;
    } else if (!zs.enabled) {
        r.value = tr(Str::ZoneDisabled);
    } else if (zs.restRemainingSec > 0) {
        // Reached its longest run; the core keeps it closed for a while.
        r.value = std::string(tr(Str::ZoneResting)) + " " + countdownText(zs.restRemainingSec);
        r.valueTone = Tone::Warn;
    } else if (showCurrent && c.lastCurrentMa[z] >= 0) {
        r.value = currentText(c.lastCurrentMa[z]);
    } else if (showCurrent) {
        r.value = tr(Str::ZoneOff);
    }
    return r;
}

// ---- Manual run -----------------------------------------------------------------------

class ManualScreen final : public Screen {
public:
    void entered(Ctx &c) override
    {
        const ZoneIndex last = c.settings.lastManualZone;
        focusId = kZoneIdBase + (last < zoneCount(c) ? last : 0);
        focusIndex = 1 + (last < zoneCount(c) ? last : 0);
    }

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::MenuManual);
        auto &rows = out.list.rows;
        Str why = Str::Count;
        const bool blocked = runBlocked(c, why);
        if (blocked) {
            Row r = infoRow(tr(why), {}, Tone::Normal, Icon::Fault);
            r.tone = Tone::Fault;
            rows.push_back(r);
        }
        rows.push_back(headerRow(Str::HdrZones));
        for (std::size_t z = 0; z < zoneCount(c); ++z) {
            Row r = zoneRow(c, static_cast<ZoneIndex>(z), false);
            const ZoneStatus &zs = c.state.zones[z];
            r.disabled = blocked || !zs.enabled || zs.lockedOut || zs.restRemainingSec > 0;
            rows.push_back(r);
        }
        if (c.config.schedule.programCount > 0) {
            rows.push_back(headerRow(Str::HdrPrograms));
        }
        for (std::size_t i = 0; i < c.config.schedule.programCount; ++i) {
            const Program &p = c.config.schedule.programs[i];
            const ProgramTotals t = totals(c, p);
            Row r = itemRow(kProgramIdBase + static_cast<int>(i), Icon::Calendar, p.name.c_str(),
                            t.zones > 0 ? durationText(t.totalSec) : std::string(tr(Str::SkipNothing)));
            r.disabled = blocked || t.zones == 0;
            rows.push_back(r);
        }
    }

    void activate(Ctx &c, int id) override
    {
        if (id >= kProgramIdBase) {
            const std::size_t index = static_cast<std::size_t>(id - kProgramIdBase);
            if (index >= c.config.schedule.programCount) {
                return;
            }
            const Program &p = c.config.schedule.programs[index];
            const ProgramTotals t = totals(c, p);
            DetailModel q;
            q.icon = Icon::Play;
            q.tone = Tone::Running;
            q.headline = fmt(Str::ConfirmRunProgram, p.name.c_str());
            q.body = fmt(Str::ConfirmRunProgramBody, t.zones, durationText(t.totalSec).c_str());
            q.buttons = {button(kChoiceYes, Str::RunNow, Icon::Play), button(kChoiceNo, Str::Cancel)};
            c.ui.confirm(id, q);
            return;
        }
        const auto zone = static_cast<ZoneIndex>(id - kZoneIdBase);
        if (zone >= zoneCount(c)) {
            return;
        }
        const int maxMin = static_cast<int>(std::max<uint32_t>(c.config.zones[zone].maxRunSec / 60, 1));
        auto picker = std::make_unique<NumberPicker>(c.settings.lastManualMinutes, 1, maxMin);
        picker->caption = fmt(Str::PickRunFor, zoneName(c.config, zone).c_str());
        picker->unit = tr(Str::UnitMin);
        c.ui.openPicker(id, std::move(picker));
    }

    void picked(Ctx &c, int id, int64_t value) override
    {
        const auto zone = static_cast<ZoneIndex>(id - kZoneIdBase);
        if (zone >= zoneCount(c) || value < 1) {
            return;
        }
        const bool busy = c.state.queue.length > 0;
        if (!c.ui.post(Command::runZone(zone, static_cast<uint32_t>(value) * 60))) {
            return;
        }
        if (c.settings.lastManualZone != zone || c.settings.lastManualMinutes != value) {
            c.settings.lastManualZone = zone;
            c.settings.lastManualMinutes = static_cast<uint16_t>(value);
            c.ui.saveSettings();
        }
        const std::string name = zoneName(c.config, zone);
        c.ui.toast(
            busy ? fmt(Str::ToastZoneQueued, name.c_str())
                 : fmt(Str::ToastZoneStarted, name.c_str(), durationText(static_cast<uint32_t>(value) * 60).c_str()),
            Tone::Running, Icon::Drop);
        c.ui.goHome();
    }

    void confirmed(Ctx &c, int request, int choice) override
    {
        const std::size_t index = static_cast<std::size_t>(request - kProgramIdBase);
        if (choice != kChoiceYes || index >= c.config.schedule.programCount) {
            return;
        }
        const Program &p = c.config.schedule.programs[index];
        if (c.ui.post(Command::runProgram(p.id))) {
            c.ui.toast(fmt(Str::ToastProgramStarted, p.name.c_str()), Tone::Running, Icon::Drop);
            c.ui.goHome();
        }
    }

private:
    static ProgramTotals totals(const Ctx &c, const Program &p)
    {
        bool enabled[kMaxZones];
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            enabled[z] = c.config.zones[z].enabled;
        }
        return programTotals(p, c.config.schedule, c.config.zoneCount, enabled);
    }
};

// ---- Zones ----------------------------------------------------------------------------

class ZonesScreen final : public Screen {
public:
    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::MenuZones);
        for (std::size_t z = 0; z < zoneCount(c); ++z) {
            out.list.rows.push_back(zoneRow(c, static_cast<ZoneIndex>(z), true));
        }
    }

    void activate(Ctx &c, int id) override
    {
        const auto zone = static_cast<ZoneIndex>(id - kZoneIdBase);
        if (zone < zoneCount(c)) {
            c.ui.push(makeZoneScreen(zone));
        }
    }
};

class ZoneScreen final : public Screen {
public:
    enum : int { kFault = 1, kEnabled, kTest, kStop, kClear };

    explicit ZoneScreen(ZoneIndex zone) : zone_(zone) {}

    void build(const Ctx &c, Frame &out) override
    {
        const ZoneStatus &zs = c.state.zones[zone_];
        const ZoneConfig &zc = c.config.zones[zone_];
        out.bar.title = std::to_string(static_cast<int>(zone_) + 1) + "  " + zoneName(c.config, zone_);
        auto &rows = out.list.rows;

        if (zs.fault != FaultCode::None) {
            Row r = itemRow(kFault, Icon::Fault, faultTitle(FaultItem{zs.fault, zone_}));
            r.tone = Tone::Fault;
            r.valueIcon = Icon::ChevronRight;
            rows.push_back(r);
        }
        const bool running = zs.state != ValveState::Off;
        if (running) {
            rows.push_back(itemRow(kStop, Icon::Stop, tr(Str::BtnStop),
                                   zs.state == ValveState::Open ? countdownText(zs.remainingSec)
                                                                : std::string(tr(Str::ZoneStarting)),
                                   Tone::Running));
        } else {
            Str why = Str::Count;
            Row r = itemRow(kTest, Icon::Play, tr(Str::ZoneTest), fmt(Str::FmtMinutes, kTestMinutes));
            r.disabled = !zs.enabled || zs.lockedOut || zs.restRemainingSec > 0 || runBlocked(c, why);
            if (zs.restRemainingSec > 0) {
                r.value = std::string(tr(Str::ZoneResting)) + " " + countdownText(zs.restRemainingSec);
                r.valueTone = Tone::Warn;
            }
            rows.push_back(r);
        }
        rows.push_back(itemRow(kEnabled, Icon::Check, tr(Str::ZoneEnabled), onOff(zc.enabled),
                               zc.enabled ? Tone::Normal : Tone::Muted));
        if (zs.fault != FaultCode::None || zs.lockedOut) {
            rows.push_back(itemRow(kClear, Icon::Lock, tr(Str::ZoneClear)));
        }
        rows.push_back(infoRow(tr(Str::ZoneLastCurrent), currentText(c.lastCurrentMa[zone_])));
        const uint16_t learned = c.ports.diag.learnedCurrentMa(zone_);
        rows.push_back(infoRow(tr(Str::ZoneLearned), currentText(learned == 0 ? -1 : learned)));
        rows.push_back(infoRow(tr(Str::ZoneMaxRun), durationText(zc.maxRunSec)));
        Row hint = infoRow(tr(Str::ZoneRenameHint));
        hint.tone = Tone::Muted;
        rows.push_back(hint);
    }

    void activate(Ctx &c, int id) override
    {
        switch (id) {
        case kFault:
            c.ui.push(makeFaultScreen(FaultItem{c.state.zones[zone_].fault, zone_}));
            break;
        case kEnabled: {
            const ZoneIndex z = zone_;
            c.ui.updateConfig([z](Config &cfg) { cfg.zones[z].enabled = !cfg.zones[z].enabled; });
            break;
        }
        case kTest: {
            auto picker = std::make_unique<NumberPicker>(kTestMinutes, 1, 10);
            picker->caption = fmt(Str::PickRunFor, zoneName(c.config, zone_).c_str());
            picker->unit = tr(Str::UnitMin);
            c.ui.openPicker(kTest, std::move(picker));
            break;
        }
        case kStop:
            c.ui.stopAll();
            break;
        case kClear:
            // The core clears every latched fault at once; there is no
            // per-zone clear.
            if (c.ui.post(Command::simple(CommandType::ClearFaults))) {
                c.ui.toast(tr(Str::ToastFaultsCleared), Tone::Ok, Icon::Check);
            }
            break;
        default:
            break;
        }
    }

    void picked(Ctx &c, int id, int64_t value) override
    {
        if (id == kTest && value >= 1) {
            c.ui.post(Command::runZone(zone_, static_cast<uint32_t>(value) * 60));
        }
    }

private:
    static constexpr int kTestMinutes = 2;
    ZoneIndex zone_;
};

} // namespace

std::unique_ptr<Screen> makeHomeScreen() { return std::make_unique<HomeScreen>(); }
std::unique_ptr<Screen> makeMenuScreen() { return std::make_unique<MenuScreen>(); }
std::unique_ptr<Screen> makeManualScreen() { return std::make_unique<ManualScreen>(); }
std::unique_ptr<Screen> makeZonesScreen() { return std::make_unique<ZonesScreen>(); }
std::unique_ptr<Screen> makeZoneScreen(ZoneIndex zone) { return std::make_unique<ZoneScreen>(zone); }

} // namespace zf::ui
