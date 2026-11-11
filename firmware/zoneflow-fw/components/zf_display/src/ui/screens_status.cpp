// SPDX-License-Identifier: Apache-2.0
// Sensors and flow, faults.
#include "screen_util.hpp"
#include "zf_ui/faults.hpp"
#include "zf_ui/screens.hpp"

namespace zf::ui {
namespace {

class SensorsScreen final : public Screen {
public:
    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::MenuSensors);
        const SensorStatus &s = c.state.sensors;
        auto &rows = out.list.rows;
        auto add = [&rows](Icon icon, Str label, std::string value, Tone tone) {
            rows.push_back(infoRow(tr(label), std::move(value), tone, icon));
        };
        if (!s.rainEnabled) {
            add(Icon::Rain, Str::SensRain, tr(Str::NotUsed), Tone::Muted);
        } else {
            add(Icon::Rain, Str::SensRain, tr(s.rainActive ? Str::Wet : Str::Dry),
                s.rainActive ? Tone::Warn : Tone::Normal);
        }
        add(Icon::Bolt, Str::SensAc, tr(s.acPresent ? Str::Present : Str::Missing),
            s.acPresent ? Tone::Normal : Tone::Fault);
        add(Icon::Gauge, Str::SensCurrent, currentText(s.valveCurrentMa), Tone::Normal);
        if (!s.flowEnabled) {
            add(Icon::Flow, Str::SensFlow, tr(Str::NotUsed), Tone::Muted);
        } else {
            add(Icon::Flow, Str::SensFlow, flowText(s.flowLpm), s.flowLpm > 0.05f ? Tone::Running : Tone::Normal);
            add(Icon::Drop, Str::SensTotal, litresText(s.flowTotalLitres), Tone::Normal);
        }
        if (c.state.hardware.phase != StagePhase::None) {
            const StagePhase p = c.state.hardware.phase;
            const Str text = p == StagePhase::Ready   ? Str::BoardReady
                             : p == StagePhase::Idle  ? Str::BoardIdle
                             : p == StagePhase::Fault ? Str::BoardFault
                             : p == StagePhase::NoAc  ? Str::BoardNoAc
                                                      : Str::BoardTesting;
            add(Icon::Bolt, Str::SensBoard, tr(text), p == StagePhase::Fault ? Tone::Fault : Tone::Normal);
        }
        if (!c.state.masterEnabled) {
            add(Icon::Sprinkler, Str::SensMaster, tr(Str::NotUsed), Tone::Muted);
        } else {
            add(Icon::Sprinkler, Str::SensMaster, tr(c.state.masterOn ? Str::Open : Str::Closed),
                c.state.masterOn ? Tone::Running : Tone::Normal);
        }
        if (!s.spareEnabled) {
            add(Icon::Info, Str::SensSpare, tr(Str::NotUsed), Tone::Muted);
        } else {
            const bool holds = c.config.spareFunction == SpareFunction::InhibitLikeRain;
            add(Icon::Info, Str::SensSpare, tr(s.spareActive ? Str::Active : Str::Inactive),
                s.spareActive && holds ? Tone::Warn : Tone::Normal);
            // Set in the web interface (Settings, Sensors) or the API.
            add(Icon::Info, Str::SensSpareUse, tr(holds ? Str::SpareHolds : Str::SpareShown), Tone::Muted);
        }
    }
};

