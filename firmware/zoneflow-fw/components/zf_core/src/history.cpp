// SPDX-License-Identifier: Apache-2.0
#include "zf/history/history.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>

namespace zf {
namespace {

uint32_t crc32(const uint8_t *p, std::size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) {
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        }
    }
    return ~c;
}

void put16(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
}
uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::atomic<HistoryLog *> gLog{nullptr};

} // namespace

const char *toString(HistoryKind k)
{
    switch (k) {
    case HistoryKind::None:
        return "none";
    case HistoryKind::Run:
        return "run";
    case HistoryKind::ProgramStarted:
        return "program_started";
    case HistoryKind::ProgramSkipped:
        return "program_skipped";
    case HistoryKind::ProgramMissed:
        return "program_missed";
    case HistoryKind::RunDropped:
        return "run_dropped";
    case HistoryKind::FaultRaised:
        return "fault_raised";
    case HistoryKind::FaultCleared:
        return "fault_cleared";
    case HistoryKind::Boot:
        return "boot";
    case HistoryKind::TimeSet:
        return "time_set";
    case HistoryKind::ConfigChanged:
        return "config_changed";
    case HistoryKind::RainDelay:
        return "rain_delay";
    case HistoryKind::Sensor:
        return "sensor";
    case HistoryKind::Maintenance:
        return "maintenance";
    case HistoryKind::PauseExpired:
        return "pause_expired";
    case HistoryKind::Ota:
        return "ota";
    case HistoryKind::Count:
        break;
    }
    return "unknown";
}

const char *toString(HistoryResetReason r)
{
    switch (r) {
    case HistoryResetReason::Unknown:
        return "unknown";
    case HistoryResetReason::PowerOn:
        return "power_on";
    case HistoryResetReason::Software:
        return "software";
    case HistoryResetReason::Panic:
        return "panic";
    case HistoryResetReason::Watchdog:
        return "watchdog";
    case HistoryResetReason::Brownout:
        return "brownout";
    case HistoryResetReason::External:
        return "external";
    case HistoryResetReason::DeepSleep:
        return "deep_sleep";
    }
    return "unknown";
}

const char *toString(HistoryOtaStage s)
{
    switch (s) {
    case HistoryOtaStage::Started:
        return "started";
    case HistoryOtaStage::Failed:
        return "failed";
    case HistoryOtaStage::Installed:
        return "installed";
    }
    return "unknown";
}

std::optional<HistoryRecord> historyFromEvent(const Event &e)
{
    HistoryRecord r;
    r.utc = e.utc;
    switch (e.type) {
    case EventType::RunEnded: {
        r.kind = HistoryKind::Run;
        r.zone = e.run.zone;
        r.programId = e.run.programId;
        r.a = static_cast<uint8_t>(e.run.source);
        r.b = static_cast<uint8_t>(e.run.reason);
        r.c = e.run.plannedSec;
        r.d = e.run.actualSec;
        uint32_t dl = kHistoryNoVolume;
        if (e.run.centilitres >= 0) {
            dl = std::min<uint32_t>(static_cast<uint32_t>(e.run.centilitres + 5) / 10u, kHistoryNoVolume - 1u);
        }
        r.e = static_cast<uint32_t>(e.run.currentMa) | (dl << 16);
        return r;
    }
    case EventType::ProgramStarted:
        r.kind = HistoryKind::ProgramStarted;
        r.programId = e.program.programId;
        r.a = static_cast<uint8_t>(e.program.source);
        r.b = e.program.late ? 1 : 0;
        return r;
    case EventType::ProgramSkipped:
        r.kind = HistoryKind::ProgramSkipped;
        r.programId = e.program.programId;
        r.a = static_cast<uint8_t>(e.program.skipReason);
        return r;
    case EventType::ProgramMissed:
        r.kind = HistoryKind::ProgramMissed;
        r.programId = e.program.programId;
        r.d = static_cast<uint32_t>(std::max<UtcSeconds>(0, e.program.scheduledAt));
        return r;
    case EventType::RunDropped:
        r.kind = HistoryKind::RunDropped;
        r.zone = e.dropped.zone;
        r.programId = e.dropped.programId;
        r.a = static_cast<uint8_t>(e.dropped.source);
        r.b = static_cast<uint8_t>(e.dropped.reason);
        return r;
    case EventType::FaultRaised:
    case EventType::FaultCleared:
        r.kind = e.type == EventType::FaultRaised ? HistoryKind::FaultRaised : HistoryKind::FaultCleared;
        r.zone = e.fault.zone;
        r.a = static_cast<uint8_t>(e.fault.code);
        return r;
    case EventType::TimeStatusChanged:
        r.kind = HistoryKind::TimeSet;
        r.a = static_cast<uint8_t>(e.time.status);
        return r;
    case EventType::ConfigApplied:
        r.kind = HistoryKind::ConfigChanged;
        return r;
    case EventType::RainDelayChanged:
        r.kind = HistoryKind::RainDelay;
        r.d = static_cast<uint32_t>(std::max<UtcSeconds>(0, e.rainDelay.until));
        return r;
    case EventType::SensorChanged:
        r.kind = HistoryKind::Sensor;
        r.a = static_cast<uint8_t>(e.sensor.sensor);
        r.b = e.sensor.active ? 1 : 0;
        return r;
    case EventType::MaintenanceChanged:
        r.kind = HistoryKind::Maintenance;
        r.b = e.flag.on ? 1 : 0;
        return r;
    case EventType::PauseExpired:
        r.kind = HistoryKind::PauseExpired;
        return r;
    case EventType::ZoneChanged:
    case EventType::MasterChanged:
    case EventType::QueueChanged:
    case EventType::CommandRejected:
    case EventType::Count:
        return std::nullopt;
    }
    return std::nullopt;
}

