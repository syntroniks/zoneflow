// SPDX-License-Identifier: Apache-2.0
// A simulated rev A output stage for host tests and the display simulator:
// the two keep-alive detectors, the over-current latch, a register-level
// TCA9539 that is in reset whenever DRV_EN is low, the valve-common relay
// and the valve current. Faults can be injected in each part.
//
// The model follows hardware design sections 7 to 9 at the level the
// firmware can observe: read-back levels, I2C acknowledges and register
// contents, and the current. Timings are nominal values from the design's
// tables, adjustable per test.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "fakes.hpp"
#include "zf/hw/output_stage.hpp"
#include "zf/zone/output_enable.hpp"
#include "zf_board/board.hpp"

namespace zf::test {

inline hw::ExpanderLayout revALayout()
{
    hw::ExpanderLayout l;
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        l.zone[z] = {board::kExpanderBits.zone[z].port, board::kExpanderBits.zone[z].bit};
    }
    l.master = {board::kExpanderBits.master.port, board::kExpanderBits.master.bit};
    l.relay = {board::kExpanderBits.relay.port, board::kExpanderBits.relay.bit};
    l.variant8ZoneN = {board::kExpanderBits.variant8ZoneN.port, board::kExpanderBits.variant8ZoneN.bit};
    l.sensorFaultN = {board::kExpanderBits.sensorFaultN.port, board::kExpanderBits.sensorFaultN.bit};
    return l;
}

// One keep-alive detector: on after `onMs` of edges no more than `maxGapMs`
// apart, off `holdMs` after the last edge (`holdHighMs` if the net stopped
// high). Faults: stuck on, on while the net is held high (a shorted pump
// capacitor: C22 for A, C51 for B), never on.
//
// The hold time depends on how long the keep-alive ran before it stopped,
// because the reservoir takes time to fill (review A2 4.6 question 1,
// hardware/sim/failsafe/results/warmup.md). `warmup` gives the share of the
// full hold time after a toggle of a given length, interpolated linearly
// between the points of that table for the corner that charges slowest.
enum class Warmup : uint8_t {
    None,      // full hold time after any toggle
    SlowDrive, // detector A at the slow corner: 0.61 of 0.68 s after 0.1 s
    SlowRelay, // detector B at the slow corner: 0.08 of 0.20 s after 0.1 s
};

inline double warmupShare(Warmup w, uint64_t toggledMs)
{
    struct Point {
        uint64_t ms;
        double share;
    };
    // results/warmup.md, last column: tA 0.61 0.66 0.67 0.68 0.68 and
    // tB 0.08 0.14 0.17 0.19 0.20 after 0.1, 0.15, 0.2, 0.3 and 0.5 s.
    static constexpr Point kDrive[] = {{0, 0.0}, {100, 0.90}, {150, 0.97}, {200, 0.985}, {300, 1.0}};
    static constexpr Point kRelay[] = {{0, 0.0}, {100, 0.40}, {150, 0.70}, {200, 0.85}, {300, 0.95}, {500, 1.0}};
    const Point *p = w == Warmup::SlowDrive ? kDrive : kRelay;
    const std::size_t n = w == Warmup::SlowDrive ? std::size(kDrive) : std::size(kRelay);
    if (w == Warmup::None || toggledMs >= p[n - 1].ms) {
        return 1.0;
    }
    for (std::size_t i = 1; i < n; ++i) {
        if (toggledMs <= p[i].ms) {
            const double f = static_cast<double>(toggledMs - p[i - 1].ms) / static_cast<double>(p[i].ms - p[i - 1].ms);
            return p[i - 1].share + f * (p[i].share - p[i - 1].share);
        }
    }
    return 1.0;
}

struct FakeDetector {
    uint32_t onMs = 20;
    uint32_t holdMs = 600;
    uint32_t holdHighMs = 900;
    uint32_t maxGapMs = 20;
    // A single rising edge, with the net then held high, lifts the gate of
    // Q2 to about 1 V, which turns a Q2 with a low threshold on until the
    // bleed resistors pull the gate down again (hardware design 19.7, note to
    // step 2; hardware/sim/failsafe/sequence.py): on for this long after the
    // edge. 0: the edge turns nothing on. Detector B only.
    uint32_t singleEdgeOnMs = 0;
    Warmup warmup = Warmup::None;
    bool stuckOn = false;
    bool onWhilePinHigh = false;
    bool neverOn = false;

