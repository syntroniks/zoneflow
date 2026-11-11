// SPDX-License-Identifier: Apache-2.0
// The ESPHome package's safety glue (firmware/esphome/components/zoneflow/
// supervisor.*) in the soak simulation: the same output stage, zone driver
// and current code as the standalone firmware, under the glue that differs
// from zf::Controller. The ESPHome sprinkler component is played by a small
// timer here: it starts a zone, and stops it when its time is up.
#include <algorithm>
#include <chrono>
#include <memory>
#include <set>
#include <vector>

#include "alloc_count.hpp"
#include "physical_oracle.hpp"
#include "scenario.hpp"
#include "soak.hpp"
#include "schedule_logic.h"
#include "supervisor.h"
#include "zf/time/civil.hpp"
#include "world.hpp"
#include "zf/hw/reference_store.hpp"

namespace zf::soak {
namespace {

class EspClock final : public IClock {
public:
    EspClock(World &w, uint64_t boot) : w_(w), boot_(boot) {}
    UtcSeconds nowUtc() const override { return w_.sys.now(); }
    MonoMs monoMs() const override { return w_.phys - boot_ + 300; }

private:
    World &w_;
    uint64_t boot_;
};

zfe::SupervisorConfig randomSupervisorConfig(Rng &r)
{
    zfe::SupervisorConfig c;
    c.zone_count = static_cast<uint8_t>(r.range(1, 8));
    c.max_concurrent = static_cast<uint8_t>(r.range(1, 2));
    c.master_enabled = r.chance(0.4);
    c.inter_zone_delay_ms = static_cast<uint32_t>(r.range(0, 3000));
    c.min_rest_sec = static_cast<uint32_t>(r.chance(0.7) ? 1800 : r.range(60, 3600));
    for (auto &z : c.zones) {
        z.enabled = !r.chance(0.05);
        z.max_run_sec = static_cast<uint32_t>(r.chance(0.5) ? 3600 : r.range(60, 14400));
    }
    c.stage.pinHighTestEveryRun = r.chance(0.7);
    return c;
}

struct EspDevice {
    EspDevice(World &w, uint64_t boot, const zfe::SupervisorConfig &cfg)
        : clock(w, boot), bus(w), reference(w.nvs), sup(bus, guard, clock, test::revALayout(), cfg, &reference)
    {
    }
    EspClock clock;
    OutputEnableGuard guard{OutputEnableMode::Toggle, 500};
    ObservedBus bus;
    hw::BlobReferenceStore reference;
    zfe::Supervisor sup;
    uint32_t windowSeq = 0;
    uint32_t budgetSeq = 0;
    CurrentWindowStats window{};
    std::array<uint64_t, kMaxZones> stopAt{}; // the sprinkler component's timers (device mono)
};

class EspRunner {
public:
    explicit EspRunner(const Options &o) : w(o.seed, Mode::Esphome), opt(o), scenario(w, o)
    {
        res.seed = o.seed;
        res.mode = o.mode;
        w.tracing = o.trace;
        phys.violation = [this](const char *id, const std::string &d) { violation(id, d); };
    }

    Result run();

private:
    void violation(const char *id, const std::string &d)
    {
        ++res.violationCount;
        w.count(std::string("violation.") + id);
        w.note(std::string("VIOLATION ") + id + ": " + d);
        if (res.violations.size() < 20) {
            res.violations.push_back({id, w.phys, d});
            if (res.violations.size() == 1) {
                res.firstEvents = w.traceTail;
            }
        }
    }
    bool up() const { return dev != nullptr && !halted; }
    void boot();
    void shutdown(const char *why, bool powerLoss);
    void tick();
    FirmwareView view() const;
    void sprinkler();
    void schedule();

    // The zoneflow_schedule component (README: covered since gap 7): its
    // programs as ProgramSpec, the last consumed key of each kept in flash
    // over restarts, evaluated once a second against the system clock (UTC
    // as local time). E8: no start key runs twice; a start whose minute the
    // device ran through with a valid clock runs then.
    struct EspProgram {
        zfe::sched::ProgramSpec spec;
        uint8_t zone = 0;
        uint32_t sec = 60;
        int64_t lastKey = zfe::sched::kNoKey; // the preference
        std::set<int64_t> ran;                 // oracle
        int64_t seenMinute = zfe::sched::kNoKey;
        uint64_t seenAtPhys = 0;
    };
    std::vector<EspProgram> programs;
    uint64_t lastScheduleSec = 0;

