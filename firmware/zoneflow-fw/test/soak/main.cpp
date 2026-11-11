// SPDX-License-Identifier: Apache-2.0
// zf_soak: runs the system soak simulation over a range of seeds, in
// parallel processes, and reports what it found. See README.md.
//
//   zf_soak --mode chaos --seeds 1-200 --days 30 --jobs 16
//   zf_soak --mode liveness --seeds 1-12 --days 365 --jobs 12
//   zf_soak --mode halt --seeds 0-269 --days 0.05 --jobs 16
//   zf_soak --mode esphome --seeds 1-64 --days 7 --jobs 16
//   zf_soak --mode chaos --seed 17 --days 30 --shrink     (minimal reproduction)
//   zf_soak --ci                                          (the CI subset)
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "scenario.hpp"
#include "soak.hpp"

namespace zf::soak {

Result runSeed(const Options &opt) { return opt.mode == Mode::Esphome ? runEsphome(opt) : runStandalone(opt); }

} // namespace zf::soak

namespace {

using namespace zf::soak;

struct Batch {
    Mode mode = Mode::Chaos;
    uint64_t first = 1;
    uint64_t last = 1;
    double days = 7.0;
};

std::string escape(const std::string &s)
{
    std::string o;
    for (char c : s) {
        if (c == '\n') {
            o += "\\n";
        } else if (c == '\\') {
            o += "\\\\";
        } else {
            o += c;
        }
    }
    return o;
}

std::string unescape(const std::string &s)
{
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            o += s[i + 1] == 'n' ? '\n' : s[i + 1];
            ++i;
        } else {
            o += s[i];
        }
    }
    return o;
}

void writeResult(std::FILE *f, const Result &r)
{
    std::fprintf(f, "R\t%llu\t%s\t%.6f\t%llu\t%llu\t%llu\n", static_cast<unsigned long long>(r.seed), toString(r.mode),
                 r.simDays, static_cast<unsigned long long>(r.ticks), static_cast<unsigned long long>(r.maxTickNs),
                 static_cast<unsigned long long>(r.violationCount));
    for (const Violation &v : r.violations) {
        std::fprintf(f, "V\t%llu\t%s\t%llu\t%s\n", static_cast<unsigned long long>(r.seed), v.invariant.c_str(),
                     static_cast<unsigned long long>(v.atMs), escape(v.detail).c_str());
    }
    for (const auto &[k, v] : r.stats) {
        std::fprintf(f, "S\t%llu\t%s\t%llu\n", static_cast<unsigned long long>(r.seed), k.c_str(),
                     static_cast<unsigned long long>(v));
    }
    if (!r.firstEvents.empty()) {
        std::fprintf(f, "E\t%llu\t%s\n", static_cast<unsigned long long>(r.seed), escape(r.firstEvents).c_str());
    }
    std::fflush(f);
}

struct Summary {
    std::map<std::string, uint64_t> seeds;
    std::map<std::string, double> days;
    std::map<std::string, uint64_t> ticks;
    std::map<std::string, uint64_t> maxTickNs;
    std::map<std::string, std::map<std::string, std::vector<uint64_t>>> failing; // mode -> invariant -> seeds
    std::map<std::string, std::map<std::string, uint64_t>> stats;                // mode -> key -> sum or max
    std::map<std::string, std::string> firstDetail;                              // "mode/inv" -> one example
    std::map<uint64_t, std::string> traces;
};

void readResults(const std::string &path, Summary &s)
{
    std::ifstream in(path);
    std::string line;
    std::map<uint64_t, std::string> modeOf;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, '\t')) {
            f.push_back(part);
        }
        if (f.empty()) {
            continue;
        }
        if (f[0] == "R" && f.size() >= 7) {
            const uint64_t seed = std::stoull(f[1]);
            modeOf[seed] = f[2];
            s.seeds[f[2]] += 1;
            s.days[f[2]] += std::stod(f[3]);
            s.ticks[f[2]] += std::stoull(f[4]);
            s.maxTickNs[f[2]] = std::max<uint64_t>(s.maxTickNs[f[2]], std::stoull(f[5]));
        } else if (f[0] == "V" && f.size() >= 5) {
            const uint64_t seed = std::stoull(f[1]);
            const std::string &m = modeOf[seed];
            auto &v = s.failing[m][f[2]];
            if (v.empty() || v.back() != seed) {
                v.push_back(seed);
            }
            const std::string key = m + "/" + f[2];
            if (s.firstDetail.count(key) == 0) {
                s.firstDetail[key] =
                    "seed " + f[1] + " at " + std::to_string(std::stoull(f[3]) / 1000) + " s: " + unescape(f[4]);
            }
        } else if (f[0] == "S" && f.size() >= 4) {
            const uint64_t seed = std::stoull(f[1]);
            const std::string &m = modeOf[seed];
            const uint64_t v = std::stoull(f[3]);
            uint64_t &slot = s.stats[m][f[2]];
            slot = f[2].find("max") != std::string::npos ? std::max(slot, v) : slot + v;
        } else if (f[0] == "E" && f.size() >= 3) {
            s.traces[std::stoull(f[1])] = unescape(f[2]);
        }
    }
}

