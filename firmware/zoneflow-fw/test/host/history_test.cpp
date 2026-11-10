// SPDX-License-Identifier: Apache-2.0
// Persistent history: chunk codec, batching, ring rollover, recovery after a
// power cut, damaged chunks, failing storage, queries; the controller's
// RunEnded event; learned current and pause time in the snapshot.
#include <gtest/gtest.h>

#include <algorithm>

#include "fakes.hpp"
#include "zf/app/controller.hpp"
#include "zf/history/history.hpp"

using namespace zf;
using namespace zf::test;

namespace {

HistoryRecord rec(uint32_t n, uint8_t zone = 0xFF)
{
    HistoryRecord r;
    r.utc = 1780000000 + n;
    r.kind = HistoryKind::Run;
    r.zone = zone;
    r.programId = static_cast<uint16_t>(n % 7);
    r.c = 600;
    r.d = n;
    return r;
}

// Appends n records, flushing whenever the log asks for it.
void fill(HistoryLog &log, uint32_t from, uint32_t n, MonoMs &now, uint8_t zone = 0xFF)
{
    for (uint32_t i = 0; i < n; ++i) {
        log.append(rec(from + i, zone), now);
        now += 1000;
        if (log.flushDue(now)) {
            ASSERT_TRUE(log.flush(now));
        }
    }
}

} // namespace

TEST(HistoryCodec, RoundTripsAndRejectsDamage)
{
    std::vector<HistoryRecord> in;
    for (uint32_t i = 0; i < HistoryLog::kRecordsPerChunk; ++i) {
        in.push_back(rec(i, static_cast<uint8_t>(i % 8)));
    }
    const std::string blob = HistoryLog::encodeChunk(77, in);
    ASSERT_EQ(blob.size(), HistoryLog::kChunkBytes);
    uint32_t seq = 0;
    std::vector<HistoryRecord> out;
    ASSERT_TRUE(HistoryLog::decodeChunk(blob, seq, out));
    EXPECT_EQ(seq, 77u);
    EXPECT_EQ(out, in);

    for (std::size_t at : {std::size_t{0}, std::size_t{5}, std::size_t{9}, blob.size() - 1}) {
        std::string bad = blob;
        bad[at] = static_cast<char>(bad[at] ^ 0x10);
        EXPECT_FALSE(HistoryLog::decodeChunk(bad, seq, out)) << "flipped byte " << at;
    }
    EXPECT_FALSE(HistoryLog::decodeChunk(blob.substr(0, blob.size() - 20), seq, out)) << "torn write";
    EXPECT_FALSE(HistoryLog::decodeChunk("", seq, out));
}

TEST(HistoryLog, BatchesWritesAndWritesFaultsAtOnce)
{
    MemoryStorage storage;
    HistoryLog log(storage);
    log.load();
    MonoMs now = 1000;
    log.append(rec(1), now);
    EXPECT_FALSE(log.flushDue(now + 1000));
    EXPECT_TRUE(log.flushDue(now + HistoryLog::kFlushIntervalMs)) << "after the interval";
    ASSERT_TRUE(log.flush(now + HistoryLog::kFlushIntervalMs));
    EXPECT_EQ(storage.writes, 1);
    EXPECT_FALSE(log.flushDue(now + HistoryLog::kFlushIntervalMs + 1));

    now += HistoryLog::kFlushIntervalMs + 10;
    for (std::size_t i = 0; i + 1 < HistoryLog::kFlushRecords; ++i) {
        log.append(rec(10 + static_cast<uint32_t>(i)), now);
    }
    EXPECT_FALSE(log.flushDue(now));
    log.append(rec(99), now);
    EXPECT_TRUE(log.flushDue(now)) << "after kFlushRecords records";
    ASSERT_TRUE(log.flush(now));

    log.append(rec(100), now, true);
    EXPECT_TRUE(log.flushDue(now)) << "urgent";
}

