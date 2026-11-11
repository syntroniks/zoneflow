// SPDX-License-Identifier: Apache-2.0
// The per-zone run budget (spec section 2 rule 4) kept over a power cut.
//
// RetainedRecord keeps it in RTC memory over a software restart, written
// every control tick. RTC memory is lost on a power cut or a brownout, and a
// reset during its write leaves it torn. BudgetRecord is the copy in flash
// for those cases (firmware architecture section 20, "Run budget over a
// restart and a power cut"; system soak simulation, section 22 gap 3):
//
//  - two copies in NVS (`budget_a`, `budget_b`), written alternately by the
//    persist task, each with a sequence number and a CRC-32, so a write cut
//    by a reset damages at most the copy it was writing;
//  - written when a zone opens or closes, when a zone finishes its rest, and
//    every kPeriodMs while a zone is open, at most once per kMinIntervalMs:
//    a few writes per run, none while nothing runs (flash wear);
//  - read only when the RTC copy is not intact. What was lost since the last
//    write is assumed used: a zone open at the write is charged kLostOpenMs
//    more, every other zone kLostClosedMs, and the time the controller was
//    off counts as no rest;
//  - when copies exist and none can be read, every zone is taken to have
//    used its whole budget and must rest (the worst case). No copy at all
//    (a new board, or flash erased) is an empty budget.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "zf/types.hpp"
#include "zf/zone/zone_driver.hpp"

namespace zf {

class IBlobStorage;

struct BudgetRecord {
    static constexpr uint32_t kMagic = 0x5A464247u; // "ZFBG"
    static constexpr uint16_t kVersion = 1;
    static constexpr uint32_t kPeriodMs = 30000;     // while a zone is open
    static constexpr uint32_t kMinIntervalMs = 1000; // at most one write this often
    // From a post to the flash: the writer polls every 250 ms, and a write
    // may wait for a sector erase.
    static constexpr uint32_t kWriterLatencyMs = 1000;
    // What a power cut may lose of a zone's on-time since the last write.
    static constexpr uint32_t kLostClosedMs = kMinIntervalMs + kWriterLatencyMs;
    static constexpr uint32_t kLostOpenMs = kPeriodMs + kLostClosedMs;
    static constexpr const char *kKeyA = "budget_a";
    static constexpr const char *kKeyB = "budget_b";

    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t size = 0;
    uint32_t seq = 0; // increments per write; odd in budget_a, even in budget_b
    std::array<uint32_t, kMaxZones> burstMs{};
    std::array<uint32_t, kMaxZones> closedForMs{};
    uint16_t openMask = 0; // zones open when it was written
    uint16_t reserved = 0;
    uint32_t crc = 0;
};

void seal(BudgetRecord &r);
bool intact(const BudgetRecord &r);

// How a record with two copies was read.
enum class RecordLoad : uint8_t {
    None,      // no copy stored: a new board or an erased flash
    Certain,   // the newest copy was read (both copies valid, or one copy and no other)
    Uncertain, // one copy valid and the other present but damaged: the damaged one may be newer
    Lost,      // copies present, none valid
};
const char *toString(RecordLoad l);

// Reads both copies. `out` and `seq` are the newest valid copy's.
RecordLoad loadBudget(IBlobStorage &storage, BudgetRecord &out, uint32_t &seq);

// The decision of loadBudget() from two copies read by other means (the
// ESPHome preferences). `present`: the copy exists; `ok`: it is intact.
RecordLoad chooseBudget(bool presentA, bool okA, const BudgetRecord &a, bool presentB, bool okB,
                        const BudgetRecord &b, BudgetRecord &out, uint32_t &seq);

// When to write: on every open, close and finished rest, every kPeriodMs
// while a zone is open, never more often than kMinIntervalMs. Both
// firmwares call due() every control tick; when it returns true, `out`
// holds the budget to store (sequence number and CRC are the writer's).
class BudgetPoster {
public:
    bool due(const ZoneDriver &driver, MonoMs now, BudgetRecord &out);

private:
    uint16_t openMask_ = 0;
    uint16_t restedMask_ = 0xFF;
    MonoMs lastMs_ = 0;
    bool posted_ = false;
    bool dirty_ = false;
};

// The budget to restore from what loadBudget() found, charged with what may
// have been lost since (see the top of this file). `maxRunMs` is each
// zone's limit, for the worst case.
std::array<ZoneBudget, kMaxZones> budgetAfterPowerCut(RecordLoad load, const BudgetRecord &r,
                                                      const std::array<uint32_t, kMaxZones> &maxRunMs);

} // namespace zf