    bool on = false;
    bool pulse = false; // on from a single rising edge (singleEdgeOnMs)
    uint64_t pulseUntilMs = 0;
    uint64_t toggleSinceMs = 0;
    bool toggling = false;
    uint64_t lastEdgeMs = 0;
    uint32_t edges = 0;
    uint64_t toggledMs = 0; // length of the latest toggling, ms

    void step(uint64_t nowMs, bool edge, bool pinHigh)
    {
        if (edge && pinHigh && !toggling && singleEdgeOnMs > 0) {
            pulseUntilMs = nowMs + singleEdgeOnMs; // a lone rising edge
        }
        pulse = pinHigh && nowMs < pulseUntilMs;
        if (edge) {
            if (!toggling || nowMs - lastEdgeMs > maxGapMs) {
                toggling = true;
                toggleSinceMs = nowMs;
                edges = 0;
            }
            ++edges;
            lastEdgeMs = nowMs;
            toggledMs = nowMs - toggleSinceMs;
        }
        const uint64_t idle = nowMs - lastEdgeMs;
        if (toggling && idle > maxGapMs) {
            toggling = false;
        }
        // A single edge charges the pump once and never enables (8.4).
        if (toggling && edges >= 3 && nowMs - toggleSinceMs >= onMs && !neverOn) {
            on = true;
        }
        if (on && !toggling && idle >= hold(pinHigh)) {
            on = false;
        }
    }
    uint64_t hold(bool pinHigh) const
    {
        const double full = pinHigh ? holdHighMs : holdMs;
        return static_cast<uint64_t>(full * warmupShare(warmup, toggledMs));
    }
    bool level(bool pinHigh) const { return stuckOn || on || pulse || (onWhilePinHigh && pinHigh); }
};

// Release times of both detectors at the tolerance corners of review A2
// (section 4.3, 4.5 and results/warmup.md), 5 ms edges, ms after the last
// edge with the pin low / with the pin high.
//
// After the single rising edge of step 2 the relay read-back stays low for
// up to 0.68 s at -20 C, 0.83 s at 27 C and 1.33 s at 70 C over the 96
// healthy corners (hardware answers A2, section 8, `failsafe.py sequence`).
// The corner with Q2 at 0.65 V and 70 C is the one that needs 1.33 s; the
// slow-charge corner, at -20 C, takes the 0.68 s. The typical and earliest
// corners are given none: the simulation names only the longest per
// temperature.
enum class DetectorCorner : uint8_t {
    Typical,         // typical parts, 27 C: tA 670 / 1010, tB 570 / 860
    LatestRelease,   // 70 C, 3.6 V, large capacitors, Q2 at 0.65 V: tA 830 / 1250, tB 1450 / 2190, one edge 1.33 s
    EarliestRelease, // 70 C, 3.0 V, small capacitors, Q2 at 1.45 V, leaky diodes: tA 210, tB 100
    SlowCharge,      // -20 C, 3.0 V, large reservoirs, small pumps: tA 680, tB 200, slow to fill, one edge 0.68 s
};

// What else drives the keep-alive net (review A2 section 3.3 and appendix A).
enum class KaNet : uint8_t {
    Pin,          // only the pin and R33: healthy
    Disconnected, // U4 pin 10 not connected: the net stays at R33's low
    ToggleStrong, // a 0 ohm bridge to a net that toggles by itself (backlight, boot log): it wins
    ToggleWeak,   // a 1 kohm bridge to such a net: the pin wins while driven; floating, the net toggles
    HighStrong,   // KA shorted to 3V3
    HighWeak,     // a node of detector A shorted to 3V3 (KA_N0): the pin wins while driven, floating it reads high
};