std::string HistoryLog::keyFor(std::size_t slot)
{
    char buf[8];
    std::snprintf(buf, sizeof buf, "hist%02u", static_cast<unsigned>(slot % kChunks));
    return buf;
}

std::string HistoryLog::encodeChunk(uint32_t seq, const std::vector<HistoryRecord> &records)
{
    const std::size_t n = std::min(records.size(), kRecordsPerChunk);
    std::string out(kHeaderSize + n * kRecordSize, '\0');
    auto *p = reinterpret_cast<uint8_t *>(out.data());
    put16(p, kMagic);
    p[2] = kVersion;
    p[3] = static_cast<uint8_t>(n);
    put32(p + 4, seq);
    for (std::size_t i = 0; i < n; ++i) {
        const HistoryRecord &r = records[i];
        uint8_t *q = p + kHeaderSize + i * kRecordSize;
        put32(q, static_cast<uint32_t>(std::clamp<UtcSeconds>(r.utc, 0, 0xFFFFFFFFLL)));
        q[4] = static_cast<uint8_t>(r.kind);
        q[5] = r.zone;
        put16(q + 6, r.programId);
        q[8] = r.a;
        q[9] = r.b;
        put16(q + 10, r.c);
        put32(q + 12, r.d);
        put32(q + 16, r.e);
    }
    put32(p + 8, 0);
    put32(p + 8, crc32(p, out.size()));
    return out;
}

bool HistoryLog::decodeChunk(std::string_view blob, uint32_t &seq, std::vector<HistoryRecord> &records)
{
    records.clear();
    if (blob.size() < kHeaderSize) {
        return false;
    }
    std::string copy(blob);
    auto *p = reinterpret_cast<uint8_t *>(copy.data());
    const std::size_t n = p[3];
    if (get16(p) != kMagic || p[2] != kVersion || n > kRecordsPerChunk ||
        blob.size() != kHeaderSize + n * kRecordSize) {
        return false;
    }
    const uint32_t crc = get32(p + 8);
    put32(p + 8, 0);
    if (crc32(p, copy.size()) != crc) {
        return false;
    }
    seq = get32(p + 4);
    records.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const uint8_t *q = p + kHeaderSize + i * kRecordSize;
        HistoryRecord r;
        r.utc = get32(q);
        r.kind = q[4] < static_cast<uint8_t>(HistoryKind::Count) ? static_cast<HistoryKind>(q[4]) : HistoryKind::None;
        r.zone = q[5];
        r.programId = get16(q + 6);
        r.a = q[8];
        r.b = q[9];
        r.c = get16(q + 10);
        r.d = get32(q + 12);
        r.e = get32(q + 16);
        records.push_back(r);
    }
    return true;
}

void HistoryLog::load()
{
    bool any = false;
    uint32_t newest = 0;
    uint32_t oldest = 0;
    std::vector<HistoryRecord> newestRecords;
    for (std::size_t slot = 0; slot < kChunks; ++slot) {
        std::string blob;
        uint32_t seq = 0;
        std::vector<HistoryRecord> recs;
        if (!storage_.read(keyFor(slot).c_str(), blob) || !decodeChunk(blob, seq, recs) || seq % kChunks != slot) {
            continue;
        }
        if (!any || seq > newest) {
            newest = seq;
            newestRecords = std::move(recs);
        }
        oldest = any ? std::min(oldest, seq) : seq;
        any = true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    loaded_ = true;
    if (!any) {
        seq_ = 0;
        oldestSeq_ = 0;
        open_.clear();
        return;
    }
    // Chunks older than kChunks - 1 sequences before the newest are stale
    // leftovers (their slot would have been reused); queries check this.
    oldestSeq_ = std::max<uint32_t>(oldest, newest >= kChunks - 1 ? newest - (kChunks - 1) : 0);
    if (newestRecords.size() >= kRecordsPerChunk) {
        seq_ = newest + 1;
        open_.clear();
        if (seq_ >= oldestSeq_ + kChunks) {
            oldestSeq_ = seq_ - (kChunks - 1);
        }
    } else {
        seq_ = newest;
        open_ = std::move(newestRecords);
    }
    unwritten_ = 0;
}

void HistoryLog::append(const HistoryRecord &r, MonoMs now, bool urgent)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (unwritten_ == 0) {
        firstUnwrittenMs_ = now;
    }
    open_.push_back(r);
    ++unwritten_;
    urgent_ = urgent_ || urgent;
    if (open_.size() >= kRecordsPerChunk) {
        // Seal: it must be written as it is, then a new chunk starts. If an
        // earlier sealed chunk still waits (storage failing), it is lost.
        sealed_ = std::move(open_);
        sealedSeq_ = seq_;
        open_.clear();
        ++seq_;
        if (seq_ >= oldestSeq_ + kChunks) {
            oldestSeq_ = seq_ - (kChunks - 1);
        }
        unwritten_ = 0;
        urgent_ = true; // write the sealed chunk soon
    }
}

