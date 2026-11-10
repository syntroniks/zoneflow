// SPDX-License-Identifier: Apache-2.0
// Program list and the program editor.
//
// The editor works on a copy (ProgramEdit). Every value is one press, one
// turn and one press. Nothing reaches the core until the user leaves the
// screen and answers "Save".
#include <algorithm>

#include "screen_util.hpp"
#include "zf_ui/next_run.hpp"
#include "zf_ui/screens.hpp"

namespace zf::ui {
namespace {

// ---- Programs -------------------------------------------------------------------------

class ProgramsScreen final : public Screen {
public:
    enum : int { kSeasonal = 1, kAdd = 2, kProgramBase = 100 };

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::MenuPrograms);
        auto &rows = out.list.rows;
        const ScheduleConfig &s = c.config.schedule;
        for (std::size_t i = 0; i < s.programCount; ++i) {
            const Program &p = s.programs[i];
            Row r = itemRow(kProgramBase + static_cast<int>(i), Icon::Calendar, p.name.c_str());
            if (!p.enabled) {
                r.value = tr(Str::Off);
            } else if (c.state.timeStatus == TimeStatus::Unset) {
                r.value = tr(Str::TimeUnset);
                r.valueTone = Tone::Warn;
            } else {
                const NextRun n = nextStart(p, c.tz, c.state.utc, c.state.rainDelayUntil);
                bool anyZone = false;
                for (std::size_t z = 0; z < c.config.zoneCount && z < kMaxZones; ++z) {
                    anyZone = anyZone || p.durationSec[z] > 0;
                }
                if (n.valid && anyZone) {
                    r.value = whenShortText(n.at, c.state.utc, c.tz);
                    r.valueTone = Tone::Normal;
                } else {
                    r.value = tr(Str::ProgNextNone);
                    r.valueTone = Tone::Warn;
                }
            }
            rows.push_back(r);
        }
        Row add = itemRow(kAdd, Icon::Plus, tr(Str::ProgAdd));
        if (s.programCount >= kMaxPrograms) {
            add.disabled = true;
            add.value = tr(Str::ProgFull);
        }
        rows.push_back(add);
        rows.push_back(itemRow(kSeasonal, Icon::Sun, tr(Str::ProgSeasonal), percentText(s.seasonalPercent),
                               s.seasonalPercent == 100 ? Tone::Muted : Tone::Normal));
    }

    void activate(Ctx &c, int id) override
    {
        if (id == kSeasonal) {
            auto picker = std::make_unique<NumberPicker>(c.config.schedule.seasonalPercent, 0, 250, 5);
            picker->caption = tr(Str::ProgSeasonal);
            picker->unit = tr(Str::UnitPercent);
            c.ui.openPicker(kSeasonal, std::move(picker));
            return;
        }
        if (id == kAdd) {
            ProgramEdit edit(Program{});
            if (ProgramEdit::makeNew(c.config.schedule, edit)) {
                c.ui.push(makeProgramScreen(edit));
            } else {
                c.ui.toast(tr(Str::ProgFull), Tone::Warn, Icon::Fault);
            }
            return;
        }
        const std::size_t index = static_cast<std::size_t>(id - kProgramBase);
        if (id >= kProgramBase && index < c.config.schedule.programCount) {
            c.ui.push(makeProgramScreen(ProgramEdit(c.config.schedule.programs[index])));
        }
    }

    void picked(Ctx &c, int id, int64_t value) override
    {
        if (id == kSeasonal) {
            const auto percent = static_cast<uint16_t>(std::clamp<int64_t>(value, 0, 250));
            if (c.ui.updateConfig([percent](Config &cfg) { cfg.schedule.seasonalPercent = percent; })) {
                c.ui.toast(tr(Str::ToastSaved), Tone::Ok, Icon::Check);
            }
        }
    }
};

// ---- Program editor ---------------------------------------------------------------------

