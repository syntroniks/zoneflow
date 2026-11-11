// SPDX-License-Identifier: Apache-2.0
// What happens to the controller over a run: a deterministic queue of
// timed events, Poisson processes that keep adding to it, and the world
// faults both rigs share. The rigs add their own processes (users, power,
// firmware halts).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include "soak.hpp"
#include "world.hpp"
#include "zf/config/config.hpp"

namespace zf::soak {

class Scenario {
public:
    Scenario(World &w, const Options &opt);

    // One event at a time. Ties keep their order of scheduling.
    void at(uint64_t t, const char *kind, std::function<void()> fn);
    // A process: fn at exponentially distributed intervals with this mean.
    void every(const char *kind, double meanMs, std::function<void()> fn);
    void runDue(uint64_t now);
    uint64_t nextAt() const;
    bool enabled(const char *kind) const;

    // Faults in the world outside the firmware (wiring, transformer, board,
    // bus, ADC, sensors, flash, RTC), at rates for this mode.
    // `onRepair` runs when a board fault is repaired: the rig has a person
    // acknowledge it.
    void addWorldFaults(std::function<void()> onRepair);

    // Every event class a run can have, for shrinking and the documentation.
    static const std::vector<std::string> &kinds();

    double busTransientRate = 0.0;

private:
    struct Item {
        uint64_t t;
        uint64_t seq;
        const char *kind;
        std::function<void()> fn;
        bool operator>(const Item &o) const { return t != o.t ? t > o.t : seq > o.seq; }
    };
    struct Process {
        const char *kind;
        double meanMs;
        std::function<void()> fn;
    };
    void schedule(Process *p, uint64_t afterMs);
    std::vector<std::unique_ptr<Process>> procs_;
    World &w_;
    const Options &opt_;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q_;
    uint64_t seq_ = 0;
};

// Random but valid configurations and programs.
Config randomConfig(Rng &r, Mode mode);
Program randomProgram(Rng &r, uint16_t id, uint8_t zoneCount, int64_t anchorDay);
// The time zones a run picks from: with and without DST, both hemispheres,
// half-hour offsets and a transition at 24:00.
const std::vector<std::string> &timeZones();
// Log-uniform between lo and hi.
uint64_t logUniform(Rng &r, uint64_t lo, uint64_t hi);

} // namespace zf::soak