class FakeBoard final : public hw::IExpanderBus {
public:
    FakeBoard(OutputEnableGuard &guard, FakeClock &clock) : guard_(&guard), clock_(clock)
    {
        setCorner(DetectorCorner::Typical);
    }

    // The board outlives a firmware boot (test/soak): after a restart the
    // keep-alive pin belongs to the new firmware's guard.
    void attach(OutputEnableGuard &guard) { guard_ = &guard; }

    void setCorner(DetectorCorner c)
    {
        a.onMs = 20;
        b.onMs = 40;
        a.warmup = Warmup::None;
        b.warmup = Warmup::None;
        b.singleEdgeOnMs = 0;
        switch (c) {
        case DetectorCorner::Typical:
            a.holdMs = 670, a.holdHighMs = 1010, b.holdMs = 570, b.holdHighMs = 860;
            break;
        case DetectorCorner::LatestRelease:
            a.holdMs = 830, a.holdHighMs = 1250, b.holdMs = 1450, b.holdHighMs = 2190;
            b.singleEdgeOnMs = 1330;
            break;
        case DetectorCorner::EarliestRelease:
            a.holdMs = 210, a.holdHighMs = 270, b.holdMs = 100, b.holdHighMs = 130;
            break;
        case DetectorCorner::SlowCharge:
            a.holdMs = 680, a.holdHighMs = 1020, b.holdMs = 200, b.holdHighMs = 300;
            a.warmup = Warmup::SlowDrive;
            b.warmup = Warmup::SlowRelay;
            b.singleEdgeOnMs = 680;
            break;
        }
    }

    // ---- what the tests set ----
    FakeDetector a; // gate drive: comparator, latch logic, DRV_EN
    FakeDetector b; // relay transistor Q2
    bool eightZone = true;
    bool sensorFault = false;
    bool acPresent = true;
    bool vBuck = true; // false: U1 failed and F5 open, logic on USB (rule 15)
    // VIN_MON, GPIO3: the mean of a 100 ms window, mV (hardware design
    // 19.10: 960 at 24 VAC, 40 VA, typical load). Without 24 VAC the
    // rectified supply is gone: 0.
    int32_t vinMonMv = 960;
    bool rectifierOpen = false; // D2, R1 or C1 failed: VIN_DC is 0 with mains present
    // The logic rail, mV. The current channels rest at VB = 0.2385 x 3V3
    // (19.10), 787 mV at 3.30 V. U3 shorted gives 4.66 V or more on 24 VAC.
    int32_t v3v3Mv = 3300;
    // Offsets of the rest levels from VB, mV (an amplifier or resistor fault).
    int32_t restLoOffsetMv = 0;
    int32_t restHiOffsetMv = 0;
    // U13 (hardware design 9.5): above 4.04 V on 3V3 its output holds the
    // set input of the over-current latch low. `ocSetHeld` does the same
    // without the over-voltage (U13's output shorted low, R13 shorted, U6B or
    // U6C stuck, ...). Either way the latch sets as soon as KA_OK rises, so
    // DRV_EN stays low, A_RB never goes high, OC reads high at rest (the
    // latch needs KA_OK to hold) and low at the first edges of step 3.
    bool ocSetHeld = false;
    static constexpr int32_t kU13TripMv = 4040;
    bool u13Acting() const { return ocSetHeld || v3v3Mv >= kU13TripMv; }
    int32_t restLevelMv() const { return static_cast<int32_t>(v3v3Mv * 0.2385 + 0.5); }
    int32_t vinMon() const { return !acPresent || rectifierOpen ? 0 : vinMonMv; }
    bool latchStuck = false;     // the latch does not clear when detector A lets go
    bool relayWelded = false;    // K1 contact welded, JP1 bridged or Q1 shorted
    int32_t shortedOutputMa = 0; // a shorted output device: current whenever K1 is closed
    std::array<int32_t, 9> valveMa{300, 300, 300, 300, 300, 300, 300, 300, 250}; // zones, master
    int32_t noiseMa = 0;
    bool expanderIgnoresReset = false; // DRV_EN stuck high: the expander answers with no keep-alive
    bool expanderNeverActive = false;  // DRV_EN stuck low or RESET held: the expander never answers
    bool q3Shorted = false;            // V_DRV on whatever DRV_EN_N does (Q3, or GPIO42 driven high)
    bool vDrvNeverOn = false;          // V_DRV, or its read-back on GPIO42, never high
    bool r12Open = false;              // Q1's gate keeps its charge when RLY_ON is released
    // B_RB, GPIO7: forced low (R93 open, read-back shorted to ground, ...).
    bool relayReadbackLow = false;
    // Q2 shorted gate to drain: Q2 sits at its threshold and GPIO7 reads
    // 0.6 times that (hardware design 7.4). 0: no such fault.
    int32_t q2GateDrainMv = 0;
    KaNet kaNet = KaNet::Pin;
    // ToggleWeak: whether the floating pin's swing crosses the input high
    // threshold. With 1 kohm against R33 the review puts it between the
    // thresholds; the detectors follow it either way.
    bool kaWeakReadsHigh = false;
    // The device does not read the pin while it floats (a firmware fault).
    bool floatNotRead = false;
    // The ADC delivers no more windows (rule 19: the measurement is lost).
    bool windowsStopped = false;
    // An output whose triac conducts in one polarity only (0 to 7 zones, 8
    // the master; -1 none): while it is driven the window mean moves by 0.82
    // times the mean-removed rms, as a half-wave sine does (current_sense.hpp).
    int halfWaveOutput = -1;
    // The processor has stopped with interrupts off (test/soak): the timer
    // interrupt no longer runs, so the pin keeps the level it last had and
    // the read-backs are not sampled.
    bool cpuHalted = false;
    // Record every bus transaction in `log`. Long runs turn it off: the log
    // grows by one string per transaction.
    bool logBus = true;
    // Expander pins forced by a fault, per port: they read (and act) low or high.
    std::array<uint8_t, 2> pinStuckLow{0, 0};
    std::array<uint8_t, 2> pinStuckHigh{0, 0};
    // Bus faults: the next n transactions fail like this.
    int nackNext = 0;
    int timeoutNext = 0;
    bool busStuck = false;  // every transaction times out until recover()
    int corruptReadOf = -1; // register whose reads come back with bit 0 flipped
    int recovers = 0;