class ProgramScreen final : public Screen {
public:
    enum : int {
        kEnabled = 1,
        kRunNow,
        kAddStart,
        kRule,
        kWeekdays,
        kInterval,
        kSeasonal,
        kObey,
        kDelete,
        kStartBase = 20, // + index
        kZoneBase = 40,  // + zone
    };
    enum : int { kAskSave = 1, kAskDelete, kAskRun };

    explicit ProgramScreen(const ProgramEdit &edit) : edit_(edit) {}

    bool holdsScreen() const override { return edit_.dirty() && !leaving_; }

    void build(const Ctx &c, Frame &out) override
    {
        const Program &p = edit_.program();
        out.bar.title = p.name.c_str();
        auto &rows = out.list.rows;

        const Str problem = edit_.problem(c.config.zoneCount);
        if (problem != Str::Count) {
            Row r = infoRow(tr(problem), {}, Tone::Normal, Icon::Fault);
            r.tone = Tone::Warn;
            rows.push_back(r);
        }
        rows.push_back(itemRow(kEnabled, Icon::Check, tr(Str::ProgEnabled), onOff(p.enabled),
                               p.enabled ? Tone::Normal : Tone::Muted));
        if (!edit_.isNew()) {
            Row run = itemRow(kRunNow, Icon::Play, tr(Str::RunNow));
            Str why = Str::Count;
            // Run what is stored, not half-finished edits.
            run.disabled = edit_.dirty() || runBlocked(c, why);
            rows.push_back(run);
        }

        rows.push_back(headerRow(Str::HdrStartTimes));
        for (std::size_t i = 0; i < edit_.startCount(); ++i) {
            rows.push_back(itemRow(kStartBase + static_cast<int>(i), Icon::Clock,
                                   fmt(Str::ProgStartN, static_cast<int>(i) + 1), clockText(p.startMinutes[i]),
                                   Tone::Normal));
        }
        if (edit_.canAddStart()) {
            rows.push_back(itemRow(kAddStart, Icon::Plus, tr(Str::ProgAddStart)));
        }

        rows.push_back(headerRow(Str::HdrDays));
        rows.push_back(itemRow(kRule, Icon::Calendar, tr(Str::ProgRule), tr(ruleName(p.days.type)), Tone::Normal));
        if (p.days.type == DayRuleType::Weekdays) {
            const bool none = (p.days.weekdayMask & 0x7F) == 0;
            rows.push_back(itemRow(kWeekdays, Icon::None, tr(Str::ProgWeekdays), weekdaysText(p.days.weekdayMask),
                                   none ? Tone::Warn : Tone::Normal));
        } else if (p.days.type == DayRuleType::Interval) {
            rows.push_back(itemRow(kInterval, Icon::None, tr(Str::ProgEvery),
                                   p.days.intervalDays == 1 ? std::string(tr(Str::OneDay))
                                                            : fmt(Str::FmtDays, static_cast<int>(p.days.intervalDays)),
                                   Tone::Normal));
        }

        rows.push_back(headerRow(Str::HdrRunTimes));
        for (std::size_t z = 0; z < zoneCount(c); ++z) {
            const uint32_t sec = p.durationSec[z];
            Row r = itemRow(kZoneBase + static_cast<int>(z), Icon::None, zoneName(c.config, static_cast<ZoneIndex>(z)),
                            sec == 0 ? std::string(tr(Str::Off)) : durationText(sec),
                            sec == 0 ? Tone::Muted : Tone::Normal);
            r.badge = std::to_string(z + 1);
            rows.push_back(r);
        }

        rows.push_back(headerRow(Str::HdrOptions));
        rows.push_back(
            itemRow(kSeasonal, Icon::Sun, tr(Str::ProgSeasonal),
                    p.useSeasonalAdjust ? percentText(c.config.schedule.seasonalPercent) : std::string(tr(Str::Off)),
                    p.useSeasonalAdjust ? Tone::Normal : Tone::Muted));
        rows.push_back(itemRow(kObey, Icon::Rain, tr(Str::ProgObey),
                               tr(p.obeySensorInhibit ? Str::ProgObeyYes : Str::ProgObeyNo),
                               p.obeySensorInhibit ? Tone::Normal : Tone::Muted));
        if (!edit_.isNew()) {
            Row del = itemRow(kDelete, Icon::Trash, tr(Str::ProgDelete));
            del.tone = Tone::Fault;
            rows.push_back(del);
        }
    }