class FaultsScreen final : public Screen {
public:
    enum : int { kClearAll = 1, kFaultBase = 100 };

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = tr(Str::MenuFaults);
        items_ = listFaults(c.state);
        auto &rows = out.list.rows;
        if (items_.empty()) {
            Row r = infoRow(tr(Str::FaultsNone), {}, Tone::Normal, Icon::Check);
            r.tone = Tone::Ok;
            rows.push_back(r);
            return;
        }
        bool latched = false;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            Row r = itemRow(kFaultBase + static_cast<int>(i), Icon::Fault, faultTitle(items_[i]));
            r.tone = faultTone(items_[i].code);
            r.valueIcon = Icon::ChevronRight;
            rows.push_back(r);
            latched = latched || !faultClearsItself(items_[i].code);
        }
        if (latched) {
            rows.push_back(itemRow(kClearAll, Icon::Check, tr(Str::FaultsClearAll)));
        }
    }

    void activate(Ctx &c, int id) override
    {
        if (id == kClearAll) {
            if (c.ui.post(Command::simple(CommandType::ClearFaults))) {
                c.ui.toast(tr(Str::ToastFaultsCleared), Tone::Ok, Icon::Check);
            }
            return;
        }
        const std::size_t index = static_cast<std::size_t>(id - kFaultBase);
        if (id >= kFaultBase && index < items_.size()) {
            c.ui.push(makeFaultScreen(items_[index]));
        }
    }

private:
    std::vector<FaultItem> items_;
};

// One fault: what happened and what to check. A keep-alive detector fault
// is not cleared with one press: "Repaired" asks again before it posts
// AcknowledgeRepair (hardware design 19.7, the fault state).
class FaultScreen final : public Screen {
public:
    enum : int { kBack = 1, kClear, kRepaired, kAskRepaired = 1 };

    explicit FaultScreen(const FaultItem &item) : item_(item) {}
    FrameKind kind() const override { return FrameKind::Detail; }

    void build(const Ctx &c, Frame &out) override
    {
        out.bar.title = item_.zone == kNoZone ? std::string(tr(Str::MenuFaults)) : zoneName(c.config, item_.zone);
        DetailModel &d = out.detail;
        d.icon = Icon::Fault;
        d.tone = faultTone(item_.code);
        d.headline = faultTitle(item_);
        d.body = tr(faultBody(item_.code));
        const std::string step = faultStepText(item_);
        if (!step.empty()) {
            d.body += "\n";
            d.body += step;
        }
        d.buttons.push_back(button(kBack, Str::Ok));
        if (item_.repairNeeded) {
            d.buttons.push_back(button(kRepaired, Str::FaultRepaired, Icon::Check));
        } else if (!faultClearsItself(item_.code)) {
            d.buttons.push_back(button(kClear, Str::FaultClear, Icon::Check));
        } else {
            d.body += "\n";
            d.body += tr(Str::FaultClearsItself);
            d.body += ".";
        }
    }

    void activate(Ctx &c, int id) override
    {
        if (id == kRepaired) {
            DetailModel q;
            q.icon = Icon::Fault;
            q.tone = Tone::Fault;
            q.headline = tr(Str::FaultRepairedAsk);
            q.body = tr(Str::FaultRepairedAskBody);
            // Cancel first: a stray press does not confirm a repair.
            q.buttons = {button(kChoiceNo, Str::Cancel), button(kChoiceYes, Str::FaultRepairedYes, Icon::Check)};
            c.ui.confirm(kAskRepaired, q);
            return;
        }
        if (id == kClear && c.ui.post(Command::simple(CommandType::ClearFaults))) {
            c.ui.toast(tr(Str::ToastFaultsCleared), Tone::Ok, Icon::Check);
        }
        c.ui.pop();
    }

    void confirmed(Ctx &c, int request, int choice) override
    {
        if (request == kAskRepaired && choice == kChoiceYes && c.ui.post(Command::acknowledgeRepair())) {
            c.ui.toast(tr(Str::ToastRepairConfirmed), Tone::Ok, Icon::Check);
            c.ui.pop();
        }
    }

private:
    FaultItem item_;
};

} // namespace

std::unique_ptr<Screen> makeSensorsScreen() { return std::make_unique<SensorsScreen>(); }
std::unique_ptr<Screen> makeFaultsScreen() { return std::make_unique<FaultsScreen>(); }
std::unique_ptr<Screen> makeFaultScreen(const FaultItem &item) { return std::make_unique<FaultScreen>(item); }

} // namespace zf::ui
