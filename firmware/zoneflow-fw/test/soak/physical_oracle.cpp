// SPDX-License-Identifier: Apache-2.0
#include "physical_oracle.hpp"

#include <algorithm>

namespace zf::soak {

void PhysicalOracle::budgetLost()
{
    for (Budget &b : budget_) {
        if (b.burstMs > 0) {
            b.lost = true;
        }
    }
}

void PhysicalOracle::step(World &w, const FirmwareView &fw, uint64_t dtMs)
{
    const uint64_t now = w.phys;
    auto fail = [&](const char *id, const std::string &d) {
        if (violation) {
            violation(id, d);
        }
    };

    // ---- S2: halts --------------------------------------------------------------------------
    const bool kaHigh = w.board.kaHigh();
    if (fw.halted) {
        const uint64_t since = now - fw.lastBeatMs;
        // The firmware's half: the timer interrupt stops the edges.
        if (!fw.frozen && !fw.off && kaHigh != kaWasHigh_ && since > kEdgeStopMs + kIsrMs && !edgeReported_ &&
            w.board.kaNet == test::KaNet::Pin) {
            edgeReported_ = true;
            fail("S2-edges-after-timeout", "keep-alive edge " + std::to_string(since) + " ms after the last heartbeat");
        }
        // The system's half: the working detector layer cuts the current.
        const bool pinHigh = fw.frozen && fw.frozenPinHigh;
        const bool aWorks = !faultDisablesA(w.boardFault);
        const bool bWorks = !faultDisablesB(w.boardFault);
        uint64_t hold = UINT64_MAX;
        if (aWorks) {
            hold = std::min(hold, pinHigh ? kAReleaseHighMs : kAReleaseLowMs);
        }
        if (bWorks) {
            hold = std::min(hold, pinHigh ? kBReleaseHighMs : kBReleaseLowMs);
        }
        const int on = w.energisedCount();
        if (hold != UINT64_MAX && on > 0) {
            // While halted the valves are only on by the board's inertia.
            maxHaltToSafeMs_ = std::max(maxHaltToSafeMs_, since);
            const uint64_t limit = kEdgeStopMs + hold + 10;
            if (since > limit && !haltReported_) {
                haltReported_ = true;
                fail("S2-halt-contract", std::to_string(on) + " output(s) energised " + std::to_string(since) +
                                             " ms after the last heartbeat (limit " + std::to_string(limit) +
                                             " ms, board fault " + toString(w.boardFault) +
                                             (pinHigh ? ", pin frozen high)" : ")"));
            }
        }
        if (hold == UINT64_MAX) {
            w.count("s2.skipped_double_fault_ms", dtMs);
        }
    } else {
        haltReported_ = false;
        edgeReported_ = false;
    }
    kaWasHigh_ = kaHigh;

    // ---- S1: energised without command, while the firmware runs ---------------------------
    bool anyCommanded = false;
    for (std::size_t o = 0; o < kOutputs; ++o) {
        anyCommanded = anyCommanded || (fw.running && fw.commanded[o] && w.energised(o));
    }
    for (std::size_t o = 0; o < kOutputs; ++o) {
        const bool unc = w.energised(o) && !(fw.running && fw.commanded[o]);
        if (!unc || fw.halted) {
            uncommandedSince_[o] = 0;
            uncommandedReported_[o] = false;
            continue;
        }
        // A failed-short triac waters with any run: the relay feeds every
        // valve common, and its current hides inside the run's window. Spec
        // section 2 rule 6 only promises the relay opens afterwards.
        if (static_cast<int>(o) == w.shortedOutput && anyCommanded) {
            w.count("gap.shorted_output_watering_ms", dtMs);
            uncommandedSince_[o] = 0;
            continue;
        }
        if (uncommandedSince_[o] == 0) {
            uncommandedSince_[o] = now;
        }
        const uint64_t d = now - uncommandedSince_[o] + dtMs;
        maxUncommandedMs_ = std::max(maxUncommandedMs_, d);
        if (d > kUncommandedLimitMs && !uncommandedReported_[o]) {
            uncommandedReported_[o] = true;
            fail("S1-uncommanded", "output " + std::to_string(o) + " energised without a command for " +
                                       std::to_string(d) + " ms (board fault " + toString(w.boardFault) +
                                       (fw.running ? ")" : ", firmware not running)"));
        }
    }

    // ---- S3: concurrency ----------------------------------------------------------------------
    if (fw.running) {
        int zones = 0;
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            zones += fw.commanded[z] && w.energised(z) ? 1 : 0;
        }
        if (zones > fw.maxConcurrent) {
            fail("S3-concurrency", std::to_string(zones) + " zones on, limit " + std::to_string(fw.maxConcurrent));
        }
    }

    // ---- S4: run budget -----------------------------------------------------------------------
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        Budget &b = budget_[z];
        if (static_cast<int>(z) == w.shortedOutput) {
            b = Budget{}; // the firmware cannot switch it off; S1 covers it
            continue;
        }
        if (w.energised(z)) {
            b.burstMs += dtMs;
            b.closedMs = 0;
            b.halted = b.halted || fw.halted || !fw.running;
            const uint64_t limit =
                fw.maxRunMs[z] + kBudgetSlackMs + (b.halted ? kEdgeStopMs + kBReleaseHighMs + 10 : 0);
            if (b.burstMs > limit && !b.reported) {
                b.reported = true;
                if (b.lost) {
                    w.count("gap.budget_lost_over_power_cycle");
                    w.note("zone " + std::to_string(z) + " on for " + std::to_string(b.burstMs) +
                           " ms since its last rest: the budget was lost in a power cycle");
                } else {
                    fail("S4-budget", "zone " + std::to_string(z) + " on for " + std::to_string(b.burstMs) +
                                          " ms without a rest of " + std::to_string(fw.minRestMs) + " ms, maximum " +
                                          std::to_string(fw.maxRunMs[z]) + " ms");
                }
            }
        } else if (b.burstMs > 0) {
            b.closedMs += dtMs;
            if (b.closedMs >= fw.minRestMs) {
                b = Budget{};
            }
        }
    }
}

} // namespace zf::soak
