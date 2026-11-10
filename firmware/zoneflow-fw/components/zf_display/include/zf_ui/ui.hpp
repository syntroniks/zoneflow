// SPDX-License-Identifier: Apache-2.0
// The display's controller: navigation stack, input routing, pickers,
// backlight state and toasts. Pure logic. It reads zf::AppServices, produces
// a Frame for the view, and never touches LVGL or hardware, so the host
// tests drive it with the same input events the device produces.
//
// Threading: every method runs on the UI task. Core events reach
// handleEvent() through the task's own queue, never from the EventBus
// handler directly.
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "zf/app/services.hpp"
#include "zf_display/ports.hpp"
#include "zf_ui/input.hpp"
#include "zf_ui/model.hpp"
#include "zf_ui/picker.hpp"
#include "zf_ui/screen.hpp"

namespace zf::ui {

enum class Power : uint8_t { Active, Dim, Off };

class Ui {
public:
    static constexpr uint32_t kStaleMs = 5000;
    static constexpr uint32_t kToastMs = 3000;
    // Idle on another screen for this long: back to Home.
    static constexpr uint32_t kHomeReturnMs = 90000;
    // A zone must have run alone this long before its current is recorded.
    static constexpr uint32_t kCurrentSettleMs = 3000;

    Ui(const AppServices &svc, const display::Ports &ports);
    ~Ui();

    void begin(MonoMs now);
    void handleInput(const InputEvent &e, MonoMs now);
    void handleEvent(const Event &e, MonoMs now);
    // Call at least every 100 ms.
    void tick(MonoMs now);

    const Frame &frame() const { return frame_; }
    // Changes whenever the frame was rebuilt.
    uint32_t frameSerial() const { return frameSerial_; }
    Power power() const { return power_; }
    // 0..100, already reduced when dimmed, 0 when off.
    uint8_t backlightPercent() const;

    // ---- for screens ----
    void push(std::unique_ptr<Screen> screen);
    void pop();
    void goHome();
    void openPicker(int id, std::unique_ptr<Picker> picker);
    // Shows a question; the screen below gets confirmed(request, button id).
    // The first button is focused: make it the safe answer.
    void confirm(int request, DetailModel question);
    void finishConfirm(int request, int choice);
    void toast(std::string text, Tone tone = Tone::Normal, Icon icon = Icon::None);
    // Posts a command; shows "Busy" and returns false if the queue is full.
    bool post(const Command &cmd);
    void stopAll();
    // Reads the configuration, applies `change`, validates and stores it,
    // then tells the core to reload. Shows the reason and returns false if
    // the core rejects it.
    bool updateConfig(const std::function<void(Config &)> &change);
    void saveSettings();
    void startWizard();
    void factoryReset();

    std::size_t depth() const { return stack_.size(); }
    bool pickerOpen() const { return picker_ != nullptr; }
    const SystemState &state() const { return state_; }
    const Config &config() const { return config_; }
    const display::UiSettings &settings() const { return settings_; }

private:
    Ctx ctx();
    Screen &top() { return *stack_.back(); }
    void refresh(MonoMs now);
    void trackCurrent(MonoMs now);
    void rebuild();
    void buildBar(StatusBarModel &bar);
    void resolveListFocus(Screen &screen, ListModel &list);
    static void resolveButtonFocus(Screen &screen, const std::vector<Button> &buttons, int &focus);
    void moveFocus(int steps);
    void activateFocused();
    void back(bool toHome);
    void closePicker(bool commit);
    void updatePower(MonoMs now);
    bool keepLit() const;
    void reap();

    AppServices svc_;
    display::Ports ports_;
    SystemState state_{};
    Config config_{};
    TimeZone tz_ = TimeZone::utc();
    display::UiSettings settings_{};
    uint32_t lastSequence_ = 0;
    uint32_t lastConfigRevision_ = 0;
    MonoMs lastSnapshotMs_ = 0;
    bool haveSnapshot_ = false;
    bool stale_ = false;
    std::array<int32_t, kMaxZones> lastCurrentMa_{};
    int soloZone_ = -1;
    MonoMs soloSinceMs_ = 0;
    uint32_t headEntryId_ = 0;
    uint32_t headRemainingSec_ = 0;

    std::vector<std::unique_ptr<Screen>> stack_;
    std::vector<std::unique_ptr<Screen>> graveyard_; // popped during a callback
    std::unique_ptr<Picker> picker_;
    int pickerId_ = 0;

    Frame frame_{};
    uint32_t frameSerial_ = 0;
    bool dirty_ = true;

    MonoMs now_ = 0;
    MonoMs lastInputMs_ = 0;
    Power power_ = Power::Active;
    bool toastOpen_ = false;
    ToastModel toast_{};
    MonoMs toastUntilMs_ = 0;
};

} // namespace zf::ui
