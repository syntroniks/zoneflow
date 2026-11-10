// SPDX-License-Identifier: Apache-2.0
// RMS current from synthetic waveforms of the rev A front end (hardware
// design 10 and 19.2): 50 and 60 Hz, clipping into the low-gain channel, a
// DC component, noise and the noise floor.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>

#include "zf/diag/current_sense.hpp"

using namespace zf;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kBiasMv = 790.0;
constexpr double kHiMvPerMa = 1.0 / 1.503;
constexpr double kLoMvPerMa = 1.0 / 7.50;
constexpr uint32_t kRate = 5000; // per channel
constexpr uint32_t kWindowMs = 100;

struct Wave {
    double rmsMa = 0; // sine current, RMS
    double hz = 50;
    double phase = 0;      // radians at the window start
    bool halfWave = false; // one polarity only (a triac half failed)
    double noiseMv = 0;    // RMS, Gaussian, on both channels
    double offsetMv = 0;   // bias shift that is not current (amplifier drift)
};

// One 100 ms window as the ADC would deliver it: calibrated millivolts,
// clipped to the 0 to 1600 mV range of 6 dB attenuation, rounded.
CurrentWindowStats window(const Wave &w, uint32_t seq = 1, unsigned seed = 1)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> noise(0.0, w.noiseMv > 0 ? w.noiseMv : 1.0);
    CurrentWindowStats s;
    s.seq = seq;
    s.startMs = 1000;
    s.endMs = 1100;
    const uint32_t n = kRate * kWindowMs / 1000;
    for (uint32_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kRate;
        double ma = w.rmsMa * std::sqrt(2.0) * std::sin(2 * kPi * w.hz * t + w.phase);
        if (w.halfWave && ma < 0) {
            ma = 0;
        }
        auto adc = [&](double mvPerMa) {
            double mv = kBiasMv + w.offsetMv + ma * mvPerMa + (w.noiseMv > 0 ? noise(rng) : 0.0);
            mv = std::clamp(mv, 0.0, 1600.0);
            return static_cast<int32_t>(std::lround(mv));
        };
        s.hi.add(adc(kHiMvPerMa));
        s.lo.add(adc(kLoMvPerMa));
    }
    return s;
}

} // namespace

TEST(CurrentSense, AveragingWouldGiveTheBiasNotTheCurrent)
{
    const CurrentWindowStats s = window({250, 50});
    EXPECT_NEAR(s.hi.meanMv(), kBiasMv, 1.0);
    CurrentSense cs;
    const CurrentReading r = cs.process(s, false, 30);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.ma, 250, 3);
    EXPECT_EQ(r.channel, CurrentChannel::High);
}

TEST(CurrentSense, WholeCyclesAt50And60HzAnyPhase)
{
    CurrentSense cs;
    for (double hz : {50.0, 60.0}) {
        for (double phase : {0.0, 0.7, 2.1, 4.0}) {
            const CurrentReading r = cs.process(window({300, hz, phase}), false, 30);
            ASSERT_TRUE(r.valid);
            EXPECT_NEAR(r.ma, 300, 3) << hz << " Hz, phase " << phase;
        }
    }
}

TEST(CurrentSense, SmallestLoadOnTheHighGainChannel)
{
    CurrentSense cs;
    const CurrentReading r = cs.process(window({50, 60}), false, 30);
    ASSERT_TRUE(r.valid);
    EXPECT_NEAR(r.ma, 50, 2); // 33 mV rms, 83 counts (design 10.5)
    EXPECT_EQ(r.channel, CurrentChannel::High);
    EXPECT_FALSE(r.hiClipped);
}

TEST(CurrentSense, ClippedHighChannelSwitchesToLowGain)
{
    CurrentSense cs;
    // 1.5 A rms: 2.12 A peak, far past the high channel's +/-1.17 A.
    const CurrentReading r = cs.process(window({1500, 50}), false, 30);
    ASSERT_TRUE(r.valid);
    EXPECT_TRUE(r.hiClipped);
    EXPECT_EQ(r.channel, CurrentChannel::Low);
    EXPECT_NEAR(r.ma, 1500, 20);
    // Just below 60 mV at the trough: the high channel is not trusted.
    const double ma = (kBiasMv - 55.0) / kHiMvPerMa / std::sqrt(2.0);
    EXPECT_EQ(cs.process(window({ma, 50}), false, 30).channel, CurrentChannel::Low);
}