    // ---- observation ----
    bool latched = false;
    std::array<uint8_t, 2> out{0xFF, 0xFF};
    std::array<uint8_t, 2> cfg{0xFF, 0xFF};
    std::array<uint8_t, 2> pol{0x00, 0x00};
    std::vector<std::string> log; // "w06=c0", "r00", "probe", ...
    int maxValvesLow = 0;
    int writes = 0;
    int pairWrites = 0;
    // The keep-alive waveform while the pin is driven (contract 19.4).
    uint64_t minPhaseMs = UINT64_MAX; // shortest time between two level changes
    uint64_t floatMs = 0;             // total time the pin was an input

    bool kaHigh() const { return kaNetHigh_; }
    bool pinIsInput() const { return pinInput_; }
    // DRV_EN: the expander is out of reset.
    bool drvEn() const { return !expanderNeverActive && ((a.level(kaNetHigh_) && !latched) || expanderIgnoresReset); }
    // V_DRV, the opto LED supply behind Q3, which GPIO42 reads since review A2.
    bool vDrv() const { return q3Shorted || (a.level(kaNetHigh_) && !latched && !vDrvNeverOn); }
    bool driveEnabled() const { return vDrv(); }
    bool expanderActive() const { return drvEn(); }
    bool relayTransistorOn() const { return b.level(kaNetHigh_); }
    bool q1On() const { return q1Gate_; }
    bool relayClosed() const { return relayWelded || (vBuck && acPresent && q1Gate_ && relayTransistorOn()); }
    // The level on GPIO7 (B_RB), mV.
    int32_t relayReadbackMv() const
    {
        if (q2GateDrainMv > 0) {
            return q2GateDrainMv;
        }
        return relayReadbackLow || !vBuck || !acPresent || relayTransistorOn() ? 0 : 3050;
    }
    // Digital read of GPIO7: low under 0.25 VDD, high over 0.75 VDD. A level
    // between them is taken as high, the case only the analogue read finds.
    bool relayReadbackHigh() const { return relayReadbackMv() > 825; }
    // Valves whose opto LED is driven: the pin low and V_DRV on.
    bool valveDriven(std::size_t i) const
    {
        if (i >= 4 && i < 8 && !eightZone) {
            return false;
        }
        const auto bit = i < 8 ? board::kExpanderBits.zone[i] : board::kExpanderBits.master;
        return vDrv() && !pinLevel(bit.port, bit.bit);
    }
    int valvesLow() const
    {
        int n = 0;
        for (std::size_t i = 0; i < 9; ++i) {
            n += valveDriven(i) ? 1 : 0;
        }
        return n;
    }
    int32_t currentMa() const
    {
        if (!acPresent) {
            return 0;
        }
        int32_t ma = noiseMa;
        if (relayClosed()) {
            ma += shortedOutputMa;
            for (std::size_t i = 0; i < 9; ++i) {
                ma += valveDriven(i) ? valveMa[i] : 0;
            }
        }
        return ma;
    }
    // The comparator trips while drive is enabled: drive and relay drop.
    void trip()
    {
        if (a.level(kaNetHigh_)) {
            latched = true;
            update();
        }
    }

