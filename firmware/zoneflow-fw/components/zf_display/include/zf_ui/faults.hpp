// SPDX-License-Identifier: Apache-2.0
// Faults as the display lists them: one entry per zone fault and one per
// system fault, each with a plain-language title and what to check.
#pragma once

#include <string>
#include <vector>

#include "zf/events/state.hpp"
#include "zf_ui/model.hpp"
#include "zf_ui/strings.hpp"

namespace zf::ui {

struct FaultItem {
    FaultCode code = FaultCode::None;
    ZoneIndex zone = kNoZone; // kNoZone: system fault, or the master valve
};

// Zone faults first (zone order), then system faults in FaultCode order.
// A zone-type code in the system mask that no zone carries is the master
// valve's (the core attributes it to no zone).
std::vector<FaultItem> listFaults(const SystemState &state);

// "Zone 3: no current", "No 24 V AC supply".
std::string faultTitle(const FaultItem &item);
// What happened and what to check, as one paragraph.
Str faultBody(FaultCode code);
// Conditions clear when their cause goes away; the rest are latched until
// the user clears them.
bool faultClearsItself(FaultCode code);
// Fault for latched faults, Warn for conditions.
Tone faultTone(FaultCode code);

} // namespace zf::ui
