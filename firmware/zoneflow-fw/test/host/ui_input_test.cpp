// SPDX-License-Identifier: Apache-2.0
// Input decoding: button debounce, long press and repeat; encoder detents
// and acceleration.
#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "zf_board/board.hpp"
#include "zf_ui/input.hpp"

using namespace zf;
using namespace zf::ui;

namespace {

// Feeds a level for a duration at a 5 ms sample period; returns the edges.
struct ButtonRig {
    ButtonDecoder dec;
    MonoMs now = 0;
    std::vector<ButtonEdge> edges;

    explicit ButtonRig(const ButtonTiming &t) : dec(t) { hold(false, 50); }
    void hold(bool pressed, MonoMs ms)
    {
        for (MonoMs t = 0; t < ms; t += 5) {
            now += 5;
            const ButtonEdge e = dec.update(pressed, now);
            if (e != ButtonEdge::None) {
                edges.push_back(e);
            }
        }
    }
    int count(ButtonEdge e) const { return static_cast<int>(std::count(edges.begin(), edges.end(), e)); }
};

} // namespace

TEST(UiButton, PressFiresOnceWhenTheButtonGoesDown)
{
    ButtonRig rig(ButtonTiming{});
    rig.hold(true, 5);
    EXPECT_TRUE(rig.edges.empty()) << "one sample is not a press";
    rig.hold(true, 5);
    EXPECT_EQ(rig.edges.size(), 1u) << "two samples in a row are";
    rig.hold(true, 200);
    rig.hold(false, 100);
    ASSERT_EQ(rig.edges.size(), 1u);
    EXPECT_EQ(rig.edges[0], ButtonEdge::Press);
}

TEST(UiButton, ContactBounceGivesOnePress)
{
    ButtonRig rig(ButtonTiming{});
    for (int i = 0; i < 6; ++i) {
        rig.hold(true, 5);
        rig.hold(false, 5);
    }
    rig.hold(true, 100);
    for (int i = 0; i < 6; ++i) {
        rig.hold(false, 5);
        rig.hold(true, 5);
    }
    rig.hold(false, 100);
    EXPECT_EQ(rig.count(ButtonEdge::Press), 1);
}

TEST(UiButton, LongPressReplacesTheClick)
{
    ButtonTiming t;
    t.longPressMs = 700;
    ButtonRig rig(t);
    rig.hold(true, 300);
    EXPECT_TRUE(rig.edges.empty()) << "a click is reported on release";
    rig.hold(false, 100);
    ASSERT_EQ(rig.edges.size(), 1u);
    EXPECT_EQ(rig.edges[0], ButtonEdge::Press);

    rig.edges.clear();
    rig.hold(true, 1500);
    rig.hold(false, 100);
    ASSERT_EQ(rig.edges.size(), 1u) << "no click after a long press";
    EXPECT_EQ(rig.edges[0], ButtonEdge::LongPress);
}

TEST(UiButton, RepeatsWhileHeld)
{
    ButtonTiming t;
    t.repeatDelayMs = 600;
    t.repeatMs = 200;
    ButtonRig rig(t);
    rig.hold(true, 1250);
    rig.hold(false, 100);
    EXPECT_EQ(rig.count(ButtonEdge::Press), 1);
    // Down at 10 ms, the second sample; repeats at 610, 810, 1010, 1210.
    EXPECT_EQ(rig.count(ButtonEdge::Repeat), 4);
}

// The encoder switch: Press on the way down as before, and Hold once after
// three seconds held (Show setup code). A short press gives no Hold.
TEST(UiButton, SelectHoldFollowsItsPress)
{
    ButtonRig rig(selectButtonTiming());
    rig.hold(true, 100);
    ASSERT_EQ(rig.edges, (std::vector<ButtonEdge>{ButtonEdge::Press})) << "no delay on the way down";
    rig.hold(true, kSelectHoldMs - 200);
    EXPECT_EQ(rig.count(ButtonEdge::Hold), 0) << "not yet";
    rig.hold(true, 300);
    EXPECT_EQ(rig.count(ButtonEdge::Hold), 1);
    rig.hold(true, 5000);
    rig.hold(false, 100);
    EXPECT_EQ(rig.edges, (std::vector<ButtonEdge>{ButtonEdge::Press, ButtonEdge::Hold})) << "Hold is sent once";

    rig.edges.clear();
    rig.hold(true, 500);
    rig.hold(false, 100);
    EXPECT_EQ(rig.edges, (std::vector<ButtonEdge>{ButtonEdge::Press}));
}

TEST(UiButton, HomeHoldStillStopsAndBackStillRepeats)
{
    ButtonRig home(homeButtonTiming());
    home.hold(true, 900);
    home.hold(false, 100);
    EXPECT_EQ(home.edges, (std::vector<ButtonEdge>{ButtonEdge::LongPress}));
    ButtonRig back(backButtonTiming());
    back.hold(true, 1500);
    EXPECT_GE(back.count(ButtonEdge::Repeat), 2);
    EXPECT_EQ(back.count(ButtonEdge::Hold), 0);
}

TEST(UiButton, HeldAtPowerUpIsNotAPress)
{
    ButtonTiming t;
    t.longPressMs = 700;
    ButtonDecoder dec(t);
    MonoMs now = 0;
    int edges = 0;
    for (int i = 0; i < 400; ++i) {
        now += 5;
        edges += dec.update(true, now) != ButtonEdge::None ? 1 : 0;
    }
    for (int i = 0; i < 20; ++i) {
        now += 5;
        edges += dec.update(false, now) != ButtonEdge::None ? 1 : 0;
    }
    EXPECT_EQ(edges, 0);
    for (int i = 0; i < 20; ++i) {
        now += 5;
        edges += dec.update(true, now) != ButtonEdge::None ? 1 : 0;
    }
    for (int i = 0; i < 20; ++i) {
        now += 5;
        edges += dec.update(false, now) != ButtonEdge::None ? 1 : 0;
    }
    EXPECT_EQ(edges, 1) << "works normally after the first release";
}

