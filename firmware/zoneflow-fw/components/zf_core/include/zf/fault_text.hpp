// SPDX-License-Identifier: Apache-2.0
// The one table of fault code to user text. Every place that names a fault
// to a person takes its words from here or is tested against it:
//
//   display      zf_ui/strings.hpp Fault*Title (test/host/fault_text_test.cpp)
//   web UI       web/src/i18n/en.ts fault_<code> (web/tests/unit/fault-text.test.ts,
//                which reads this file)
//   ESPHome      the "Faults" text sensor and the fault log line
//                (components/zoneflow/zoneflow.cpp uses this table)
//   MQTT, REST   publish the code (first column); the receiver looks up the text
//
// Columns: FaultCode, stable code (REST, MQTT, logs), title, zone title.
// The title is used where no zone is named. The zone title is a printf
// format whose %s is "Zone 3", a zone name or "Master valve"; it is empty for
// faults that never belong to one output. English only; a translation of a
// user interface keeps its own table with the same meaning.
//
// Wording rules (docs/design/display-ui.md section 10): say what happened,
// in the user's words, no codes, no exclamation marks. Titles fit one line
// of the display (Montserrat 28, 320 px).
#pragma once

#include <cstddef>
#include <string_view>

#include "zf/events/event.hpp"
#include "zf/hal.hpp"

// X(code, id, title, zoneTitle)
#define ZF_FAULT_TEXT_TABLE(X)                                                                                         \
    X(ZoneOpenCircuit, "zone_open_circuit", "Valve drew no current", "%s: no current")                                 \
    X(ZoneOverCurrent, "zone_over_current", "Valve current too high", "%s: current too high")                          \
    X(ShortCircuit, "short_circuit", "Short circuit", "%s: short circuit")                                             \
    X(CurrentWhileIdle, "current_while_idle", "Current with all valves off", "")                                       \
    X(AcLost, "ac_lost", "No 24 V AC supply", "")                                                                      \
    X(FlowWithoutZones, "flow_without_zones", "Water flows with all valves off", "")                                   \
    X(ConfigInvalid, "config_invalid", "Settings could not be read", "")                                               \
    X(TimeInvalid, "time_invalid", "Clock not set", "")                                                                \
    X(StorageError, "storage_error", "Settings could not be saved", "")                                                \
    X(MasterLockedOut, "master_locked_out", "Master valve locked", "")                                                 \
    X(CurrentSenseUnavailable, "current_sense_unavailable", "Valve current not measured", "")                          \
    X(OverCurrentTrip, "over_current_trip", "Protection tripped", "%s: protection tripped")                            \
    X(KeepAliveDetector, "keepalive_detector", "Safety circuit failed its test", "")                                   \
    X(OutputExpander, "output_expander", "Valve driver not responding", "")                                            \
    X(RelayWelded, "relay_welded", "Valve relay stuck closed", "")                                                     \
    X(OutputShorted, "output_shorted", "Valve output stuck on", "")                                                    \
    X(TripLatchStuck, "trip_latch_stuck", "Protection did not reset", "")                                              \
    X(SensorSupply, "sensor_supply", "Sensor supply overloaded", "")                                                   \
    X(ZoneDcCurrent, "zone_dc_current", "Valve current lopsided", "%s: valve current lopsided")                        \
    X(MainsSignal, "mains_signal", "Mains signal wrong", "")                                                           \
    X(KeepAliveBridged, "keepalive_bridged", "Safety signal disturbed", "")                                            \
    X(RelayGateBleed, "relay_gate_bleed", "Valve relay driver faulty", "")                                             \
    X(NotCommissioned, "not_commissioned", "Safety timing not learned", "")

namespace zf {

struct FaultText {
    FaultCode code;
    std::string_view id;
    std::string_view title;
    std::string_view zoneTitle; // printf format with one %s; empty: never per output
};

inline constexpr FaultText kFaultTexts[] = {
#define ZF_FAULT_TEXT_ROW(code, id, title, zoneTitle) {FaultCode::code, id, title, zoneTitle},
    ZF_FAULT_TEXT_TABLE(ZF_FAULT_TEXT_ROW)
#undef ZF_FAULT_TEXT_ROW
};

inline constexpr std::size_t kFaultTextCount = sizeof kFaultTexts / sizeof kFaultTexts[0];
static_assert(kFaultTextCount == static_cast<std::size_t>(FaultCode::Count) - 1,
              "every FaultCode except None needs a row in ZF_FAULT_TEXT_TABLE");

// The row for `code`, or nullptr for None and out-of-range values.
constexpr const FaultText *faultText(FaultCode code)
{
    for (const FaultText &t : kFaultTexts) {
        if (t.code == code) {
            return &t;
        }
    }
    return nullptr;
}

// The fault code a failed output-stage check raises (hardware design 19.7).
FaultCode faultCodeFor(HwCheck c);

} // namespace zf
