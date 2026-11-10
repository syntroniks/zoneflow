// SPDX-License-Identifier: Apache-2.0
// Sensor logic: contact inputs with debounce, flow meter, 24 VAC presence.
// GPIO, PCNT and ADC access stays behind ISensorHal.
#pragma once

#include <array>
#include <cstdint>

#include "zf/types.hpp"

namespace zf {

enum class ContactType : uint8_t {
    NormallyOpen,   // contact closes when the sensor is active (wet)
    NormallyClosed, // contact opens when the sensor is active (most rain sensors)
};

// Two-sided debounce: the input must hold a new level for assertMs (going
// active) or releaseMs (going inactive) before the output follows. The first
// sample after reset is taken as-is so a wet sensor inhibits from boot.
class Debouncer {
public:
    void configure(uint32_t assertMs, uint32_t releaseMs)
    {
        assertMs_ = assertMs;
        releaseMs_ = releaseMs;
    }
    void reset() { primed_ = false; }

    // Returns true when the debounced output changed.
    bool update(bool raw, MonoMs now);
    bool value() const { return value_; }

private:
    uint32_t assertMs_ = 0;
    uint32_t releaseMs_ = 0;
    bool primed_ = false;
    bool value_ = false;
    bool candidate_ = false;
    MonoMs candidateSinceMs_ = 0;
};

struct ContactInputConfig {
    bool enabled = false;
    ContactType contact = ContactType::NormallyClosed;
    uint32_t debounceMs = 2000;
};

// Rain sensor or spare input.
class ContactSensor {
public:
    void configure(const ContactInputConfig &cfg);
    // Returns true when active() changed.
    bool update(bool contactClosed, MonoMs now);
    bool active() const { return cfg_.enabled && debouncer_.value(); }
    bool rawActive() const { return rawActive_; }

private:
    ContactInputConfig cfg_{};
    Debouncer debouncer_{};
    bool rawActive_ = false;
};

// 24 VAC presence: fast to declare loss, slower to declare return.
class AcMonitor {
public:
    static constexpr uint32_t kLossMs = 60;
    static constexpr uint32_t kReturnMs = 500;
    AcMonitor() { debouncer_.configure(kReturnMs, kLossMs); }
    bool update(bool present, MonoMs now) { return debouncer_.update(present, now); }
    bool present() const { return debouncer_.value(); }

private:
    Debouncer debouncer_{};
};

struct FlowConfig {
    bool enabled = false;
    float pulsesPerLitre = 450.0f; // K-factor from the meter's datasheet
    float leakThresholdLpm = 0.5f; // flow with all valves shut above this is a leak
    uint32_t leakDetectSec = 60;
};

class FlowMeter {
public:
    void configure(const FlowConfig &cfg);
    // pulseCounter is a free-running hardware counter; wrap-around is handled.
    void update(uint32_t pulseCounter, MonoMs now);

    uint64_t totalPulses() const { return total_; }
    float totalLitres() const;
    float rateLpm() const { return rateLpm_; }
    // Volume since the last markSession() call (e.g. per zone run).
    float sessionLitres() const;
    void markSession() { sessionStart_ = total_; }
    // Volume counted since the counter stood at startPulses (totalPulses()).
    float litresSince(uint64_t startPulses) const
    {
        return cfg_.pulsesPerLitre > 0.0f && total_ >= startPulses
                   ? static_cast<float>(total_ - startPulses) / cfg_.pulsesPerLitre
                   : 0.0f;
    }

private:
    static constexpr std::size_t kWindow = 6; // samples, one per second
    FlowConfig cfg_{};
    bool primed_ = false;
    uint32_t lastCounter_ = 0;
    uint64_t total_ = 0;
    uint64_t sessionStart_ = 0;
    std::array<uint64_t, kWindow> histPulses_{};
    std::array<MonoMs, kWindow> histMs_{};
    std::size_t histCount_ = 0;
    std::size_t histHead_ = 0;
    MonoMs lastSampleMs_ = 0;
    float rateLpm_ = 0.0f;
};

} // namespace zf
