// SPDX-License-Identifier: Apache-2.0
// A screen is one level of the navigation stack. It fills the Frame from the
// core's state every time something changes and reacts to the row or button
// the user pressed. Screens hold no copy of zone or queue state.
#pragma once

#include <array>
#include <cstdint>

#include "zf/app/services.hpp"
#include "zf/time/time_zone.hpp"
#include "zf_display/ports.hpp"
#include "zf_ui/model.hpp"

namespace zf::ui {

class Ui;

struct Ctx {
    Ui &ui;
    const AppServices &svc;
    const display::Ports &ports;
    const SystemState &state; // latest snapshot from the core
    const Config &config;
    const TimeZone &tz;
    display::UiSettings &settings;
    MonoMs now;
    // No snapshot for Ui::kStaleMs: the control task is not publishing, so
    // valve state is unknown and the display must not claim "off".
    bool stale;
    // Total valve current last seen while a zone ran alone, -1 if never.
    const std::array<int32_t, kMaxZones> &lastCurrentMa;
    // Time left that the core last reported for the head of the queue, and
    // the queue entry it belongs to. The core does not publish the time
    // left while a run is paused; it keeps it unchanged, so this is it.
    uint32_t headEntryId;
    uint32_t headRemainingSec;
};

class Screen {
public:
    virtual ~Screen() = default;

    virtual FrameKind kind() const { return FrameKind::List; }
    virtual bool isHome() const { return false; }
    // Unsaved edits or the setup wizard: no automatic return to Home.
    virtual bool holdsScreen() const { return false; }

    // Called once, when the screen is pushed.
    virtual void entered(Ctx & /*ctx*/) {}
    // Fills bar.title and the model for kind(). Focus and scrolling are
    // resolved by Ui from focusId afterwards.
    virtual void build(const Ctx &ctx, Frame &out) = 0;
    // The focused row or button was pressed.
    virtual void activate(Ctx & /*ctx*/, int /*id*/) {}
    virtual void picked(Ctx & /*ctx*/, int /*id*/, int64_t /*value*/) {}
    virtual void pickerChanged(Ctx & /*ctx*/, int /*id*/, int64_t /*value*/) {}
    virtual void pickerCancelled(Ctx & /*ctx*/, int /*id*/) {}
    // Answer to Ui::confirm(): the id of the chosen button.
    virtual void confirmed(Ctx & /*ctx*/, int /*request*/, int /*choice*/) {}
    // Back or Home was pressed. Return false to stay (after asking, or
    // because the screen handled the key itself).
    virtual bool mayLeave(Ctx & /*ctx*/, bool /*toHome*/) { return true; }

    int focusId = -1;    // id of the focused row or button; -1 = default
    int focusIndex = -1; // where it was, to stay close when rows change
    int top = 0;         // first visible list row
};

// Button ids used by Ui::confirm() questions.
inline constexpr int kChoiceNo = 0;
inline constexpr int kChoiceYes = 1;
inline constexpr int kChoiceOther = 2;

} // namespace zf::ui
