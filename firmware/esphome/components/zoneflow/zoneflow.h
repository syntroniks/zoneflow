// SPDX-License-Identifier: Apache-2.0
// ESPHome component for the ZoneFlow rev A board: hardware keep-alive and
// fail-safe supervision, the valve outputs on the I2C expander, relay
// sequencing, current measurement and fault handling. The logic is in
// supervisor.h and the shared zf_core library; this file wires it to ESPHome.
//
// The main loop is the control loop. Every 50 ms it hands the supervisor its
// inputs; the zone driver then beats the keep-alive heartbeat. If the ESPHome
// loop stalls for more than 500 ms, the keep-alive interrupt stops making
// edges and the board removes valve drive and opens the valve-common relay in
// hardware (spec section 2 rule 3, hardware design 19.4).
#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"

#ifdef USE_OTA_STATE_LISTENER
#include "esphome/components/ota/ota_backend.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_BUTTON
#include "esphome/components/button/button.h"
#endif
#ifdef USE_VALVE
#include "esphome/components/valve/valve.h"
#endif
#ifdef USE_ZONEFLOW_SPRINKLER
#include "esphome/components/sprinkler/sprinkler.h"
#endif

#include "board_io.h"
#include "supervisor.h"
#include "zf/app/budget.hpp"
#include "zf/hw/reference_store.hpp"

namespace esphome::zoneflow {

class ZoneFlowComponent;

// This board's detector release times (hardware design 19.7, the limits of
// step 9) in ESPHome's preferences, in flash. The record is the standalone
// firmware's: magic, version, the two times, CRC-32. Writes are synced at
// once; they happen at commissioning and after a confirmed repair, with no
// valve on.
class PreferenceReferenceStore final : public zf::IDetectorReferenceStore {
 public:
  void init() { this->pref_ = global_preferences->make_preference<Blob>(fnv1_hash("zoneflow_ka_ref"), true); }
  bool load(zf::DetectorReference &out) override {
    Blob b{};
    return this->pref_.load(&b) && zf::hw::BlobReferenceStore::decode(std::string_view(b.data, sizeof b.data), out);
  }
  bool save(const zf::DetectorReference &r) override {
    Blob b{};
    const std::string s = zf::hw::BlobReferenceStore::encode(r);
    std::memcpy(b.data, s.data(), std::min(s.size(), sizeof b.data));
    return this->pref_.save(&b) && global_preferences->sync();
  }
  bool clear() override {
    Blob b{};  // all zero: fails the magic, reads as not stored
    return this->pref_.save(&b) && global_preferences->sync();
  }

 private:
  struct Blob {
    char data[14];
  };
  ESPPreferenceObject pref_;
};

// The run budget over a power cut (zf/app/budget.hpp): two copies of the
// standalone firmware's BudgetRecord in ESPHome's preferences, written
// alternately and synced at once, and a marker written with the first copy.
// ESPHome's preferences do not say whether a copy is missing or damaged, so
// a copy that does not load counts as damaged once the other copy's
// sequence number shows both were written, and both missing with the marker
// present counts as lost (the worst case).
class PreferenceBudgetStore {
 public:
  void init() {
    this->a_ = global_preferences->make_preference<zf::BudgetRecord>(fnv1_hash("zoneflow_budget_a"), true);
    this->b_ = global_preferences->make_preference<zf::BudgetRecord>(fnv1_hash("zoneflow_budget_b"), true);
    this->marker_ = global_preferences->make_preference<uint32_t>(fnv1_hash("zoneflow_budget_used"), true);
  }
  zf::RecordLoad load(zf::BudgetRecord &out) {
    zf::BudgetRecord a{};
    zf::BudgetRecord b{};
    const bool okA = this->a_.load(&a) && zf::intact(a);
    const bool okB = this->b_.load(&b) && zf::intact(b);
    uint32_t marker = 0;
    const bool used = this->marker_.load(&marker) && marker == zf::BudgetRecord::kMagic;
    const uint32_t newest = std::max(okA ? a.seq : 0u, okB ? b.seq : 0u);
    // A copy that did not load exists if the marker says one was written and
    // the newest valid copy is not the first write.
    const bool presentA = okA || (used && (!okB || newest >= 2));
    const bool presentB = okB || (used && (!okA || newest >= 2));
    return zf::chooseBudget(presentA, okA, a, presentB, okB, b, out, this->seq_);
  }
  bool save(zf::BudgetRecord r) {
    r.seq = ++this->seq_;
    zf::seal(r);
    bool ok = ((r.seq & 1u) != 0 ? this->a_ : this->b_).save(&r);
    if (r.seq == 1) {
      const uint32_t marker = zf::BudgetRecord::kMagic;
      ok = this->marker_.save(&marker) && ok;
    }
    return ok && global_preferences->sync();
  }

