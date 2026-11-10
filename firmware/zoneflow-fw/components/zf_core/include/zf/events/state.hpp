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
#include "zf/hal.hpp"

namespace zf {

struct ZoneStatus {
    ValveState state = ValveState::Off;
    uint32_t remainingSec = 0;
    StopReason lastStop = StopReason::None;
    FaultCode fault = FaultCode::None; // latched until ClearFaults
    bool enabled = false;
    bool lockedOut = false;
    uint32_t restRemainingSec = 0; // > 0: reached its maximum run time, resting
    uint16_t learnedCurrentMa = 0; // mean current seen with this zone alone since boot; 0 = not yet
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
    uint32_t pausedForSec = 0;        // since the first pause, while paused
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

// The board's output stage (rev A: expander, relay, keep-alive detectors)
// and the current measurement. Hardware design section 19.
struct HardwareStatus {
    StagePhase phase = StagePhase::None;
    uint8_t zonesFitted = 0; // from the board's variant strap; 0 = not read yet
    bool relayClosed = false;
    bool sensorSupplyOn = false;
    bool sensorSupplyFault = false;
    HwCheck check = HwCheck::None; // latched output-stage fault
    uint8_t step = 0;              // its self-test step (hardware design 19.7)
    bool repairNeeded = false;     // acknowledging it needs a confirmed repair
    uint32_t selfTestsPassed = 0;
    uint16_t enableDriveMs = 0;  // last start: detector A on after the first edge
    uint16_t enableRelayMs = 0;  // detector B
    uint16_t releaseDriveMs = 0; // last stop: detector A off after the last edge
    uint16_t releaseRelayMs = 0; // detector B
    uint32_t mainsMilliHz = 0;   // 0 = unknown
    int32_t noiseFloorMa = 0;    // learned with the relay open
    CurrentChannel currentChannel = CurrentChannel::None;
    int32_t currentDcMa = 0;        // DC component of the latest window
    int32_t lastTripCurrentMa = -1; // current window before the last over-current trip
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
    bool masterLockedOut = false;
    uint8_t masterFaultCount = 0; // consecutive current faults on the master
    QueueStatus queue{};
    SensorStatus sensors{};
    UtcSeconds rainDelayUntil = 0;
    uint8_t inhibitFlags = 0;  // InhibitFlag bits
    uint32_t activeFaults = 0; // bit n = FaultCode n
    bool maintenance = false;
    uint32_t configRevision = 0;
    HardwareStatus hardware{};
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
