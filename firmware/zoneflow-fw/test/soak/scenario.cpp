// SPDX-License-Identifier: Apache-2.0
#include "scenario.hpp"

#include <algorithm>
#include <cmath>

namespace zf::soak {

namespace {
constexpr double kMin = 60'000.0;
constexpr double kHour = 3'600'000.0;
constexpr double kDay = 86'400'000.0;
} // namespace

const std::vector<std::string> &Scenario::kinds()
{
    static const std::vector<std::string> k = {
        "ac.dropout",       "ac.sag",      "ac.high",     "wire.fault",   "board.fault",    "bus.error",  "adc.fail",
        "adc.noise",        "rain",        "leak",        "mains.bridge", "kapin",          "flash.rot",  "rtc",
        "user.display",     "user.rest",   "user.mqtt",   "user.flood",   "user.config",    "user.time",  "user.clear",
        "user.maintenance", "net.wifi",    "net.sntp",    "power.cut",    "power.brownout", "fw.restart", "fw.hang",
        "fw.freeze",        "fw.halt_end", "halt.script", "fault.end",    "user.run",
    };
    return k;
}

Scenario::Scenario(World &w, const Options &opt) : w_(w), opt_(opt) {}

bool Scenario::enabled(const char *kind) const
{
    const std::string k(kind);
    return std::find(opt_.disabledKinds.begin(), opt_.disabledKinds.end(), k) == opt_.disabledKinds.end();
}

void Scenario::at(uint64_t t, const char *kind, std::function<void()> fn)
{
    q_.push(Item{t, seq_++, kind, std::move(fn)});
}

void Scenario::every(const char *kind, double meanMs, std::function<void()> fn)
{
    if (!enabled(kind) || meanMs <= 0.0) {
        return;
    }
    procs_.push_back(std::make_unique<Process>(Process{kind, meanMs, std::move(fn)}));
    schedule(procs_.back().get(), static_cast<uint64_t>(w_.rng.exp(meanMs)));
}

void Scenario::schedule(Process *p, uint64_t afterMs)
{
    const uint64_t t = (w_.phys + std::max<uint64_t>(afterMs, kIsrMs)) / kIsrMs * kIsrMs;
    at(t, p->kind, [this, p] {
        p->fn();
        schedule(p, static_cast<uint64_t>(w_.rng.exp(p->meanMs)));
    });
}

void Scenario::runDue(uint64_t now)
{
    while (!q_.empty() && q_.top().t <= now) {
        Item it = q_.top();
        q_.pop();
        // Shrinking: events past the limit do not happen (the end of a
        // fault already started still does, so the world gets healthy).
        const bool start = std::string(it.kind) != "fault.end" && std::string(it.kind) != "fw.halt_end";
        if (opt_.eventLimitMs != 0 && it.t > opt_.eventLimitMs && start) {
            continue;
        }
        if (!enabled(it.kind) && start) {
            continue;
        }
        w_.count(std::string("event.") + it.kind);
        it.fn();
    }
}

uint64_t Scenario::nextAt() const { return q_.empty() ? UINT64_MAX : q_.top().t; }

uint64_t logUniform(Rng &r, uint64_t lo, uint64_t hi)
{
    const double a = std::log(static_cast<double>(lo));
    const double b = std::log(static_cast<double>(hi));
    return static_cast<uint64_t>(std::exp(a + (b - a) * r.real()));
}

void Scenario::addWorldFaults(std::function<void()> onRepair)
{
    World &w = w_;
    const bool chaos = w.mode == Mode::Chaos || w.mode == Mode::Esphome;
    if (!chaos) {
        return;
    }
    auto end = [this](uint64_t afterMs, std::function<void()> fn) {
        at((w_.phys + std::max<uint64_t>(afterMs, kIsrMs)) / kIsrMs * kIsrMs, "fault.end", std::move(fn));
    };

    every("ac.dropout", 2 * kDay, [&w, end] {
        if (!w.transformer) {
            return;
        }
        const uint64_t d = logUniform(w.rng, 20, 2 * 3600 * 1000);
        w.transformer = false;
        w.note("24 VAC lost for " + std::to_string(d) + " ms");
        end(d, [&w] {
            w.transformer = true;
            w.note("24 VAC back");
        });
    });
    every("ac.sag", 2 * kDay, [&w, end] {
        w.volts = 0.7 + 0.2 * w.rng.real();
        w.note("transformer sags to " + std::to_string(static_cast<int>(w.volts * 24)) + " V");
        end(logUniform(w.rng, 1000, 3 * 3600 * 1000), [&w] { w.volts = 1.0; });
    });
    every("ac.high", 4 * kDay, [&w, end] {
        w.volts = 1.33; // 32 VAC, unloaded transformer at high line (spec section 1)
        end(logUniform(w.rng, 60'000, 12 * 3600 * 1000), [&w] { w.volts = 1.0; });
    });
    every("wire.fault", 1 * kDay, [&w, end] {
        const std::size_t o = w.rng.below(kOutputs);
        if (w.out[o].wire != Wire::Ok) {
            return;
        }
        static constexpr Wire kKinds[] = {Wire::Open, Wire::Short, Wire::Over, Wire::Intermittent, Wire::HalfWave};
        const Wire k = kKinds[w.rng.below(std::size(kKinds))];
        w.out[o].wire = k;
        w.note("output " + std::to_string(o) + " wiring: " + toString(k));
        end(logUniform(w.rng, 60'000, 3 * 86'400'000ull), [&w, o] {
            w.out[o].wire = Wire::Ok;
            w.note("output " + std::to_string(o) + " wiring repaired");
        });
    });
    every("board.fault", 5 * kDay, [this, &w, end, onRepair] {
        if (w.boardFault != BoardFault::None) {
            return;
        }
        const auto f = static_cast<BoardFault>(1 + w.rng.below(static_cast<uint64_t>(BoardFault::Count) - 1));
        w.setBoardFault(f);
        end(logUniform(w.rng, 600'000, 4 * 86'400'000ull), [&w, onRepair] {
            w.setBoardFault(BoardFault::None);
            w.note("board repaired");
            if (onRepair) {
                onRepair();
            }
        });
        (void)this;
    });
    every("bus.error", 12 * kHour, [&w, end] {
        switch (w.rng.below(4)) {
        case 0:
            w.board.nackNext = static_cast<int>(w.rng.range(1, 3));
            w.note("I2C: no acknowledge");
            break;
        case 1:
            w.board.timeoutNext = static_cast<int>(w.rng.range(1, 3));
            w.note("I2C: timeout");
            break;
        case 2:
            w.board.busStuck = true;
            w.note("I2C: bus stuck");
            break;
        default:
            w.board.corruptReadOf = static_cast<int>(w.rng.below(8));
            w.note("I2C: register reads corrupted");
            end(logUniform(w.rng, 100, 60'000), [&w] { w.board.corruptReadOf = -1; });
            break;
        }
    });
    every("adc.fail", 3 * kDay, [&w, end] {
        w.adcOk = false;
        w.note("ADC calibration lost");
        end(logUniform(w.rng, 1000, 86'400'000), [&w] { w.adcOk = true; });
    });
    every("adc.noise", 2 * kDay, [&w] {
        w.noiseMv = 0.3 + w.rng.real() * (w.rng.chance(0.2) ? 15.0 : 3.0);
        w.biasMv = 740.0 + w.rng.real() * 100.0;
    });
    every("rain", 1 * kDay, [this, &w, end] {
        // Wet for hours, with contact bounce at both edges.
        for (int i = 0; i < 6; ++i) {
            at(w.phys + static_cast<uint64_t>(w.rng.range(0, 400)) / kIsrMs * kIsrMs + kIsrMs, "rain",
               [&w] { w.rainContact = !w.rainContact; });
        }
        at(w.phys + 2000, "rain", [&w] { w.rainContact = true; });
        end(logUniform(w.rng, 60'000, 86'400'000), [&w] { w.rainContact = false; });
    });
    every("leak", 5 * kDay, [&w, end] {
        w.leakLpm = 0.2 + w.rng.real() * 5.0;
        w.note("leak");
        end(logUniform(w.rng, 60'000, 6 * 3600 * 1000), [&w] { w.leakLpm = 0.0; });
    });
    every("mains.bridge", 20 * kDay, [&w, end] {
        w.zeroCrossBridged = true;
        end(logUniform(w.rng, 1000, 60'000), [&w] { w.zeroCrossBridged = false; });
    });
    every("kapin", 30 * kDay, [&w, end] {
        w.kaPinPlain = false;
        w.note("keep-alive pin routed to a peripheral");
        end(logUniform(w.rng, 1000, 60'000), [&w] { w.kaPinPlain = true; });
    });
    every("flash.rot", 10 * kDay, [&w] { w.nvs.rot(); });
    every("rtc", 10 * kDay, [&w, end] {
        switch (w.rng.below(3)) {
        case 0:
            w.rtc.answers = false;
            w.note("RTC stops answering");
            end(logUniform(w.rng, 60'000, 5 * 86'400'000ull), [&w] { w.rtc.answers = true; });
            break;
        case 1:
            w.rtc.timeLost = true;
            w.note("RTC lost its time (battery)");
            break;
        default:
            w.rtc.driftPpm = w.rng.normal() * 50.0;
            break;
        }
    });
    if (w.mode == Mode::Chaos || w.mode == Mode::Esphome) {
        busTransientRate = w.rng.chance(0.3) ? 1e-5 : 0.0;
    }
    (void)kMin;
}

const std::vector<std::string> &timeZones()
{
    static const std::vector<std::string> z = {
        "UTC0",
        "CET-1CEST,M3.5.0,M10.5.0/3",
        "GMT0BST,M3.5.0/1,M10.5.0",
        "EST5EDT,M3.2.0,M11.1.0",
        "PST8PDT,M3.2.0,M11.1.0",
        "AEST-10AEDT,M10.1.0,M4.1.0/3",
        "NZST-12NZDT,M9.5.0,M4.1.0/3",
        "<-04>4<-03>,M9.1.6/24,M4.1.6/24",      // Chile: transitions at 24:00
        "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0", // Lord Howe: a 30-minute DST
        "NST3:30NDT,M3.2.0,M11.1.0",
        "IST-5:30",
        "<+0545>-5:45",
        "<-03>3",
    };
    return z;
}

Program randomProgram(Rng &r, uint16_t id, uint8_t zoneCount, int64_t anchorDay)
{
    Program p;
    p.id = id;
    p.name.assign("P" + std::to_string(id));
    p.enabled = !r.chance(0.1);
    p.startCount = static_cast<uint8_t>(r.range(1, 3));
    for (uint8_t i = 0; i < p.startCount; ++i) {
        // Often near midnight and in the DST hours.
        const double u = r.real();
        if (u < 0.25) {
            p.startMinutes[i] = static_cast<uint16_t>(r.range(60, 179)); // 01:00 to 02:59
        } else if (u < 0.35) {
            p.startMinutes[i] = static_cast<uint16_t>(r.chance(0.5) ? r.range(0, 5) : r.range(1434, 1439));
        } else {
            p.startMinutes[i] = static_cast<uint16_t>(r.range(0, 1439));
        }
    }
    switch (r.below(4)) {
    case 0:
        p.days.type = DayRuleType::Weekdays;
        p.days.weekdayMask = static_cast<uint8_t>(r.range(1, 127));
        break;
    case 1:
        p.days.type = DayRuleType::Interval;
        p.days.intervalDays = static_cast<uint16_t>(r.range(1, 4));
        p.days.intervalAnchorDay = static_cast<int32_t>(anchorDay + r.range(-10, 10));
        break;
    case 2:
        p.days.type = DayRuleType::OddDays;
        break;
    default:
        p.days.type = DayRuleType::EvenDays;
        break;
    }
    const int n = static_cast<int>(r.range(1, 4));
    for (int i = 0; i < n; ++i) {
        const auto z = static_cast<std::size_t>(r.below(std::max<uint8_t>(zoneCount, 1)));
        p.durationSec[z] = static_cast<uint32_t>(r.range(30, 1800));
    }
    p.useSeasonalAdjust = r.chance(0.7);
    p.obeySensorInhibit = r.chance(0.8);
    return p;
}

Config randomConfig(Rng &r, Mode mode)
{
    Config c = Config::defaults();
    c.timezone.assign(r.pick(timeZones()));
    c.zoneCount = static_cast<uint8_t>(r.chance(0.8) ? r.range(4, 8) : r.range(1, 8));
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        ZoneConfig &zc = c.zones[z];
        zc.enabled = !r.chance(0.05);
        const double u = r.real();
        zc.maxRunSec = u < 0.5 ? 3600 : u < 0.7 ? 1800 : u < 0.8 ? 600 : static_cast<uint32_t>(r.range(60, 14400));
        zc.defaultRunSec = static_cast<uint32_t>(r.range(60, 1800));
        if (r.chance(0.2)) {
            zc.current = {static_cast<uint16_t>(r.range(40, 200)), static_cast<uint16_t>(r.range(500, 1000))};
        }
    }
    c.master.enabled = r.chance(0.4);
    c.master.preOpenMs = static_cast<uint32_t>(r.chance(0.5) ? 1000 : r.range(0, 5000));
    c.master.postCloseMs = static_cast<uint32_t>(r.chance(0.5) ? 2000 : r.range(0, 10000));
    c.driver.maxConcurrent = static_cast<uint8_t>(r.range(1, 2));
    c.driver.interZoneDelayMs = static_cast<uint32_t>(r.chance(0.5) ? 2000 : r.range(0, 5000));
    c.driver.minRestSec = static_cast<uint32_t>(r.chance(0.7) ? 1800 : r.range(60, 3600));
    // Zero-cross switching (README, "not covered" until gap 7 was closed).
    c.driver.zeroCrossSwitching = r.chance(0.3);
    c.rain.enabled = r.chance(0.5);
    c.rain.contact = r.chance(0.5) ? ContactType::NormallyOpen : ContactType::NormallyClosed;
    c.rain.debounceMs = static_cast<uint32_t>(r.chance(0.7) ? 2000 : r.range(0, 10000));
    c.flow.enabled = r.chance(0.5);
    c.diag.currentEnabled = !r.chance(0.05);
    if (r.chance(0.3)) {
        c.diag.settleMs = static_cast<uint32_t>(r.range(100, 2000));
        c.diag.confirmSamples = static_cast<uint8_t>(r.range(1, 6));
    }
    c.schedule.seasonalPercent = static_cast<uint16_t>(r.chance(0.6) ? 100 : r.range(20, 200));
    c.schedule.missedStartGraceMin = static_cast<uint16_t>(r.chance(0.6) ? 60 : r.range(0, 240));
    c.schedule.pauseTimeoutMin = static_cast<uint16_t>(r.chance(0.7) ? 120 : r.range(1, 240));
    if (mode == Mode::Liveness) {
        // No faults: nothing may refuse a scheduled start.
        c.rain.enabled = false;
        c.master.enabled = r.chance(0.3);
        c.diag.currentEnabled = true;
        c.schedule.seasonalPercent = static_cast<uint16_t>(r.range(50, 150));
        c.driver.minRestSec = 1800;
        for (auto &z : c.zones) {
            z.enabled = true;
            z.maxRunSec = 3600;
            z.current = {80, 700};
        }
        c.schedule.programCount = 0;
        return c;
    }
    const int64_t today = 20400;
    c.schedule.programCount = static_cast<uint8_t>(r.range(0, 6));
    for (uint8_t i = 0; i < c.schedule.programCount; ++i) {
        c.schedule.programs[i] = randomProgram(r, static_cast<uint16_t>(i * 7 + 1 + r.below(5)), c.zoneCount, today);
    }
    // Ids must be unique.
    for (uint8_t i = 0; i < c.schedule.programCount; ++i) {
        c.schedule.programs[i].id = static_cast<uint16_t>(i * 11 + 1 + r.below(10));
    }
    return c;
}

} // namespace zf::soak
