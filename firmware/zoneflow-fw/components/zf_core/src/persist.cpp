// SPDX-License-Identifier: Apache-2.0
#include "zf/app/persist.hpp"

#include <cstring>

#include "zf/app/retained.hpp"
#include "zf/config/config_json.hpp"

namespace zf {

namespace {

constexpr char kFrameMagic[4] = {'Z', 'F', 'R', '1'};
constexpr std::size_t kFrameHeader = 12; // magic, seq, crc

uint32_t frameCrc(uint32_t seq, std::string_view json)
{
    return crc32(json.data(), json.size(), crc32(&seq, sizeof seq));
}

// 0: not a framed copy; otherwise the copy's status.
enum class Copy : uint8_t { Absent, Damaged, Valid };

Copy readRuntimeCopy(IBlobStorage &storage, const char *key, SchedulerPersist &out, uint32_t &seq, bool legacyOk)
{
    std::string blob;
    if (!storage.read(key, blob)) {
        return Copy::Absent;
    }
    if (blob.size() >= kFrameHeader && std::memcmp(blob.data(), kFrameMagic, 4) == 0) {
        uint32_t crc = 0;
        std::memcpy(&seq, blob.data() + 4, 4);
        std::memcpy(&crc, blob.data() + 8, 4);
        const std::string_view json(blob.data() + kFrameHeader, blob.size() - kFrameHeader);
        if (crc != frameCrc(seq, json) || !persistFromJson(std::string(json), out)) {
            return Copy::Damaged;
        }
        return Copy::Valid;
    }
    // Plain JSON from a firmware before the frame: only in the first slot.
    seq = 0;
    return legacyOk && persistFromJson(blob, out) ? Copy::Valid : Copy::Damaged;
}

} // namespace

std::string frameRuntime(uint32_t seq, const std::string &json)
{
    std::string out(kFrameHeader, '\0');
    const uint32_t crc = frameCrc(seq, json);
    std::memcpy(out.data(), kFrameMagic, 4);
    std::memcpy(out.data() + 4, &seq, 4);
    std::memcpy(out.data() + 8, &crc, 4);
    out += json;
    return out;
}

RecordLoad loadRuntime(IBlobStorage &storage, SchedulerPersist &out, uint32_t &seq)
{
    SchedulerPersist a;
    SchedulerPersist b;
    uint32_t seqA = 0;
    uint32_t seqB = 0;
    const Copy ca = readRuntimeCopy(storage, ConfigStore::kRuntimeKey, a, seqA, true);
    const Copy cb = readRuntimeCopy(storage, kRuntimeKeyB, b, seqB, false);
    seq = 0;
    if (ca == Copy::Absent && cb == Copy::Absent) {
        return RecordLoad::None;
    }
    if (ca != Copy::Valid && cb != Copy::Valid) {
        return RecordLoad::Lost;
    }
    const bool useA = ca == Copy::Valid && (cb != Copy::Valid || seqA >= seqB);
    out = useA ? a : b;
    seq = useA ? seqA : seqB;
    const bool otherDamaged = useA ? cb == Copy::Damaged : ca == Copy::Damaged;
    return otherDamaged ? RecordLoad::Uncertain : RecordLoad::Certain;
}

bool persistWriteOnce(PersistMailbox &box, IBlobStorage &storage)
{
    bool wrote = false;
    SchedulerPersist state;
    if (box.take(state)) {
        wrote = true;
        const std::string json = persistToJson(state);
        // Odd sequence numbers in `runtime`, even in `runtime_b`: a write cut
        // by a reset damages at most the copy it was writing, the older one.
        const uint32_t seq = box.nextRuntimeSeq();
        const char *key = (seq & 1u) != 0 ? ConfigStore::kRuntimeKey : kRuntimeKeyB;
        box.reportWrite(!json.empty() && storage.write(key, frameRuntime(seq, json)));
    }
    BudgetRecord budget;
    if (box.takeBudget(budget)) {
        wrote = true;
        budget.seq = box.nextBudgetSeq();
        seal(budget);
        const char *key = (budget.seq & 1u) != 0 ? BudgetRecord::kKeyA : BudgetRecord::kKeyB;
        box.reportWrite(storage.write(
            key, std::string_view(reinterpret_cast<const char *>(&budget), sizeof budget)));
    }
    return wrote;
}

} // namespace zf
