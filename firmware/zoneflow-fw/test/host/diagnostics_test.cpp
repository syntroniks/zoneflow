// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include "zf/diag/current_monitor.hpp"
#include "zf/diag/sensors.hpp"

using namespace zf;

// ---- contact sensors ---------------------------------------------------------

TEST(ContactSensor, NormallyClosedRainSensorWithDebounce)
{
    ContactSensor s;
    s.configure({true, ContactType::NormallyClosed, 2000});
    EXPECT_FALSE(s.update(true, 0)); // closed = dry
    EXPECT_FALSE(s.active());
    EXPECT_FALSE(s.update(false, 100)); // opens: wet candidate
    EXPECT_FALSE(s.update(false, 2099));
    EXPECT_TRUE(s.update(false, 2100));
    EXPECT_TRUE(s.active());
    // A short bounce back does not clear it.
    EXPECT_FALSE(s.update(true, 3000));
    EXPECT_FALSE(s.update(false, 3500));
    EXPECT_TRUE(s.active());
}

TEST(ContactSensor, NormallyOpenAndFirstSampleTakenImmediately)
{
    ContactSensor s;
    s.configure({true, ContactType::NormallyOpen, 5000});
    EXPECT_TRUE(s.update(true, 0)); // wet at boot inhibits at once
    EXPECT_TRUE(s.active());
}

TEST(ContactSensor, DisabledIsNeverActive)
{
    ContactSensor s;
    s.configure({false, ContactType::NormallyOpen, 0});
    s.update(true, 0);
    s.update(true, 1000);
    EXPECT_FALSE(s.active());
    EXPECT_TRUE(s.rawActive());
}

TEST(AcMonitor, FastLossSlowReturn)
{
    AcMonitor ac;
    ac.update(true, 0);
    EXPECT_TRUE(ac.present());
    ac.update(false, 10);
    EXPECT_TRUE(ac.present());
    ac.update(false, 10 + AcMonitor::kLossMs);
    EXPECT_FALSE(ac.present());
    ac.update(true, 200);
    ac.update(true, 200 + AcMonitor::kReturnMs - 1);
    EXPECT_FALSE(ac.present());
    ac.update(true, 200 + AcMonitor::kReturnMs);
    EXPECT_TRUE(ac.present());
}

// ---- flow --------------------------------------------------------------------

TEST(FlowMeter, LitresAndRateFromKFactor)
{
    FlowMeter f;
    f.configure({true, 450.0f, 0.5f, 60});
    uint32_t counter = 1000; // arbitrary start value
    f.update(counter, 0);
    // 15 pulses per second = 900 pulses per minute = 2 L/min at 450 p/L.
    for (MonoMs t = 1000; t <= 10000; t += 1000) {
        counter += 15;
        f.update(counter, t);
    }
    EXPECT_EQ(f.totalPulses(), 150u);
    EXPECT_NEAR(f.totalLitres(), 150.0f / 450.0f, 1e-5);
    EXPECT_NEAR(f.rateLpm(), 2.0f, 1e-3);
}

TEST(FlowMeter, CounterWrapAndSessions)
{
    FlowMeter f;
    f.configure({true, 10.0f, 0.5f, 60});
    f.update(0xFFFFFFF0u, 0);
    f.markSession();
    f.update(0x00000010u, 1000); // wrapped: +32 pulses
    EXPECT_EQ(f.totalPulses(), 32u);
    EXPECT_NEAR(f.sessionLitres(), 3.2f, 1e-5);
}

TEST(FlowMeter, RateDropsToZeroWhenFlowStops)
{
    FlowMeter f;
    f.configure({true, 1.0f, 0.5f, 60});
    uint32_t c = 0;
    f.update(c, 0);
    for (MonoMs t = 1000; t <= 3000; t += 1000) {
        f.update(c += 10, t);
    }
    EXPECT_GT(f.rateLpm(), 0.0f);
    for (MonoMs t = 4000; t <= 12000; t += 1000) {
        f.update(c, t);
    }
    EXPECT_EQ(f.rateLpm(), 0.0f);
}

// ---- valve current -------------------------------------------------------------

namespace {
CurrentMonitorConfig currentCfg()
{
    CurrentMonitorConfig c;
    c.settleMs = 500;
    c.idleMaxMa = 60;
    c.shortCircuitMa = 3000;
    c.confirmSamples = 3;
    for (auto &w : c.zone) {
        w = {100, 500};
    }
    c.master = {100, 500};
    return c;
}
constexpr uint16_t bit(unsigned b) { return static_cast<uint16_t>(1u << b); }
} // namespace

TEST(CurrentMonitor, NormalCurrentNoFault)
{
    CurrentMonitor m;
    m.configure(currentCfg());
    for (MonoMs t = 0; t < 3000; t += 100) {
        EXPECT_EQ(m.evaluate(250, bit(2), 2, 0, t).fault, CurrentFault::None);
    }
    EXPECT_EQ(m.observedMa(2), 250);
}

TEST(CurrentMonitor, IgnoresInrushDuringSettle)
{
    CurrentMonitor m;
    m.configure(currentCfg());
    for (MonoMs t = 0; t < 500; t += 50) {
        EXPECT_EQ(m.evaluate(1500, bit(0), 0, 0, t).fault, CurrentFault::None);
    }
}