    void activate(Ctx &c, int id) override
    {
        const Program &p = edit_.program();
        if (id >= kZoneBase) {
            const auto zone = static_cast<ZoneIndex>(id - kZoneBase);
            if (zone >= zoneCount(c)) {
                return;
            }
            const int maxMin = static_cast<int>(std::max<uint32_t>(c.config.zones[zone].maxRunSec / 60, 1));
            auto picker = std::make_unique<NumberPicker>(edit_.zoneMinutes(zone), 0, maxMin);
            picker->caption = fmt(Str::PickRunTime, zoneName(c.config, zone).c_str());
            picker->unit = tr(Str::UnitMin);
            picker->minIsOff = true;
            c.ui.openPicker(id, std::move(picker));
            return;
        }
        if (id >= kStartBase) {
            const std::size_t index = static_cast<std::size_t>(id - kStartBase);
            if (index >= edit_.startCount()) {
                return;
            }
            auto picker = std::make_unique<TimePicker>(p.startMinutes[index], true);
            picker->caption = fmt(Str::PickStart, static_cast<int>(index) + 1);
            c.ui.openPicker(id, std::move(picker));
            return;
        }
        switch (id) {
        case kEnabled:
            edit_.setEnabled(!p.enabled);
            break;
        case kSeasonal:
            edit_.setSeasonal(!p.useSeasonalAdjust);
            break;
        case kObey:
            edit_.setObeySensor(!p.obeySensorInhibit);
            break;
        case kAddStart: {
            auto picker = std::make_unique<TimePicker>(edit_.suggestedStart(), false);
            picker->caption = fmt(Str::PickStart, static_cast<int>(edit_.startCount()) + 1);
            c.ui.openPicker(kAddStart, std::move(picker));
            break;
        }
        case kRule: {
            std::vector<std::string> options;
            for (DayRuleType t : kRules) {
                options.emplace_back(tr(ruleName(t)));
            }
            auto picker = std::make_unique<ChoicePicker>(std::move(options), static_cast<int>(p.days.type));
            picker->caption = tr(Str::ProgRule);
            c.ui.openPicker(kRule, std::move(picker));
            break;
        }
        case kWeekdays: {
            auto picker = std::make_unique<WeekdayPicker>(p.days.weekdayMask);
            picker->caption = tr(Str::ProgWeekdays);
            c.ui.openPicker(kWeekdays, std::move(picker));
            break;
        }
        case kInterval: {
            auto picker = std::make_unique<NumberPicker>(p.days.intervalDays, 1, 60);
            picker->caption = tr(Str::PickInterval);
            picker->unit = tr(Str::UnitDays);
            c.ui.openPicker(kInterval, std::move(picker));
            break;
        }
        case kRunNow: {
            DetailModel q;
            q.icon = Icon::Play;
            q.tone = Tone::Running;
            q.headline = fmt(Str::ConfirmRunProgram, p.name.c_str());
            q.buttons = {button(kChoiceYes, Str::RunNow, Icon::Play), button(kChoiceNo, Str::Cancel)};
            c.ui.confirm(kAskRun, q);
            break;
        }
        case kDelete: {
            DetailModel q;
            q.icon = Icon::Trash;
            q.tone = Tone::Fault;
            q.headline = fmt(Str::ConfirmDelete, p.name.c_str());
            q.body = tr(Str::ConfirmDeleteBody);
            // Cancel first: a stray press does not delete.
            q.buttons = {button(kChoiceNo, Str::Cancel), button(kChoiceYes, Str::Delete, Icon::Trash, Tone::Fault)};
            c.ui.confirm(kAskDelete, q);
            break;
        }
        default:
            break;
        }
    }

