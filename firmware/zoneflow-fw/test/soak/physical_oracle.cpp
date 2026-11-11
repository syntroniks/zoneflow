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
    const bool kaEdge = kaHigh != kaWasHigh_;
    if (kaEdge) {
        kaLastEdgeMs_ = now;
    }
    if (fw.halted) {
        const uint64_t since = now - fw.lastBeatMs;
        // The firmware's half: the timer interrupt stops the edges.
        if (!fw.frozen && !fw.off && kaEdge && since > kEdgeStopMs + kIsrMs && !edgeReported_ &&
            w.board.kaNet == test::KaNet::Pin) {
            edgeReported_ = true;
            fail("S2-edges-after-timeout", "keep-alive edge " + std::to_string(since) + " ms after the last heartbeat");
        }
        // The system's half: the working detector layer cuts the current,
        // its hold-up counted from the last edge on the keep-alive net at the
        // level the net has kept since (hardware design 19.4). A reset after
        // a freeze with the pin high pulls it low: one more edge, which in
        // the board model re-arms the hold-up (counted below).
        const bool pinHigh = kaHigh;
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
            const uint64_t edgeAge = now - std::max(kaLastEdgeMs_, fw.lastBeatMs);
            const uint64_t limit = hold + 10;
            if (kaLastEdgeMs_ > fw.lastBeatMs + kEdgeStopMs + kIsrMs) {
                w.count("s2.edge_after_halt_by_reset");
            }
            if (edgeAge > limit && !haltReported_) {
                haltReported_ = true;
                fail("S2-halt-contract", std::to_string(on) + " output(s) energised " + std::to_string(edgeAge) +
                                             " ms after the last keep-alive edge, " + std::to_string(since) +
                                             " ms after the last heartbeat (limit " + std::to_string(limit) +
                                             " ms, board fault " + toString(w.boardFault) +
                                             (pinHigh ? ", pin high)" : ")"));
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
        if (fw.commanded[o] != lastCommanded_[o]) {
            lastCommanded_[o] = fw.commanded[o];
            lastChangeMs_ = now;
        }
    }
    if (w.shortedOutput < 0 || !anyCommanded || fw.halted ||
        !w.energised(static_cast<std::size_t>(w.shortedOutput))) {
        shortedSince_ = 0;
        shortedReported_ = false;
    }
    for (std::size_t o = 0; o < kOutputs; ++o) {
        const bool unc = w.energised(o) && !(fw.running && fw.commanded[o]);
        if (!unc || fw.halted) {
            uncommandedSince_[o] = 0;
            uncommandedReported_[o] = false;
            continue;
        }
        // A failed-short triac waters with any run: the relay feeds every
        // valve common. The firmware finds it by the rise in current with no
        // output switched and opens the relay (gap 2, closed). Only within
        // the settle time of an output change, or without a usable current
        // measurement, does it water until the run ends: counted.
        if (static_cast<int>(o) == w.shortedOutput && anyCommanded) {
            if (shortedSince_ == 0) {
                shortedSince_ = now;
                shortedBlind_ = now - lastChangeMs_ < kRiseBlindMs || !fw.currentUsable;
            }
            uncommandedSince_[o] = 0;
            if (shortedBlind_ || !fw.currentUsable) {
                w.count("gap.shorted_output_in_settle_or_unmeasured_ms", dtMs);
                continue;
            }
            const uint64_t d = now - shortedSince_ + dtMs;
            w.count("s1.shorted_output_watering_ms", dtMs);
            if (d > kUncommandedLimitMs && !shortedReported_) {
                shortedReported_ = true;
                fail("S1-shorted-output", "failed-short output " + std::to_string(o) + " watered " +
                                              std::to_string(d) + " ms during another output's run");
            }
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
    const bool stopped = !fw.running && wasRunning_;
    wasRunning_ = fw.running;
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        Budget &b = budget_[z];
        if (stopped && b.burstMs > 0) {
            ++b.restarts;
        }
        if (static_cast<int>(z) == w.shortedOutput) {
            b = Budget{}; // the firmware cannot switch it off; S1 covers it
            continue;
        }
        if (w.energised(z) && !(fw.running && !fw.halted && fw.commanded[z])) {
            // A valve still on after the firmware stopped it by stopping the
            // keep-alive (a fault path), or after the firmware halted: the
            // detectors let go within their hold-up, which S1 and S2 bound
            // per episode. The firmware cannot count it; the budget counts
            // commanded time.
            w.count(fw.running && !fw.halted ? "s4.uncommanded_tail_ms" : "s4.halt_tail_ms", dtMs);
            b.closedMs = 0;
        } else if (w.energised(z)) {
            b.burstMs += dtMs;
            b.closedMs = 0;
            // The retained record is saved every tick: a restart loses up to
            // one tick of on-time from the budget.
            const uint64_t limit = fw.maxRunMs[z] + kBudgetSlackMs + b.restarts * (kTickMs + kIsrMs);
            if (b.burstMs > limit && !b.reported) {
                b.reported = true;
                if (b.lost) {
                    // The budget is kept in flash as well since gap 3 was
                    // closed: a power cycle no longer excuses an overrun.
                    fail("S4-budget-power-cycle", "zone " + std::to_string(z) + " on for " +
                                                      std::to_string(b.burstMs) +
                                                      " ms without a rest, across a power cycle or torn record");
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