TEST(CurrentMonitor, OpenCircuitAttributedToLastOpenedOutput)
{
    CurrentMonitor m;
    m.configure(currentCfg());
    const uint16_t mask = bit(0) | bit(4) | bit(CurrentMonitor::kMasterBit);
    // Expected 300..1500 mA for three outputs; 250 mA means one is open.
    CurrentVerdict v;
    v = m.evaluate(250, mask, 4, 0, 600);
    EXPECT_EQ(v.fault, CurrentFault::None);
    v = m.evaluate(250, mask, 4, 0, 700);
    EXPECT_EQ(v.fault, CurrentFault::None);
    v = m.evaluate(250, mask, 4, 0, 800);
    EXPECT_EQ(v.fault, CurrentFault::OpenCircuit);
    EXPECT_EQ(v.outputBit, 4);
    // Reported once only.
    EXPECT_EQ(m.evaluate(250, mask, 4, 0, 900).fault, CurrentFault::None);
}

TEST(CurrentMonitor, OverCurrentNeedsConfirmation)
{
    CurrentMonitor m;
    m.configure(currentCfg());
    EXPECT_EQ(m.evaluate(900, bit(1), 1, 0, 600).fault, CurrentFault::None);
    EXPECT_EQ(m.evaluate(300, bit(1), 1, 0, 700).fault, CurrentFault::None); // noise clears
    EXPECT_EQ(m.evaluate(900, bit(1), 1, 0, 800).fault, CurrentFault::None);
    EXPECT_EQ(m.evaluate(900, bit(1), 1, 0, 900).fault, CurrentFault::None);
    EXPECT_EQ(m.evaluate(900, bit(1), 1, 0, 1000).fault, CurrentFault::OverCurrent);
}

TEST(CurrentMonitor, ShortCircuitTripsEvenDuringSettle)
{
    CurrentMonitor m;
    m.configure(currentCfg());
    EXPECT_EQ(m.evaluate(3500, bit(6), 6, 0, 10).fault, CurrentFault::None);
    const auto v = m.evaluate(3500, bit(6), 6, 0, 60);
    EXPECT_EQ(v.fault, CurrentFault::ShortCircuit);
    EXPECT_EQ(v.outputBit, 6);
}

TEST(CurrentMonitor, CurrentWhileIdle)
{
    CurrentMonitor m;
    m.configure(currentCfg());
    CurrentVerdict v;
    for (MonoMs t = 600; t <= 800; t += 100) {
        v = m.evaluate(200, 0, 0xFF, 0, t);
    }
    EXPECT_EQ(v.fault, CurrentFault::CurrentWhileIdle);
    EXPECT_EQ(v.outputBit, 0xFF);
}

TEST(CurrentMonitor, UnknownMaximumSkipsOverCurrentCheck)
{
    auto cfg = currentCfg();
    cfg.zone[3] = {0, 0};
    CurrentMonitor m;
    m.configure(cfg);
    for (MonoMs t = 600; t < 2000; t += 100) {
        EXPECT_EQ(m.evaluate(2000, bit(3), 3, 0, t).fault, CurrentFault::None);
    }
}

TEST(CurrentMonitor, DisabledOrNoReading)
{
    auto cfg = currentCfg();
    CurrentMonitor m;
    m.configure(cfg);
    for (MonoMs t = 600; t < 2000; t += 100) {
        EXPECT_EQ(m.evaluate(-1, bit(0), 0, 0, t).fault, CurrentFault::None);
    }
    cfg.enabled = false;
    m.configure(cfg);
    for (MonoMs t = 600; t < 2000; t += 100) {
        EXPECT_EQ(m.evaluate(5000, bit(0), 0, 0, t).fault, CurrentFault::None);
    }
}

TEST(CurrentMonitor, LearnsZoneShareWithMaster)
{
    CurrentMonitor m;
    m.configure(currentCfg());
    const uint16_t master = bit(CurrentMonitor::kMasterBit);
    m.evaluate(200, master, CurrentMonitor::kMasterBit, 0, 1000);
    m.evaluate(450, master | bit(5), 5, 0, 1000);
    m.evaluate(450, master | bit(5), 5, 0, 2000);
    EXPECT_EQ(m.observedMa(CurrentMonitor::kMasterBit), 200);
    EXPECT_EQ(m.observedMa(5), 250);
}

// Review finding: a fault current that straddles short_circuit_ma alternates
// between "short circuit" and "over-current" from one sample to the next (for
// example 50 Hz ripple sampled every 50 ms). Each change of condition used to
// restart the confirmation count, so neither fault was ever declared.
TEST(CurrentMonitor, ShortAndOverCurrentAlternatingStillTrips)
{
    CurrentMonitor m;
    m.configure(currentCfg());
    CurrentFault first = CurrentFault::None;
    int samples = 0;
    for (MonoMs t = 600; t < 2600 && first == CurrentFault::None; t += 50) {
        const int32_t ma = (samples % 2 == 0) ? 3100 : 2900;
        first = m.evaluate(ma, bit(2), 2, 0, t).fault;
        ++samples;
    }
    EXPECT_TRUE(first == CurrentFault::ShortCircuit || first == CurrentFault::OverCurrent);
    EXPECT_LE(samples, 3);
}
