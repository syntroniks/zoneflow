// SPDX-License-Identifier: Apache-2.0
// Counts heap allocations made through operator new, for the invariants on
// allocation in the control tick and on memory growth (S10, S11).
#pragma once

#include <cstdint>

namespace zf::soak {
uint64_t allocCount();
// Allocations made while gInTick is set.
uint64_t tickAllocCount();
// Bytes allocated through operator new and not yet freed.
uint64_t liveBytes();
// Set around Controller::tick(); with ZF_SOAK_TRAP_ALLOC set, an allocation
// then traps, so a debugger shows where it came from.
extern bool gInTick;
// The harness's own bookkeeping inside a tick (counters, the flash model,
// the bus observer) is not the firmware's heap use.
struct HarnessScope {
    bool saved = gInTick;
    HarnessScope() { gInTick = false; }
    ~HarnessScope() { gInTick = saved; }
};
} // namespace zf::soak
