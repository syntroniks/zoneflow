// SPDX-License-Identifier: Apache-2.0
// State that must survive a software restart but is not worth a flash write:
// each zone's run-time budget, the latched output-stage fault and the
// over-current trips of the last day. The device keeps it in RTC memory
// (RTC_NOINIT), which a software reset, a panic or a watchdog reset keep and
// a power cycle or a brownout lose.
//
// Why: an admin could restart the controller to cut a zone's rest after it
// reached its maximum run time (firmware security residual risk 10), and a
// restart must not clear a fault that needs a person (hardware design 19.7).
//
// Integrity: magic, version, size, a CRC-32 and the CRC's complement. Random
// power-on contents, a half-written record and a record from another
// firmware layout all fail the check and are ignored: the budget then starts
// empty, as after a power cycle.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zf/types.hpp"

namespace zf {

inline constexpr std::size_t kTripsKept = 3; // hardware design 19.8: three trips per output per day

struct RetainedRecord {
    static constexpr uint32_t kMagic = 0x5A465254u; // "ZFRT"
    static constexpr uint16_t kVersion = 1;

    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t size = 0;
    std::array<uint32_t, kMaxZones> burstMs{};
    std::array<uint32_t, kMaxZones> closedForMs{};
    uint8_t hwFault = 0; // HwCheck latched in the output stage
    uint8_t reserved[3] = {};
    // Per output (zones, then the master): how long ago each of the last
    // trips was, in seconds plus one. 0 = no trip.
    std::array<std::array<uint32_t, kTripsKept>, kMaxZones + 1> tripAgeSec{};
    uint32_t crc = 0;
    uint32_t crcInverted = 0;
};

uint32_t crc32(const void *data, std::size_t len, uint32_t crc = 0);

// Fills magic, version, size and both CRCs.
void seal(RetainedRecord &r);
// True when every integrity field matches.
bool intact(const RetainedRecord &r);

class IRetainedStore {
public:
    virtual ~IRetainedStore() = default;
    // False when nothing intact is stored.
    virtual bool load(RetainedRecord &out) = 0;
    virtual void save(const RetainedRecord &r) = 0;
};

} // namespace zf