    // One keep-alive interrupt period: the guard decides the pin, the
    // detectors follow the net, the guard samples the read-backs. While the
    // pin is an input the interrupt reads it every 1 ms.
    void step5ms(uint32_t periodMs = 5)
    {
        const bool level = cpuHalted ? pinLevel_ : guard_->step(periodMs);
        pinInput_ = cpuHalted ? pinInput_ : guard_->pinIsInput();
        const uint64_t now = clock_.mono;
        ++periods_;
        // The net's own toggling, for a bridge to a net that toggles.
        const bool other = (periods_ & 1u) != 0;
        bool net = level;
        bool reads = false;
        switch (kaNet) {
        case KaNet::Pin:
            net = pinInput_ ? false : level;
            break;
        case KaNet::Disconnected:
            net = false;
            break;
        case KaNet::ToggleStrong:
            net = other;
            reads = other;
            break;
        case KaNet::ToggleWeak:
            net = pinInput_ ? other : level;
            reads = other && kaWeakReadsHigh;
            break;
        case KaNet::HighStrong:
            net = true;
            reads = true;
            break;
        case KaNet::HighWeak:
            net = pinInput_ ? true : level;
            reads = true;
            break;
        }
        if (pinInput_) {
            floatMs += periodMs;
            for (uint32_t i = 0; i < periodMs && !floatNotRead; ++i) {
                if (!cpuHalted) {
                    guard_->sampleFloat(reads && (kaNet != KaNet::ToggleStrong || (i & 1u) == 0));
                }
            }
        } else if (level != pinLevel_) {
            minPhaseMs = std::min<uint64_t>(minPhaseMs, now - pinChangedMs_);
            pinChangedMs_ = now;
            pinLevel_ = level;
        }
        const bool edge = net != kaNetHigh_;
        kaNetHigh_ = net;
        a.step(now, edge, kaNetHigh_);
        b.step(now, edge, kaNetHigh_);
        if (latched && !a.level(kaNetHigh_) && !latchStuck) {
            latched = false; // KA_OK low clears the latch
        }
        if (u13Acting() && a.level(kaNetHigh_)) {
            latched = true; // the set input is held low: the latch sets as KA_OK rises
        }
        update();
        maxValvesLow = std::max(maxValvesLow, valvesLow());
        // Current windows: 100 ms, contiguous. Peak current in the window.
        windowPeak_ = std::max(windowPeak_, currentMa());
        if (now >= windowStartMs_ + 100 && windowsStopped) {
            windowStartMs_ = now; // the ADC delivers nothing (rule 19)
            windowPeak_ = currentMa();
        } else if (now >= windowStartMs_ + 100) {
            CurrentReading r;
            r.seq = ++windowSeq_;
            r.startMs = windowStartMs_;
            r.endMs = now;
            r.valid = true;
            r.ma = windowPeak_;
            r.rawMa = windowPeak_;
            r.relayReadbackMv = relayReadbackMv();
            r.vinMonMv = vinMon();
            r.meanLoMv = restLevelMv() + restLoOffsetMv;
            r.meanHiMv = restLevelMv() + restHiOffsetMv;
            windowDcMv_ = halfWaveOutput >= 0 && valveDriven(static_cast<std::size_t>(halfWaveOutput))
                              ? 0.82 * (windowPeak_ / 1.503)
                              : 0.0;
            window_ = r;
            windowStartMs_ = now;
            windowPeak_ = currentMa();
        }
        // B_RB is pulled up from V_BUCK: it reads low without 24 VAC (7.4).
        if (!cpuHalted) {
            guard_->sample(vDrv(), relayReadbackHigh(), latched);
        }
    }
    const CurrentReading &window() const { return window_; }
    // The latest window as the device's ADC task delivers it: count, sum,
    // sum of squares and extremes per channel, made from the window's
    // current (as an rms around the rest level) and its means.
    CurrentWindowStats windowStats() const
    {
        CurrentWindowStats w;
        w.seq = window_.seq;
        w.startMs = window_.startMs;
        w.endMs = window_.endMs;
        if (window_.seq == 0) {
            return w;
        }
        const auto fill = [](ChannelStats &c, double meanMv, double rmsMv) {
            constexpr uint32_t kN = 500;
            c.n = kN;
            c.sumMv = static_cast<int64_t>(meanMv * kN + 0.5);
            c.sumSqMv = static_cast<uint64_t>((meanMv * meanMv + rmsMv * rmsMv) * kN + 0.5);
            c.minMv = static_cast<int32_t>(meanMv - rmsMv * 1.4142);
            c.maxMv = static_cast<int32_t>(meanMv + rmsMv * 1.4142);
        };
        const double ma = window_.ma < 0 ? 0.0 : window_.ma;
        fill(w.hi, window_.meanHiMv + windowDcMv_, ma / 1.503);
        fill(w.lo, window_.meanLoMv + windowDcMv_ * 1.503 / 7.50, ma / 7.50);
        fill(w.relayRb, window_.relayReadbackMv, 0.0);
        fill(w.vinMon, window_.vinMonMv, 0.0);
        return w;
    }