TEST(CurrentSense, BiasDriftIsNotCurrent)
{
    CurrentSense cs;
    const CurrentReading r = cs.process(window({200, 50, 0, false, 0, 40}), false, 30);
    EXPECT_NEAR(r.ma, 200, 3); // the mean is removed per window
}

TEST(CurrentSense, NoiseFloorLearnedWithTheRelayOpenAndRemovedInQuadrature)
{
    CurrentSense cs;
    // Noise alone, 6 mV rms on the ADC: 9 mA on the high-gain channel.
    for (unsigned i = 0; i < 40; ++i) {
        cs.process(window({0, 50, 0, false, 6.0}, i + 1, i + 1), true, 30);
    }
    EXPECT_NEAR(cs.noiseFloorMa(), 9, 1);
    const CurrentWindowStats loaded = window({60, 50, 0, false, 6.0}, 100, 99);
    const CurrentReading r = cs.process(loaded, false, 30);
    EXPECT_NEAR(r.rawMa, std::sqrt(60.0 * 60 + 9.0 * 9), 2);
    EXPECT_NEAR(r.ma, 60, 2);
    // An idle window reads about zero after the floor is removed.
    EXPECT_LE(cs.process(window({0, 50, 0, false, 6.0}, 101, 7), true, 30).ma, 4);
}

TEST(CurrentSense, CurrentWithTheRelayOpenDoesNotTeachTheFloor)
{
    CurrentSense cs;
    cs.process(window({0, 50}), true, 30);
    cs.process(window({200, 50}, 2), true, 30); // welded relay: real current
    EXPECT_LE(cs.noiseFloorMa(), 1);
}

TEST(CurrentSense, HalfWaveCurrentIsADcComponent)
{
    CurrentSense cs;
    cs.process(window({0, 50}), true, 30); // idle mean learned
    ASSERT_TRUE(cs.baselineKnown());
    const CurrentReading full = cs.process(window({300, 50}, 2), false, 30);
    EXPECT_FALSE(full.dcComponent);
    EXPECT_NEAR(full.dcMa, 0, 3);
    const CurrentReading half = cs.process(window({300, 50, 0, true}, 3), false, 30);
    EXPECT_TRUE(half.dcComponent);
    // Half of a 300 mA rms sine: peak 424 mA, mean 424 / pi = 135 mA.
    EXPECT_NEAR(half.dcMa, 135, 4);
    // Mean-removed RMS of the half wave: 0.386 x 424 = 164 mA.
    EXPECT_NEAR(half.ma, 164, 4);
}

TEST(CurrentSense, NoDcVerdictWithoutAnIdleMean)
{
    CurrentSense cs;
    const CurrentReading half = cs.process(window({300, 50, 0, true}), false, 30);
    EXPECT_FALSE(half.dcComponent);
}

TEST(CurrentSense, NoiseOnALoadAveragesOut)
{
    CurrentSense cs;
    const CurrentReading r = cs.process(window({250, 60, 1.0, false, 3.0}, 1, 42), false, 30);
    // 3 mV rms noise is 4.5 mA on the high channel; in quadrature with
    // 250 mA it adds 0.04 mA, within the tolerance.
    EXPECT_NEAR(r.ma, 250, 3);
}

TEST(CurrentSense, ShortWindowIsNotAReading)
{
    CurrentSense cs;
    CurrentWindowStats s;
    s.seq = 1;
    for (int i = 0; i < 20; ++i) {
        s.hi.add(790);
        s.lo.add(790);
    }
    EXPECT_FALSE(cs.process(s, false, 30).valid);
}

TEST(CurrentSense, ChannelStatsArithmetic)
{
    ChannelStats c;
    for (int v : {700, 900, 700, 900}) {
        c.add(v);
    }
    EXPECT_DOUBLE_EQ(c.meanMv(), 800.0);
    EXPECT_DOUBLE_EQ(c.acRmsMv(), 100.0);
    EXPECT_EQ(c.minMv, 700);
    EXPECT_EQ(c.maxMv, 900);
}
