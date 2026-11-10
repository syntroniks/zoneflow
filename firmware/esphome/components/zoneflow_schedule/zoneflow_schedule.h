// SPDX-License-Identifier: Apache-2.0
// On-device scheduler for the ZoneFlow ESPHome package. Programs are made of
// ordinary entities (template datetime, select, switch, number) that Home
// Assistant shows and edits and that ESPHome keeps in flash. Once a second
// this component reads them, asks schedule_logic whether a start has come
// and, if nothing inhibits it, queues the program's zones on the sprinkler
// controller. It needs a valid clock (SNTP, or the PCF8563 after a power cut)
// and no network: watering carries on with Home Assistant down.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/datetime/time_entity.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sprinkler/sprinkler.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/zoneflow/zoneflow.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "schedule_logic.h"

namespace esphome::zoneflow_schedule {

namespace sched = ::zfe::sched;

struct Program {
  const char *name = "";
  switch_::Switch *enabled = nullptr;
  std::array<datetime::TimeEntity *, sched::kMaxStarts> starts{};
  std::array<switch_::Switch *, sched::kMaxStarts> start_enabled{};  // nullptr: always used
  select::Select *days = nullptr;
  std::array<switch_::Switch *, 7> weekdays{};  // Sunday first
  number::Number *scale = nullptr;               // percent of each zone's run duration
  text::Text *zones = nullptr;                   // "all" or "1,2,5"; nullptr = every zone
  bool obey_rain = true;
  // State.
  int64_t last_key = sched::kNoKey;
  uint32_t spec_hash = 0;
  ESPPreferenceObject pref;
};

class ZoneFlowSchedule : public Component {
 public:
  void set_sprinkler(sprinkler::Sprinkler *s) { this->sprinkler_ = s; }
  void set_time(time::RealTimeClock *t) { this->time_ = t; }
  void set_zoneflow(zoneflow::ZoneFlowComponent *z) { this->zoneflow_ = z; }
  void set_grace_minutes(uint16_t m) { this->grace_min_ = m; }
  void set_rain_sensor(binary_sensor::BinarySensor *s) { this->rain_ = s; }
  void set_rain_sensor_enabled(switch_::Switch *s) { this->rain_enabled_ = s; }
  void set_rain_delay_number(number::Number *n) { this->rain_delay_number_ = n; }
  void set_status_sensor(text_sensor::TextSensor *s) { this->status_ = s; }
  void set_next_run_sensor(text_sensor::TextSensor *s) { this->next_run_ = s; }
  void add_program(const Program &p) { this->programs_.push_back(p); }
  // The sprinkler's per-zone enable switches: a disabled zone is left out of
  // scheduled programs too (queued runs would otherwise ignore them).
  void set_zone_enable_switch(uint8_t zone, switch_::Switch *s) {
    if (zone < this->zone_enabled_.size())
      this->zone_enabled_[zone] = s;
  }

  float get_setup_priority() const override { return setup_priority::DATA; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Runs a program now (manual), with the same inhibits as a scheduled start
  // except the rain sensor and the rain delay.
  void run_program(size_t index);
  // 0 cancels. Kept in flash; survives a restart.
  void set_rain_delay_hours(float hours);
  float rain_delay_remaining_hours() const;
  bool rain_active() const;
  const std::string &next_run_text() const { return this->next_text_; }
  const std::string &status_text() const { return this->status_text_; }

 protected:
  sched::ProgramSpec spec_(const Program &p) const;
  static uint32_t hash_(const sched::ProgramSpec &s);
  // Why a start may not run now; nullptr if it may.
  const char *inhibit_(bool scheduled, bool obey_rain) const;
  bool start_(Program &p, bool scheduled);
  void set_status_(const std::string &text);
  void update_next_(const sched::LocalMinute &now);

  sprinkler::Sprinkler *sprinkler_{nullptr};
  time::RealTimeClock *time_{nullptr};
  zoneflow::ZoneFlowComponent *zoneflow_{nullptr};
  binary_sensor::BinarySensor *rain_{nullptr};
  switch_::Switch *rain_enabled_{nullptr};
  number::Number *rain_delay_number_{nullptr};
  text_sensor::TextSensor *status_{nullptr};
  text_sensor::TextSensor *next_run_{nullptr};
  std::vector<Program> programs_;
  std::array<switch_::Switch *, 8> zone_enabled_{};
  uint16_t grace_min_{60};
  int64_t rain_delay_until_{0};
  ESPPreferenceObject rain_pref_;
  uint32_t last_eval_ms_{0};
  bool rain_was_active_{false};
  std::string status_text_;
  std::string next_text_;
};

template<typename... Ts> class RunProgramAction : public Action<Ts...>, public Parented<ZoneFlowSchedule> {
 public:
  TEMPLATABLE_VALUE(uint8_t, program)
  // 1-based program number.
  void play(const Ts &...x) override {
    const uint8_t n = this->program_.value(x...);
    if (n > 0)
      this->parent_->run_program(n - 1);
  }
};

template<typename... Ts> class SetRainDelayAction : public Action<Ts...>, public Parented<ZoneFlowSchedule> {
 public:
  TEMPLATABLE_VALUE(float, hours)
  void play(const Ts &...x) override { this->parent_->set_rain_delay_hours(this->hours_.value(x...)); }
};

}  // namespace esphome::zoneflow_schedule
