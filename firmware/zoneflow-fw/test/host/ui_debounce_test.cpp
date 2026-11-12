// SPDX-License-Identifier: Apache-2.0
// Button debounce against waveforms of the bare contact.
//
// Rev A has no RC filter on the encoder switch, Back and Manual/Home
// (hardware design 15.4): the pin sees every closure and opening of a
// chattering contact, and ButtonDecoder is all there is. These tests play a
// waveform of the pin, edge by edge in microseconds, to a sampler that does
// what src/port/inputs.cpp does: read the level every 5 ms, give the decoder
// the time in whole milliseconds. Every waveform is run at twenty sample
// phases, with the timer on time and with each sample late by up to 1 ms.
//
// The waveforms in data/bounce are made up to stated limits. They are not
// recordings: no board exists. The file format is that of a logic analyser,
// so that a recording can be added (data/bounce/make_waveforms.py).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "zf_ui/input.hpp"

using namespace zf;
using namespace zf::ui;

namespace {

constexpr int64_t kPeriodUs = kButtonSamplePeriodMs * 1000;

// The level at the pin over time: closed (low) from each edge's time on.
struct Waveform {
    struct Edge {
        int64_t us;
        bool closed;
    };
    std::vector<Edge> edges;
    int64_t endUs = 0;

    bool closedAt(int64_t us) const
    {
        bool closed = false;
        for (const Edge &e : edges) {
            if (e.us > us) {
                break;
            }
            closed = e.closed;
        }
        return closed;
    }
};

Waveform load(const std::string &name)
{
    const std::string path = std::string(ZF_BOUNCE_DIR) + "/" + name;
    std::ifstream in(path);
    EXPECT_TRUE(in.good()) << path;
    Waveform w;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line.rfind("time_us", 0) == 0) {
            continue;
        }
        std::istringstream row(line);
        int64_t us = 0;
        char comma = 0;
        int pin = 1;
        row >> us >> comma >> pin;
        EXPECT_TRUE(!row.fail() && comma == ',' && (pin == 0 || pin == 1)) << path << ": " << line;
        EXPECT_TRUE(w.edges.empty() || us > w.edges.back().us) << path << ": times must rise";
        w.edges.push_back({us, pin == 0});
        w.endUs = us;
    }
    EXPECT_FALSE(w.edges.empty()) << path;
    return w;
}

// What inputs.cpp does with a pin, on a waveform. phaseUs moves the sample
// instants; with jitter each sample is late by 0 to 1000 us, from the seed.
std::vector<ButtonEdge> play(const Waveform &w, const ButtonTiming &timing, int64_t phaseUs, uint32_t jitterSeed = 0,
                             int64_t periodUs = kPeriodUs)
{
    ButtonDecoder dec(timing);
    std::mt19937 rng(jitterSeed);
    std::vector<ButtonEdge> out;
    for (int64_t t = phaseUs; t <= w.endUs; t += periodUs) {
        const int64_t at = t + (jitterSeed != 0 ? static_cast<int64_t>(rng() % 1001) : 0);
        const ButtonEdge e = dec.update(w.closedAt(at), static_cast<MonoMs>(at / 1000));
        if (e != ButtonEdge::None) {
            out.push_back(e);
        }
    }
    return out;
}

std::string text(const std::vector<ButtonEdge> &edges)
{
    static const char *const kNames[] = {"None", "Press", "LongPress", "Repeat", "Hold"};
    std::string s;
    for (ButtonEdge e : edges) {
        s += (s.empty() ? "" : " ") + std::string(kNames[static_cast<int>(e)]);
    }
    return s.empty() ? "nothing" : s;
}

using Edges = std::vector<ButtonEdge>;
const ButtonEdge P = ButtonEdge::Press;
const ButtonEdge L = ButtonEdge::LongPress;
const ButtonEdge R = ButtonEdge::Repeat;

// What each control must make of each waveform: the encoder switch (Press on
// the way down), Back (the same, and Repeat from 0.6 s every 0.4 s) and
// Manual/Home (Press on release, LongPress at 0.8 s in its place).
struct Case {
    const char *file;
    Edges select;
    Edges back;
    Edges home;
};
const Case kWaveforms[] = {
    {"short-press-30ms.csv", {P}, {P}, {P}},  {"short-press-30ms-all-in.csv", {P}, {P}, {P}},
    {"click-chatter-3ms.csv", {P}, {P}, {P}}, {"click-chatter-14ms.csv", {P}, {P}, {P}},
    {"hold-1s.csv", {P}, {P, R, R}, {L}},     {"double-click.csv", {P, P}, {P, P}, {P, P}},
    {"spikes-no-press.csv", {}, {}, {}},
};