TEST(HistoryLog, KeepsTheNewestRecordsWhenTheRingWraps)
{
    MemoryStorage storage;
    HistoryLog log(storage);
    log.load();
    MonoMs now = 0;
    const uint32_t total = HistoryLog::kCapacity + 3 * HistoryLog::kRecordsPerChunk + 7;
    fill(log, 0, total, now);
    ASSERT_TRUE(log.flush(now));
    EXPECT_LE(storage.data.size(), HistoryLog::kChunks) << "never more blobs than chunks";

    HistoryQuery q;
    q.limit = 100;
    const HistoryPage page = log.query(q);
    EXPECT_EQ(page.newestId, total);
    ASSERT_EQ(page.entries.size(), 100u);
    EXPECT_EQ(page.entries.front().id, total);
    EXPECT_EQ(page.entries.front().record, rec(total - 1));
    EXPECT_TRUE(page.more);
    // At most kCapacity records survive; the oldest kept is a chunk start.
    EXPECT_GE(page.oldestId, total - HistoryLog::kCapacity + 1);
    EXPECT_EQ((page.oldestId - 1) % HistoryLog::kRecordsPerChunk, 0u);

    // Walk all pages back: ids strictly decrease, each record matches its id.
    uint64_t before = UINT64_MAX;
    std::size_t seen = 0;
    uint64_t last = UINT64_MAX;
    for (;;) {
        HistoryQuery p;
        p.before = before;
        p.limit = 100;
        const HistoryPage pg = log.query(p);
        for (const HistoryEntry &e : pg.entries) {
            ASSERT_LT(e.id, last);
            last = e.id;
            EXPECT_EQ(e.record, rec(static_cast<uint32_t>(e.id - 1)));
            ++seen;
        }
        if (!pg.more) {
            break;
        }
        before = pg.entries.back().id;
    }
    EXPECT_GT(seen, HistoryLog::kCapacity - HistoryLog::kRecordsPerChunk);
    EXPECT_LE(seen, HistoryLog::kCapacity);
}

TEST(HistoryLog, SurvivesARestartAndLosesOnlyWhatWasNotWritten)
{
    MemoryStorage storage;
    MonoMs now = 0;
    {
        HistoryLog log(storage);
        log.load();
        fill(log, 0, 120, now); // two full chunks and 20 records
        ASSERT_TRUE(log.flush(now));
        log.append(rec(500), now); // buffered, then "power fails"
    }
    HistoryLog after(storage);
    after.load();
    HistoryQuery q;
    q.limit = 5;
    const HistoryPage page = after.query(q);
    EXPECT_EQ(page.newestId, 120u);
    ASSERT_FALSE(page.entries.empty());
    EXPECT_EQ(page.entries.front().record, rec(119));

    // Ids continue where the stored log ended.
    after.append(rec(121), now);
    ASSERT_TRUE(after.flush(now));
    EXPECT_EQ(after.query(q).newestId, 121u);
}

TEST(HistoryLog, ADamagedChunkCostsOnlyItsOwnRecords)
{
    MemoryStorage storage;
    MonoMs now = 0;
    {
        HistoryLog log(storage);
        log.load();
        fill(log, 0, 3 * HistoryLog::kRecordsPerChunk + 10, now);
        ASSERT_TRUE(log.flush(now));
    }
    // Corrupt chunk 1 (ids 51..100).
    std::string &blob = storage.data.at(HistoryLog::keyFor(1));
    blob[30] = static_cast<char>(blob[30] ^ 0x55);

    HistoryLog log(storage);
    log.load();
    HistoryQuery q;
    q.limit = 100;
    std::vector<uint64_t> ids;
    uint64_t before = UINT64_MAX;
    for (;;) {
        q.before = before;
        const HistoryPage pg = log.query(q);
        for (const HistoryEntry &e : pg.entries) {
            ids.push_back(e.id);
        }
        if (!pg.more) {
            break;
        }
        before = pg.entries.back().id;
    }
    EXPECT_EQ(ids.size(), 3 * HistoryLog::kRecordsPerChunk + 10 - HistoryLog::kRecordsPerChunk);
    EXPECT_TRUE(std::none_of(ids.begin(), ids.end(), [](uint64_t id) { return id > 50 && id <= 100; }));
    EXPECT_NE(std::find(ids.begin(), ids.end(), 50u), ids.end());
    EXPECT_NE(std::find(ids.begin(), ids.end(), 101u), ids.end());
}

TEST(HistoryLog, ANewestChunkThatIsDamagedFallsBackToTheOneBefore)
{
    MemoryStorage storage;
    MonoMs now = 0;
    {
        HistoryLog log(storage);
        log.load();
        fill(log, 0, HistoryLog::kRecordsPerChunk + 5, now);
        ASSERT_TRUE(log.flush(now));
    }
    storage.data.at(HistoryLog::keyFor(1)).resize(40); // torn
    HistoryLog log(storage);
    log.load();
    HistoryQuery q;
    EXPECT_EQ(log.query(q).newestId, HistoryLog::kRecordsPerChunk);
    log.append(rec(1000), now);
    ASSERT_TRUE(log.flush(now));
    EXPECT_EQ(log.query(q).newestId, HistoryLog::kRecordsPerChunk + 1) << "continues in a fresh chunk";
}

