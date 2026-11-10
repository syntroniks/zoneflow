// SPDX-License-Identifier: Apache-2.0
#include "screen_util.hpp"

#include <vector>

#include "zf_ui/tz_table.hpp"

namespace zf::ui {
namespace {

constexpr int kRainHours[] = {0, 6, 12, 24, 48, 72, 120, 168, 240, 336};
constexpr int kRainChoices = static_cast<int>(sizeof kRainHours / sizeof kRainHours[0]);

} // namespace

std::string zoneName(const Config &config, ZoneIndex zone)
{
    if (zone < kMaxZones && !config.zones[zone].name.empty()) {
        return config.zones[zone].name.c_str();
    }
    return fmt(Str::FmtZone, static_cast<int>(zone) + 1);
}

const Program *findProgram(const Config &config, uint16_t id)
{
    for (std::size_t i = 0; i < config.schedule.programCount && i < kMaxPrograms; ++i) {
        if (config.schedule.programs[i].id == id) {
            return &config.schedule.programs[i];
        }
    }
    return nullptr;
}

bool runBlocked(const Ctx &c, Str &why)
{
    if (c.state.maintenance || (c.state.inhibitFlags & kInhibitMaintenance) != 0) {
        why = Str::ManualMaintenance;
        return true;
    }
    if (!c.state.sensors.acPresent || (c.state.inhibitFlags & kInhibitNoAc) != 0) {
        why = Str::ManualNoAc;
        return true;
    }
    if (c.state.masterLockedOut) {
        why = Str::ManualMasterLocked;
        return true;
    }
    return false;
}

std::unique_ptr<Picker> makeRainDelayPicker(const Ctx &c)
{
    std::vector<std::string> options;
    for (int h : kRainHours) {
        options.push_back(h == 0    ? std::string(tr(Str::Off))
                          : h < 24  ? fmt(Str::FmtHours, h)
                          : h == 24 ? std::string(tr(Str::OneDay))
                                    : fmt(Str::FmtDays, h / 24));
    }
    // Start on the running delay rounded up, or on one day.
    int index = 3;
    if (c.state.rainDelayUntil > c.state.utc) {
        const int64_t leftHours = (c.state.rainDelayUntil - c.state.utc + 3599) / 3600;
        index = kRainChoices - 1;
        for (int i = 1; i < kRainChoices; ++i) {
            if (kRainHours[i] >= leftHours) {
                index = i;
                break;
            }
        }
    }
    auto picker = std::make_unique<ChoicePicker>(std::move(options), index);
    picker->caption = tr(Str::RainCaption);
    return picker;
}

void applyRainDelay(Ctx &c, int64_t choice)
{
    if (choice < 0 || choice >= kRainChoices) {
        return;
    }
    const int hours = kRainHours[choice];
    if (hours == 0) {
        if (c.ui.post(Command::rainDelay(0))) {
            c.ui.toast(tr(Str::ToastRainOff), Tone::Normal, Icon::Rain);
        }
        return;
    }
    if (c.state.timeStatus == TimeStatus::Unset) {
        c.ui.toast(tr(Str::RainNeedsClock), Tone::Warn, Icon::Clock);
        return;
    }
    const UtcSeconds until = c.state.utc + static_cast<UtcSeconds>(hours) * 3600;
    if (c.ui.post(Command::rainDelay(until))) {
        c.ui.toast(fmt(Str::ToastRainSet, whenText(until, c.state.utc, c.tz).c_str()), Tone::Warn, Icon::Rain);
    }
}

int buildTimeZoneRows(const Ctx &c, ListModel &list)
{
    const int current = tzFind(c.config.timezone.view());
    if (current < 0) {
        // Set from the web interface to something outside the table.
        Row r = infoRow(tr(Str::TimeCustomZone), c.config.timezone.c_str(), Tone::Muted, Icon::Check);
        list.rows.push_back(r);
    }
    for (std::size_t i = 0; i < tzCount(); ++i) {
        const TzEntry &e = tzEntry(i);
        Row r = itemRow(static_cast<int>(i), Icon::None, e.cities, tzOffsetText(e.posix));
        if (static_cast<int>(i) == current) {
            r.icon = Icon::Check;
            r.valueTone = Tone::Normal;
        }
        list.rows.push_back(r);
    }
    return current < 0 ? tzFind("UTC0") : current;
}

bool applyTimeZone(Ctx &c, int id)
{
    if (id < 0 || static_cast<std::size_t>(id) >= tzCount()) {
        return false;
    }
    const char *posix = tzEntry(static_cast<std::size_t>(id)).posix;
    return c.ui.updateConfig([posix](Config &cfg) { cfg.timezone.assign(posix); });
}

} // namespace zf::ui
