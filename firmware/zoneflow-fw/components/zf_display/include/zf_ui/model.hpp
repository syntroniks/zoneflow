// SPDX-License-Identifier: Apache-2.0
// What the view draws. The UI logic (ui.hpp, screens) fills a Frame from the
// core's state; the view (zf_view) turns it into LVGL objects. Nothing here
// knows about LVGL, so the host tests check the same data the screen shows.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zf::ui {

// Colour carries meaning, and never alone: every tone also has an icon or a
// word next to it (see docs/design/display-ui.md, visual language).
enum class Tone : uint8_t {
    Normal,
    Muted,   // secondary text
    Running, // a valve is open
    Warn,    // held back: rain delay, sensor, paused, needs attention
    Fault,
    Ok,
};

enum class Icon : uint8_t {
    None,
    Drop,
    Clock,
    Calendar,
    Rain,
    Wifi,
    WifiOff,
    Fault,
    Sprinkler,
    Gauge,
    Sliders,
    Play,
    Stop,
    Pause,
    Skip,
    Menu,
    Check,
    Lock,
    Sun,
    Info,
    Bolt,
    Flow,
    ChevronRight,
    ChevronLeft,
    Knob,
    Trash,
    Plus,
    Count,
};

struct Button {
    int id = 0;
    Icon icon = Icon::None;
    std::string label;
    Tone tone = Tone::Normal;
};

struct InfoLine {
    Icon icon = Icon::None;
    Tone tone = Tone::Normal;
    std::string text;
    bool banner = false; // filled background: faults
};

struct StatusBarModel {
    bool home = false; // Home: clock and date on the left
    bool back = false; // show the back chevron before the title
    std::string title; // other screens
    std::string step;  // setup wizard: "2/5"
    std::string clock; // "14:32", or "--:--" while the clock is not set
    std::string date;  // "Tue 10 Nov"
    std::string run;   // other screens, while a valve is open: "3  12:34"
    Tone runTone = Tone::Running;
    Icon wifi = Icon::None;
    Tone wifiTone = Tone::Normal;
    bool rainHold = false; // rain delay or sensor is holding the schedule
    bool fault = false;
};

struct HomeModel {
    std::string chip; // "WATERING", "PAUSED", "ALL OFF"
    Tone chipTone = Tone::Muted;
    Icon chipIcon = Icon::None;
    std::string chipNote; // program name or "Manual"
    std::string badge;    // zone number; empty when idle
    std::string title;    // zone name, "Next run", or a fault
    Tone titleTone = Tone::Normal;
    Icon titleIcon = Icon::None; // drawn where the badge would be
    std::string big;             // large numerals: time left, or next start time
    std::string bigNote;         // "left", "Tomorrow"
    int progressPermille = -1;   // -1: no bar
    InfoLine lines[2];
    std::vector<Button> buttons;
    int focus = 0; // index into buttons
};

enum class RowKind : uint8_t {
    Item,   // can take focus
    Header, // section label
    Info,   // read-only line
};

struct Row {
    int id = -1;
    RowKind kind = RowKind::Item;
    Icon icon = Icon::None;
    std::string badge; // zone number, drawn in place of the icon
    std::string label;
    Tone tone = Tone::Normal; // label and icon
    std::string value;
    Tone valueTone = Tone::Muted;
    Icon valueIcon = Icon::None;
    bool disabled = false; // shown, cannot take focus
};

struct ListModel {
    static constexpr int kVisibleRows = 5;
    std::vector<Row> rows;
    int focus = -1; // index into rows; -1: nothing can take focus
    int top = 0;    // first visible row
};

struct DetailModel {
    Icon icon = Icon::None;
    Tone tone = Tone::Normal;
    std::string headline;
    std::string body;
    std::string qr; // when set, a QR code is drawn beside the text
    std::vector<Button> buttons;
    int focus = 0;
};

struct PickerSegment {
    std::string text;
    bool cursor = false; // the part the knob changes
    bool on = true;      // weekday cells: selected or not
    bool fixed = false;  // separator, never takes the cursor
};

struct PickerModel {
    std::string caption;
    std::vector<PickerSegment> segments;
    std::string unit;
    std::string hint;
    bool cells = false; // draw segments as toggle cells (weekdays)
};

struct ToastModel {
    Icon icon = Icon::None;
    Tone tone = Tone::Normal;
    std::string text;
};

enum class FrameKind : uint8_t { Home, List, Detail };

struct Frame {
    FrameKind kind = FrameKind::Home;
    StatusBarModel bar;
    HomeModel home;
    ListModel list;
    DetailModel detail;
    bool pickerOpen = false;
    PickerModel picker;
    bool toastOpen = false;
    ToastModel toast;
};

} // namespace zf::ui
