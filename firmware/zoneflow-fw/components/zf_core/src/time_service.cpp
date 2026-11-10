// SPDX-License-Identifier: Apache-2.0
#include "zf/time/time_service.hpp"

namespace zf {

TimeStatus TimeService::boot()
{
    UtcSeconds t = 0;
    if (rtc_ != nullptr && rtc_->read(t)) {
        rtcOk_.store(true, std::memory_order_release);
        if (plausible(t) && system_.set(t)) {
            status_.store(TimeStatus::FromRtc, std::memory_order_release);
            return TimeStatus::FromRtc;
        }
    }
    // The system clock may still be valid after a software reset (it keeps
    // counting across esp_restart). Trust it only if plausible.
    if (plausible(system_.now())) {
        status_.store(TimeStatus::FromRtc, std::memory_order_release);
        return TimeStatus::FromRtc;
    }
    status_.store(TimeStatus::Unset, std::memory_order_release);
    return TimeStatus::Unset;
}

void TimeService::onNetworkSync()
{
    const UtcSeconds now = system_.now();
    if (!plausible(now)) {
        return;
    }
    status_.store(TimeStatus::Synced, std::memory_order_release);
    writeRtc(now);
}

bool TimeService::setManual(UtcSeconds utc)
{
    if (!plausible(utc) || !system_.set(utc)) {
        return false;
    }
    status_.store(TimeStatus::Synced, std::memory_order_release);
    writeRtc(utc);
    return true;
}

void TimeService::writeRtc(UtcSeconds t)
{
    if (rtc_ != nullptr) {
        rtcOk_.store(rtc_->write(t), std::memory_order_release);
    }
}

} // namespace zf
