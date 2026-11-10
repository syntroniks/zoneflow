// SPDX-License-Identifier: Apache-2.0
#include "zf/events/event_bus.hpp"

namespace zf {

EventBus::Handle EventBus::subscribe(EventHandler handler, void *ctx, EventMask mask)
{
    if (handler == nullptr) {
        return kInvalidHandle;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].handler == nullptr) {
            slots_[i] = Slot{handler, ctx, mask};
            return static_cast<Handle>(i);
        }
    }
    return kInvalidHandle;
}

void EventBus::unsubscribe(Handle handle)
{
    if (handle < 0 || static_cast<std::size_t>(handle) >= slots_.size()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    slots_[static_cast<std::size_t>(handle)] = Slot{};
}

void EventBus::publish(const Event &event) const
{
    // Copy under the lock, call without it.
    std::array<Slot, kMaxSubscribers> copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        copy = slots_;
    }
    const EventMask bit = eventBit(event.type);
    for (const Slot &s : copy) {
        if (s.handler != nullptr && (s.mask & bit) != 0) {
            s.handler(event, s.ctx);
        }
    }
}

std::size_t EventBus::subscriberCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    for (const Slot &s : slots_) {
        n += s.handler != nullptr ? 1 : 0;
    }
    return n;
}

} // namespace zf