TEST(HistoryLog, FailedWritesKeepTheRecordsAndRetry)
{
    MemoryStorage storage;
    HistoryLog log(storage);
    log.load();
    MonoMs now = 0;
    storage.failWrites = true;
    log.append(rec(1), now, true);
    EXPECT_FALSE(log.flush(now));
    EXPECT_EQ(log.writeFailures(), 1u);
    HistoryQuery q;
    HistoryPage pg = log.query(q);
    ASSERT_EQ(pg.entries.size(), 1u) << "still visible from RAM";
    EXPECT_EQ(pg.unflushed, 1u);
    EXPECT_FALSE(log.flushDue(now + 1000)) << "waits before retrying";
    storage.failWrites = false;
    EXPECT_TRUE(log.flushDue(now + HistoryLog::kFlushIntervalMs));
    ASSERT_TRUE(log.flush(now + HistoryLog::kFlushIntervalMs));
    EXPECT_EQ(log.query(q).unflushed, 0u);
}

TEST(HistoryLog, FiltersBySinceZoneAndAfter)
{
    MemoryStorage storage;
    HistoryLog log(storage);
    log.load();
    MonoMs now = 0;
    for (uint32_t i = 0; i < 30; ++i) {
        log.append(rec(i, static_cast<uint8_t>(i % 3)), now);
    }
    HistoryQuery q;
    q.since = 1780000000 + 25;
    EXPECT_EQ(log.query(q).entries.size(), 5u);
    q = {};
    q.zone = 1;
    const HistoryPage z = log.query(q);
    EXPECT_EQ(z.entries.size(), 10u);
    EXPECT_TRUE(
        std::all_of(z.entries.begin(), z.entries.end(), [](const HistoryEntry &e) { return e.record.zone == 1; }));
    q = {};
    q.after = 27;
    const HistoryPage a = log.query(q);
    ASSERT_EQ(a.entries.size(), 3u);
    EXPECT_EQ(a.entries.back().id, 28u);
}

TEST(HistoryEvents, KeepsWhatMattersAndDropsTheRest)
{
    Event e{};
    e.utc = 1780000000;
    e.type = EventType::ZoneChanged;
    EXPECT_FALSE(historyFromEvent(e).has_value());
    e.type = EventType::QueueChanged;
    EXPECT_FALSE(historyFromEvent(e).has_value());
    e.type = EventType::CommandRejected;
    EXPECT_FALSE(historyFromEvent(e).has_value());

    e.type = EventType::RunEnded;
    e.run = RunEndedData{2, RunSource::Schedule, StopReason::Completed, 4, 600, 598, 312, 14055};
    auto r = historyFromEvent(e);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->kind, HistoryKind::Run);
    EXPECT_EQ(r->zone, 2);
    EXPECT_EQ(r->programId, 4);
    EXPECT_EQ(r->c, 600);
    EXPECT_EQ(r->d, 598u);
    EXPECT_EQ(r->e & 0xFFFFu, 312u);
    EXPECT_EQ(r->e >> 16, 1406u) << "140.55 L in decilitres, rounded";

    e.run.centilitres = -1;
    EXPECT_EQ(historyFromEvent(e)->e >> 16, kHistoryNoVolume);

    e.type = EventType::FaultRaised;
    e.fault = {FaultCode::ShortCircuit, 3, 0};
    r = historyFromEvent(e);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->kind, HistoryKind::FaultRaised);
    EXPECT_EQ(r->a, static_cast<uint8_t>(FaultCode::ShortCircuit));
    EXPECT_EQ(r->zone, 3);

    for (EventType t :
         {EventType::ProgramStarted, EventType::ProgramSkipped, EventType::ProgramMissed, EventType::RunDropped,
          EventType::FaultCleared, EventType::RainDelayChanged, EventType::ConfigApplied, EventType::TimeStatusChanged,
          EventType::MaintenanceChanged, EventType::SensorChanged, EventType::PauseExpired}) {
        Event x{};
        x.type = t;
        EXPECT_TRUE(historyFromEvent(x).has_value()) << toString(t);
    }
}

// ---- controller: RunEnded, learned current, pause time -----------------------------

