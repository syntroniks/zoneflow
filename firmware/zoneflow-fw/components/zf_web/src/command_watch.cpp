// SPDX-License-Identifier: Apache-2.0
#include "zf_web/command_watch.hpp"

namespace zf::web {

uint32_t CommandWatch::key(CommandType type, ZoneIndex zone, uint16_t programId)
{
    // Bit 31 marks an armed key so that no valid key is 0.
    return 0x80000000u | (static_cast<uint32_t>(type) << 24) | (static_cast<uint32_t>(zone) << 16) | programId;
}

EventBus::Handle CommandWatch::attach(EventBus &bus)
{
    return bus.subscribe(&CommandWatch::onEvent, this, eventBit(EventType::CommandRejected));
}

void CommandWatch::arm(const Command &cmd)
{
    result_.store(0, std::memory_order_relaxed);
    armed_.store(key(cmd.type, cmd.zone, cmd.programId), std::memory_order_release);
}

std::optional<RunError> CommandWatch::rejected() const
{
    const uint32_t r = result_.load(std::memory_order_acquire);
    if (r == 0) {
        return std::nullopt;
    }
    return static_cast<RunError>(r - 1);
}

void CommandWatch::disarm() { armed_.store(0, std::memory_order_release); }

void CommandWatch::onEvent(const Event &e, void *ctx)
{
    auto *self = static_cast<CommandWatch *>(ctx);
    if (e.type != EventType::CommandRejected) {
        return;
    }
    const uint32_t armed = self->armed_.load(std::memory_order_acquire);
    if (armed != 0 && armed == key(e.rejected.command, e.rejected.zone, e.rejected.programId)) {
        self->result_.store(static_cast<uint32_t>(e.rejected.error) + 1, std::memory_order_release);
    }
}

} // namespace zf::web
