// SPDX-License-Identifier: Apache-2.0
#include "zf/app/budget.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include "zf/app/retained.hpp"
#include "zf/config/config_store.hpp"

namespace zf {

namespace {
uint32_t bodyCrc(const BudgetRecord &r) { return crc32(&r, offsetof(BudgetRecord, crc)); }

bool readCopy(IBlobStorage &storage, const char *key, BudgetRecord &out, bool &present)
{
    std::string blob;
    present = storage.read(key, blob);
    if (!present || blob.size() != sizeof(BudgetRecord)) {
        return false;
    }
    std::memcpy(&out, blob.data(), sizeof out);
    return intact(out);
}
} // namespace

void seal(BudgetRecord &r)
{
    r.magic = BudgetRecord::kMagic;
    r.version = BudgetRecord::kVersion;
    r.size = static_cast<uint16_t>(sizeof(BudgetRecord));
    r.reserved = 0;
    r.crc = bodyCrc(r);
}

bool intact(const BudgetRecord &r)
{
    return r.magic == BudgetRecord::kMagic && r.version == BudgetRecord::kVersion &&
           r.size == sizeof(BudgetRecord) && r.crc == bodyCrc(r);
}

const char *toString(RecordLoad l)
{
    switch (l) {
    case RecordLoad::None:
        return "none";
    case RecordLoad::Certain:
        return "certain";
    case RecordLoad::Uncertain:
        return "uncertain";
    case RecordLoad::Lost:
        return "lost";
    }
    return "?";
}

RecordLoad loadBudget(IBlobStorage &storage, BudgetRecord &out, uint32_t &seq)
{
    BudgetRecord a;
    BudgetRecord b;
    bool presentA = false;
    bool presentB = false;
    const bool okA = readCopy(storage, BudgetRecord::kKeyA, a, presentA);
    const bool okB = readCopy(storage, BudgetRecord::kKeyB, b, presentB);
    return chooseBudget(presentA, okA, a, presentB, okB, b, out, seq);
}

RecordLoad chooseBudget(bool presentA, bool okA, const BudgetRecord &a, bool presentB, bool okB,
                        const BudgetRecord &b, BudgetRecord &out, uint32_t &seq)
{
    seq = 0;
    if (!presentA && !presentB) {
        return RecordLoad::None;
    }
    if (!okA && !okB) {
        return RecordLoad::Lost;
    }
    const BudgetRecord &newest = okA && okB ? (a.seq >= b.seq ? a : b) : okA ? a : b;
    out = newest;
    seq = newest.seq;
    // One copy damaged: it may have been the newer write.
    return (okA && okB) || (okA && !presentB) || (okB && !presentA) ? RecordLoad::Certain : RecordLoad::Uncertain;
}

bool BudgetPoster::due(const ZoneDriver &driver, MonoMs now, BudgetRecord &out)
{
    const auto budget = driver.exportBudget();
    const auto open = static_cast<uint16_t>(driver.outputMask() & ((1u << kMaxZones) - 1u));
    uint16_t rested = 0;
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        rested |= budget[z].burstMs == 0 ? static_cast<uint16_t>(1u << z) : 0;
    }
    if (open != openMask_ || rested != restedMask_) {
        dirty_ = true;
        openMask_ = open;
        restedMask_ = rested;
    }
    const bool periodic = open != 0 && now - lastMs_ >= BudgetRecord::kPeriodMs;
    if ((!dirty_ && !periodic) || (posted_ && now - lastMs_ < BudgetRecord::kMinIntervalMs)) {
        return false;
    }
    out = BudgetRecord{};
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        out.burstMs[z] = budget[z].burstMs;
        out.closedForMs[z] = budget[z].closedForMs;
    }
    out.openMask = open;
    posted_ = true;
    dirty_ = false;
    lastMs_ = now;
    return true;
}

std::array<ZoneBudget, kMaxZones> budgetAfterPowerCut(RecordLoad load, const BudgetRecord &r,
                                                      const std::array<uint32_t, kMaxZones> &maxRunMs)
{
    std::array<ZoneBudget, kMaxZones> out{};
    if (load == RecordLoad::None) {
        return out; // nothing has ever run on this flash
    }
    for (std::size_t z = 0; z < kMaxZones; ++z) {
        if (load == RecordLoad::Lost) {
            out[z] = {maxRunMs[z], 0}; // the worst case: used up, must rest
            continue;
        }
        const bool open = (r.openMask & (1u << z)) != 0;
        // What may have run after the write: up to the next periodic write
        // while open, or a start right after it that the rate limit kept
        // from being written. An uncertain read lost one more write.
        uint64_t lost = open ? BudgetRecord::kLostOpenMs : BudgetRecord::kLostClosedMs;
        if (load == RecordLoad::Uncertain) {
            lost += BudgetRecord::kLostOpenMs;
        }
        const uint64_t burst = std::min<uint64_t>(static_cast<uint64_t>(r.burstMs[z]) + lost, maxRunMs[z]);
        // A zone that had rested fully at the write and was closed keeps an
        // empty budget plus what a start right after the write could use.
        out[z] = {static_cast<uint32_t>(burst), open || load == RecordLoad::Uncertain ? 0u : r.closedForMs[z]};
    }
    return out;
}

} // namespace zf