int runBatches(const std::vector<Batch> &batches, Options base, unsigned jobs, const std::string &report,
               bool showTraces)
{
    struct Job {
        Mode mode;
        uint64_t seed;
        double days;
    };
    std::vector<Job> work;
    for (const Batch &b : batches) {
        for (uint64_t s = b.first; s <= b.last; ++s) {
            work.push_back({b.mode, s, b.days});
        }
    }
    char dirTemplate[] = "/tmp/zf_soak_XXXXXX";
    const char *dir = mkdtemp(dirTemplate);
    if (dir == nullptr) {
        std::perror("mkdtemp");
        return 2;
    }
    jobs = std::max(1u, std::min<unsigned>(jobs, static_cast<unsigned>(work.size())));
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<pid_t> pids;
    for (unsigned j = 0; j < jobs; ++j) {
        const pid_t pid = fork();
        if (pid < 0) {
            std::perror("fork");
            return 2;
        }
        if (pid == 0) {
            const std::string path = std::string(dir) + "/job" + std::to_string(j) + ".tsv";
            std::FILE *f = std::fopen(path.c_str(), "w");
            for (std::size_t i = j; i < work.size(); i += jobs) {
                Options o = base;
                o.mode = work[i].mode;
                o.seed = work[i].seed;
                o.days = work[i].days;
                const Result r = runSeed(o);
                writeResult(f, r);
                if (r.violationCount != 0) {
                    std::fprintf(stderr, "  %s seed %llu: %llu violation(s), first %s\n", toString(r.mode),
                                 static_cast<unsigned long long>(r.seed),
                                 static_cast<unsigned long long>(r.violationCount),
                                 r.violations.empty() ? "?" : r.violations.front().invariant.c_str());
                }
            }
            std::fclose(f);
            std::_Exit(0);
        }
        pids.push_back(pid);
    }
    int failedChildren = 0;
    for (pid_t p : pids) {
        int st = 0;
        waitpid(p, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            ++failedChildren;
        }
    }
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    Summary s;
    for (unsigned j = 0; j < jobs; ++j) {
        const std::string path = std::string(dir) + "/job" + std::to_string(j) + ".tsv";
        readResults(path, s);
        std::remove(path.c_str());
    }
    rmdir(dir);

    std::ostringstream out;
    out << "zf_soak summary (" << work.size() << " runs, " << jobs << " processes, " << static_cast<int>(wall)
        << " s wall)\n";
    uint64_t total = 0;
    for (const auto &[mode, n] : s.seeds) {
        out << "  " << mode << ": " << n << " seeds, " << static_cast<uint64_t>(s.days[mode] + 0.5)
            << " simulated days, " << s.ticks[mode] << " control ticks, slowest tick " << s.maxTickNs[mode] / 1000
            << " us\n";
        for (const auto &[inv, seeds] : s.failing[mode]) {
            out << "    VIOLATION " << inv << " in " << seeds.size() << " seed(s):";
            for (std::size_t i = 0; i < seeds.size() && i < 12; ++i) {
                out << " " << seeds[i];
            }
            out << "\n      e.g. " << s.firstDetail[mode + "/" + inv] << "\n";
            total += seeds.size();
        }
        out << "    stats:";
        int col = 0;
        for (const auto &[k, v] : s.stats[mode]) {
            if (k.rfind("event.", 0) == 0 || k.rfind("violation.", 0) == 0) {
                continue;
            }
            out << (col++ % 4 == 0 ? "\n      " : "  ") << k << "=" << v;
        }
        out << "\n";
    }
    if (failedChildren != 0) {
        out << "  " << failedChildren << " worker process(es) crashed (sanitizer report above)\n";
    }
    out << (total == 0 && failedChildren == 0 ? "RESULT: no violation\n" : "RESULT: violations found\n");
    std::fputs(out.str().c_str(), stdout);
    if (showTraces) {
        for (const auto &[seed, t] : s.traces) {
            std::printf("--- seed %llu: events before the first violation ---\n%s",
                        static_cast<unsigned long long>(seed), t.c_str());
        }
    }
    if (!report.empty()) {
        std::ofstream(report) << out.str();
    }
    return total == 0 && failedChildren == 0 ? 0 : 1;
}