    void picked(Ctx &c, int id, int64_t value) override
    {
        const int64_t today = c.tz.localDay(c.state.utc);
        if (id >= kZoneBase) {
            const auto zone = static_cast<ZoneIndex>(id - kZoneBase);
            if (zone < kMaxZones) {
                edit_.setZoneMinutes(zone, static_cast<int>(value), c.config.zones[zone].maxRunSec);
            }
        } else if (id >= kStartBase) {
            edit_.setStart(static_cast<std::size_t>(id - kStartBase), static_cast<int>(value));
        } else if (id == kAddStart) {
            edit_.setStart(edit_.startCount(), static_cast<int>(value));
        } else if (id == kRule) {
            edit_.setRule(kRules[std::clamp<int64_t>(value, 0, 3)], today);
        } else if (id == kWeekdays) {
            edit_.setWeekdayMask(static_cast<uint8_t>(value));
        } else if (id == kInterval) {
            edit_.setIntervalDays(static_cast<int>(value), today);
        }
    }

    bool mayLeave(Ctx &c, bool toHome) override
    {
        if (leaving_ || !edit_.dirty()) {
            return true;
        }
        homeAfter_ = toHome;
        DetailModel q;
        q.icon = Icon::Calendar;
        q.headline = fmt(Str::ConfirmSave, edit_.program().name.c_str());
        q.body = tr(Str::ConfirmSaveBody);
        q.buttons = {button(kChoiceYes, Str::Save, Icon::Check), button(kChoiceOther, Str::Discard),
                     button(kChoiceNo, Str::Cancel)};
        c.ui.confirm(kAskSave, q);
        return false;
    }

    void confirmed(Ctx &c, int request, int choice) override
    {
        if (request == kAskRun) {
            if (choice == kChoiceYes && c.ui.post(Command::runProgram(edit_.program().id))) {
                c.ui.toast(fmt(Str::ToastProgramStarted, edit_.program().name.c_str()), Tone::Running, Icon::Drop);
                c.ui.goHome();
            }
            return;
        }
        if (request == kAskDelete) {
            if (choice != kChoiceYes) {
                return;
            }
            const uint16_t id = edit_.program().id;
            if (c.ui.updateConfig([id](Config &cfg) { ProgramEdit::removeFrom(cfg, id); })) {
                leave(c, false);
            }
            return;
        }
        if (request != kAskSave || choice == kChoiceNo) {
            return;
        }
        if (choice == kChoiceYes) {
            bool room = true;
            const ProgramEdit &edit = edit_;
            if (!c.ui.updateConfig([&edit, &room](Config &cfg) { room = edit.applyTo(cfg); })) {
                return; // rejected by the core: stay, the reason is on screen
            }
            if (!room) {
                c.ui.toast(tr(Str::ProgFull), Tone::Warn, Icon::Fault);
                return;
            }
            c.ui.toast(tr(Str::ToastSaved), Tone::Ok, Icon::Check);
        }
        leave(c, homeAfter_);
    }

private:
    static constexpr DayRuleType kRules[4] = {DayRuleType::Weekdays, DayRuleType::Interval, DayRuleType::OddDays,
                                              DayRuleType::EvenDays};

    static Str ruleName(DayRuleType t)
    {
        switch (t) {
        case DayRuleType::Weekdays:
            return Str::RuleWeekdays;
        case DayRuleType::Interval:
            return Str::RuleInterval;
        case DayRuleType::OddDays:
            return Str::RuleOdd;
        case DayRuleType::EvenDays:
            return Str::RuleEven;
        }
        return Str::RuleWeekdays;
    }

    void leave(Ctx &c, bool toHome)
    {
        leaving_ = true;
        if (toHome) {
            c.ui.goHome();
        } else {
            c.ui.pop();
        }
    }

    ProgramEdit edit_;
    bool leaving_ = false;
    bool homeAfter_ = false;
};

} // namespace

std::unique_ptr<Screen> makeProgramsScreen() { return std::make_unique<ProgramsScreen>(); }
std::unique_ptr<Screen> makeProgramScreen(const ProgramEdit &edit) { return std::make_unique<ProgramScreen>(edit); }

} // namespace zf::ui