    World w;
    Options opt;
    Result res;
    Scenario scenario;
    PhysicalOracle phys;
    zfe::SupervisorConfig cfg;
    std::unique_ptr<EspDevice> dev;
    bool halted = false;
    bool frozen = false;
    bool frozenHigh = false;
    uint64_t lastBeat = 0;
    uint64_t bootAt = 0;
    mutable FirmwareView last_{};
    bool allocReported = false;
};

void EspRunner::boot()
{
    dev = std::make_unique<EspDevice>(w, w.phys, cfg);
    dev->bus.transientRate = scenario.busTransientRate;
    dev->bus.violation = [this](const std::string &id, const std::string &d) { violation(id.c_str(), d); };
    dev->bus.keepAliveStopped = [this] { return dev && dev->guard.request() == KeepAliveRequest::Off; };
    w.board.attach(dev->guard);
    w.board.cpuHalted = false;
    // zoneflow.cpp: the record in RTC_NOINIT memory, if intact.
    RetainedRecord rec;
    const bool restored = w.rtcMem.load(rec);
    if (!restored) {
        phys.budgetLost();
        w.count("boot.retained_lost");
    }
    BudgetRecord budget;
    const RecordLoad budgetLoad = loadBudget(w.nvs, budget, dev->budgetSeq);
    w.count(std::string("boot.budget_") + toString(budgetLoad));
    dev->sup.begin(restored ? &rec : nullptr, budgetLoad, &budget);
    halted = false;
    frozen = false;
    lastBeat = w.phys;
    w.count("boots");
    w.note(std::string("ESPHome boot") + (restored ? "" : ", run budget lost"));
}

void EspRunner::shutdown(const char *why, bool powerLoss)
{
    if (dev) {
        dev->guard.trip();
        dev.reset();
    }
    w.board.attach(w.resetGuard);
    w.board.cpuHalted = false;
    w.nvs.interrupt();
    w.flashBusyUntil = 0;
    if (powerLoss) {
        w.rtcMem.powerLoss(w.rng);
        w.sys.powerLoss();
    } else if (w.rng.chance(0.05)) {
        w.rtcMem.tear(w.rng);
        w.count("retained.torn");
    }
    halted = false;
    frozen = false;
    w.note(std::string("firmware down: ") + why);
    w.count(std::string("shutdown.") + why);
}

FirmwareView EspRunner::view() const
{
    FirmwareView v;
    v.running = up();
    v.halted = halted || (dev == nullptr && w.phys - lastBeat < 4000);
    v.frozen = frozen;
    v.frozenPinHigh = frozenHigh;
    v.off = dev == nullptr;
    v.lastBeatMs = lastBeat;
    if (dev) {
        zf::ZoneDriver &drv = dev->sup.driver();
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            v.commanded[z] = drv.state(static_cast<ZoneIndex>(z)) == ValveState::Open;
            v.maxRunMs[z] = static_cast<uint64_t>(drv.effectiveMaxRunSec(static_cast<ZoneIndex>(z))) * 1000u;
        }
        v.commanded[kMasterOut] = drv.masterOn();
        v.maxConcurrent = drv.config().maxConcurrent;
        v.minRestMs = static_cast<uint64_t>(drv.config().minRestSec) * 1000u;
        v.currentUsable = dev->sup.stage().currentLost == zf::CurrentLoss::None;
        last_ = v;
    } else {
        v.maxRunMs = last_.maxRunMs;
        v.minRestMs = last_.minRestMs;
        v.maxConcurrent = last_.maxConcurrent;
    }
    return v;
}

void EspRunner::tick()
{
    EspDevice &d = *dev;
    const MonoMs m = d.clock.monoMs();
    if (m % kWindowMs == 0) {
        d.window = w.takeWindow(++d.windowSeq, m - kWindowMs, m);
    }
    // The sprinkler component's timers.
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        if (d.stopAt[z] != 0 && m >= d.stopAt[z]) {
            d.stopAt[z] = 0;
            d.sup.stop_zone(static_cast<uint8_t>(z));
        }
    }
    zfe::SupervisorInputs in;
    in.ac_present = w.transformer && w.volts > 0.4;
    in.mains_milli_hz = w.zeroCrossBridged ? 100000 : w.mainsMilliHz;
    in.window = d.window;
    in.current_sense_available = w.adcOk;
    in.sensor_supply_on = true;
    in.flow_lpm = -1.0f;
    in.keepalive_pin_plain = w.kaPinPlain;
    in.die_temp_known = true;
    in.die_temp_c = w.dieTempC;
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t a0 = tickAllocCount();
    gInTick = true;
    d.sup.tick(in);
    gInTick = false;
    const auto ns = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
    res.maxTickNs = std::max(res.maxTickNs, ns);
    if (tickAllocCount() != a0 && !allocReported) {
        allocReported = true;
        violation("S11-alloc-in-tick", "heap allocation in Supervisor::tick()");
    }
    RetainedRecord rec;
    d.sup.save_retained(rec);
    w.rtcMem.save(rec);
    // zoneflow.cpp: the run budget in flash, two copies written alternately
    // and synced from the main loop (zf/app/budget.hpp).
    BudgetRecord budget;
    if (d.sup.budget_due(budget)) {
        budget.seq = ++d.budgetSeq;
        seal(budget);
        const char *key = (budget.seq & 1u) != 0 ? BudgetRecord::kKeyA : BudgetRecord::kKeyB;
        w.nvs.write(key, std::string_view(reinterpret_cast<const char *>(&budget), sizeof budget));
        w.count("flash.writes.budget");
    }
    lastBeat = w.phys;
    ++res.ticks;
    // S5 for the glue: no zone open while locked out, inhibited or without power.
    zf::ZoneDriver &drv = d.sup.driver();
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        if (drv.state(static_cast<ZoneIndex>(z)) != ValveState::Open) {
            continue;
        }
        if (drv.lockedOut(static_cast<ZoneIndex>(z))) {
            violation("S5-locked-out-open", "zone " + std::to_string(z) + " open while locked out");
        }
        if (d.sup.stage().phase != StagePhase::Ready) {
            violation("S5-open-without-stage",
                      "zone " + std::to_string(z) + " open with the stage in " + toString(d.sup.stage().phase));
        }
        if (d.sup.maintenance() || !d.sup.ac_present()) {
            violation("S5-open-inhibited", "zone " + std::to_string(z) + " open in maintenance or without 24 VAC");
        }
    }
}

