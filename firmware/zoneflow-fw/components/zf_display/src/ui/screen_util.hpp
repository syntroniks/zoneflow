// SPDX-License-Identifier: Apache-2.0
// Helpers shared by the screen implementations. Private to src/ui.
#pragma once

#include <memory>
#include <string>

#include "zf_ui/format.hpp"
#include "zf_ui/model.hpp"
#include "zf_ui/picker.hpp"
#include "zf_ui/screen.hpp"
#include "zf_ui/strings.hpp"
#include "zf_ui/ui.hpp"

namespace zf::ui {

inline Row itemRow(int id, Icon icon, std::string label, std::string value = {}, Tone valueTone = Tone::Muted)
{
    Row r;
    r.id = id;
    r.kind = RowKind::Item;
    r.icon = icon;
    r.label = std::move(label);
    r.value = std::move(value);
    r.valueTone = valueTone;
    return r;
}

inline Row headerRow(Str text)
{
    Row r;
    r.kind = RowKind::Header;
    r.label = tr(text);
    r.tone = Tone::Muted;
    return r;
}

inline Row infoRow(std::string label, std::string value = {}, Tone valueTone = Tone::Normal, Icon icon = Icon::None)
{
    Row r;
    r.kind = RowKind::Info;
    r.icon = icon;
    r.label = std::move(label);
    r.value = std::move(value);
    r.valueTone = valueTone;
    return r;
}

inline Button button(int id, Str label, Icon icon = Icon::None, Tone tone = Tone::Normal)
{
    Button b;
    b.id = id;
    b.label = tr(label);
    b.icon = icon;
    b.tone = tone;
    return b;
}

inline const char *onOff(bool on) { return tr(on ? Str::On : Str::Off); }

// The configured name; "Zone 3" if it is empty.
std::string zoneName(const Config &config, ZoneIndex zone);
// Number of zones this controller uses, never above kMaxZones.
inline std::size_t zoneCount(const Ctx &c) { return c.state.zoneCount < kMaxZones ? c.state.zoneCount : kMaxZones; }
const Program *findProgram(const Config &config, uint16_t id);

// True if the core will refuse to open a valve right now; `why` gets the
// reason for the user.
bool runBlocked(const Ctx &c, Str &why);

// The rain delay picker and its result, shared by Home and the menu.
std::unique_ptr<Picker> makeRainDelayPicker(const Ctx &c);
void applyRainDelay(Ctx &c, int64_t choice);

// Rows of the time zone list, shared by Setup and the wizard. Row id = index
// into the table. Returns the row to focus first.
int buildTimeZoneRows(const Ctx &c, ListModel &list);
bool applyTimeZone(Ctx &c, int id);

} // namespace zf::ui