    // ---- IExpanderBus ----
    hw::BusStatus write(uint8_t r, uint8_t v) override
    {
        if (logBus) {
            log.push_back("w" + hex(r) + "=" + hex(v));
        }
        if (const auto st = fault(); st != hw::BusStatus::Ok) {
            return st;
        }
        ++writes;
        switch (r) {
        case hw::reg::kOutput0:
        case hw::reg::kOutput1:
            out[r - hw::reg::kOutput0] = v;
            break;
        case hw::reg::kPolarity0:
        case hw::reg::kPolarity1:
            pol[r - hw::reg::kPolarity0] = v;
            break;
        case hw::reg::kConfig0:
        case hw::reg::kConfig1:
            cfg[r - hw::reg::kConfig0] = v;
            break;
        default:
            break;
        }
        update();
        maxValvesLow = std::max(maxValvesLow, valvesLow());
        return hw::BusStatus::Ok;
    }
    hw::BusStatus read(uint8_t r, uint8_t &v) override
    {
        if (logBus) {
            log.push_back("r" + hex(r));
        }
        if (const auto st = fault(); st != hw::BusStatus::Ok) {
            return st;
        }
        switch (r) {
        case hw::reg::kInput0:
        case hw::reg::kInput1:
            v = pins(static_cast<uint8_t>(r - hw::reg::kInput0));
            break;
        case hw::reg::kOutput0:
        case hw::reg::kOutput1:
            v = out[r - hw::reg::kOutput0];
            break;
        case hw::reg::kPolarity0:
        case hw::reg::kPolarity1:
            v = pol[r - hw::reg::kPolarity0];
            break;
        default:
            v = cfg[(r - hw::reg::kConfig0) & 1];
            break;
        }
        if (r == corruptReadOf) {
            v ^= 0x01;
        }
        return hw::BusStatus::Ok;
    }
    // One transaction: both registers of a pair change in it, port 1 nine
    // SCL periods after port 0 (TI SCPS202C figure 31). Logged as one entry.
    hw::BusStatus writePair(uint8_t r, uint8_t v0, uint8_t v1) override
    {
        if (logBus) {
            log.push_back("w" + hex(r) + "=" + hex(v0) + "," + hex(v1));
        }
        if (const auto st = fault(); st != hw::BusStatus::Ok) {
            return st;
        }
        ++writes;
        ++pairWrites;
        const auto i = static_cast<std::size_t>((r & 0x06u) == hw::reg::kOutput0   ? 0
                                                : (r & 0x06u) == hw::reg::kConfig0 ? 2
                                                                                   : 1);
        auto &regs = i == 0 ? out : i == 2 ? cfg : pol;
        regs[0] = v0;
        regs[1] = v1;
        update();
        maxValvesLow = std::max(maxValvesLow, valvesLow());
        return hw::BusStatus::Ok;
    }
    hw::BusStatus probe() override
    {
        if (logBus) {
            log.push_back("probe");
        }
        if (busStuck) {
            return hw::BusStatus::Timeout;
        }
        return expanderActive() ? hw::BusStatus::Ok : hw::BusStatus::Nack;
    }
    void recover() override
    {
        ++recovers;
        busStuck = false;
    }

private:
    static std::string hex(unsigned v)
    {
        static const char *d = "0123456789abcdef";
        return std::string{d[(v >> 4) & 15], d[v & 15]};
    }
    static uint8_t relayPort() { return board::kExpanderBits.relay.port; }
    static uint8_t relayMask() { return static_cast<uint8_t>(1u << board::kExpanderBits.relay.bit); }
    hw::BusStatus fault()
    {
        if (busStuck) {
            return hw::BusStatus::Timeout;
        }
        if (timeoutNext > 0) {
            --timeoutNext;
            return hw::BusStatus::Timeout;
        }
        if (!expanderActive()) {
            return hw::BusStatus::Nack;
        }
        if (nackNext > 0) {
            --nackNext;
            return hw::BusStatus::Nack;
        }
        return hw::BusStatus::Ok;
    }
    // The expander in reset while DRV_EN is low; Q1's gate follows RLY_ON,
    // and keeps its charge with R12 open when nothing drives it.
    void update()
    {
        if (!expanderActive()) {
            out = {0xFF, 0xFF};
            cfg = {0xFF, 0xFF};
            pol = {0x00, 0x00};
        }
        const uint8_t rp = relayPort();
        const uint8_t rm = relayMask();
        bool gate = q1Gate_;
        if ((cfg[rp] & rm) == 0) {
            gate = (out[rp] & rm) != 0;
        } else if (!r12Open) {
            gate = false;
        }
        if ((pinStuckHigh[rp] & rm) != 0) {
            gate = true;
        }
        if ((pinStuckLow[rp] & rm) != 0) {
            gate = false;
        }
        q1Gate_ = gate;
    }
    // The physical level of one expander pin.
    bool pinLevel(uint8_t port, uint8_t bit) const { return ((pins(port) >> bit) & 1u) != 0; }
    // Pin levels: driven bits as written; inputs show the pull resistors.
    uint8_t pins(uint8_t port) const
    {
        uint8_t pulled = 0;
        const bool vdrv = vDrv();
        for (const auto &z : board::kExpanderBits.zone) {
            pulled |= z.port == port && vdrv ? static_cast<uint8_t>(1u << z.bit) : 0;
        }
        const auto &m = board::kExpanderBits.master;
        pulled |= m.port == port && vdrv ? static_cast<uint8_t>(1u << m.bit) : 0;
        const auto &v = board::kExpanderBits.variant8ZoneN;
        pulled |= v.port == port && !eightZone ? static_cast<uint8_t>(1u << v.bit) : 0;
        const auto &s = board::kExpanderBits.sensorFaultN;
        pulled |= s.port == port && !sensorFault ? static_cast<uint8_t>(1u << s.bit) : 0;
        // RLY_ON as an input reads the gate of Q1 through R11.
        pulled |= relayPort() == port && q1Gate_ ? relayMask() : 0;
        const auto level = static_cast<uint8_t>((pulled & cfg[port]) | (out[port] & ~cfg[port]));
        return static_cast<uint8_t>((level & ~pinStuckLow[port]) | pinStuckHigh[port]);
    }

