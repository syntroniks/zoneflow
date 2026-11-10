// SPDX-License-Identifier: Apache-2.0
// Commands: the only way for other modules to change what the controller
// does. Any task may post; the control task drains the queue each tick.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>

#include "zf/types.hpp"

namespace zf {

enum class CommandType : uint8_t {
    RunZone,    // zone, durationSec
    RunProgram, // programId
    StopAll,
    Pause,
    Resume,
    Skip,
    SetRainDelay, // until (UTC, 0 clears)
    ClearFaults,
    SetMaintenance, // flag; stops everything and blocks starts (OTA)
    ReloadConfig,   // ConfigStore changed; re-read and apply
    // flag: the person confirms the board was repaired. A keep-alive
    // detector fault (hardware design 19.7 steps 1, 2 and 9) clears only
    // with it; ClearFaults leaves such a fault in place.
    AcknowledgeRepair,
};

const char *toString(CommandType t);

struct Command {
    CommandType type = CommandType::StopAll;
    ZoneIndex zone = kNoZone;
    uint16_t programId = 0;
    uint32_t durationSec = 0;
    UtcSeconds until = 0;
    bool flag = false;

    static Command runZone(ZoneIndex z, uint32_t sec)
    {
        Command c;
        c.type = CommandType::RunZone;
        c.zone = z;
        c.durationSec = sec;
        return c;
    }
    static Command runProgram(uint16_t id)
    {
        Command c;
        c.type = CommandType::RunProgram;
        c.programId = id;
        return c;
    }
    static Command simple(CommandType t)
    {
        Command c;
        c.type = t;
        return c;
    }
    static Command rainDelay(UtcSeconds until)
    {
        Command c;
        c.type = CommandType::SetRainDelay;
        c.until = until;
        return c;
    }
    static Command acknowledgeRepair()
    {
        Command c;
        c.type = CommandType::AcknowledgeRepair;
        c.flag = true;
        return c;
    }
    static Command maintenance(bool on)
    {
        Command c;
        c.type = CommandType::SetMaintenance;
        c.flag = on;
        return c;
    }
};
static_assert(std::is_trivially_copyable_v<Command>);

class ICommandSink {
public:
    virtual ~ICommandSink() = default;
    // Non-blocking. Returns false if the queue is full.
    virtual bool post(const Command &cmd) = 0;
};

// Fixed-capacity multi-producer, single-consumer queue. The lock is held
// only for a struct copy.
class CommandQueue final : public ICommandSink {
public:
    static constexpr std::size_t kCapacity = 16;

    bool post(const Command &cmd) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == kCapacity) {
            return false;
        }
        items_[(head_ + count_) % kCapacity] = cmd;
        ++count_;
        return true;
    }

    // Control task.
    bool pop(Command &out)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) {
            return false;
        }
        out = items_[head_];
        head_ = (head_ + 1) % kCapacity;
        --count_;
        return true;
    }

private:
    std::mutex mutex_;
    std::array<Command, kCapacity> items_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

} // namespace zf
