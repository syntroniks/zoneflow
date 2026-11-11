// SPDX-License-Identifier: Apache-2.0
// System soak simulation: the firmware's framework-free layers, as the
// browser demo links them, run for simulated weeks against the simulated
// rev A board and a model of the world around it, with invariants checked
// on every simulated tick. See README.md in this directory.
//
// Everything here is deterministic from a seed: the same seed and options
// give the same run, event for event.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace zf::soak {

// xoshiro256** seeded through splitmix64. Deterministic on every host.
class Rng {
public:
    explicit Rng(uint64_t seed = 1)
    {
        uint64_t x = seed;
        for (auto &s : s_) {
            x += 0x9E3779B97F4A7C15ull;
            uint64_t z = x;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            s = z ^ (z >> 31);
        }
    }
    uint64_t next()
    {
        const uint64_t r = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return r;
    }
    // [0, n)
    uint64_t below(uint64_t n) { return n == 0 ? 0 : next() % n; }
    // [lo, hi]
    int64_t range(int64_t lo, int64_t hi)
    {
        return lo + static_cast<int64_t>(below(static_cast<uint64_t>(hi - lo) + 1));
    }
    double real() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }
    bool chance(double p) { return real() < p; }
    // Exponential with this mean.
    double exp(double mean);
    double normal();
    template <typename T> const T &pick(const std::vector<T> &v) { return v[below(v.size())]; }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    std::array<uint64_t, 4> s_{};
};

enum class Mode : uint8_t {
    Chaos,    // everything at once: faults, reboots, clock trouble, users
    Liveness, // no faults: every scheduled program must run to completion
    Halt,     // firmware halts at random instants with one board fault present
    Esphome,  // the ESPHome supervisor glue on the same board and world
};
const char *toString(Mode m);
bool parseMode(const std::string &s, Mode &out);

struct Options {
    uint64_t seed = 1;
    Mode mode = Mode::Chaos;
    double days = 7.0;
    // Stop at the first violation (shrinking wants the earliest one).
    bool stopAtFirst = false;
    // Only events before this simulated time are generated (ms; 0 = all).
    // Used to shrink a failing run: the shortest prefix that still fails.
    uint64_t eventLimitMs = 0;
    // Event classes switched off (names from Scenario::kinds()), for shrinking.
    std::vector<std::string> disabledKinds;
    // Print every world event and violation as it happens.
    bool trace = false;
    // Fail a control tick that took longer than this on the host (us; 0 = off).
    uint32_t maxTickUs = 0;
};

struct Violation {
    std::string invariant; // short id: "S1-uncommanded", "S4-budget", ...
    uint64_t atMs = 0;     // simulated time since the start of the run
    std::string detail;
};

struct Result {
    uint64_t seed = 0;
    Mode mode = Mode::Chaos;
    double simDays = 0.0;
    std::vector<Violation> violations; // the first few
    uint64_t violationCount = 0;
    std::map<std::string, uint64_t> stats; // counters: events, faults, checks
    uint64_t maxTickNs = 0;
    uint64_t ticks = 0;
    std::string firstEvents; // trace of the events before the first violation (short)
};

Result runSeed(const Options &opt);
// Each mode's run function.
Result runStandalone(const Options &opt);
Result runEsphome(const Options &opt);

} // namespace zf::soak