 private:
  ESPPreferenceObject a_;
  ESPPreferenceObject b_;
  ESPPreferenceObject marker_;
  uint32_t seq_ = 0;
};

enum class BinaryKind : uint8_t {
  AC_PRESENT,
  HARDWARE_FAULT,
  ANY_FAULT,
  LEAK,
  MASTER_VALVE,
  RELAY_CLOSED,
  SENSOR_SUPPLY_FAULT,
  CURRENT_SENSE_FAULT,
  ZONE_FAULT,
  ZONE_ACTIVE,
};
enum class SensorKind : uint8_t {
  VALVE_CURRENT,
  ZONE_CURRENT,
  NOISE_FLOOR,
  MAINS_FREQUENCY,
  SELF_TESTS_PASSED,
  ZONE_REMAINING,
};
enum class TextKind : uint8_t { OUTPUT_STAGE, FAULTS, ZONE_STATUS };
enum class ButtonKind : uint8_t { CLEAR_FAULTS, ACKNOWLEDGE_REPAIR, STOP_ALL, SELF_TEST };

class ZoneFlowComponent : public Component,
                          public i2c::I2CDevice
#ifdef USE_OTA_STATE_LISTENER
    ,
                          public ota::OTAGlobalStateListener
#endif
{
 public:
  // ---- configuration (codegen) ----
  void set_pins(const BoardPins &pins) { this->pins_ = pins; }
  zfe::SupervisorConfig &config() { return this->cfg_; }
  void set_layout(const zf::hw::ExpanderLayout &layout) { this->layout_ = layout; }
  void set_zone_name(uint8_t zone, const char *name) {
    if (zone < zf::kMaxZones)
      this->zone_names_[zone] = name;
  }
#ifdef USE_SENSOR
  void set_flow_rate_sensor(sensor::Sensor *s) { this->flow_rate_ = s; }
#endif
#ifdef USE_ZONEFLOW_SPRINKLER
  void set_sprinkler(sprinkler::Sprinkler *s) { this->sprinkler_ = s; }
#endif

  // ---- Component ----
  float get_setup_priority() const override { return setup_priority::HARDWARE; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void on_shutdown() override;
  void on_safe_shutdown() override;
#ifdef USE_OTA_STATE_LISTENER
  void on_ota_global_state(ota::OTAState state, float progress, uint8_t error, ota::OTAComponent *comp) override;
#endif

  // ---- commands (automations, lambdas, entities) ----
  // duration_sec 0: until stopped, at most the zone's maximum run time.
  bool start_zone(uint8_t zone, uint32_t duration_sec = 0);
  void stop_zone(uint8_t zone);
  // Stops every output and the sprinkler controller.
  void stop_all();
  void clear_faults();
  // A fault of a safety layer (self-test steps 1, 2, 2b, 5b, 9) needs this, pressed twice
  // within 10 s: the second press confirms the board was repaired.
  void acknowledge_repair_press();
  void request_self_test() {
    if (this->sup_)
      this->sup_->request_poll();
  }

  // ---- state ----
  bool ready() const { return this->sup_ != nullptr; }
  zf::ValveState zone_state(uint8_t zone) const;
  bool zone_active(uint8_t zone) const { return this->zone_state(zone) != zf::ValveState::Off; }
  uint32_t zone_remaining_s(uint8_t zone) const;
  bool zone_locked_out(uint8_t zone) const { return this->sup_ && this->sup_->zone_locked_out(zone); }
  uint8_t zone_count() const { return this->cfg_.zone_count; }
  uint8_t zones_available() const { return this->sup_ ? this->sup_->zones_available() : 0; }
  const char *zone_name(uint8_t zone) const {
    return zone < zf::kMaxZones && this->zone_names_[zone] ? this->zone_names_[zone] : "";
  }
  std::string zone_status_text(uint8_t zone) const;
  std::string faults_text() const;
  const char *stage_text() const;
  bool any_fault() const { return this->sup_ && this->sup_->any_latched_fault(); }
  bool hardware_fault() const { return this->sup_ && this->sup_->hardware_fault(); }
  bool ac_present() const { return this->sup_ && this->sup_->ac_present(); }
  bool master_on() const { return this->sup_ && this->sup_->master_on(); }
  int32_t current_ma() const { return this->sup_ ? this->sup_->current_ma() : -1; }
  zfe::Supervisor *supervisor() { return this->sup_.get(); }

  // ---- entities ----
  template<typename F> void add_on_fault_callback(F &&cb) { this->fault_callbacks_.add(std::forward<F>(cb)); }
  // Rule 17: true from just before step 2b to just after it. on_float_test
  // fires with true and false; display.yaml uses it to run the backlight PWM
  // at a mid duty and redraw the display meanwhile.
  bool float_test_active() const { return this->sup_ && this->sup_->float_test(); }
  template<typename F> void add_on_float_test_callback(F &&cb) {
    this->float_test_callbacks_.add(std::forward<F>(cb));
  }
  template<typename F> void add_on_state_callback(F &&cb) { this->state_callbacks_.add(std::forward<F>(cb)); }
#ifdef USE_BINARY_SENSOR
  void register_binary_sensor(binary_sensor::BinarySensor *s, BinaryKind kind, uint8_t zone) {
    this->binary_sensors_.push_back({s, kind, zone});
  }
#endif
#ifdef USE_SENSOR
  void register_sensor(sensor::Sensor *s, SensorKind kind, uint8_t zone) { this->sensors_.push_back({s, kind, zone}); }
#endif
#ifdef USE_TEXT_SENSOR
  void register_text_sensor(text_sensor::TextSensor *s, TextKind kind, uint8_t zone) {
    this->text_sensors_.push_back({s, kind, zone});
  }
#endif

 protected:
  // zf::IClock on esp_timer and the system clock.
  class Clock final : public zf::IClock {
   public:
    zf::UtcSeconds nowUtc() const override;
    zf::MonoMs monoMs() const override;
  };

  void publish_all_(bool periodic);
  void notify_faults_();

  BoardPins pins_{};
  zfe::SupervisorConfig cfg_{};
  zf::hw::ExpanderLayout layout_{};
  std::array<const char *, zf::kMaxZones> zone_names_{};
  Clock clock_;
  std::unique_ptr<ExpanderBus> bus_;
  std::unique_ptr<zfe::Supervisor> sup_;
  CurrentSampler sampler_;
  bool sensor_supply_on_ = false;
  bool timer_ok_ = false;
  bool sampler_ok_ = false;
  uint32_t last_tick_ms_ = 0;
  uint32_t last_publish_ms_ = 0;
  uint32_t published_seq_ = UINT32_MAX;
  uint32_t last_fault_bits_ = 0;
  bool last_hw_fault_ = false;
  std::array<uint8_t, zf::kMaxZones> last_zone_fault_{};
  uint32_t repair_armed_ms_ = 0;
  bool repair_armed_ = false;
#ifdef USE_SENSOR
  sensor::Sensor *flow_rate_{nullptr};
#endif
#ifdef USE_ZONEFLOW_SPRINKLER
  sprinkler::Sprinkler *sprinkler_{nullptr};
#endif
  CallbackManager<void(std::string)> fault_callbacks_;
  CallbackManager<void(bool)> float_test_callbacks_;
  bool last_float_test_ = false;
  PreferenceReferenceStore reference_;
  PreferenceBudgetStore budget_store_;
  float die_temp_c_ = 0.0f;
  bool die_temp_known_ = false;
  uint32_t die_temp_ms_ = 0;
  CallbackManager<void()> state_callbacks_;

#ifdef USE_BINARY_SENSOR
  struct BinaryEntry {
    binary_sensor::BinarySensor *s;
    BinaryKind kind;
    uint8_t zone;
  };
  std::vector<BinaryEntry> binary_sensors_;
#endif
#ifdef USE_SENSOR
  struct SensorEntry {
    sensor::Sensor *s;
    SensorKind kind;
    uint8_t zone;
  };
  std::vector<SensorEntry> sensors_;
#endif
#ifdef USE_TEXT_SENSOR
  struct TextEntry {
    text_sensor::TextSensor *s;
    TextKind kind;
    uint8_t zone;
  };
  std::vector<TextEntry> text_sensors_;
#endif
};

// ---- entity platforms ----------------------------------------------------------------------

#ifdef USE_SWITCH
// One valve output, as the sprinkler component's valve_switch_id. On: start
// the zone until stopped (bounded by its maximum run time); off: stop it. The
// state is what the output is doing, not what was asked: a refused start
// reads back off.
class ZoneSwitch : public switch_::Switch, public Component, public Parented<ZoneFlowComponent> {
 public:
  void set_zone(uint8_t zone) { this->zone_ = zone; }
  void setup() override;

 protected:
  void write_state(bool state) override;
  uint8_t zone_;
};
#endif

#ifdef USE_VALVE
// A zone as a Home Assistant valve. Open starts the zone through the sprinkler
// controller (its run duration and multiplier) when one is attached,
// otherwise directly until its maximum run time. Close stops it.
class ZoneValve : public valve::Valve, public Component, public Parented<ZoneFlowComponent> {
 public:
  void set_zone(uint8_t zone) { this->zone_ = zone; }
#ifdef USE_ZONEFLOW_SPRINKLER
  void set_sprinkler(sprinkler::Sprinkler *s) { this->sprinkler_ = s; }
#endif
  void setup() override;
  valve::ValveTraits get_traits() override;

 protected:
  void control(const valve::ValveCall &call) override;
  void update_();
  uint8_t zone_;
#ifdef USE_ZONEFLOW_SPRINKLER
  sprinkler::Sprinkler *sprinkler_{nullptr};
#endif
};
#endif

#ifdef USE_BUTTON
class ZoneFlowButton : public button::Button, public Parented<ZoneFlowComponent> {
 public:
  void set_kind(ButtonKind kind) { this->kind_ = kind; }

 protected:
  void press_action() override;
  ButtonKind kind_;
};
#endif

class FaultTrigger : public Trigger<std::string> {
 public:
  explicit FaultTrigger(ZoneFlowComponent *parent) {
    parent->add_on_fault_callback([this](const std::string &what) { this->trigger(what); });
  }
};

class FloatTestTrigger : public Trigger<bool> {
 public:
  explicit FloatTestTrigger(ZoneFlowComponent *parent) {
    parent->add_on_float_test_callback([this](bool running) { this->trigger(running); });
  }
};

// ---- actions -------------------------------------------------------------------------------

template<typename... Ts> class ClearFaultsAction : public Action<Ts...>, public Parented<ZoneFlowComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->clear_faults(); }
};

template<typename... Ts> class StopAllAction : public Action<Ts...>, public Parented<ZoneFlowComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->stop_all(); }
};

template<typename... Ts> class SelfTestAction : public Action<Ts...>, public Parented<ZoneFlowComponent> {
 public:
  void play(const Ts &...x) override { this->parent_->request_self_test(); }
};

}  // namespace esphome::zoneflow
