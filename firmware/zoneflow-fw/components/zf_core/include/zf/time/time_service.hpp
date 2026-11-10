// SPDX-License-Identifier: Apache-2.0
// Wall-clock policy: RTC at boot, SNTP when online, RTC written after sync.
// The scheduler runs only when status() is not Unset.
//
// Threads: boot() from app_main before the control task starts.
// onNetworkSync() from the SNTP callback (lwIP task). setManual() from any
// task. status() from any task. The RTC driver must be safe to call from
// those tasks (the I2C master driver serialises bus access).
#pragma once

#include <atomic>
#include <cstdint>

#include "zf/events/event.hpp"
#include "zf/types.hpp"

namespace zf {

class IRtc {
public:
    virtual ~IRtc() = default;
    // False if the chip does not answer or reports lost time
    // (oscillator stopped or backup voltage low).
    virtual bool read(UtcSeconds &out) = 0;
    virtual bool write(UtcSeconds utc) = 0;
};

class ISystemTime {
public:
    virtual ~ISystemTime() = default;
    virtual UtcSeconds now() const = 0;
    virtual bool set(UtcSeconds utc) = 0;
};

class TimeService {
public:
    // Anything earlier is treated as "clock never set": 2025-01-01T00:00:00Z.
    static constexpr UtcSeconds kMinValidUtc = 1735689600;

    TimeService(ISystemTime &system, IRtc *rtc) : system_(system), rtc_(rtc) {}

    // Reads the RTC and sets the system clock from it if plausible.
    TimeStatus boot();
    // SNTP has already stepped or slewed the system clock.
    void onNetworkSync();
    // User or API sets the time. Returns false if implausible.
    bool setManual(UtcSeconds utc);

    TimeStatus status() const { return status_.load(std::memory_order_acquire); }
    bool rtcPresent() const { return rtcOk_.load(std::memory_order_acquire); }

    static bool plausible(UtcSeconds t) { return t >= kMinValidUtc; }

private:
    void writeRtc(UtcSeconds t);

    ISystemTime &system_;
    IRtc *rtc_;
    std::atomic<TimeStatus> status_{TimeStatus::Unset};
    std::atomic<bool> rtcOk_{false};
};

} // namespace zf