class Debounce : public ::testing::TestWithParam<Case> {};

TEST_P(Debounce, EveryControlGivesTheSameEventsAtEverySamplePhase)
{
    const Case &c = GetParam();
    const Waveform w = load(c.file);
    const struct {
        const char *name;
        ButtonTiming timing;
        const Edges &want;
    } controls[] = {
        {"select", selectButtonTiming(), c.select},
        {"back", backButtonTiming(), c.back},
        {"home", homeButtonTiming(), c.home},
    };
    for (const auto &control : controls) {
        for (int64_t phase = 0; phase < kPeriodUs; phase += 250) {
            for (uint32_t seed : {0u, 1u, 2u, 3u}) {
                const Edges got = play(w, control.timing, phase, seed);
                ASSERT_EQ(got, control.want) << c.file << ", " << control.name << ", samples at " << phase
                                             << " us, jitter seed " << seed << ": " << text(got);
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Waveforms, Debounce, ::testing::ValuesIn(kWaveforms),
                         [](const ::testing::TestParamInfo<Case> &info) {
                             std::string n = info.param.file;
                             n = n.substr(0, n.find('.'));
                             std::replace(n.begin(), n.end(), '-', '_');
                             return n;
                         });

// ---- the limits, on waveforms made here ------------------------------------------------

// Chatter: closures and openings of 20 to 900 us, for at most lengthUs, ending in the given state.
void chatter(Waveform &w, std::mt19937 &rng, int64_t startUs, int64_t lengthUs, bool endsClosed)
{
    bool closed = endsClosed; // the burst starts with the first change of state
    int64_t t = startUs;
    if (lengthUs == 0) {
        w.edges.push_back({t, endsClosed});
        return;
    }
    w.edges.push_back({t, closed});
    for (;;) {
        t += 20 + static_cast<int64_t>(rng() % 881);
        if (t >= startUs + lengthUs) {
            break;
        }
        closed = !closed;
        w.edges.push_back({t, closed});
    }
    if (closed != endsClosed) {
        w.edges.push_back({startUs + lengthUs, endsClosed});
    }
}

Waveform onePress(std::mt19937 &rng, int64_t closingUs, int64_t closedUs, int64_t openingUs)
{
    Waveform w;
    w.edges.push_back({0, false});
    const int64_t first = 100000;
    chatter(w, rng, first, closingUs, true);
    chatter(w, rng, first + closingUs + closedUs, openingUs, false);
    w.endUs = first + closingUs + closedUs + openingUs + 200000;
    return w;
}

// The brief for the change: no missed press of 30 ms or more, no second event from chatter of up to
// 10 ms. 2000 presses with chatter of 0 to 10 ms at each end and 20 to 60 ms closed between, each at
// its own sample phase with the timer up to 1 ms late.
TEST(DebounceLimits, AShortPressWithTenMsOfChatterIsOnePress)
{
    std::mt19937 rng(20261112);
    for (int i = 0; i < 2000; ++i) {
        const Waveform w = onePress(rng, rng() % 10001, 20000 + rng() % 40001, rng() % 10001);
        const int64_t phase = rng() % kPeriodUs;
        const uint32_t seed = 1 + rng() % 1000;
        for (const ButtonTiming &t : {selectButtonTiming(), backButtonTiming(), homeButtonTiming()}) {
            ASSERT_EQ(play(w, t, phase, seed), Edges{P}) << "press " << i;
        }
    }
}

// Where the margins end, with each sample up to 1 ms late. Two samples in a row fall into any
// closure of 12 ms: samples are at most 6 ms apart. A release is believed 20 ms after the first
// sample that read open, in whole milliseconds, so at least 17 ms after the chatter began: 16 ms of
// chatter is tried here.
TEST(DebounceLimits, TwelveMsClosedIsEnoughAndSixteenMsOfChatterDoesNoHarm)
{
    std::mt19937 rng(7);
    for (int i = 0; i < 20000; ++i) {
        const Waveform w = onePress(rng, rng() % 16001, 12000 + rng() % 30001, rng() % 16001);
        ASSERT_EQ(play(w, selectButtonTiming(), rng() % kPeriodUs, 1 + rng() % 1000), Edges{P}) << "press " << i;
    }
}

// With the timer on time, 10 ms closed is enough, whatever the chatter round it.
TEST(DebounceLimits, TenMsClosedIsEnoughWithTheTimerOnTime)
{
    std::mt19937 rng(11);
    for (int i = 0; i < 20000; ++i) {
        const Waveform w = onePress(rng, rng() % 10001, 10000, rng() % 10001);
        ASSERT_EQ(play(w, selectButtonTiming(), rng() % kPeriodUs), Edges{P}) << "press " << i;
    }
}

// Why the release time is the long one. With a release believed as quickly as a press, two samples
// in a row, 10 ms of chatter still gives one press at this sample period, with nothing to spare:
// a third sample inside the burst and the press ends and starts again. 16 ms of chatter does that.
// With releaseMs at 20 the same waveforms give one press each (the test above).
TEST(DebounceLimits, AQuickReleaseWouldDoubleTheEventsOfAWornSwitch)
{
    ButtonTiming quick = selectButtonTiming();
    quick.releaseMs = quick.pressMs;
    std::mt19937 rng(20261112);
    int doubled = 0;
    for (int i = 0; i < 2000; ++i) {
        const Waveform w = onePress(rng, 16000, 30000, 16000);
        const int64_t phase = rng() % kPeriodUs;
        doubled += play(w, quick, phase).size() > 1 ? 1 : 0;
        ASSERT_EQ(play(w, selectButtonTiming(), phase), Edges{P}) << "press " << i;
    }
    EXPECT_GT(doubled, 0);
    RecordProperty("doubled_of_2000", doubled);
}

// Why the press time is the short one. Until the filters went, both times were 20 ms, behind an
// RC of 1 ms. On the bare contact that timing loses short presses: a contact that chatters for
// 10 ms and is then closed for 20 ms is not always seen closed on five samples in a row.
TEST(DebounceLimits, TwentyMsBothWaysWouldMissShortPresses)
{
    ButtonTiming old = selectButtonTiming();
    old.pressMs = 20;
    old.releaseMs = 20;
    std::mt19937 rng(20261112);
    int missed = 0;
    for (int i = 0; i < 2000; ++i) {
        const Waveform w = onePress(rng, 10000, 20000, 10000);
        missed += play(w, old, rng() % kPeriodUs).empty() ? 1 : 0;
    }
    EXPECT_GT(missed, 0);
    RecordProperty("missed_of_2000", missed);
}

// One sample that reads low is not a press, at any phase: the press time is under the sample
// period only so that the second sample in a row counts, never the first alone.
TEST(DebounceLimits, OneLowSampleIsNotAPress)
{
    for (int64_t phase = 0; phase < kPeriodUs; phase += 250) {
        Waveform w;
        w.edges = {{0, false}, {100000, true}, {100000 + kPeriodUs - 100, false}}; // closed for 4.9 ms
        w.endUs = 400000;
        ASSERT_TRUE(play(w, selectButtonTiming(), phase).empty()) << phase;
    }
    EXPECT_LT(selectButtonTiming().pressMs, kButtonSamplePeriodMs);
    EXPECT_GT(selectButtonTiming().pressMs, 0);
}

// The simulator and the browser demo sample every 10 ms (sim/controls.hpp). The same decoder and
// timings must give the same events there.
TEST(DebounceLimits, TheSimulatorsTenMsSamplingGivesTheSameEvents)
{
    for (const Case &c : kWaveforms) {
        if (std::string(c.file).find("short-press") != std::string::npos ||
            std::string(c.file).find("14ms") != std::string::npos) {
            continue; // made for the device's 5 ms: closed for less than two 10 ms samples, or chatter over 10 ms
        }
        const Waveform w = load(c.file);
        for (int64_t phase = 0; phase < 10000; phase += 500) {
            ASSERT_EQ(play(w, backButtonTiming(), phase, 0, 10000), c.back) << c.file << " at " << phase;
            ASSERT_EQ(play(w, homeButtonTiming(), phase, 0, 10000), c.home) << c.file << " at " << phase;
        }
    }
}

} // namespace
