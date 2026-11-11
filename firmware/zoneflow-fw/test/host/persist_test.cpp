// SPDX-License-Identifier: Apache-2.0
// Runtime-state persistence off the control task. The controller only posts
// a plain-data copy to a mailbox; a low-priority writer task builds the JSON
// and writes flash. A slow or stalled flash write (NVS garbage collection)
// must not hold up the control loop or its heartbeat.
#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <new>
#include <thread>

#include "fakes.hpp"
#include "zf/app/controller.hpp"
#include "zf/app/persist.hpp"

using namespace zf;
using namespace zf::test;

// ---- allocation counter -----------------------------------------------------------
// Counts operator new calls on the thread that switched counting on.
namespace {
thread_local bool tCounting = false;
thread_local long tAllocations = 0;
} // namespace

// Every non-aligned form is replaced, so new and delete always pair through
// malloc and free (AddressSanitizer checks the pairing).
namespace {
void *countedAlloc(std::size_t n) noexcept
{
    if (tCounting) {
        ++tAllocations;
    }
    return std::malloc(n == 0 ? 1 : n);
}
} // namespace

void *operator new(std::size_t n)
{
    if (void *p = countedAlloc(n)) {
        return p;
    }
    throw std::bad_alloc();
}
void *operator new[](std::size_t n)
{
    if (void *p = countedAlloc(n)) {
        return p;
    }
    throw std::bad_alloc();
}
void *operator new(std::size_t n, const std::nothrow_t &) noexcept { return countedAlloc(n); }
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept { return countedAlloc(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }

namespace {

// Storage whose writes block until released, as NVS does during garbage
// collection.
class StallingStorage final : public IBlobStorage {
public:
    MemoryStorage mem;
    std::mutex m;
    std::condition_variable cv;
    bool released = false;
    std::atomic<int> writesStarted{0};
    std::atomic<int> writesOnControlThread{0};
    std::thread::id controlThread = std::this_thread::get_id();

    bool read(const char *key, std::string &out) override { return mem.read(key, out); }
    bool write(const char *key, std::string_view data) override
    {
        if (std::this_thread::get_id() == controlThread) {
            ++writesOnControlThread;
        }
        ++writesStarted;
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [this] { return released; });
        return mem.write(key, data);
    }
    bool erase(const char *key) override { return mem.erase(key); }
    void release()
    {
        {
            std::lock_guard<std::mutex> lock(m);
            released = true;
        }
        cv.notify_all();
    }
};

Config scheduledConfig()
{
    Config c = Config::defaults();
    c.driver.interZoneDelayMs = 0;
    c.schedule.programs[0] = makeProgram(1, {360}, {{0, 600}});
    c.schedule.programCount = 1;
    return c;
}

} // namespace

TEST(PersistMailbox, KeepsOnlyTheLatestCopy)
{
    PersistMailbox box;
    SchedulerPersist out;
    EXPECT_FALSE(box.take(out));
    SchedulerPersist a;
    a.rainDelayUntil = 1;
    SchedulerPersist b;
    b.rainDelayUntil = 2;
    box.post(a);
    box.post(b);
    ASSERT_TRUE(box.take(out));
    EXPECT_EQ(out.rainDelayUntil, 2);
    EXPECT_FALSE(box.take(out));
}

TEST(PersistMailbox, WriterReportsFailureOnce)
{
    PersistMailbox box;
    MemoryStorage mem;
    EXPECT_FALSE(persistWriteOnce(box, mem)); // nothing posted
    SchedulerPersist p;
    p.rainDelayUntil = 42;
    box.post(p);
    mem.failWrites = true;
    EXPECT_TRUE(persistWriteOnce(box, mem));
    EXPECT_TRUE(box.takeFailure());
    EXPECT_FALSE(box.takeFailure());
    mem.failWrites = false;
    box.post(p);
    EXPECT_TRUE(persistWriteOnce(box, mem));
    EXPECT_FALSE(box.takeFailure());
    SchedulerPersist back;
    ASSERT_TRUE(persistFromJson(mem.data["runtime"], back));
    EXPECT_EQ(back.rainDelayUntil, 42);
}

// The control loop never writes storage itself: a scheduled start only posts
// the new state, and the writer stores it.
TEST(PersistHandOff, ControllerPostsAndWriterStores)
{
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    MemoryStorage storage;
    ConfigStore config{storage};
    ASSERT_TRUE(config.update(scheduledConfig()).ok);
    const int writesBefore = storage.writes;
    clock.utc = utcOf(2026, 6, 1, 5, 59, 50);
    Controller ctrl(ControllerDeps{clock, out, sensors, oe, bus, state, config, &storage});
    ctrl.begin();
    ctrl.setTimeStatus(TimeStatus::Synced);
    for (int i = 0; i < 400; ++i) {
        clock.advanceMs(50);
        ctrl.tick();
    }
    ASSERT_TRUE(out.zone[0]);
    EXPECT_EQ(storage.writes, writesBefore); // nothing written on the control path
    EXPECT_TRUE(persistWriteOnce(ctrl.persistMailbox(), storage));
    SchedulerPersist p;
    ASSERT_TRUE(persistFromJson(storage.data["runtime"], p));
    ASSERT_EQ(p.count, 1);
    EXPECT_EQ(p.fired[0].key, Scheduler::makeKey(civil::daysFromCivil(2026, 6, 1), 360));
}