bool HistoryLog::flushDue(MonoMs now) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!sealed_.empty() || urgent_) {
        return true;
    }
    return unwritten_ > 0 && (unwritten_ >= kFlushRecords || now - firstUnwrittenMs_ >= kFlushIntervalMs);
}

bool HistoryLog::flush(MonoMs now)
{
    std::vector<HistoryRecord> sealed;
    uint32_t sealedSeq = 0;
    std::vector<HistoryRecord> open;
    uint32_t seq = 0;
    std::size_t writtenUpTo = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sealed = sealed_;
        sealedSeq = sealedSeq_;
        if (unwritten_ > 0 || urgent_) {
            open = open_;
            seq = seq_;
        }
        writtenUpTo = open.size();
        urgent_ = false;
    }
    bool ok = true;
    if (!sealed.empty()) {
        if (storage_.write(keyFor(sealedSeq).c_str(), encodeChunk(sealedSeq, sealed))) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (sealedSeq_ == sealedSeq) {
                sealed_.clear();
            }
        } else {
            ok = false;
        }
    }
    if (!open.empty()) {
        if (storage_.write(keyFor(seq).c_str(), encodeChunk(seq, open))) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (seq_ == seq) {
                // Records appended meanwhile stay unwritten.
                unwritten_ = open_.size() - std::min(open_.size(), writtenUpTo);
                firstUnwrittenMs_ = now;
            }
        } else {
            ok = false;
        }
    }
    if (!ok) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++failures_;
        firstUnwrittenMs_ = now; // retry after another interval
    }
    return ok;
}

uint32_t HistoryLog::writeFailures() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return failures_;
}

HistoryPage HistoryLog::query(const HistoryQuery &q) const
{
    HistoryPage page;
    std::vector<HistoryRecord> open;
    std::vector<HistoryRecord> sealed;
    uint32_t seq = 0;
    uint32_t sealedSeq = 0;
    uint32_t oldestSeq = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open = open_;
        sealed = sealed_;
        seq = seq_;
        sealedSeq = sealedSeq_;
        oldestSeq = oldestSeq_;
        page.unflushed = unwritten_ + sealed_.size();
    }
    const std::size_t limit = std::max<std::size_t>(1, q.limit);
    bool first = true;
    for (uint64_t s = seq + 1ull; s-- > oldestSeq;) {
        std::vector<HistoryRecord> recs;
        const auto chunkSeq = static_cast<uint32_t>(s);
        if (chunkSeq == seq) {
            recs = open;
        } else if (!sealed.empty() && chunkSeq == sealedSeq) {
            recs = sealed;
        } else {
            std::string blob;
            uint32_t got = 0;
            if (!storage_.read(keyFor(chunkSeq).c_str(), blob) || !decodeChunk(blob, got, recs) || got != chunkSeq) {
                continue; // missing, damaged or already reused
            }
        }
        const uint64_t base = static_cast<uint64_t>(chunkSeq) * kRecordsPerChunk;
        if (first && !recs.empty()) {
            page.newestId = base + recs.size();
            first = false;
        }
        for (std::size_t i = recs.size(); i-- > 0 && !page.more;) {
            const uint64_t id = base + i + 1;
            const HistoryRecord &r = recs[i];
            if (id >= q.before || id <= q.after || r.utc < q.since || (q.zone != 0xFF && r.zone != q.zone)) {
                continue;
            }
            if (page.entries.size() == limit) {
                page.more = true; // one more match exists; no need to read further
            } else {
                page.entries.push_back({id, r});
            }
        }
        if (page.more || (q.after > 0 && base < q.after)) {
            break; // full, or everything older is excluded by `after`
        }
    }
    // The oldest chunk still kept; a damaged chunk inside the range is skipped.
    page.oldestId = page.newestId > 0 ? static_cast<uint64_t>(oldestSeq) * kRecordsPerChunk + 1 : 0;
    return page;
}

void setHistoryLog(HistoryLog *log) { gLog.store(log, std::memory_order_release); }
HistoryLog *historyLog() { return gLog.load(std::memory_order_acquire); }

} // namespace zf