    OutputEnableGuard *guard_;
    FakeClock &clock_;
    bool kaNetHigh_ = false;
    bool pinInput_ = false;
    bool pinLevel_ = false;
    uint64_t pinChangedMs_ = 0;
    uint64_t periods_ = 0;
    bool q1Gate_ = false;
    uint64_t windowStartMs_ = 0;
    int32_t windowPeak_ = 0;
    uint32_t windowSeq_ = 0;
    CurrentReading window_{};
    double windowDcMv_ = 0.0;
};

// Detector release times kept in RAM, for tests.
struct FakeReferenceStore final : IDetectorReferenceStore {
    DetectorReference stored{};
    bool have = false;
    int saves = 0;
    int clears = 0;
    bool load(DetectorReference &out) override
    {
        if (!have) {
            return false;
        }
        out = stored;
        return true;
    }
    bool save(const DetectorReference &r) override
    {
        stored = r;
        have = true;
        ++saves;
        return true;
    }
    bool clear() override
    {
        have = false;
        stored = {};
        ++clears;
        return true;
    }
};

// Output stage on the simulated board, stepped in 5 ms periods with a
// control tick every 50 ms.
struct StageRig {
    FakeClock clock;
    OutputEnableGuard guard{OutputEnableMode::Toggle, 500};
    FakeBoard board{guard, clock};
    FakeReferenceStore reference;
    hw::OutputStage stage;
    bool sensorSupplyOn = true;
    bool senseAvailable = true;
    uint16_t idleThresholdMa = 30;
    // The ESP32-S3 die temperature (19.7 item 2: commissioning only at 25 to
    // 50 C). Room temperature unless a test says otherwise.
    bool dieTempKnown = true;
    float dieTempC = 35.0f;
    uint64_t ticks = 0;