void EspRunner::schedule()
{
    const UtcSeconds now = w.sys.now();
    if (now < 1'600'000'000 || static_cast<uint64_t>(now) == lastScheduleSec) {
        return; // no valid clock, or this second is done (the component's loop: once a second)
    }
    lastScheduleSec = static_cast<uint64_t>(now);
    const int64_t day = now >= 0 ? now / 86400 : (now - 86399) / 86400;
    const auto minute = static_cast<int>((now - day * 86400) / 60);
    const zf::civil::Date date = zf::civil::civilFromDays(day);
    const zfe::sched::LocalMinute lm =
        zfe::sched::local_minute(date.year, date.month, date.day, minute / 60, minute % 60);
    for (EspProgram &p : programs) {
        const zfe::sched::Decision d = zfe::sched::evaluate(p.spec, lm, 60, p.lastKey);
        w.count("e8.evaluations");
        if (d.missed > 0) {
            w.count("e8.missed", d.missed);
        }
        if (d.run) {
            if (!p.ran.insert(d.start_key).second) {
                violation("E8-duplicate", "ESPHome schedule ran start " + std::to_string(d.start_key) + " twice");
            }
            w.count(d.late ? "e8.started_late" : "e8.started");
            const zf::StartResult res2 = dev->sup.start_zone(p.zone, p.sec);
            if (res2.ok()) {
                dev->stopAt[p.zone] = dev->clock.monoMs() + static_cast<uint64_t>(p.sec) * 1000u;
            }
        }
        // A start minute the device ran through, second by second, with a
        // valid clock, from its first second, runs in it.
        const int64_t key = lm.key();
        if (key != p.seenMinute) {
            const bool contiguous = p.seenMinute == key - 1 && w.phys - p.seenAtPhys <= 1500;
            bool isStart = p.spec.enabled && p.spec.days.matches(lm.day);
            bool startMinute = false;
            for (int16_t s : p.spec.start_minute) {
                startMinute = startMinute || s == lm.minute;
            }
            isStart = isStart && startMinute;
            if (isStart && contiguous && p.ran.count(key) == 0 && !d.run) {
                violation("E8-lost", "ESPHome schedule start " + std::to_string(key) + " came and did not run");
            }
            p.seenMinute = key;
        }
        p.seenAtPhys = w.phys;
        if (p.ran.size() > 4096) {
            p.ran.erase(p.ran.begin()); // bounded: old keys
        }
    }
}

void EspRunner::sprinkler()
{
    if (!up()) {
        return;
    }
    EspDevice &d = *dev;
    Rng &r = w.rng;
    const auto z = static_cast<uint8_t>(r.below(9));
    switch (r.below(8)) {
    case 0:
        d.sup.stop_all();
        for (auto &t : d.stopAt) {
            t = 0;
        }
        break;
    case 1:
        d.sup.clear_faults();
        break;
    case 2:
        d.sup.acknowledge_repair(r.chance(0.7));
        break;
    case 3:
        d.sup.set_maintenance(!d.sup.maintenance());
        break;
    default: {
        // duration 0: "until stopped", the zone's maximum run time.
        const uint32_t sec = r.chance(0.2) ? 0 : static_cast<uint32_t>(logUniform(r, 5, 4 * 3600));
        const zf::StartResult res2 = d.sup.start_zone(z, sec);
        if (res2.ok() && z < kMaxZones && sec != 0) {
            d.stopAt[z] = d.clock.monoMs() + static_cast<uint64_t>(sec) * 1000u;
        }
        break;
    }
    }
}

