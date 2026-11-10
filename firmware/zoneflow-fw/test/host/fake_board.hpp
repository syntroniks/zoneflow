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

#include <array>
#include <cstdint>
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
// apart, off `holdMs` after the last edge (`holdHighMs` if the pin stopped
// high). Faults: stuck on, and on while the pin is held high (a shorted pump
// capacitor: C22 for A, C51 for B).
struct FakeDetector {
    uint32_t onMs = 20;
    uint32_t holdMs = 600;
    uint32_t holdHighMs = 900;
    uint32_t maxGapMs = 20;
    bool stuckOn = false;
    bool onWhilePinHigh = false;
    bool neverOn = false;

    bool on = false;
    uint64_t toggleSinceMs = 0;
    bool toggling = false;
    uint64_t lastEdgeMs = 0;
    uint32_t edges = 0;

    void step(uint64_t nowMs, bool edge, bool pinHigh)
    {
        if (edge) {
            if (!toggling || nowMs - lastEdgeMs > maxGapMs) {
                toggling = true;
                toggleSinceMs = nowMs;
                edges = 0;
            }
            ++edges;
            lastEdgeMs = nowMs;
        }
        const uint64_t idle = nowMs - lastEdgeMs;
        if (toggling && idle > maxGapMs) {
            toggling = false;
        }
        // A single edge charges the pump once and never enables (8.4).
        if (toggling && edges >= 3 && nowMs - toggleSinceMs >= onMs && !neverOn) {
            on = true;
        }
        if (on && !toggling && idle >= (pinHigh ? holdHighMs : holdMs)) {
            on = false;
        }
    }
    bool level(bool pinHigh) const { return stuckOn || on || (onWhilePinHigh && pinHigh); }
};

class FakeBoard final : public hw::IExpanderBus {
public:
    FakeBoard(OutputEnableGuard &guard, FakeClock &clock) : guard_(guard), clock_(clock)
    {
        a.onMs = 20;
        b.onMs = 40;
        b.holdMs = 500;
        b.holdHighMs = 800;
    }

    // ---- what the tests set ----
    FakeDetector a; // gate drive, DRV_EN
    FakeDetector b; // relay transistor Q2
    bool eightZone = true;
    bool sensorFault = false;
    bool acPresent = true;
    bool latchStuck = false;     // the latch does not clear when detector A lets go
    bool relayWelded = false;    // K1 contact welded
    int32_t shortedOutputMa = 0; // a shorted output device: current whenever K1 is closed
    std::array<int32_t, 9> valveMa{300, 300, 300, 300, 300, 300, 300, 300, 250}; // zones, master
    int32_t noiseMa = 0;
    bool expanderIgnoresReset = false;
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

