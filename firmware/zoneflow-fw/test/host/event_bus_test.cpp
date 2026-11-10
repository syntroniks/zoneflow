// SPDX-License-Identifier: Apache-2.0
#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "zf/events/command.hpp"
#include "zf/events/event_bus.hpp"
#include "zf/events/state.hpp"

using namespace zf;

namespace {
void record(const Event &e, void *ctx) { static_cast<std::vector<EventType> *>(ctx)->push_back(e.type); }
} // namespace

TEST(EventBus, DeliversByMask)
{
    EventBus bus;
    std::vector<EventType> all;
    std::vector<EventType> zonesOnly;
    ASSERT_NE(bus.subscribe(&record, &all), EventBus::kInvalidHandle);
    ASSERT_NE(bus.subscribe(&record, &zonesOnly, eventBit(EventType::ZoneChanged)), EventBus::kInvalidHandle);
    Event e{};
    e.type = EventType::QueueChanged;
    bus.publish(e);
    e.type = EventType::ZoneChanged;
    bus.publish(e);
    EXPECT_EQ(all.size(), 2u);
    EXPECT_EQ(zonesOnly, std::vector<EventType>{EventType::ZoneChanged});
}

TEST(EventBus, UnsubscribeAndCapacity)
{
    EventBus bus;
    std::vector<EventType> sink;
    std::vector<EventBus::Handle> handles;
    for (std::size_t i = 0; i < EventBus::kMaxSubscribers; ++i) {
        handles.push_back(bus.subscribe(&record, &sink));
        ASSERT_NE(handles.back(), EventBus::kInvalidHandle);
    }
    EXPECT_EQ(bus.subscribe(&record, &sink), EventBus::kInvalidHandle);
    bus.unsubscribe(handles[3]);
    EXPECT_EQ(bus.subscriberCount(), EventBus::kMaxSubscribers - 1);
    EXPECT_NE(bus.subscribe(&record, &sink), EventBus::kInvalidHandle);
    EXPECT_EQ(bus.subscribe(nullptr, nullptr), EventBus::kInvalidHandle);
}

TEST(EventBus, HandlerMayUnsubscribeDuringPublish)
{
    struct Ctx {
        EventBus *bus;
        EventBus::Handle self;
        int calls = 0;
    } ctx;
    EventBus bus;
    ctx.bus = &bus;
    ctx.self = bus.subscribe(
        [](const Event &, void *p) {
            auto *c = static_cast<Ctx *>(p);
            ++c->calls;
            c->bus->unsubscribe(c->self); // would deadlock if publish held the lock
        },
        &ctx);
    Event e{};
    e.type = EventType::QueueChanged;
    bus.publish(e);
    bus.publish(e);
    EXPECT_EQ(ctx.calls, 1);
}

TEST(CommandQueue, FifoAndCapacity)
{
    CommandQueue q;
    for (std::size_t i = 0; i < CommandQueue::kCapacity; ++i) {
        ASSERT_TRUE(q.post(Command::runZone(static_cast<ZoneIndex>(i % 8), static_cast<uint32_t>(i))));
    }
    EXPECT_FALSE(q.post(Command::simple(CommandType::StopAll)));
    Command c;
    ASSERT_TRUE(q.pop(c));
    EXPECT_EQ(c.durationSec, 0u);
    ASSERT_TRUE(q.pop(c));
    EXPECT_EQ(c.durationSec, 1u);
}

TEST(CommandQueue, ManyProducers)
{
    CommandQueue q;
    std::vector<std::thread> producers;
    std::atomic<int> accepted{0};
    for (int t = 0; t < 4; ++t) {
        producers.emplace_back([&] {
            for (int i = 0; i < 100; ++i) {
                if (q.post(Command::simple(CommandType::Skip))) {
                    ++accepted;
                }
            }
        });
    }
    int popped = 0;
    Command c;
    while (popped < accepted.load() || !producers.empty()) {
        while (q.pop(c)) {
            ++popped;
        }
        if (!producers.empty()) {
            for (auto &p : producers) {
                p.join();
            }
            producers.clear();
        }
    }
    while (q.pop(c)) {
        ++popped;
    }
    EXPECT_EQ(popped, accepted.load());
}

TEST(StateStore, SnapshotAndSequence)
{
    StateStore s;
    EXPECT_EQ(s.sequence(), 0u);
    SystemState st;
    st.zoneCount = 4;
    st.zones[1].state = ValveState::Open;
    s.publish(st);
    EXPECT_EQ(s.sequence(), 1u);
    const SystemState snap = s.snapshot();
    EXPECT_EQ(snap.zoneCount, 4);
    EXPECT_EQ(snap.zones[1].state, ValveState::Open);
    EXPECT_EQ(snap.sequence, 1u);
}