// Soak finding (test/soak, chaos seed 348 and three others): a power cut a
// second after a scheduled start, before the 2 s rate limit let the
// controller post the consumed start, and the start ran again, late, after
// the restart. A consumed start is now posted on the tick that consumed it.
TEST(PersistHandOff, ConsumedStartIsPostedOnTheSameTick)
{
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    MemoryStorage storage;
    ConfigStore config{storage};
    ASSERT_TRUE(config.update(scheduledConfig()).ok);
    clock.utc = utcOf(2026, 6, 1, 5, 59, 59);
    clock.mono = 300; // just booted: the rate limit has not run out yet
    Controller ctrl(ControllerDeps{clock, out, sensors, oe, bus, state, config, &storage});
    ctrl.begin();
    ctrl.setTimeStatus(TimeStatus::Synced);
    for (int i = 0; i < 40 && !out.zone[0]; ++i) {
        clock.advanceMs(50);
        ctrl.tick();
    }
    ASSERT_TRUE(out.zone[0]);
    // The writer's next poll stores it; a power cut after that cannot repeat it.
    ASSERT_TRUE(persistWriteOnce(ctrl.persistMailbox(), storage));
    SchedulerPersist p;
    ASSERT_TRUE(persistFromJson(storage.data["runtime"], p));
    ASSERT_EQ(p.count, 1);
    EXPECT_EQ(p.fired[0].key, Scheduler::makeKey(civil::daysFromCivil(2026, 6, 1), 360));
}

TEST(PersistHandOff, WriteFailureRaisesStorageError)
{
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    MemoryStorage storage;
    ConfigStore config{storage};
    ASSERT_TRUE(config.update(scheduledConfig()).ok);
    Controller ctrl(ControllerDeps{clock, out, sensors, oe, bus, state, config, &storage});
    ctrl.begin();
    ctrl.handle(Command::rainDelay(utcOf(2026, 6, 2)));
    clock.advanceMs(2500);
    ctrl.tick();
    storage.failWrites = true;
    ASSERT_TRUE(persistWriteOnce(ctrl.persistMailbox(), storage));
    clock.advanceMs(50);
    ctrl.tick();
    EXPECT_NE(ctrl.activeFaults() & (1u << static_cast<unsigned>(FaultCode::StorageError)), 0u);
}

// The hand-off under a stalled flash write: the writer thread is stuck in
// write() (as during NVS garbage collection) while the control loop keeps
// ticking, keeps its heartbeat fresh and keeps posting. Nothing on the
// control thread touches storage or allocates.
TEST(PersistHandOff, StalledFlashWriteDoesNotStallTheControlLoop)
{
    FakeClock clock;
    FakeOutputs out;
    FakeSensors sensors;
    OutputEnableGuard oe{OutputEnableMode::Toggle, 500};
    EventBus bus;
    StateStore state;
    out.log.reserve(4096); // the fake's own log must not count as an allocation
    StallingStorage storage;
    MemoryStorage configMem;
    ConfigStore config{configMem};
    ASSERT_TRUE(config.update(scheduledConfig()).ok);
    clock.utc = utcOf(2026, 6, 1, 5, 59, 50);
    Controller ctrl(ControllerDeps{clock, out, sensors, oe, bus, state, config, &storage});
    ctrl.begin();
    ctrl.setTimeStatus(TimeStatus::Synced);

    std::atomic<bool> stop{false};
    std::thread writer([&] {
        while (!stop.load()) {
            persistWriteOnce(ctrl.persistMailbox(), storage);
            std::this_thread::yield();
        }
    });

    // Run past the 06:00 start and change the (expired) rain delay every 3 s
    // so the controller keeps posting while the writer is stuck.
    long allocations = 0;
    int stalledTicks = 0;
    int oeDropped = 0;
    for (int i = 0; i < 1200; ++i) {
        if (i % 60 == 0) {
            ctrl.handle(Command::rainDelay(utcOf(2026, 5, 1) + i)); // in the past: queue untouched
        }
        clock.advanceMs(50);
        tCounting = true;
        tAllocations = 0;
        ctrl.tick();
        tCounting = false;
        allocations += tAllocations;
        if (storage.writesStarted.load() > 0 && out.anyOn()) {
            ++stalledTicks;
            // The heartbeat is fresh on every tick: OE keeps toggling.
            if (!oe.allowed(static_cast<uint32_t>(clock.mono))) {
                ++oeDropped;
            }
        }
    }
    EXPECT_GT(stalledTicks, 100);
    EXPECT_EQ(oeDropped, 0);
    EXPECT_TRUE(out.zone[0]);
    EXPECT_EQ(storage.writesOnControlThread.load(), 0);
    EXPECT_EQ(allocations, 0);

    storage.release();
    stop = true;
    writer.join();
    persistWriteOnce(ctrl.persistMailbox(), storage); // the last posted copy
    SchedulerPersist p;
    ASSERT_TRUE(persistFromJson(storage.mem.data["runtime"], p));
    EXPECT_EQ(p.rainDelayUntil, utcOf(2026, 5, 1) + 1140);
}
