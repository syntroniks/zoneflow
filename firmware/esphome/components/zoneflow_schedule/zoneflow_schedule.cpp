// SPDX-License-Identifier: Apache-2.0
#include "zoneflow_schedule.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::zoneflow_schedule {

static const char *const TAG = "zoneflow.schedule";
static constexpr const char *kWeekday[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

void ZoneFlowSchedule::setup() {
  for (size_t i = 0; i < this->programs_.size(); ++i) {
    auto &p = this->programs_[i];
    p.pref = global_preferences->make_preference<int64_t>(fnv1_hash(std::string("zfsched.") + p.name));
    int64_t key = sched::kNoKey;
    if (p.pref.load(&key))
      p.last_key = key;
  }
  this->rain_pref_ = global_preferences->make_preference<int64_t>(fnv1_hash("zfsched.rain_delay"));
  int64_t until = 0;
  if (this->rain_pref_.load(&until))
    this->rain_delay_until_ = until;
}

void ZoneFlowSchedule::dump_config() {
  ESP_LOGCONFIG(TAG, "ZoneFlow schedule: %u program(s), missed-start grace %u min", (unsigned) this->programs_.size(),
                this->grace_min_);
  for (const auto &p : this->programs_)
    ESP_LOGCONFIG(TAG, "  %s%s", p.name, p.obey_rain ? ", obeys rain" : "");
}

sched::ProgramSpec ZoneFlowSchedule::spec_(const Program &p) const {
  sched::ProgramSpec s;
  s.enabled = p.enabled != nullptr && p.enabled->state;
  for (size_t i = 0; i < sched::kMaxStarts; ++i) {
    const auto *t = p.starts[i];
    const bool used = p.start_enabled[i] == nullptr || p.start_enabled[i]->state;
    s.start_minute[i] =
        used && t != nullptr && t->has_state() ? static_cast<int16_t>(t->hour * 60 + t->minute) : -1;
  }
  int choice = 0;
  if (p.days != nullptr && p.days->has_state())
    choice = sched::day_choice_from_label(p.days->current_option().c_str());
  uint8_t mask = 0;
  for (size_t d = 0; d < 7; ++d) {
    if (p.weekdays[d] != nullptr && p.weekdays[d]->state)
      mask |= static_cast<uint8_t>(1u << d);
  }
  s.days = sched::day_rule(static_cast<sched::DayChoice>(choice < 0 ? 0 : choice), mask);
  return s;
}

uint32_t ZoneFlowSchedule::hash_(const sched::ProgramSpec &s) {
  uint32_t h = s.enabled ? 1u : 0u;
  for (auto m : s.start_minute)
    h = h * 31u + static_cast<uint16_t>(m);
  h = h * 31u + static_cast<uint32_t>(s.days.type);
  h = h * 31u + s.days.weekdayMask;
  h = h * 31u + s.days.intervalDays;
  return h;
}

bool ZoneFlowSchedule::rain_active() const {
  const bool enabled = this->rain_enabled_ == nullptr || this->rain_enabled_->state;
  return enabled && this->rain_ != nullptr && this->rain_->has_state() && this->rain_->state;
}

const char *ZoneFlowSchedule::inhibit_(bool scheduled, bool obey_rain) const {
  if (this->zoneflow_ != nullptr) {
    if (!this->zoneflow_->ready())
      return "controller not ready";
    if (this->zoneflow_->hardware_fault())
      return "output stage fault";
    if (!this->zoneflow_->ac_present())
      return "no 24 VAC";
  }
  if (scheduled && obey_rain) {
    if (this->rain_active())
      return "rain sensor";
    if (sched::rain_delay_active(this->rain_delay_until_, static_cast<int64_t>(::time(nullptr))))
      return "rain delay";
  }
  return nullptr;
}

bool ZoneFlowSchedule::start_(Program &p, bool scheduled) {
  const char *why = this->inhibit_(scheduled, p.obey_rain);
  if (why != nullptr) {
    this->set_status_(std::string(p.name) + " skipped: " + why);
    return false;
  }
  auto active = this->sprinkler_->active_valve();
  if (active.has_value()) {
    auto origin = this->sprinkler_->active_valve_request_is_from();
    if (!origin.has_value() || origin.value() != sprinkler::QUEUE) {
      this->set_status_(std::string(p.name) + " skipped: a manual run is in progress");
      return false;
    }
  }
  const float scale =
      p.scale != nullptr && p.scale->has_state() && !std::isnan(p.scale->state) ? p.scale->state / 100.0f : 1.0f;
  const auto valves = static_cast<uint8_t>(std::min<size_t>(this->sprinkler_->number_of_valves(), 8));
  const uint8_t mask = sched::parse_zone_list(
      p.zones != nullptr && p.zones->has_state() ? p.zones->state.c_str() : nullptr, valves);
  size_t queued = 0;
  for (uint8_t z = 0; z < valves; ++z) {
    if ((mask & (1u << z)) == 0)
      continue;
    if (!this->sprinkler_->is_a_valid_valve(z))
      continue;
    if (z < this->zone_enabled_.size() && this->zone_enabled_[z] != nullptr && !this->zone_enabled_[z]->state)
      continue;
    // valve_run_duration_adjusted() applies the sprinkler multiplier (the
    // seasonal adjustment); the program's scale comes on top.
    const auto sec = static_cast<uint32_t>(std::lround(this->sprinkler_->valve_run_duration_adjusted(z) * scale));
    if (sec == 0)
      continue;
    this->sprinkler_->queue_valve(z, sec);
    ++queued;
  }
  if (queued == 0) {
    this->set_status_(std::string(p.name) + " skipped: nothing to run");
    return false;
  }
  if (!active.has_value())
    this->sprinkler_->start_from_queue();
  char buf[96];
  snprintf(buf, sizeof buf, "%s started (%u zone%s%s)", p.name, (unsigned) queued, queued == 1 ? "" : "s",
           scheduled ? "" : ", manual");
  this->set_status_(buf);
  return true;
}

void ZoneFlowSchedule::run_program(size_t index) {
  if (index >= this->programs_.size())
    return;
  this->start_(this->programs_[index], false);
}

void ZoneFlowSchedule::set_rain_delay_hours(float hours) {
  const auto now = static_cast<int64_t>(::time(nullptr));
  this->rain_delay_until_ = hours > 0.0f ? now + static_cast<int64_t>(std::lround(hours * 3600.0f)) : 0;
  this->rain_pref_.save(&this->rain_delay_until_);
  if (hours > 0.0f && this->sprinkler_ != nullptr) {
    // A rain delay set now also ends a scheduled run in progress.
    auto origin = this->sprinkler_->active_valve_request_is_from();
    if (origin.has_value() && origin.value() == sprinkler::QUEUE)
      this->sprinkler_->shutdown(true);
  }
  this->set_status_(hours > 0.0f ? "rain delay set" : "rain delay cancelled");
}

float ZoneFlowSchedule::rain_delay_remaining_hours() const {
  const auto now = static_cast<int64_t>(::time(nullptr));
  if (!sched::rain_delay_active(this->rain_delay_until_, now))
    return 0.0f;
  return static_cast<float>(this->rain_delay_until_ - now) / 3600.0f;
}

void ZoneFlowSchedule::set_status_(const std::string &text) {
  ESP_LOGI(TAG, "%s", text.c_str());
  this->status_text_ = text;
  if (this->status_ != nullptr)
    this->status_->publish_state(text);
}

void ZoneFlowSchedule::update_next_(const sched::LocalMinute &now) {
  int64_t best = sched::kNoKey;
  const char *name = "";
  for (const auto &p : this->programs_) {
    const int64_t k = sched::next_start(this->spec_(p), now);
    if (k != sched::kNoKey && (best == sched::kNoKey || k < best)) {
      best = k;
      name = p.name;
    }
  }
  std::string text = "none";
  if (best != sched::kNoKey) {
    const auto parts = sched::key_parts(best);
    char buf[64];
    snprintf(buf, sizeof buf, "%s %02d:%02d %s", kWeekday[parts.weekday % 7], parts.hour, parts.minute, name);
    text = buf;
  }
  if (text != this->next_text_) {
    this->next_text_ = text;
    if (this->next_run_ != nullptr)
      this->next_run_->publish_state(text);
  }
}

void ZoneFlowSchedule::loop() {
  const uint32_t ms = millis();
  if (ms - this->last_eval_ms_ < 1000)
    return;
  this->last_eval_ms_ = ms;

  // Rain sensor going active removes a scheduled run in progress (manual runs
  // carry on), as in the standalone firmware.
  const bool rain = this->rain_active();
  if (rain && !this->rain_was_active_ && this->sprinkler_ != nullptr) {
    auto origin = this->sprinkler_->active_valve_request_is_from();
    if (origin.has_value() && origin.value() == sprinkler::QUEUE) {
      this->sprinkler_->shutdown(true);
      this->set_status_("scheduled run stopped: rain sensor");
    }
  }
  this->rain_was_active_ = rain;

  if (this->rain_delay_number_ != nullptr) {
    const float left = std::ceil(this->rain_delay_remaining_hours());
    if (!this->rain_delay_number_->has_state() || this->rain_delay_number_->state != left)
      this->rain_delay_number_->publish_state(left);
  }

  const ESPTime now = this->time_->now();
  if (!now.is_valid())
    return;  // no scheduled start without a valid clock; manual runs still work
  const sched::LocalMinute lm = sched::local_minute(now.year, now.month, now.day_of_month, now.hour, now.minute);
  for (auto &p : this->programs_) {
    const sched::ProgramSpec spec = this->spec_(p);
    const uint32_t h = hash_(spec);
    const int64_t before = p.last_key;
    if (h != p.spec_hash) {
      // An edit (start time, days, enabled) never starts a time that passed.
      if (p.spec_hash != 0)
        sched::rearm(lm, p.last_key);
      p.spec_hash = h;
    }
    const sched::Decision d = sched::evaluate(spec, lm, this->grace_min_, p.last_key);
    if (d.missed > 0)
      this->set_status_(std::string(p.name) + ": " + std::to_string(d.missed) + " start(s) missed");
    if (d.run)
      this->start_(p, true);
    if (p.last_key != before)
      p.pref.save(&p.last_key);
  }
  this->update_next_(lm);
}

}  // namespace esphome::zoneflow_schedule
