// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "zf/types.hpp"

namespace zf {

// Injected time source. The device implementation reads the system clock
// (set from the RTC or SNTP) and esp_timer; tests use a fake.
class IClock {
public:
    virtual ~IClock() = default;
    // Wall-clock UTC. May jump (SNTP step, RTC set, manual set).
    virtual UtcSeconds nowUtc() const = 0;
    // Monotonic time since boot. Never jumps. Used for all durations.
    virtual MonoMs monoMs() const = 0;
};

} // namespace zf
