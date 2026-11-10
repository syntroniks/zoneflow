// SPDX-License-Identifier: Apache-2.0
#include "zf/config/config.hpp"

#include <cstdio>
#include <string>

#include "zf/time/time_zone.hpp"

namespace zf {
namespace {

std::string idx(const char *base, std::size_t i, const char *field)
{
    return std::string(base) + "[" + std::to_string(i) + "]." + field;
}

ValidationResult checkWindow(const CurrentWindow &w, const std::string &path)
{
    if (w.maxMa != 0 && w.minMa > w.maxMa) {
        return ValidationResult::bad(path, "current_min_ma is above current_max_ma");
    }
    return ValidationResult::good();
}

} // namespace

Config Config::defaults()
{
    Config c;
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        char name[16];
        std::snprintf(name, sizeof name, "Zone %u", static_cast<unsigned>(i + 1));
        c.zones[i].name.assign(name);
    }
    return c;
}

ValidationResult validate(const Config &cfg)
{
    using VR = ValidationResult;
    if (cfg.deviceName.empty()) {
        return VR::bad("device_name", "must not be empty");
    }
    if (!TimeZone::parse(cfg.timezone.view())) {
        return VR::bad("timezone", "not a valid POSIX TZ string");
    }
    if (cfg.zoneCount < 1 || cfg.zoneCount > kMaxZones) {
        return VR::bad("zone_count", "must be 1.." + std::to_string(kMaxZones));
    }
    for (std::size_t i = 0; i < kMaxZones; ++i) {
        const ZoneConfig &z = cfg.zones[i];
        if (z.maxRunSec < 1 || z.maxRunSec > kHardMaxRunSec) {
            return VR::bad(idx("zones", i, "max_run_s"), "must be 1.." + std::to_string(kHardMaxRunSec));
        }
        if (auto r = checkWindow(z.current, idx("zones", i, "current_min_ma")); !r.ok) {
            return r;
        }
    }
    if (cfg.master.preOpenMs > 60000) {
        return VR::bad("master.pre_open_ms", "must be 0..60000");
    }
    if (cfg.master.postCloseMs > 600000) {
        return VR::bad("master.post_close_ms", "must be 0..600000");
    }
    if (auto r = checkWindow(cfg.master.current, "master.current_min_ma"); !r.ok) {
        return r;
    }
    if (cfg.driver.maxConcurrent < 1 || cfg.driver.maxConcurrent > kMaxConcurrentZonesLimit) {
        return VR::bad("driver.max_concurrent", "must be 1.." + std::to_string(kMaxConcurrentZonesLimit));
    }
    if (cfg.driver.interZoneDelayMs > 600000) {
        return VR::bad("driver.inter_zone_delay_ms", "must be 0..600000");
    }
    if (cfg.driver.minRestSec < 60 || cfg.driver.minRestSec > 86400) {
        return VR::bad("driver.min_rest_s", "must be 60..86400");
    }
    if (cfg.rain.debounceMs > 600000) {
        return VR::bad("sensors.rain.debounce_ms", "must be 0..600000");
    }
    if (cfg.spare.debounceMs > 600000) {
        return VR::bad("sensors.spare.debounce_ms", "must be 0..600000");
    }
    if (!(cfg.flow.pulsesPerLitre > 0.0f && cfg.flow.pulsesPerLitre <= 100000.0f)) {
        return VR::bad("sensors.flow.pulses_per_litre", "must be above 0 and at most 100000");
    }
    if (!(cfg.flow.leakThresholdLpm >= 0.0f && cfg.flow.leakThresholdLpm <= 1000.0f)) {
        return VR::bad("sensors.flow.leak_threshold_lpm", "must be 0..1000");
    }
    if (cfg.flow.leakDetectSec < 1 || cfg.flow.leakDetectSec > 86400) {
        return VR::bad("sensors.flow.leak_detect_s", "must be 1..86400");
    }
    if (cfg.diag.settleMs < 50 || cfg.diag.settleMs > 10000) {
        return VR::bad("diagnostics.settle_ms", "must be 50..10000");
    }
    if (cfg.diag.shortCircuitMa <= cfg.diag.idleMaxMa) {
        return VR::bad("diagnostics.short_circuit_ma", "must be above idle_max_ma");
    }
    if (cfg.diag.confirmSamples < 1 || cfg.diag.confirmSamples > 20) {
        return VR::bad("diagnostics.confirm_samples", "must be 1..20");
    }

    const ScheduleConfig &s = cfg.schedule;
    if (s.seasonalPercent > 250) {
        return VR::bad("schedule.seasonal_percent", "must be 0..250");
    }
    if (s.missedStartGraceMin > 720) {
        return VR::bad("schedule.missed_start_grace_min", "must be 0..720");
    }
    if (s.programCount > kMaxPrograms) {
        return VR::bad("schedule.programs", "at most " + std::to_string(kMaxPrograms) + " programs");
    }
    for (std::size_t i = 0; i < s.programCount; ++i) {
        const Program &p = s.programs[i];
        if (p.id == 0) {
            return VR::bad(idx("schedule.programs", i, "id"), "must be non-zero");
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (s.programs[j].id == p.id) {
                return VR::bad(idx("schedule.programs", i, "id"), "duplicate id");
            }
        }
        if (p.startCount > kMaxStartTimes) {
            return VR::bad(idx("schedule.programs", i, "starts"),
                           "at most " + std::to_string(kMaxStartTimes) + " start times");
        }
        for (std::size_t k = 0; k < p.startCount; ++k) {
            if (p.startMinutes[k] >= 1440) {
                return VR::bad(idx("schedule.programs", i, "starts"), "start time out of range");
            }
        }
        if (p.days.type == DayRuleType::Weekdays && p.days.weekdayMask > 0x7F) {
            return VR::bad(idx("schedule.programs", i, "days"), "invalid weekday mask");
        }
        if (p.days.type == DayRuleType::Interval && (p.days.intervalDays < 1 || p.days.intervalDays > 365)) {
            return VR::bad(idx("schedule.programs", i, "days.every"), "must be 1..365");
        }
        for (std::size_t z = 0; z < kMaxZones; ++z) {
            if (p.durationSec[z] > kHardMaxRunSec) {
                return VR::bad(idx("schedule.programs", i, "durations_s"),
                               "duration above " + std::to_string(kHardMaxRunSec) + " s");
            }
        }
    }
    return VR::good();
}

} // namespace zf
