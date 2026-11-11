// SPDX-License-Identifier: Apache-2.0
#include "zf_ui/input.hpp"

#include <cstdlib>

namespace zf::ui {

ButtonEdge ButtonDecoder::update(bool pressedRaw, MonoMs now)
{
    if (!primed_) {
        primed_ = true;
        candidate_ = pressedRaw;
        candidateSinceMs_ = now;
        ignoreUntilRelease_ = pressedRaw;
        return ButtonEdge::None;
    }
    if (pressedRaw != candidate_) {
        candidate_ = pressedRaw;
        candidateSinceMs_ = now;
    }
    const bool stable = now - candidateSinceMs_ >= timing_.debounceMs;

    if (ignoreUntilRelease_) {
        if (stable && !candidate_) {
            ignoreUntilRelease_ = false;
        }
        return ButtonEdge::None;
    }

    if (stable && candidate_ != down_) {
        down_ = candidate_;
        if (down_) {
            downSinceMs_ = now;
            longSent_ = false;
            holdSent_ = false;
            repeating_ = false;
            return timing_.longPressMs == 0 ? ButtonEdge::Press : ButtonEdge::None;
        }
        // Released.
        return (timing_.longPressMs != 0 && !longSent_) ? ButtonEdge::Press : ButtonEdge::None;
    }

    if (!down_) {
        return ButtonEdge::None;
    }
    const MonoMs held = now - downSinceMs_;
    if (timing_.holdMs != 0 && !holdSent_ && held >= timing_.holdMs) {
        holdSent_ = true;
        return ButtonEdge::Hold;
    }
    if (timing_.longPressMs != 0 && !longSent_ && held >= timing_.longPressMs) {
        longSent_ = true;
        return ButtonEdge::LongPress;
    }
    if (timing_.repeatMs != 0 && held >= timing_.repeatDelayMs) {
        if (!repeating_ || now - lastRepeatMs_ >= timing_.repeatMs) {
            repeating_ = true;
            lastRepeatMs_ = now;
            return ButtonEdge::Repeat;
        }
    }
    return ButtonEdge::None;
}

int EncoderDecoder::update(int32_t rawCount, MonoMs now)
{
    if (!primed_) {
        primed_ = true;
        anchor_ = rawCount;
        lastRaw_ = rawCount;
        lastChangeMs_ = now;
        return 0;
    }
    if (rawCount != lastRaw_) {
        lastRaw_ = rawCount;
        lastChangeMs_ = now;
    }
    // Unsigned subtraction: correct across a wrap of the 32-bit count.
    const auto diff = static_cast<int32_t>(static_cast<uint32_t>(rawCount) - static_cast<uint32_t>(anchor_));
    const int steps = diff / countsPerDetent_; // truncates towards zero
    if (steps != 0) {
        anchor_ =
            static_cast<int32_t>(static_cast<uint32_t>(anchor_) + static_cast<uint32_t>(steps * countsPerDetent_));
        return steps;
    }
    if (diff != 0 && now - lastChangeMs_ >= kSettleMs) {
        anchor_ = rawCount; // at rest between detents: a count was lost
    }
    return 0;
}

int Accelerator::apply(int steps, MonoMs now)
{
    if (steps == 0) {
        return 0;
    }
    const int direction = steps > 0 ? 1 : -1;
    const MonoMs dt = primed_ ? now - lastMs_ : kResetMs + 1;
    if (dt > kResetMs || direction != direction_) {
        rate_ = 0;
    } else {
        const int instant = static_cast<int>(static_cast<MonoMs>(std::abs(steps)) * 1000 / (dt == 0 ? 1 : dt));
        rate_ = (rate_ + instant) / 2;
    }
    primed_ = true;
    direction_ = direction;
    lastMs_ = now;

    multiplier_ = rate_ >= kRate10 ? 10 : rate_ >= kRate5 ? 5 : rate_ >= kRate2 ? 2 : 1;
    return steps * multiplier_;
}

} // namespace zf::ui
