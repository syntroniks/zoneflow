// SPDX-License-Identifier: Apache-2.0
#include "zf/diag/sensors.hpp"

namespace zf {

bool Debouncer::update(bool raw, MonoMs now)
{
    if (!primed_) {
        primed_ = true;
        const bool changed = value_ != raw;
        value_ = raw;
        candidate_ = raw;
        candidateSinceMs_ = now;
        return changed;
    }
    if (raw != candidate_) {
        candidate_ = raw;
        candidateSinceMs_ = now;
    }
    if (candidate_ == value_) {
        return false;
    }
    const uint32_t hold = candidate_ ? assertMs_ : releaseMs_;
    if (now - candidateSinceMs_ >= hold) {
        value_ = candidate_;
        return true;
    }
    return false;
}

void ContactSensor::configure(const ContactInputConfig &cfg)
{
    const bool wasActive = active();
    cfg_ = cfg;
    debouncer_.configure(cfg.debounceMs, cfg.debounceMs);
    if (!cfg.enabled && wasActive) {
        debouncer_.reset();
    }
}

bool ContactSensor::update(bool contactClosed, MonoMs now)
{
    rawActive_ = cfg_.contact == ContactType::NormallyOpen ? contactClosed : !contactClosed;
    const bool before = active();
    debouncer_.update(rawActive_, now);
    return active() != before;
}

void FlowMeter::configure(const FlowConfig &cfg) { cfg_ = cfg; }

void FlowMeter::update(uint32_t pulseCounter, MonoMs now)
{
    if (!primed_) {
        primed_ = true;
        lastCounter_ = pulseCounter;
        lastSampleMs_ = now;
        histPulses_[0] = total_;
        histMs_[0] = now;
        histCount_ = 1;
        histHead_ = 1 % kWindow;
        return;
    }
    total_ += static_cast<uint32_t>(pulseCounter - lastCounter_); // unsigned wrap is intended
    lastCounter_ = pulseCounter;

    if (now - lastSampleMs_ < 1000) {
        return;
    }
    lastSampleMs_ = now;
    histPulses_[histHead_] = total_;
    histMs_[histHead_] = now;
    histHead_ = (histHead_ + 1) % kWindow;
    if (histCount_ < kWindow) {
        ++histCount_;
    }
    const std::size_t oldest = (histHead_ + kWindow - histCount_) % kWindow;
    const MonoMs spanMs = now - histMs_[oldest];
    if (!cfg_.enabled || cfg_.pulsesPerLitre <= 0.0f || spanMs == 0) {
        rateLpm_ = 0.0f;
        return;
    }
    const auto pulses = static_cast<float>(total_ - histPulses_[oldest]);
    rateLpm_ = pulses / cfg_.pulsesPerLitre * 60000.0f / static_cast<float>(spanMs);
}

float FlowMeter::totalLitres() const
{
    return cfg_.pulsesPerLitre > 0.0f ? static_cast<float>(total_) / cfg_.pulsesPerLitre : 0.0f;
}

float FlowMeter::sessionLitres() const
{
    return cfg_.pulsesPerLitre > 0.0f ? static_cast<float>(total_ - sessionStart_) / cfg_.pulsesPerLitre : 0.0f;
}

} // namespace zf