// Shrinks one failing run: stop at the first violation, cut events after it,
// then drop every event class the failure does not need.
int shrink(Options o)
{
    o.stopAtFirst = true;
    Result r = runSeed(o);
    if (r.violations.empty()) {
        std::printf("seed %llu: no violation to shrink\n", static_cast<unsigned long long>(o.seed));
        return 0;
    }
    const std::string target = r.violations.front().invariant;
    o.eventLimitMs = r.violations.front().atMs;
    o.days = static_cast<double>(r.violations.front().atMs + 60'000) / 86'400'000.0;
    std::printf("target %s at %.1f s\n", target.c_str(), static_cast<double>(o.eventLimitMs) / 1000.0);
    for (const std::string &k : Scenario::kinds()) {
        Options t = o;
        t.disabledKinds.push_back(k);
        const Result x = runSeed(t);
        if (!x.violations.empty() && x.violations.front().invariant == target) {
            o = t;
            o.eventLimitMs = x.violations.front().atMs;
            o.days = static_cast<double>(x.violations.front().atMs + 60'000) / 86'400'000.0;
            std::printf("  without %-18s still fails at %.1f s\n", k.c_str(),
                        static_cast<double>(o.eventLimitMs) / 1000.0);
        }
    }
    std::printf("\nminimal reproduction:\n  zf_soak --mode %s --seed %llu --days %.6f --event-limit-ms %llu",
                toString(o.mode), static_cast<unsigned long long>(o.seed), o.days,
                static_cast<unsigned long long>(o.eventLimitMs));
    if (!o.disabledKinds.empty()) {
        std::printf(" --disable ");
        for (std::size_t i = 0; i < o.disabledKinds.size(); ++i) {
            std::printf("%s%s", i ? "," : "", o.disabledKinds[i].c_str());
        }
    }
    std::printf(" --trace\n");
    o.trace = true;
    o.stopAtFirst = true;
    std::printf("\ntrace:\n");
    std::fflush(stdout);
    r = runSeed(o);
    for (const Violation &v : r.violations) {
        std::printf("%s at %.3f s: %s\n", v.invariant.c_str(), static_cast<double>(v.atMs) / 1000.0, v.detail.c_str());
    }
    return 1;
}

bool parseRange(const std::string &s, uint64_t &a, uint64_t &b)
{
    const auto dash = s.find('-');
    try {
        if (dash == std::string::npos) {
            a = b = std::stoull(s);
        } else {
            a = std::stoull(s.substr(0, dash));
            b = std::stoull(s.substr(dash + 1));
        }
    } catch (...) {
        return false;
    }
    return a <= b;
}

void usage()
{
    std::fprintf(stderr, "usage: zf_soak [--mode chaos|liveness|halt|esphome] [--seeds A-B | --seed N] [--days D]\n"
                         "               [--jobs N] [--report FILE] [--traces] [--max-tick-us N]\n"
                         "               [--shrink] [--trace] [--event-limit-ms N] [--disable kind,kind]\n"
                         "       zf_soak --ci [--jobs N]     the deterministic subset CI runs\n"
                         "       zf_soak --long [--jobs N]   the long run (hours)\n");
}

} // namespace

int main(int argc, char **argv)
{
    Options base;
    Batch one;
    unsigned jobs = std::max(1u, static_cast<unsigned>(sysconf(_SC_NPROCESSORS_ONLN)));
    bool doShrink = false;
    bool ci = false;
    bool lng = false;
    bool traces = false;
    std::string report;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--mode") {
            if (!parseMode(next(), one.mode)) {
                usage();
                return 2;
            }
        } else if (a == "--seeds" || a == "--seed") {
            if (!parseRange(next(), one.first, one.last)) {
                usage();
                return 2;
            }
        } else if (a == "--days") {
            one.days = std::stod(next());
        } else if (a == "--jobs") {
            jobs = static_cast<unsigned>(std::stoul(next()));
        } else if (a == "--report") {
            report = next();
        } else if (a == "--traces") {
            traces = true;
        } else if (a == "--max-tick-us") {
            base.maxTickUs = static_cast<uint32_t>(std::stoul(next()));
        } else if (a == "--shrink") {
            doShrink = true;
        } else if (a == "--trace") {
            base.trace = true;
        } else if (a == "--event-limit-ms") {
            base.eventLimitMs = std::stoull(next());
        } else if (a == "--disable") {
            std::stringstream ss(next());
            std::string k;
            while (std::getline(ss, k, ',')) {
                base.disabledKinds.push_back(k);
            }
        } else if (a == "--ci") {
            ci = true;
        } else if (a == "--long") {
            lng = true;
        } else {
            usage();
            return 2;
        }
    }
    if (doShrink || (base.trace && one.first == one.last)) {
        Options o = base;
        o.mode = one.mode;
        o.seed = one.first;
        o.days = one.days;
        if (doShrink) {
            return shrink(o);
        }
        const Result r = runSeed(o);
        for (const Violation &v : r.violations) {
            std::printf("%s at %.3f s: %s\n", v.invariant.c_str(), static_cast<double>(v.atMs) / 1000.0,
                        v.detail.c_str());
        }
        return r.violationCount == 0 ? 0 : 1;
    }
    std::vector<Batch> batches;
    if (ci) {
        // A few minutes on a CI runner (4 cores) under the sanitizers.
        batches = {{Mode::Chaos, 1, 24, 2.0},
                   {Mode::Liveness, 1, 2, 45.0},
                   {Mode::Halt, 0, static_cast<uint64_t>(27 * 2 - 1), 0.02},
                   {Mode::Esphome, 1, 4, 1.0}};
    } else if (lng) {
        batches = {{Mode::Chaos, 1, 2000, 14.0},
                   {Mode::Liveness, 1, 32, 365.0},
                   {Mode::Halt, 0, 27 * 40 - 1, 0.05},
                   {Mode::Esphome, 1, 160, 5.0}};
    } else {
        batches = {one};
    }
    return runBatches(batches, base, jobs, report, traces);
}
