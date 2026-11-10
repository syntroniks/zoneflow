// SPDX-License-Identifier: Apache-2.0
// Snapshot of everything a UI or integration needs to show. The control task
// publishes a new snapshot whenever something changes (and at least once a
// second); readers on any task take a copy.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "zf/events/event.hpp"

namespace zf {

struct ZoneStatus {
    ValveState state = ValveState::Off;
    uint32_t remainingSec = 0;
    StopReason lastStop = StopReason::None;
    FaultCode fault = FaultCode::None; // latched until ClearFaults
    bool enabled = false;
    bool lockedOut = false;
};

struct QueueItem {
    uint32_t id = 0;
    ZoneIndex zone = kNoZone;
    uint32_t durationSec = 0;
    RunSource source = RunSource::ManualZone;
    uint16_t programId = 0;
};

struct QueueStatus {
    static constexpr std::size_t kShown = 8;
    QueueState state = QueueState::Idle;
    uint8_t length = 0;
    uint32_t currentRemainingSec = 0; // head entry, when running
    uint8_t shownCount = 0;
    std::array<QueueItem, kShown> items{}; // head first
};

struct SensorStatus {
    bool rainEnabled = false;
    bool rainActive = false;
    bool spareEnabled = false;
    bool spareActive = false;
    bool acPresent = false;
    bool flowEnabled = false;
    float flowLpm = 0.0f;
    float flowTotalLitres = 0.0f;
    int32_t valveCurrentMa = -1;
};

struct SystemState {
    uint32_t sequence = 0;
    MonoMs monoMs = 0;
    UtcSeconds utc = 0;
    TimeStatus timeStatus = TimeStatus::Unset;
    uint8_t zoneCount = 0;
    std::array<ZoneStatus, kMaxZones> zones{};
    bool masterEnabled = false;
    bool masterOn = false;
    QueueStatus queue{};
    SensorStatus sensors{};
    UtcSeconds rainDelayUntil = 0;
    uint8_t inhibitFlags = 0;  // InhibitFlag bits
    uint32_t activeFaults = 0; // bit n = FaultCode n
    bool maintenance = false;
    uint32_t configRevision = 0;
};

class StateStore {
public:
    // Control task only.
    void publish(const SystemState &state)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = state;
        state_.sequence = sequence_.load(std::memory_order_relaxed) + 1;
        sequence_.store(state_.sequence, std::memory_order_release);
    }

    // Any task. Holds the lock for one struct copy.
    SystemState snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    // Cheap change check for pollers.
    uint32_t sequence() const { return sequence_.load(std::memory_order_acquire); }

private:
    mutable std::mutex mutex_;
    SystemState state_{};
    std::atomic<uint32_t> sequence_{0};
};

} // namespace zf
