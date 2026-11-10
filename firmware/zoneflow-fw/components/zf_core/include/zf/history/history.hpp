// SPDX-License-Identifier: Apache-2.0
// Persistent event history: runs, skips, faults and system events, kept in a
// bounded ring of fixed-size records in flash (NVS blobs through
// IBlobStorage).
//
// Layout: kChunks blobs ("hist00".."hist23"), each a 12-byte header (magic,
// version, record count, chunk sequence, CRC-32 of the whole chunk) and up
// to kRecordsPerChunk 20-byte records. The chunk with the highest valid
// sequence is the open one; slot = sequence % kChunks, so a new chunk
// overwrites the oldest. A record's id is sequence * kRecordsPerChunk +
// index + 1: ids only grow, and survive restarts.
//
// Wear and power loss:
//  - Records are buffered in RAM and the open chunk is rewritten at most
//    every kFlushIntervalMs, after kFlushRecords new records, when it fills,
//    or at once for an urgent record (a fault). NVS spreads the rewrites over
//    its pages; an NVS blob write is atomic (old or new value after a power
//    cut).
//  - A chunk that fails its CRC or magic is ignored, so a damaged chunk costs
//    its records, never the rest of the log.
//  - What was buffered when power failed is lost: at most kFlushIntervalMs
//    of events (faults are written at once).
//
// Threads: append() and query() from any task (a mutex held for a record
// copy or a chunk copy, never across flash I/O except in flush()); flush()
// and load() from one task (the recorder). The control task never calls
// any of it: it publishes events, the recorder task converts and stores them.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "zf/config/config_store.hpp"
#include "zf/events/event.hpp"

namespace zf {

enum class HistoryKind : uint8_t {
    None = 0,
    Run, // zone, programId, a=RunSource, b=StopReason, c=planned s, d=actual s, e=current mA | centilitres<<16
    ProgramStarted, // programId, a=RunSource, b=late
    ProgramSkipped, // programId, a=SkipReason
    ProgramMissed,  // programId, d=scheduled UTC
    RunDropped,     // zone, programId, a=RunSource, b=StartError
    FaultRaised,    // zone (or none), a=FaultCode
    FaultCleared,   // zone (or none), a=FaultCode
    Boot,           // a=HistoryResetReason
    TimeSet,        // a=TimeStatus
    ConfigChanged,  // d=config revision
    RainDelay,      // d=until UTC, 0 = cleared
    Sensor,         // a=SensorId, b=active
    Maintenance,    // b=on
    PauseExpired,
    Ota, // a=HistoryOtaStage, b=source (0 none, 1 upload, 2 pull), c=error code
    Count,
};

const char *toString(HistoryKind k);

enum class HistoryResetReason : uint8_t {
    Unknown,
    PowerOn,
    Software, // restart requested (reboot, update, settings)
    Panic,
    Watchdog,
    Brownout,
    External,
    DeepSleep,
};
const char *toString(HistoryResetReason r);

enum class HistoryOtaStage : uint8_t { Started, Failed, Installed };
const char *toString(HistoryOtaStage s);

// Run volume: e's upper 16 bits hold decilitres; this value means "no meter".
inline constexpr uint16_t kHistoryNoVolume = 0xFFFF;

struct HistoryRecord {
    UtcSeconds utc = 0;
    HistoryKind kind = HistoryKind::None;
    uint8_t zone = 0xFF; // 0-based, 0xFF = none
    uint16_t programId = 0;
    uint8_t a = 0;
    uint8_t b = 0;
    uint16_t c = 0;
    uint32_t d = 0;
    uint32_t e = 0;
    bool operator==(const HistoryRecord &) const = default;
};

// Events worth keeping; nullopt for the rest (zone open/close, queue
// changes and refused commands are not stored: RunEnded summarises a run).
std::optional<HistoryRecord> historyFromEvent(const Event &e);

struct HistoryQuery {
    uint64_t before = UINT64_MAX; // only ids below this (paging back)
    uint64_t after = 0;           // only ids above this (new since)
    UtcSeconds since = 0;         // only records at or after this time
    uint8_t zone = 0xFF;          // only this zone (0-based), 0xFF = all
    std::size_t limit = 50;
};

struct HistoryEntry {
    uint64_t id = 0;
    HistoryRecord record;
};

struct HistoryPage {
    std::vector<HistoryEntry> entries; // newest first
    uint64_t newestId = 0;             // 0 = log empty
    uint64_t oldestId = 0;
    bool more = false; // older matching records may exist (page with before = last id)
    std::size_t unflushed = 0;
};

class HistoryLog {
public:
    static constexpr std::size_t kChunks = 24;
    static constexpr std::size_t kRecordsPerChunk = 50;
    static constexpr std::size_t kRecordSize = 20;
    static constexpr std::size_t kHeaderSize = 12;
    static constexpr std::size_t kChunkBytes = kHeaderSize + kRecordsPerChunk * kRecordSize; // 1012
    static constexpr std::size_t kCapacity = kChunks * kRecordsPerChunk;                     // 1200
    static constexpr uint32_t kFlushIntervalMs = 60000;
    static constexpr std::size_t kFlushRecords = 16;
    static constexpr uint16_t kMagic = 0x5A48; // "ZH"
    static constexpr uint8_t kVersion = 1;

    explicit HistoryLog(IBlobStorage &storage) : storage_(storage) {}

    // Finds the newest valid chunk and continues it. Recorder task, once.
    void load();

    // Buffers a record. urgent: write at the next flushDue() check.
    void append(const HistoryRecord &r, MonoMs now, bool urgent = false);

    bool flushDue(MonoMs now) const;
    // Writes the open chunk if it has unwritten records. False on a storage
    // failure (the records stay buffered and are retried).
    bool flush(MonoMs now);

    HistoryPage query(const HistoryQuery &q) const;

    uint32_t writeFailures() const;

    // Chunk codec, exposed for tests.
    static std::string encodeChunk(uint32_t seq, const std::vector<HistoryRecord> &records);
    static bool decodeChunk(std::string_view blob, uint32_t &seq, std::vector<HistoryRecord> &records);
    static std::string keyFor(std::size_t slot);

private:
    IBlobStorage &storage_;
    mutable std::mutex mutex_;
    bool loaded_ = false;
    uint32_t seq_ = 0;                  // sequence of the open chunk
    std::vector<HistoryRecord> open_;   // its records (written and not)
    std::size_t unwritten_ = 0;         // records in open_ not yet in flash
    std::vector<HistoryRecord> sealed_; // a full chunk waiting to be written, if any
    uint32_t sealedSeq_ = 0;
    bool urgent_ = false;
    MonoMs firstUnwrittenMs_ = 0;
    uint32_t failures_ = 0;
    // Oldest sequence still stored (for oldestId); seq_ + 1 - stored count.
    uint32_t oldestSeq_ = 0;
};

// One process-wide log for modules that report events the controller does
// not see (boot, firmware updates). Set once at start-up; may be null.
void setHistoryLog(HistoryLog *log);
HistoryLog *historyLog();

} // namespace zf
