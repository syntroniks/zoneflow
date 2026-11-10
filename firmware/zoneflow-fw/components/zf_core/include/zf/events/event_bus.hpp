// SPDX-License-Identifier: Apache-2.0
// In-process publish/subscribe for controller events.
//
// Threading rules:
//  - publish() is called by the control task only.
//  - subscribe()/unsubscribe() may be called from any task at any time.
//  - Handlers run on the control task, synchronously, in subscription order.
//    A handler must return within microseconds and must not block, sleep,
//    take a lock that a blocking task may hold, or call into the controller.
//    The usual handler copies the event into the subscriber's own queue with
//    a zero timeout (see zf_platform EventQueueSubscriber) and drops it if
//    full; the subscriber then reads the StateStore for the full picture.
//  - No allocation. Capacity is fixed.
#pragma once

#include <array>
#include <cstddef>
#include <mutex>

#include "zf/events/event.hpp"

namespace zf {

using EventHandler = void (*)(const Event &event, void *ctx);

class EventBus {
public:
    static constexpr std::size_t kMaxSubscribers = 12;
    using Handle = int;
    static constexpr Handle kInvalidHandle = -1;

    Handle subscribe(EventHandler handler, void *ctx, EventMask mask = kAllEvents);
    void unsubscribe(Handle handle);
    void publish(const Event &event) const;
    std::size_t subscriberCount() const;

private:
    struct Slot {
        EventHandler handler = nullptr;
        void *ctx = nullptr;
        EventMask mask = 0;
    };
    mutable std::mutex mutex_;
    std::array<Slot, kMaxSubscribers> slots_{};
};

} // namespace zf