namespace {

class RunHistoryTest : public ::testing::Test {
protected:
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    MemoryStorage storage;
    ConfigStore config{storage};
    std::unique_ptr<Controller> ctrl;
    std::vector<Event> events;

    void start(bool flow)
    {
        bus.subscribe([](const Event &e, void *ctx) { static_cast<std::vector<Event> *>(ctx)->push_back(e); }, &events);
        Config c = Config::defaults();
        c.master.enabled = false;
        c.driver.interZoneDelayMs = 0;
        c.flow.enabled = flow;
        c.flow.pulsesPerLitre = 100.0f;
        ASSERT_TRUE(config.update(c).ok);
        ctrl = std::make_unique<Controller>(ControllerDeps{clock, out, sensors, oe, bus, state, config, &storage});
        ctrl->begin();
        ctrl->setTimeStatus(TimeStatus::Synced);
        runMs(1000);
    }
    // 50 ms ticks; while a zone is open, 300 mA and 10 pulses per tick
    // (200 pulses/s = 2 L/s at 100 pulses per litre).
    void runMs(MonoMs ms)
    {
        for (MonoMs t = 0; t < ms; t += 50) {
            sensors.sample.valveCurrentMa = out.anyOn() ? 300 : 0;
            if (out.anyOn()) {
                sensors.sample.flowPulses += 10;
            }
            clock.advanceMs(50);
            ctrl->tick();
        }
    }
    std::vector<Event> runs() const
    {
        std::vector<Event> r;
        std::copy_if(events.begin(), events.end(), std::back_inserter(r),
                     [](const Event &e) { return e.type == EventType::RunEnded; });
        return r;
    }
};

} // namespace

TEST_F(RunHistoryTest, ACompletedRunReportsDurationsCurrentAndVolume)
{
    start(true);
    ASSERT_TRUE(ctrl->handle(Command::runZone(3, 20)));
    runMs(25000);
    const auto r = runs();
    ASSERT_EQ(r.size(), 1u);
    const RunEndedData &d = r[0].run;
    EXPECT_EQ(d.zone, 3);
    EXPECT_EQ(d.source, RunSource::ManualZone);
    EXPECT_EQ(d.programId, 0);
    EXPECT_EQ(d.reason, StopReason::Completed);
    EXPECT_EQ(d.plannedSec, 20);
    EXPECT_EQ(d.actualSec, 20);
    EXPECT_EQ(d.currentMa, 300);
    // 20 s at 2 L/s; give one tick of slack at each end.
    EXPECT_NEAR(d.centilitres, 4000, 20);
    EXPECT_EQ(state.snapshot().zones[3].learnedCurrentMa, 300);
    EXPECT_EQ(state.snapshot().zones[2].learnedCurrentMa, 0) << "never ran alone";
}

TEST_F(RunHistoryTest, APausedProgramRunEndsAsRequestedAndReportsThePauseTime)
{
    start(false);
    Config c = config.get();
    Program p;
    p.id = 5;
    p.startCount = 0;
    p.durationSec[1] = 60;
    p.useSeasonalAdjust = false;
    c.schedule.programs[0] = p;
    c.schedule.programCount = 1;
    ASSERT_TRUE(config.update(c).ok);
    ASSERT_TRUE(ctrl->handle(Command::simple(CommandType::ReloadConfig)));
    ASSERT_TRUE(ctrl->handle(Command::runProgram(5)));
    runMs(10000);
    ASSERT_TRUE(ctrl->handle(Command::simple(CommandType::Pause)));
    runMs(5000);
    auto r = runs();
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].run.source, RunSource::ManualProgram);
    EXPECT_EQ(r[0].run.programId, 5);
    EXPECT_EQ(r[0].run.reason, StopReason::Requested);
    EXPECT_EQ(r[0].run.plannedSec, 60);
    EXPECT_NEAR(r[0].run.actualSec, 10, 1);
    EXPECT_EQ(r[0].run.centilitres, -1) << "no flow meter";
    EXPECT_NEAR(state.snapshot().queue.pausedForSec, 5u, 1u);

    ASSERT_TRUE(ctrl->handle(Command::simple(CommandType::Resume)));
    runMs(1000);
    EXPECT_EQ(state.snapshot().queue.pausedForSec, 0u);
    runMs(60000);
    r = runs();
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[1].run.reason, StopReason::Completed);
    EXPECT_NEAR(r[1].run.plannedSec, 50, 1) << "what was left at resume";
}