Result EspRunner::run()
{
    Rng &r = w.rng;
    static constexpr test::DetectorCorner kCorners[] = {
        test::DetectorCorner::Typical, test::DetectorCorner::LatestRelease, test::DetectorCorner::EarliestRelease,
        test::DetectorCorner::SlowCharge};
    w.board.setCorner(kCorners[r.below(4)]);
    w.board.eightZone = !r.chance(0.1);
    cfg = randomSupervisorConfig(r);
    scenario.addWorldFaults([this] {
        scenario.at(w.phys + logUniform(w.rng, 60'000, 86'400'000), "user.clear", [this] {
            if (up()) {
                dev->sup.acknowledge_repair(true);
                dev->sup.clear_faults();
            }
        });
    });
    constexpr double kHour = 3'600'000.0;
    scenario.every("user.run", 1 * kHour, [this] { sprinkler(); });
    for (int i = 0; i < 2; ++i) {
        EspProgram p;
        p.spec.enabled = true;
        const int starts = static_cast<int>(r.range(1, 4));
        for (int s = 0; s < starts; ++s) {
            p.spec.start_minute[static_cast<std::size_t>(s)] = static_cast<int16_t>(r.range(0, 1439));
        }
        p.spec.days = zfe::sched::day_rule(static_cast<zfe::sched::DayChoice>(r.below(zfe::sched::kDayChoiceCount)),
                                           static_cast<uint8_t>(r.range(1, 127)));
        p.zone = static_cast<uint8_t>(r.below(std::max<uint8_t>(cfg.zone_count, 1)));
        p.sec = static_cast<uint32_t>(r.range(30, 1200));
        programs.push_back(p);
    }
    scenario.every("power.cut", 72 * kHour, [this] {
        if (dev) {
            w.transformer = false;
            shutdown("power_cut", true);
            const uint64_t back = (w.phys + logUniform(w.rng, 200, 6ull * 3600 * 1000)) / kTickMs * kTickMs;
            scenario.at(back, "fault.end", [this] { w.transformer = true; });
            bootAt = back + 300;
        }
    });
    scenario.every("fw.restart", 48 * kHour, [this] {
        if (up()) {
            shutdown("software_restart", false);
            bootAt = (w.phys + 1000) / kTickMs * kTickMs;
        }
    });
    scenario.every("fw.freeze", 8 * 24 * kHour, [this] {
        if (up()) {
            halted = true;
            frozen = true;
            frozenHigh = w.board.kaHigh();
            w.board.cpuHalted = true;
            w.count(frozenHigh ? "halt.freeze_high" : "halt.freeze_low");
            scenario.at(w.phys + logUniform(w.rng, 300, 8000) / kIsrMs * kIsrMs, "fw.halt_end", [this] {
                shutdown("watchdog_reset", false);
                bootAt = (w.phys + 300) / kTickMs * kTickMs;
            });
        }
    });
    scenario.every("fw.hang", 6 * 24 * kHour, [this] {
        if (up()) {
            halted = true;
            w.count("halt.hang");
            scenario.at(w.phys + 5000, "fw.halt_end", [this] {
                shutdown("task_watchdog", false);
                bootAt = (w.phys + 300) / kTickMs * kTickMs;
            });
        }
    });
    boot();
    const uint64_t total = static_cast<uint64_t>(opt.days * 86'400'000.0);
    while (w.phys < total) {
        scenario.runDue(w.phys);
        if (!dev && bootAt != 0 && w.phys >= bootAt) {
            bootAt = 0;
            boot();
        }
        w.phys += kIsrMs;
        w.boardClock.mono = w.phys;
        w.board.step5ms();
        w.physics();
        phys.step(w, view());
        if (up() && dev->clock.monoMs() % kTickMs == 0 && w.phys >= w.flashBusyUntil) {
            tick();
            schedule();
        }
        if (opt.stopAtFirst && res.violationCount > 0) {
            break;
        }
    }
    w.count("sim_seconds", w.phys / 1000);
    w.count("s1.max_uncommanded_ms", phys.maxUncommandedMs());
    w.count("s2.max_halt_to_safe_ms", phys.maxHaltToSafeMs());
    res.stats = w.stats;
    res.simDays = static_cast<double>(w.phys) / 86'400'000.0;
    dev.reset();
    return res;
}

} // namespace

Result runEsphome(const Options &opt)
{
    EspRunner r(opt);
    return r.run();
}

} // namespace zf::soak
