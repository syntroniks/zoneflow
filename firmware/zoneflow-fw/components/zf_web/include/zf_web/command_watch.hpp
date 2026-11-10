// SPDX-License-Identifier: Apache-2.0
// Lets a REST request learn whether the control task rejected the command it
// posted. Commands carry no request id, so a rejection is matched by command
// type, zone and program id while one watch is armed. The API serialises
// armed watches (one at a time), so two REST calls never share a key.
//
// Threads: onEvent() runs on the control task (EventBus handler): it only
// loads and stores atomics, never blocks and never allocates. arm(),
// rejected() and disarm() run on the API task.
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "zf/events/event_bus.hpp"

namespace zf::web {

class CommandWatch {
public:
    // Subscribes to CommandRejected only. Call once; unsubscribe the handle
    // before the watch is destroyed.
    EventBus::Handle attach(EventBus &bus);

    void arm(const Command &cmd);
    // The rejection reason if a matching CommandRejected arrived since arm().
    std::optional<RunError> rejected() const;
    void disarm();

    static void onEvent(const Event &e, void *ctx);

private:
    static uint32_t key(CommandType type, ZoneIndex zone, uint16_t programId);

    std::atomic<uint32_t> armed_{0};  // 0 = nothing armed
    std::atomic<uint32_t> result_{0}; // RunError + 1, 0 = none yet
};

} // namespace zf::web