    // Typical release times of the simulated board, as commissioning
    // stores them (19.7, the limits of step 9).
    static constexpr DetectorReference kTypicalReference{670, 570};

    // commissioned: the board has stored release times, as every board has
    // after its first start. false: a new board that learns them first.
    explicit StageRig(hw::OutputStageConfig cfg = {}, bool commissioned = true)
        : reference(storeWith(commissioned)), stage(board, guard, clock, revALayout(), cfg, &reference)
    {
        clock.mono = 0;
    }

    static FakeReferenceStore storeWith(bool commissioned)
    {
        FakeReferenceStore s;
        if (commissioned) {
            s.stored = kTypicalReference;
            s.have = true;
        }
        return s;
    }

    StageTickInput input() const
    {
        StageTickInput in;
        in.acPresent = board.acPresent;
        in.current = board.window();
        in.currentSenseAvailable = senseAvailable;
        in.idleThresholdMa = idleThresholdMa;
        in.sensorSupplyOn = sensorSupplyOn;
        in.dieTempKnown = dieTempKnown;
        in.dieTempC = dieTempC;
        return in;
    }
    void tick()
    {
        guard.heartbeat(static_cast<uint32_t>(clock.mono));
        stage.tick(input());
        ++ticks;
    }
    // Runs for ms, in 5 ms interrupt periods with a tick every 50 ms.
    void run(uint64_t ms)
    {
        for (uint64_t t = 0; t < ms; t += 5) {
            clock.advanceMs(5);
            board.step5ms();
            if (clock.mono % 50 == 0) {
                tick();
            }
        }
    }
    // Runs until pred() or the time limit; returns pred().
    template <typename Pred> bool runUntil(Pred pred, uint64_t limitMs)
    {
        for (uint64_t t = 0; t < limitMs && !pred(); t += 5) {
            run(5);
        }
        return pred();
    }
    StagePhase phase() const { return stage.status().phase; }
    HwCheck fault() const { return stage.status().fault; }
};

} // namespace zf::test