    bool kaHigh() const { return kaHigh_; }
    bool driveEnabled() const { return a.level(kaHigh_) && !latched; }
    bool expanderActive() const { return driveEnabled() || expanderIgnoresReset; }
    bool relayTransistorOn() const { return b.level(kaHigh_); }
    bool relayClosed() const
    {
        return relayWelded || (expanderActive() && (cfg[relayPort()] & relayMask()) == 0 &&
                               (out[relayPort()] & relayMask()) != 0 && relayTransistorOn());
    }
    // Valves whose opto LED is driven: bit an output, driven low, V_DRV on.
    bool valveDriven(std::size_t i) const
    {
        const auto bit = i < 8 ? board::kExpanderBits.zone[i] : board::kExpanderBits.master;
        if (i >= 4 && i < 8 && !eightZone) {
            return false;
        }
        const uint8_t m = static_cast<uint8_t>(1u << bit.bit);
        return driveEnabled() && (cfg[bit.port] & m) == 0 && (out[bit.port] & m) == 0;
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
                ma += valveDriven(i) || (relayWelded && valveOutputOn(i)) ? valveMa[i] : 0;
            }
        }
        return ma;
    }
    // The comparator trips while drive is enabled: drive and relay drop.
    void trip()
    {
        if (driveEnabled()) {
            latched = true;
            reset();
        }
    }

    // One keep-alive interrupt period: the guard decides the pin, the
    // detectors follow, the guard samples the read-backs.
    void step5ms(uint32_t periodMs = 5)
    {
        const bool level = guard_.step(periodMs);
        const bool edge = level != kaHigh_;
        kaHigh_ = level;
        const uint64_t now = clock_.mono;
        a.step(now, edge, kaHigh_);
        b.step(now, edge, kaHigh_);
        if (latched && !a.level(kaHigh_) && !latchStuck) {
            latched = false; // KA_OK low clears the latch
        }
        if (!expanderActive()) {
            reset();
        }
        maxValvesLow = std::max(maxValvesLow, valvesLow());
        // Current windows: 100 ms, contiguous. Peak current in the window.
        windowPeak_ = std::max(windowPeak_, currentMa());
        if (now >= windowStartMs_ + 100) {
            CurrentReading r;
            r.seq = ++windowSeq_;
            r.startMs = windowStartMs_;
            r.endMs = now;
            r.valid = true;
            r.ma = windowPeak_;
            r.rawMa = windowPeak_;
            window_ = r;
            windowStartMs_ = now;
            windowPeak_ = currentMa();
        }
        // B_RB is pulled up from V_BUCK: it reads low without 24 VAC (7.4).
        guard_.sample(driveEnabled(), acPresent && !relayTransistorOn(), latched);
    }
    const CurrentReading &window() const { return window_; }

    // ---- IExpanderBus ----
    hw::BusStatus write(uint8_t r, uint8_t v) override
    {
        log.push_back("w" + hex(r) + "=" + hex(v));
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
        maxValvesLow = std::max(maxValvesLow, valvesLow());
        return hw::BusStatus::Ok;
    }
    hw::BusStatus read(uint8_t r, uint8_t &v) override
    {
        log.push_back("r" + hex(r));
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
    hw::BusStatus probe() override
    {
        log.push_back("probe");
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
    bool valveOutputOn(std::size_t i) const
    {
        const auto bit = i < 8 ? board::kExpanderBits.zone[i] : board::kExpanderBits.master;
        const uint8_t m = static_cast<uint8_t>(1u << bit.bit);
        return (cfg[bit.port] & m) == 0 && (out[bit.port] & m) == 0 && driveEnabled();
    }
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
    void reset()
    {
        out = {0xFF, 0xFF};
        cfg = {0xFF, 0xFF};
        pol = {0x00, 0x00};
    }
    // Pin levels: driven bits as written; inputs show the pull resistors.
    uint8_t pins(uint8_t port) const
    {
        uint8_t pulled = 0;
        const bool vdrv = driveEnabled();
        for (const auto &z : board::kExpanderBits.zone) {
            pulled |= z.port == port && vdrv ? static_cast<uint8_t>(1u << z.bit) : 0;
        }
        const auto &m = board::kExpanderBits.master;
        pulled |= m.port == port && vdrv ? static_cast<uint8_t>(1u << m.bit) : 0;
        const auto &v = board::kExpanderBits.variant8ZoneN;
        pulled |= v.port == port && !eightZone ? static_cast<uint8_t>(1u << v.bit) : 0;
        const auto &s = board::kExpanderBits.sensorFaultN;
        pulled |= s.port == port && !sensorFault ? static_cast<uint8_t>(1u << s.bit) : 0;
        return static_cast<uint8_t>((pulled & cfg[port]) | (out[port] & ~cfg[port]));
    }

    OutputEnableGuard &guard_;
    FakeClock &clock_;
    bool kaHigh_ = false;
    uint64_t windowStartMs_ = 0;
    int32_t windowPeak_ = 0;
    uint32_t windowSeq_ = 0;
    CurrentReading window_{};
};

// Output stage on the simulated board, stepped in 5 ms periods with a
// control tick every 50 ms.
struct StageRig {
    FakeClock clock;
    OutputEnableGuard guard{OutputEnableMode::Toggle, 500};
    FakeBoard board{guard, clock};
    hw::OutputStage stage;
    bool sensorSupplyOn = true;
    bool senseAvailable = true;
    uint16_t idleThresholdMa = 30;
    uint64_t ticks = 0;

    explicit StageRig(hw::OutputStageConfig cfg = {}) : stage(board, guard, clock, revALayout(), cfg)
    {
        clock.mono = 0;
    }

    StageTickInput input() const
    {
        StageTickInput in;
        in.acPresent = board.acPresent;
        in.current = board.window();
        in.currentSenseAvailable = senseAvailable;
        in.idleThresholdMa = idleThresholdMa;
        in.sensorSupplyOn = sensorSupplyOn;
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
