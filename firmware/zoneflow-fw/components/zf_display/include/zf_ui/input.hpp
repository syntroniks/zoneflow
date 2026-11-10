// SPDX-License-Identifier: Apache-2.0
// Input decoding for the local controls: a rotary encoder with push switch
// and two buttons (Back, Manual/Home). Pure logic; the device feeds it raw
// samples (PCNT count, GPIO levels), the simulator feeds it key presses.
#pragma once

#include <cstdint>

#include "zf/types.hpp"

namespace zf::ui {

enum class Key : uint8_t { Select, Back, Home };

enum class InputType : uint8_t {
    Turn,      // steps = detents, signed; accelSteps = the same after acceleration
    Press,     // a click
    LongPress, // held past the long-press time; sent once
    Repeat,    // held: sent at the repeat period
};

struct InputEvent {
    InputType type = InputType::Press;
    Key key = Key::Select;
    int16_t steps = 0;
    int16_t accelSteps = 0;

    static InputEvent turn(int steps, int accelSteps)
    {
        InputEvent e;
        e.type = InputType::Turn;
        e.steps = static_cast<int16_t>(steps);
        e.accelSteps = static_cast<int16_t>(accelSteps);
        return e;
    }
    static InputEvent press(Key k, InputType t = InputType::Press)
    {
        InputEvent e;
        e.type = t;
        e.key = k;
        return e;
    }
};

// ---- buttons -------------------------------------------------------------------

struct ButtonTiming {
    uint16_t debounceMs = 20;
    // 0: no long press; Press is sent when the button goes down.
    // Otherwise Press is sent on release, and only if LongPress was not sent.
    uint16_t longPressMs = 0;
    // 0: no repeat. Otherwise Repeat is sent every repeatMs once the button
    // has been down for repeatDelayMs.
    uint16_t repeatDelayMs = 600;
    uint16_t repeatMs = 0;
};

enum class ButtonEdge : uint8_t { None, Press, LongPress, Repeat };

class ButtonDecoder {
public:
    explicit ButtonDecoder(const ButtonTiming &timing = {}) : timing_(timing) {}

    // Call at a steady rate (10 ms or faster). Returns at most one edge.
    ButtonEdge update(bool pressedRaw, MonoMs now);
    bool down() const { return down_; }

private:
    ButtonTiming timing_;
    bool primed_ = false;
    bool ignoreUntilRelease_ = false; // held at power-up: not a press
    bool down_ = false;
    bool candidate_ = false;
    MonoMs candidateSinceMs_ = 0;
    MonoMs downSinceMs_ = 0;
    MonoMs lastRepeatMs_ = 0;
    bool longSent_ = false;
    bool repeating_ = false;
};

// ---- encoder ---------------------------------------------------------------------

// Turns a free-running quadrature count (4 counts per cycle, as PCNT gives
// with both channels) into detents. A count lost to contact bounce would
// leave the count off the detent grid for good, so a remainder that stays
// unchanged for kSettleMs is dropped.
class EncoderDecoder {
public:
    static constexpr uint32_t kSettleMs = 250;

    explicit EncoderDecoder(int countsPerDetent = 4) : countsPerDetent_(countsPerDetent) {}

    // Returns the detents turned since the last call (clockwise positive).
    int update(int32_t rawCount, MonoMs now);

private:
    int countsPerDetent_;
    bool primed_ = false;
    int32_t anchor_ = 0; // raw count at the last detent
    int32_t lastRaw_ = 0;
    MonoMs lastChangeMs_ = 0;
};

// Speed-dependent step multiplier for value pickers. Slow turning gives one
// step per detent; a quick spin covers a long range. Lists do not use it.
class Accelerator {
public:
    // Detents per second at which the multiplier rises to 2, 5 and 10.
    static constexpr int kRate2 = 14;
    static constexpr int kRate5 = 28;
    static constexpr int kRate10 = 50;
    // A pause longer than this, or a change of direction, resets the speed.
    static constexpr uint32_t kResetMs = 300;

    int apply(int steps, MonoMs now);
    int multiplier() const { return multiplier_; }

private:
    bool primed_ = false;
    int direction_ = 0;
    MonoMs lastMs_ = 0;
    int rate_ = 0; // smoothed detents per second
    int multiplier_ = 1;
};

} // namespace zf::ui