// Rev A: EC11J1525402, 30 detents and 15 pulses per turn, so one detent is
// half a quadrature cycle: two of the four edges PCNT counts per cycle
// (hardware design 19.3 item 8).
TEST(UiEncoder, RevABoardHasTwoCountsPerDetent) { EXPECT_EQ(board::kEncoderCountsPerDetent, 2); }

TEST(UiEncoder, TwoCountsAreOneDetent)
{
    EncoderDecoder enc(board::kEncoderCountsPerDetent);
    MonoMs now = 0;
    EXPECT_EQ(enc.update(1000, now), 0) << "first sample only sets the origin";
    EXPECT_EQ(enc.update(1001, now += 1), 0);
    EXPECT_EQ(enc.update(1002, now += 1), 1);
    EXPECT_EQ(enc.update(1006, now += 1), 2);
    EXPECT_EQ(enc.update(1002, now += 1), -2);
    EXPECT_EQ(enc.update(1001, now += 1), 0);
    EXPECT_EQ(enc.update(1000, now += 1), -1);
}

// The PCNT setup of src/port/inputs.cpp counts every edge of both phases.
// Turning an EC11 by one detent moves it from one rest state to the next:
// half a cycle, two edges. A full turn of 30 detents must give 30 steps, and
// back again -30, sampled every 5 ms at a brisk two turns per second.
TEST(UiEncoder, OneTurnOfTheRevAEncoderGivesThirtySteps)
{
    constexpr int kDetentsPerTurn = 30;
    constexpr int kPulsesPerTurn = 15;
    constexpr int kEdgesPerTurn = kPulsesPerTurn * 4;
    static_assert(kEdgesPerTurn / kDetentsPerTurn == board::kEncoderCountsPerDetent);
    EncoderDecoder enc(board::kEncoderCountsPerDetent);
    MonoMs now = 0;
    int32_t count = 0;
    enc.update(count, now);
    int steps = 0;
    for (int edge = 0; edge < kEdgesPerTurn; ++edge) {
        steps += enc.update(++count, now += 5);
    }
    EXPECT_EQ(steps, kDetentsPerTurn);
    for (int edge = 0; edge < kEdgesPerTurn; ++edge) {
        steps += enc.update(--count, now += 5);
    }
    EXPECT_EQ(steps, 0);
}

TEST(UiEncoder, OtherEncodersKeepTheirOwnCount)
{
    EncoderDecoder enc(4);
    MonoMs now = 0;
    enc.update(0, now);
    EXPECT_EQ(enc.update(2, now += 1), 0);
    EXPECT_EQ(enc.update(4, now += 1), 1);
}

TEST(UiEncoder, JitterAroundADetentGivesNoSteps)
{
    EncoderDecoder enc(board::kEncoderCountsPerDetent);
    MonoMs now = 0;
    enc.update(0, now);
    int steps = 0;
    for (int i = 0; i < 50; ++i) {
        steps += enc.update(i % 2 == 0 ? 1 : -1, now += 2);
    }
    EXPECT_EQ(steps, 0);
}

TEST(UiEncoder, LostCountIsDroppedAtRest)
{
    EncoderDecoder enc(board::kEncoderCountsPerDetent);
    MonoMs now = 0;
    enc.update(0, now);
    // One count went missing: the knob rests at 1 instead of 2.
    EXPECT_EQ(enc.update(1, now += 5), 0);
    EXPECT_EQ(enc.update(1, now += EncoderDecoder::kSettleMs), 0);
    // The next full detent counts as one step, not as a step plus a leftover.
    EXPECT_EQ(enc.update(3, now += 5), 1);
    EXPECT_EQ(enc.update(5, now += 5), 1);
}

TEST(UiEncoder, SurvivesCounterWrap)
{
    EncoderDecoder enc(board::kEncoderCountsPerDetent);
    MonoMs now = 0;
    enc.update(INT32_MAX - 1, now);
    EXPECT_EQ(enc.update(static_cast<int32_t>(static_cast<uint32_t>(INT32_MAX) + 1u), now += 1), 1);
}

TEST(UiAccelerator, SlowTurnsAreOneToOne)
{
    Accelerator acc;
    MonoMs now = 1000;
    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(acc.apply(1, now), 1);
        now += 150; // under 7 detents per second
    }
    EXPECT_EQ(acc.apply(-1, now), -1);
}

TEST(UiAccelerator, FastSpinMultipliesAndResets)
{
    Accelerator acc;
    MonoMs now = 1000;
    int total = 0;
    for (int i = 0; i < 20; ++i) {
        total += acc.apply(1, now);
        now += 10; // 100 detents per second
    }
    EXPECT_GT(total, 100) << "20 detents cover more than 100 steps";
    EXPECT_EQ(acc.multiplier(), 10);

    // Reversing or pausing drops back to single steps at once.
    EXPECT_EQ(acc.apply(-1, now + 10), -1);
    now += 10 + Accelerator::kResetMs + 1;
    EXPECT_EQ(acc.apply(1, now), 1);
    EXPECT_EQ(acc.apply(0, now + 5), 0);
}
