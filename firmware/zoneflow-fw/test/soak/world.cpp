// SPDX-License-Identifier: Apache-2.0
#include "world.hpp"

#include "alloc_count.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>

namespace zf::soak {

double Rng::exp(double mean)
{
    double u = real();
    if (u < 1e-12) {
        u = 1e-12;
    }
    return -std::log(u) * mean;
}

double Rng::normal()
{
    double u1 = real();
    const double u2 = real();
    if (u1 < 1e-12) {
        u1 = 1e-12;
    }
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
}

const char *toString(Mode m)
{
    switch (m) {
    case Mode::Chaos:
        return "chaos";
    case Mode::Liveness:
        return "liveness";
    case Mode::Halt:
        return "halt";
    case Mode::Esphome:
        return "esphome";
    }
    return "?";
}

bool parseMode(const std::string &s, Mode &out)
{
    for (Mode m : {Mode::Chaos, Mode::Liveness, Mode::Halt, Mode::Esphome}) {
        if (s == toString(m)) {
            out = m;
            return true;
        }
    }
    return false;
}

const char *toString(Wire w)
{
    switch (w) {
    case Wire::Ok:
        return "ok";
    case Wire::Open:
        return "open";
    case Wire::Short:
        return "short";
    case Wire::Over:
        return "over";
    case Wire::Intermittent:
        return "intermittent";
    case Wire::HalfWave:
        return "half_wave";
    }
    return "?";
}

const char *toString(BoardFault f)
{
    static constexpr const char *kNames[] = {
        "none",
        "a_stuck_on",
        "a_on_pin_high",
        "a_never_on",
        "a_late_release",
        "b_stuck_on",
        "b_on_pin_high",
        "b_never_on",
        "b_late_release",
        "latch_stuck",
        "relay_welded",
        "shorted_output",
        "exp_ignores_rst",
        "exp_never_active",
        "q3_shorted",
        "vdrv_never_on",
        "r12_open",
        "relay_rb_low",
        "q2_gate_drain",
        "ka_disconnected",
        "ka_toggle_strong",
        "ka_toggle_weak",
        "ka_high_strong",
        "ka_high_weak",
        "pin_stuck_low",
        "pin_stuck_high",
        "sensor_fault",
    };
    static_assert(std::size(kNames) == static_cast<std::size_t>(BoardFault::Count));
    const auto i = static_cast<std::size_t>(f);
    return i < std::size(kNames) ? kNames[i] : "?";
}

bool faultDisablesA(BoardFault f)
{
    switch (f) {
    case BoardFault::AStuckOn:
    case BoardFault::AOnPinHigh:
    case BoardFault::ALateRelease:
    case BoardFault::Q3Shorted:
    case BoardFault::ExpIgnoresReset:
    case BoardFault::PinStuckLow:
    case BoardFault::ShortedOutput: // gate drive cannot stop a shorted triac
    case BoardFault::KaToggleStrong:
    case BoardFault::KaHighStrong:
    case BoardFault::KaHighWeak:
    case BoardFault::KaToggleWeak:
        return true;
    default:
        return false;
    }
}

bool faultDisablesB(BoardFault f)
{
    switch (f) {
    case BoardFault::BStuckOn:
    case BoardFault::BOnPinHigh:
    case BoardFault::BLateRelease:
    case BoardFault::RelayWelded:
    case BoardFault::R12Open:
    case BoardFault::KaToggleStrong:
    case BoardFault::KaHighStrong:
    case BoardFault::KaHighWeak:
    case BoardFault::KaToggleWeak:
        return true;
    default:
        return false;
    }
}

int64_t daysFromCivil(int y, unsigned m, unsigned d) { return civil::daysFromCivil(y, m, d); }

// ---- Flash ------------------------------------------------------------------------------------

bool Flash::read(const char *key, std::string &out)
{
    HarnessScope harness;
    const auto it = data.find(key);
    if (it == data.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool Flash::write(const char *key, std::string_view v)
{
    // The NVS driver is not heap the firmware owns: not counted. A write
    // from the control task is reported instead (S11).
    const bool inTick = gInTick;
    HarnessScope harness;
    if (inTick) {
        w_.count(std::string("flash.writes_from_control_task.") + key);
        if (w_.energisedCount() > 0 && w_.onControlTaskWrite) {
            w_.onControlTaskWrite(key);
        }
    }
    if (failRate > 0.0 && w_.rng.chance(failRate)) {
        w_.count("flash.write_failed");
        return false;
    }
    // Duration of the operation: a page write, sometimes a sector erase for
    // garbage collection, rarely the 400 ms worst case (W25Q128JV, firmware
    // architecture section 9 question 2).
    uint64_t ms = static_cast<uint64_t>(w_.rng.range(1, 8));
    const double r = w_.rng.real();
    if (r < 0.01) {
        ms = static_cast<uint64_t>(w_.rng.range(100, 400));
    } else if (r < 0.12) {
        ms = static_cast<uint64_t>(w_.rng.range(30, 60));
    }
    InFlight f;
    f.key = key;
    const auto it = data.find(key);
    f.had = it != data.end();
    if (f.had) {
        f.old = it->second;
    }
    f.untilMs = w_.phys + ms;
    // Earlier writes are complete by now.
    std::erase_if(inflight_, [&](const InFlight &x) { return x.untilMs <= w_.phys; });
    inflight_.push_back(std::move(f));
    data[key] = std::string(v);
    damaged.erase(key);
    w_.flashBusyUntil = std::max(w_.flashBusyUntil, w_.phys + ms);
    w_.count(std::string("flash.writes.") + name_);
    return true;
}

bool Flash::erase(const char *key)
{
    HarnessScope harness;
    InFlight f;
    f.key = key;
    const auto it = data.find(key);
    if (it == data.end()) {
        return false;
    }
    f.had = true;
    f.old = it->second;
    f.untilMs = w_.phys + 5;
    inflight_.push_back(std::move(f));
    data.erase(key);
    w_.flashBusyUntil = std::max(w_.flashBusyUntil, w_.phys + 5);
    return true;
}

void Flash::interrupt()
{
    for (const InFlight &f : inflight_) {
        if (f.untilMs <= w_.phys) {
            continue;
        }
        w_.count("flash.interrupted");
        const double r = w_.rng.real();
        if (r < 0.6) {
            // NVS keeps the old value.
            if (f.had) {
                data[f.key] = f.old;
            } else {
                data.erase(f.key);
            }
            w_.note(std::string("flash write of ") + f.key + " interrupted: old value kept");
        } else if (r < 0.85) {
            w_.note(std::string("flash write of ") + f.key + " interrupted: new value kept");
        } else {
            // Beyond the NVS guarantee: a torn or damaged record.
            auto it = data.find(f.key);
            if (it != data.end() && !it->second.empty()) {
                std::string &v = it->second;
                if (w_.rng.chance(0.5)) {
                    v.resize(w_.rng.below(v.size()));
                } else {
                    for (int i = 0; i < 4; ++i) {
                        v[w_.rng.below(v.size())] ^= static_cast<char>(1u << w_.rng.below(8));
                    }
                }
                w_.count("flash.torn");
                damaged.insert(f.key);
                w_.note(std::string("flash write of ") + f.key + " interrupted: record torn");
            }
        }
    }
    inflight_.clear();
}

void Flash::rot()
{
    if (data.empty()) {
        return;
    }
    auto it = data.begin();
    std::advance(it, static_cast<long>(w_.rng.below(data.size())));
    if (it->second.empty()) {
        return;
    }
    it->second[w_.rng.below(it->second.size())] ^= static_cast<char>(1u << w_.rng.below(8));
    w_.count("flash.bit_rot");
    damaged.insert(it->first);
    w_.note("bit rot in " + std::string(name_) + "/" + it->first);
}

std::size_t Flash::bytes() const
{
    std::size_t n = 0;
    for (const auto &[k, v] : data) {
        n += k.size() + v.size();
    }
    return n;
}

// ---- retained memory, clocks ---------------------------------------------------------------

void RetainedMemory::tear(Rng &rng)
{
    // The reset came in the middle of the copy: a prefix of the new record
    // over the old one.
    RetainedRecord mixed = prev;
    const std::size_t n = rng.below(sizeof(RetainedRecord));
    std::memcpy(&mixed, &rec, n);
    rec = mixed;
}

void RetainedMemory::powerLoss(Rng &rng)
{
    auto *p = reinterpret_cast<uint8_t *>(&rec);
    for (std::size_t i = 0; i < sizeof rec; ++i) {
        p[i] = static_cast<uint8_t>(rng.next());
    }
    prev = rec;
}

UtcSeconds SysTime::now() const { return civil::floorDiv(static_cast<int64_t>(w_.phys) + offsetMs, 1000); }

bool SysTime::set(UtcSeconds v)
{
    offsetMs = v * 1000 - static_cast<int64_t>(w_.phys);
    return true;
}

void SysTime::powerLoss() { offsetMs = -static_cast<int64_t>(w_.phys); }

int64_t RtcChip::errorMs() const
{
    return offsetMs + static_cast<int64_t>(driftPpm * 1e-6 * static_cast<double>(w_.phys - refPhys));
}

bool RtcChip::read(UtcSeconds &out)
{
    if (!answers || timeLost) {
        return false;
    }
    out = civil::floorDiv(w_.trueUtcMs() + errorMs(), 1000);
    return true;
}

bool RtcChip::write(UtcSeconds utc)
{
    if (!answers) {
        return false;
    }
    offsetMs = utc * 1000 - w_.trueUtcMs();
    refPhys = w_.phys;
    timeLost = false;
    return true;
}

// ---- world -------------------------------------------------------------------------------------

World::World(uint64_t seed, Mode m) : rng(seed), mode(m)
{
    board.logBus = false;
    boardClock.mono = 0;
    // Somewhere in 2026 or 2027, at a random time of day.
    startUtcMs = (static_cast<int64_t>(daysFromCivil(2026, 1, 1)) + rng.range(0, 700)) * 86400000ll +
                 rng.range(0, 86399) * 1000ll;
    mainsMilliHz = rng.chance(0.5) ? 50000 : 60000;
    tripPeakMa = static_cast<int32_t>(rng.range(4000, 5400)); // hardware design 19.3 item 12
    noiseMv = 0.3 + rng.real() * 3.0;
    biasMv = 760.0 + rng.real() * 60.0;
    meterPulsesPerLitre = 450.0;
    for (std::size_t o = 0; o < kOutputs; ++o) {
        out[o].holdMa = static_cast<int32_t>(rng.range(150, 450));
        out[o].lpm = static_cast<float>(3.0 + rng.real() * 20.0);
    }
    rtc.driftPpm = rng.normal() * 20.0;
    rtc.offsetMs = 0;
    sys.offsetMs = startUtcMs;
}

void World::setBoardFault(BoardFault f)
{
    test::FakeBoard &b = board;
    // Back to a healthy board first, keeping the detector corner.
    b.a.stuckOn = b.a.onWhilePinHigh = b.a.neverOn = false;
    b.b.stuckOn = b.b.onWhilePinHigh = b.b.neverOn = false;
    if (boardFault == BoardFault::ALateRelease || boardFault == BoardFault::BLateRelease) {
        b.setCorner(test::DetectorCorner::Typical);
    }
    b.latchStuck = false;
    b.relayWelded = false;
    b.expanderIgnoresReset = b.expanderNeverActive = false;
    b.q3Shorted = b.vDrvNeverOn = b.r12Open = false;
    b.relayReadbackLow = false;
    b.q2GateDrainMv = 0;
    b.kaNet = test::KaNet::Pin;
    b.pinStuckLow = {0, 0};
    b.pinStuckHigh = {0, 0};
    b.sensorFault = false;
    shortedOutput = -1;

    boardFault = f;
    auto valveBit = [&](std::size_t o) {
        return o < kMaxZones ? board::kExpanderBits.zone[o] : board::kExpanderBits.master;
    };
    switch (f) {
    case BoardFault::None:
    case BoardFault::Count:
        break;
    case BoardFault::AStuckOn:
        b.a.stuckOn = true;
        break;
    case BoardFault::AOnPinHigh:
        b.a.onWhilePinHigh = true;
        break;
    case BoardFault::ANeverOn:
        b.a.neverOn = true;
        break;
    case BoardFault::ALateRelease:
        b.a.holdMs = 1700;
        break;
    case BoardFault::BStuckOn:
        b.b.stuckOn = true;
        break;
    case BoardFault::BOnPinHigh:
        b.b.onWhilePinHigh = true;
        break;
    case BoardFault::BNeverOn:
        b.b.neverOn = true;
        break;
    case BoardFault::BLateRelease:
        b.b.holdMs = 2000;
        break;
    case BoardFault::LatchStuck:
        b.latchStuck = true;
        break;
    case BoardFault::RelayWelded:
        b.relayWelded = true;
        break;
    case BoardFault::ShortedOutput:
        shortedOutput = static_cast<int>(rng.below(kOutputs));
        break;
    case BoardFault::ExpIgnoresReset:
        b.expanderIgnoresReset = true;
        break;
    case BoardFault::ExpNeverActive:
        b.expanderNeverActive = true;
        break;
    case BoardFault::Q3Shorted:
        b.q3Shorted = true;
        break;
    case BoardFault::VDrvNeverOn:
        b.vDrvNeverOn = true;
        break;
    case BoardFault::R12Open:
        b.r12Open = true;
        break;
    case BoardFault::RelayRbLow:
        b.relayReadbackLow = true;
        break;
    case BoardFault::Q2GateDrain:
        b.q2GateDrainMv = 600;
        break;
    case BoardFault::KaDisconnected:
        b.kaNet = test::KaNet::Disconnected;
        break;
    case BoardFault::KaToggleStrong:
        b.kaNet = test::KaNet::ToggleStrong;
        break;
    case BoardFault::KaToggleWeak:
        b.kaNet = test::KaNet::ToggleWeak;
        break;
    case BoardFault::KaHighStrong:
        b.kaNet = test::KaNet::HighStrong;
        break;
    case BoardFault::KaHighWeak:
        b.kaNet = test::KaNet::HighWeak;
        break;
    case BoardFault::PinStuckLow: {
        const auto bit = valveBit(rng.below(kOutputs));
        b.pinStuckLow[bit.port] = static_cast<uint8_t>(1u << bit.bit);
        break;
    }
    case BoardFault::PinStuckHigh: {
        const auto bit = valveBit(rng.below(kOutputs));
        b.pinStuckHigh[bit.port] = static_cast<uint8_t>(1u << bit.bit);
        break;
    }
    case BoardFault::SensorFault:
        b.sensorFault = true;
        break;
    }
    count(std::string("board_fault.") + toString(f));
    note(std::string("board fault: ") + toString(f) +
         (shortedOutput >= 0 ? " on output " + std::to_string(shortedOutput) : std::string()));
}

int World::energisedCount() const
{
    int n = 0;
    for (const Output &o : out) {
        n += o.energised ? 1 : 0;
    }
    return n;
}

void World::physics(uint64_t dtMs)
{
    board.acPresent = transformer;
    const bool relay = board.relayClosed();
    double full = 0.0;
    double half = 0.0;
    double lpm = leakLpm;
    for (std::size_t o = 0; o < kOutputs; ++o) {
        Output &x = out[o];
        const bool driven = board.valveDriven(o) || static_cast<int>(o) == shortedOutput;
        const bool on = transformer && relay && driven;
        if (on && !x.energised) {
            x.energisedSinceMs = phys;
        }
        x.energised = on;
        if (!on) {
            continue;
        }
        if (x.wire == Wire::Intermittent && phys >= x.nextFlipMs) {
            x.intermittentOn = !x.intermittentOn;
            x.nextFlipMs = phys + static_cast<uint64_t>(rng.range(20, 3000));
        }
        // 1.5 A of inrush for 100 ms (system spec section 1), three times
        // the holding current here.
        const bool inrush = phys - x.energisedSinceMs < 100;
        const double hold = x.holdMa * volts * (inrush ? 3.0 : 1.0);
        bool flows = true;
        switch (x.wire) {
        case Wire::Ok:
            full += hold;
            break;
        case Wire::Open:
            flows = false;
            break;
        case Wire::Short:
            full += 6000.0 * volts;
            flows = false;
            break;
        case Wire::Over:
            full += 1250.0 * volts;
            break;
        case Wire::Intermittent:
            full += x.intermittentOn ? hold : 0.0;
            flows = x.intermittentOn;
            break;
        case Wire::HalfWave:
            half += hold;
            break;
        }
        if (flows && o < kMaxZones) {
            lpm += x.lpm;
        }
    }
    currentMa_ = full + half;
    halfMa_ = half;
    // The comparator on the shunt signal: a peak above its threshold removes
    // gate drive and latches, with no firmware involved (hardware design 9).
    if ((full + half) * 1.41421356 >= tripPeakMa) {
        if (!board.latched) {
            board.trip();
            if (board.latched) {
                count("world.comparator_trips");
                note("comparator tripped at " + std::to_string(static_cast<int>(full + half)) + " mA rms");
            }
        }
    }
    flowPulses += lpm / 60.0 * (static_cast<double>(dtMs) / 1000.0) * meterPulsesPerLitre;
    window.sumSq += full * full;
    window.sumHalf += half;
    ++window.periods;
}

World::GridStats World::gridFor(int32_t fullMa, int32_t halfMa)
{
    const GridKey k{fullMa, halfMa};
    const auto it = grid_.find(k);
    if (it != grid_.end()) {
        return it->second;
    }
    // 100 phase points over five mains cycles stand for the 500 samples of a
    // window; the sums are scaled by five. Clipped at the ADC's range.
    GridStats g;
    constexpr int kPoints = 100;
    constexpr double kAdcMaxMv = 1750.0;
    for (int i = 0; i < kPoints; ++i) {
        const double th = 6.283185307179586 * 5.0 * i / kPoints;
        const double s = std::sin(th);
        const double ma = 1.41421356 * fullMa * s + 1.41421356 * 2.0 * halfMa * std::max(s, 0.0);
        const double hi = std::clamp(biasMv + ma / 1.503, 0.0, kAdcMaxMv);
        const double lo = std::clamp(biasMv + ma / 7.50, 0.0, kAdcMaxMv);
        for (int rep = 0; rep < 5; ++rep) {
            g.hi.add(static_cast<int32_t>(std::lround(hi)));
            g.lo.add(static_cast<int32_t>(std::lround(lo)));
        }
    }
    if (grid_.size() > 4096) {
        grid_.clear(); // bounded
    }
    grid_[k] = g;
    return g;
}

CurrentWindowStats World::takeWindow(uint32_t seq, MonoMs startMs, MonoMs endMs)
{
    const double periods = std::max<uint32_t>(window.periods, 1);
    const auto full = static_cast<int32_t>(std::lround(std::sqrt(window.sumSq / periods)));
    const auto half = static_cast<int32_t>(std::lround(window.sumHalf / periods));
    window = WindowAccumulator{};
    GridStats g = gridFor(full / 5 * 5, half / 5 * 5); // 5 mA steps keep the cache small
    // Noise: per-sample Gaussian of noiseMv, folded into the sums.
    auto addNoise = [&](ChannelStats &c) {
        // x = s + e with e ~ N(0, noise): the sum gains E = sum(e), the sum of
        // squares 2 * mean(s) * E + n * noise^2 (the cross term with the
        // waveform itself averages out).
        const double n = c.n;
        const double e = rng.normal() * noiseMv * std::sqrt(n);
        const double mean = c.meanMv();
        c.sumMv += static_cast<int64_t>(std::llround(e));
        c.sumSqMv = static_cast<uint64_t>(
            std::llround(static_cast<double>(c.sumSqMv) + 2.0 * mean * e + n * noiseMv * noiseMv));
        const auto spread = static_cast<int32_t>(std::lround(3.0 * noiseMv));
        c.minMv -= spread;
        c.maxMv += spread;
    };
    addNoise(g.hi);
    addNoise(g.lo);
    CurrentWindowStats w;
    w.seq = seq;
    w.startMs = startMs;
    w.endMs = endMs;
    w.hi = g.hi;
    w.lo = g.lo;
    const int32_t rb = board.relayReadbackMv();
    for (int i = 0; i < 500; ++i) {
        w.relayRb.add(std::min<int32_t>(rb, 1750));
    }
    return w;
}

void World::count(const std::string &k, uint64_t n)
{
    HarnessScope h;
    stats[k] += n;
}

void World::note(const std::string &what)
{
    HarnessScope h;
    char head[48];
    std::snprintf(head, sizeof head, "[%10.3f s] ", static_cast<double>(phys) / 1000.0);
    const std::string line = head + what;
    if (tracing) {
        std::fprintf(stderr, "%s\n", line.c_str());
    }
    traceTail += line;
    traceTail += '\n';
    if (traceTail.size() > 6000) {
        traceTail.erase(0, traceTail.size() - 4000);
    }
}

// ---- observed bus --------------------------------------------------------------------------

uint16_t ObservedBus::drivenWithRelay() const
{
    const test::FakeBoard &b = w_.board;
    if (!b.expanderActive()) {
        return 0;
    }
    uint16_t m = 0;
    for (std::size_t o = 0; o < kOutputs; ++o) {
        const auto bit = o < kMaxZones ? board::kExpanderBits.zone[o] : board::kExpanderBits.master;
        const uint8_t mask = static_cast<uint8_t>(1u << bit.bit);
        if ((b.cfg[bit.port] & mask) == 0 && (b.out[bit.port] & mask) == 0) {
            m |= static_cast<uint16_t>(1u << o);
        }
    }
    return m;
}

hw::BusStatus ObservedBus::write(uint8_t reg, uint8_t value)
{
    HarnessScope harness;
    if (transientRate > 0.0 && w_.rng.chance(transientRate)) {
        w_.count("bus.transient_error");
        return w_.rng.chance(0.5) ? hw::BusStatus::Nack : hw::BusStatus::Timeout;
    }
    const uint16_t before = drivenWithRelay();
    const bool relayBefore = w_.board.q1On();
    const hw::BusStatus st = w_.board.write(reg, value);
    const uint16_t after = drivenWithRelay();
    const bool relayAfter = w_.board.q1On();
    const uint64_t now = w_.phys;
    auto fail = [&](const char *id, const std::string &d) {
        HarnessScope h;
        if (violation) {
            violation(id, d);
        }
    };
    // Hardware design 19.1 rule 3: never more than three valve bits low.
    if (std::popcount(after) > 3) {
        fail("S3-valve-bits", "firmware drives " + std::to_string(std::popcount(after)) + " valve bits low");
    }
    if (relayAfter && !relayBefore) {
        relayOnSinceMs_ = now;
        // Rule 4: the relay closes before the first valve, never onto one.
        if (after != 0) {
            fail("S6-relay-onto-valve", "relay closed with a valve already driven");
        }
    }
    if (relayAfter) {
        const uint16_t newlyOn = static_cast<uint16_t>(after & ~before);
        for (std::size_t o = 0; o < kOutputs; ++o) {
            if ((newlyOn & (1u << o)) == 0) {
                continue;
            }
            // Rule 4: relay high 20 ms before the first valve.
            if (now - relayOnSinceMs_ < 20) {
                fail("S6-relay-sequence", "output " + std::to_string(o) + " on " +
                                              std::to_string(now - relayOnSinceMs_) + " ms after the relay");
            }
            // Rule 10: no two outputs switched on within 100 ms.
            if (anyValveOn_ && now - lastValveOnMs_ < 100) {
                fail("S6-stagger", "output " + std::to_string(o) + " on " + std::to_string(now - lastValveOnMs_) +
                                       " ms after the previous one");
            }
            anyValveOn_ = true;
            lastValveOnMs_ = now;
        }
        if (after != 0) {
            lastValveDrivenMs_ = now;
        } else if (before != 0) {
            lastValveDrivenMs_ = now;
        }
    }
    const bool stopping = keepAliveStopped && keepAliveStopped();
    if (stopping && relayBefore && !relayAfter) {
        w_.count("s6.relay_released_in_fault_path");
    }
    if (relayBefore && !relayAfter && w_.boardFault != BoardFault::R12Open && !stopping) {
        // Rule 4: the relay opens 40 ms after the last valve, not onto one.
        if (after != 0) {
            fail("S6-relay-off-under-valve", "relay released with a valve driven");
        } else if (now - lastValveDrivenMs_ < 40 && lastValveDrivenMs_ != 0) {
            fail("S6-relay-off-early",
                 "relay released " + std::to_string(now - lastValveDrivenMs_) + " ms after the last valve");
        }
    }
    lastDriven_ = after;
    relayWasOn_ = relayAfter;
    return st;
}

hw::BusStatus ObservedBus::read(uint8_t reg, uint8_t &value)
{
    HarnessScope harness;
    if (transientRate > 0.0 && w_.rng.chance(transientRate)) {
        w_.count("bus.transient_error");
        return w_.rng.chance(0.5) ? hw::BusStatus::Nack : hw::BusStatus::Timeout;
    }
    return w_.board.read(reg, value);
}

hw::BusStatus ObservedBus::probe() { return w_.board.probe(); }

void ObservedBus::recover() { w_.board.recover(); }

} // namespace zf::soak
