// SPDX-License-Identifier: Apache-2.0
#include "zoneflow.h"

#include <cinttypes>
#include <cmath>
#include <cstring>
#include <ctime>

#include "esp_attr.h"
#include "esp_timer.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::zoneflow {

static const char *const TAG = "zoneflow";

// Run budget, latched output-stage fault and recent trips over a software
// restart (firmware architecture section 20). Raw bytes: a constructor would
// zero it at start-up. A power cycle or brownout loses it.
RTC_NOINIT_ATTR alignas(4) static uint8_t s_retained[sizeof(zf::RetainedRecord)];

static constexpr uint32_t kTickMs = 50;  // hardware design 19.7 step 7: one control tick
static constexpr uint32_t kRepairConfirmMs = 10000;

zf::UtcSeconds ZoneFlowComponent::Clock::nowUtc() const { return static_cast<zf::UtcSeconds>(::time(nullptr)); }
zf::MonoMs ZoneFlowComponent::Clock::monoMs() const { return static_cast<zf::MonoMs>(esp_timer_get_time() / 1000); }

void ZoneFlowComponent::setup() {
  // Outputs off before anything else: the keep-alive pin low and an output.
  // The expander is in reset while the keep-alive is stopped, so every valve
  // bit and the relay bit are off in hardware (hardware design 8.2).
  if (!init_output_stage_pins(this->pins_)) {
    this->mark_failed();
    return;
  }
  this->bus_ = std::make_unique<ExpanderBus>(this);
  this->cfg_.stage.tickMs = kTickMs;
  this->sup_ = std::make_unique<zfe::Supervisor>(*this->bus_, keepalive_guard(), this->clock_, this->layout_, this->cfg_);

  zf::RetainedRecord rec;
  std::memcpy(&rec, s_retained, sizeof rec);
  const bool restored = zf::intact(rec);
  this->sup_->begin(restored ? &rec : nullptr);

  this->timer_ok_ = start_keepalive_timer(this->pins_);
  if (!this->timer_ok_) {
    // Without the interrupt the keep-alive never toggles, so nothing can turn
    // on. Report it; the device stays reachable for diagnosis.
    this->status_set_error(LOG_STR("keep-alive timer failed"));
  }
  this->sampler_ok_ = this->sampler_.start(this->pins_);
  // Rule 13: the sensor supply once the application runs.
  this->sensor_supply_on_ = enable_sensor_supply(this->pins_);
#ifdef USE_OTA_STATE_LISTENER
  ota::get_global_ota_callback()->add_global_state_listener(this);
#endif
  ESP_LOGI(TAG, "Output stage ready to self-test; run budget %s", restored ? "restored" : "empty");
}

void ZoneFlowComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "ZoneFlow rev A\n"
                "  Zones configured: %u, master valve: %s\n"
                "  Keep-alive GPIO%d, read-backs GPIO%d (drive) GPIO%d (relay), latch GPIO%d, AC_ZC GPIO%d\n"
                "  Current sense GPIO%d (high gain) GPIO%d (low gain): %s\n"
                "  Rest time %" PRIu32 " s, at most %u zone(s) at once",
                this->cfg_.zone_count, YESNO(this->cfg_.master_enabled), this->pins_.keepalive,
                this->pins_.drive_readback, this->pins_.relay_readback, this->pins_.overcurrent,
                this->pins_.zero_cross, this->pins_.current_high, this->pins_.current_low,
                this->sampler_ok_ ? "running" : "UNAVAILABLE", this->cfg_.min_rest_sec, this->cfg_.max_concurrent);
  LOG_I2C_DEVICE(this);
  for (uint8_t z = 0; z < this->cfg_.zone_count; ++z) {
    const auto &zs = this->cfg_.zones[z];
    ESP_LOGCONFIG(TAG, "  Zone %u '%s': max run %" PRIu32 " s, current window %u to %u mA", z + 1,
                  this->zone_name(z), zs.max_run_sec, zs.min_ma, zs.max_ma);
  }
}

void ZoneFlowComponent::loop() {
  if (!this->sup_)
    return;
  const uint32_t now = millis();
  if (now - this->last_tick_ms_ < kTickMs)
    return;
  this->last_tick_ms_ = now;

  zfe::SupervisorInputs in;
  const uint32_t us = isr_micros();
  in.ac_present = this->timer_ok_ && zero_cross().present(us);
  in.mains_milli_hz = zero_cross().milliHz(us);
  in.current_sense_available = this->sampler_ok_ && this->sampler_.available();
  if (this->sampler_ok_)
    in.window = this->sampler_.latest();
  in.sensor_supply_on = this->sensor_supply_on_;
#ifdef USE_SENSOR
  if (this->flow_rate_ != nullptr && this->flow_rate_->has_state() && !std::isnan(this->flow_rate_->state))
    in.flow_lpm = this->flow_rate_->state;
#endif
  this->sup_->tick(in);

  zf::RetainedRecord rec;
  this->sup_->save_retained(rec);
  std::memcpy(s_retained, &rec, sizeof rec);

  this->notify_faults_();
  const bool periodic = now - this->last_publish_ms_ >= 1000;
  if (periodic || this->sup_->change_seq() != this->published_seq_) {
    this->published_seq_ = this->sup_->change_seq();
    if (periodic)
      this->last_publish_ms_ = now;
    this->publish_all_(periodic);
  }
  if (this->repair_armed_ && now - this->repair_armed_ms_ > kRepairConfirmMs)
    this->repair_armed_ = false;
}

void ZoneFlowComponent::notify_faults_() {
  // New latched faults: stop the sprinkler controller, so it does not move on
  // to the next zone into the same fault, and fire on_fault.
  const uint32_t bits = this->sup_->general_faults() & ~zfe::kConditionMask;
  const bool hw = this->sup_->hardware_fault();
  std::string what;
  // Stop the cycle on faults that stop everything: output stage, shorts,
  // over-current, trips. An open or DC-faulted zone is only skipped.
  bool stop_cycle = (hw && !this->last_hw_fault_) || (bits & ~this->last_fault_bits_) != 0;
  if (hw && !this->last_hw_fault_)
    what = this->faults_text();
  if ((bits & ~this->last_fault_bits_) != 0 && what.empty())
    what = this->faults_text();
  for (uint8_t z = 0; z < zf::kMaxZones; ++z) {
    const auto f = static_cast<uint8_t>(this->sup_->zone_fault(z));
    const zfe::ZoneFault zf_ = this->sup_->zone_fault(z);
    if (f != 0 && f != this->last_zone_fault_[z]) {
      if (what.empty())
        what = std::string("zone ") + std::to_string(z + 1) + ": " + zfe::to_string(zf_);
      stop_cycle = stop_cycle || (zf_ != zfe::ZoneFault::OpenCircuit && zf_ != zfe::ZoneFault::DcCurrent);
    }
    this->last_zone_fault_[z] = f;
  }
  this->last_fault_bits_ = bits;
  this->last_hw_fault_ = hw;
  if (hw || this->sup_->any_latched_fault())
    this->status_set_warning(LOG_STR("fault: see the Faults sensor"));
  else
    this->status_clear_warning();
  if (what.empty())
    return;
  ESP_LOGW(TAG, "Fault: %s", what.c_str());
#ifdef USE_ZONEFLOW_SPRINKLER
  if (this->sprinkler_ != nullptr && stop_cycle)
    this->sprinkler_->shutdown(true);
#endif
  this->fault_callbacks_.call(what);
}

void ZoneFlowComponent::publish_all_(bool periodic) {
  auto *s = this->sup_.get();
#ifdef USE_BINARY_SENSOR
  for (auto &e : this->binary_sensors_) {
    bool v = false;
    switch (e.kind) {
      case BinaryKind::AC_PRESENT:
        v = s->ac_present();
        break;
      case BinaryKind::HARDWARE_FAULT:
        v = s->hardware_fault();
        break;
      case BinaryKind::ANY_FAULT:
        v = s->any_latched_fault();
        break;
      case BinaryKind::LEAK:
        v = s->leak();
        break;
      case BinaryKind::MASTER_VALVE:
        v = s->master_on();
        break;
      case BinaryKind::RELAY_CLOSED:
        v = s->stage().relayClosed;
        break;
      case BinaryKind::SENSOR_SUPPLY_FAULT:
        v = (s->general_faults() & zfe::kCondSensorSupply) != 0;
        break;
      case BinaryKind::CURRENT_SENSE_FAULT:
        v = (s->general_faults() & zfe::kCondCurrentSenseUnavailable) != 0;
        break;
      case BinaryKind::ZONE_FAULT:
        v = s->zone_fault(e.zone) != zfe::ZoneFault::None || s->zone_locked_out(e.zone);
        break;
      case BinaryKind::ZONE_ACTIVE:
        v = s->zone_state(e.zone) == zf::ValveState::Open;
        break;
    }
    if (!e.s->has_state() || e.s->state != v)
      e.s->publish_state(v);
  }
#endif
#ifdef USE_SENSOR
  for (auto &e : this->sensors_) {
    float v = NAN;
    switch (e.kind) {
      case SensorKind::VALVE_CURRENT:
        if (!periodic)
          continue;
        v = s->current_ma() >= 0 ? static_cast<float>(s->current_ma()) : NAN;
        break;
      case SensorKind::ZONE_CURRENT:
        v = s->zone_last_run_ma(e.zone) != 0 ? static_cast<float>(s->zone_last_run_ma(e.zone)) : NAN;
        break;
      case SensorKind::NOISE_FLOOR:
        v = static_cast<float>(s->noise_floor_ma());
        break;
      case SensorKind::MAINS_FREQUENCY: {
        if (!periodic)
          continue;
        const uint32_t mhz = zero_cross().milliHz(isr_micros());
        v = mhz != 0 ? static_cast<float>(mhz) / 1000.0f : NAN;
        break;
      }
      case SensorKind::SELF_TESTS_PASSED:
        v = static_cast<float>(s->stage().selfTestsPassed);
        break;
      case SensorKind::ZONE_REMAINING:
        v = static_cast<float>(this->zone_remaining_s(e.zone));
        break;
    }
    const bool same = e.s->has_state() && (e.s->state == v || (std::isnan(e.s->state) && std::isnan(v)));
    if (!same || (periodic && e.kind == SensorKind::VALVE_CURRENT))
      e.s->publish_state(v);
  }
#endif
#ifdef USE_TEXT_SENSOR
  for (auto &e : this->text_sensors_) {
    std::string v;
    switch (e.kind) {
      case TextKind::OUTPUT_STAGE:
        v = this->stage_text();
        break;
      case TextKind::FAULTS:
        v = this->faults_text();
        break;
      case TextKind::ZONE_STATUS:
        v = this->zone_status_text(e.zone);
        break;
    }
    if (!e.s->has_state() || e.s->state != v)
      e.s->publish_state(v);
  }
#endif
  this->state_callbacks_.call();
}

const char *ZoneFlowComponent::stage_text() const {
  if (!this->sup_)
    return "not started";
  return zf::toString(this->sup_->stage().phase);
}

std::string ZoneFlowComponent::faults_text() const {
  if (!this->sup_)
    return "not started";
  auto *s = this->sup_.get();
  std::string out;
  auto add = [&out](const std::string &t) {
    if (!out.empty())
      out += ", ";
    out += t;
  };
  const auto &st = s->stage();
  if (s->hardware_fault()) {
    add(std::string("output stage: ") + zf::toString(st.fault) + " (step " +
        std::to_string(zf::hwCheckStep(st.fault)) + (s->repair_needed() ? ", repair needed)" : ")"));
  }
  const uint32_t g = s->general_faults();
  if (g & zfe::kFaultCurrentWhileIdle)
    add("current with every valve off");
  if (g & zfe::kFaultOverCurrentTrip)
    add("over-current trip");
  if (g & zfe::kFaultMaster)
    add("master valve current");
  if (g & zfe::kFaultMasterLockedOut)
    add("master valve locked out");
  if (g & zfe::kFaultDcCurrent)
    add("DC current");
  if (g & zfe::kFaultLeak)
    add("flow with every valve shut");
  if (g & zfe::kCondNoAc)
    add("no 24 VAC");
  if (g & zfe::kCondCurrentSenseUnavailable)
    add("current sense unavailable");
  if (g & zfe::kCondSensorSupply)
    add("sensor supply fault");
  if (g & zfe::kCondMainsSignal)
    add("zero-cross signal implausible");
  for (uint8_t z = 0; z < this->cfg_.zone_count; ++z) {
    if (s->zone_fault(z) != zfe::ZoneFault::None)
      add("zone " + std::to_string(z + 1) + " " + zfe::to_string(s->zone_fault(z)));
    else if (s->zone_locked_out(z))
      add("zone " + std::to_string(z + 1) + " locked out");
  }
  if (s->maintenance())
    add("maintenance (OTA)");
  return out.empty() ? "none" : out;
}

zf::ValveState ZoneFlowComponent::zone_state(uint8_t zone) const {
  return this->sup_ ? this->sup_->zone_state(zone) : zf::ValveState::Off;
}

uint32_t ZoneFlowComponent::zone_remaining_s(uint8_t zone) const {
  if (!this->sup_ || this->sup_->zone_state(zone) == zf::ValveState::Off)
    return 0;
  return (this->sup_->zone_remaining_ms(zone) + 999) / 1000;
}

std::string ZoneFlowComponent::zone_status_text(uint8_t zone) const {
  if (!this->sup_)
    return "off";
  auto *s = this->sup_.get();
  if (zone >= s->zones_available())
    return "not fitted";
  if (s->zone_fault(zone) != zfe::ZoneFault::None)
    return std::string("fault: ") + zfe::to_string(s->zone_fault(zone));
  if (s->zone_locked_out(zone))
    return "locked out";
  switch (s->zone_state(zone)) {
    case zf::ValveState::Open:
      return "open";
    case zf::ValveState::Waiting:
      return "starting";
    case zf::ValveState::Off:
      break;
  }
  const uint32_t rest = s->zone_rest_remaining_ms(zone);
  if (rest > 0)
    return "resting " + std::to_string((rest + 59999) / 60000) + " min";
  return "off";
}

bool ZoneFlowComponent::start_zone(uint8_t zone, uint32_t duration_sec) {
  if (!this->sup_ || zone >= this->cfg_.zone_count)
    return false;
  const zf::StartResult r = this->sup_->start_zone(zone, duration_sec);
  if (!r.ok()) {
    ESP_LOGW(TAG, "Zone %u '%s' not started: %s", zone + 1, this->zone_name(zone), zf::toString(r.error));
    return false;
  }
  ESP_LOGI(TAG, "Zone %u '%s' accepted for %" PRIu32 " s", zone + 1, this->zone_name(zone), r.acceptedSec);
  return true;
}

void ZoneFlowComponent::stop_zone(uint8_t zone) {
  if (this->sup_)
    this->sup_->stop_zone(zone);
}

void ZoneFlowComponent::stop_all() {
#ifdef USE_ZONEFLOW_SPRINKLER
  if (this->sprinkler_ != nullptr)
    this->sprinkler_->shutdown(true);
#endif
  if (this->sup_)
    this->sup_->stop_all();
}

void ZoneFlowComponent::clear_faults() {
  if (!this->sup_)
    return;
  this->sup_->clear_faults();
  ESP_LOGI(TAG, "Faults cleared by a person: %s", this->faults_text().c_str());
}

void ZoneFlowComponent::acknowledge_repair_press() {
  if (!this->sup_)
    return;
  if (!this->sup_->repair_needed()) {
    this->clear_faults();
    return;
  }
  const uint32_t now = millis();
  if (!this->repair_armed_ || now - this->repair_armed_ms_ > kRepairConfirmMs) {
    this->repair_armed_ = true;
    this->repair_armed_ms_ = now;
    ESP_LOGW(TAG, "A keep-alive detector failed its self-test. Press again within 10 s to confirm the board was "
                  "repaired.");
    return;
  }
  this->repair_armed_ = false;
  const bool ok = this->sup_->acknowledge_repair(true);
  ESP_LOGW(TAG, "Repair acknowledged by a person: %s", ok ? "fault cleared" : "fault stays");
}

void ZoneFlowComponent::on_shutdown() {
  // Restart, OTA reboot, deep sleep: everything off and the guard tripped,
  // so the interrupt cannot drive the keep-alive again before the reset.
  force_outputs_off();
  if (this->sup_)
    this->sup_->emergency_off();
}

void ZoneFlowComponent::on_safe_shutdown() { this->on_shutdown(); }

#ifdef USE_OTA_STATE_LISTENER
void ZoneFlowComponent::on_ota_global_state(ota::OTAState state, float progress, uint8_t error,
                                            ota::OTAComponent *comp) {
  if (!this->sup_)
    return;
  if (state == ota::OTA_STARTED) {
    // Valves off before the flash is written: erasing stalls the loop.
    ESP_LOGW(TAG, "OTA started: every valve off, starts blocked");
    this->stop_all();
    this->sup_->set_maintenance(true);
  } else if (state == ota::OTA_ERROR || state == ota::OTA_ABORT) {
    this->sup_->set_maintenance(false);
  }
}
#endif

// ---- entities ------------------------------------------------------------------------------

#ifdef USE_SWITCH
void ZoneSwitch::setup() {
  this->publish_state(false);
  this->parent_->add_on_state_callback([this]() {
    const bool on = this->parent_->zone_active(this->zone_);
    if (on != this->state)
      this->publish_state(on);
  });
}

void ZoneSwitch::write_state(bool state) {
  if (state)
    this->parent_->start_zone(this->zone_, 0);
  else
    this->parent_->stop_zone(this->zone_);
  this->publish_state(this->parent_->zone_active(this->zone_));
}
#endif

#ifdef USE_VALVE
void ZoneValve::setup() {
  this->position = valve::VALVE_CLOSED;
  this->current_operation = valve::VALVE_OPERATION_IDLE;
  this->publish_state(false);
  this->parent_->add_on_state_callback([this]() { this->update_(); });
}

valve::ValveTraits ZoneValve::get_traits() {
  valve::ValveTraits t;
  t.set_supports_position(false);
  t.set_supports_toggle(true);
  t.set_supports_stop(false);
  t.set_is_assumed_state(false);
  return t;
}

void ZoneValve::update_() {
  const zf::ValveState st = this->parent_->zone_state(this->zone_);
  const float pos = st == zf::ValveState::Open ? valve::VALVE_OPEN : valve::VALVE_CLOSED;
  const valve::ValveOperation op =
      st == zf::ValveState::Waiting ? valve::VALVE_OPERATION_OPENING : valve::VALVE_OPERATION_IDLE;
  if (pos != this->position || op != this->current_operation) {
    this->position = pos;
    this->current_operation = op;
    this->publish_state(false);
  }
}

void ZoneValve::control(const valve::ValveCall &call) {
  bool open = this->position != valve::VALVE_OPEN;  // toggle
  if (call.get_position().has_value())
    open = *call.get_position() > 0.5f;
  else if (!call.get_toggle().has_value())
    return;
  if (open) {
#ifdef USE_ZONEFLOW_SPRINKLER
    if (this->sprinkler_ != nullptr) {
      this->sprinkler_->start_single_valve(this->zone_);
      this->update_();
      return;
    }
#endif
    this->parent_->start_zone(this->zone_, 0);
  } else {
#ifdef USE_ZONEFLOW_SPRINKLER
    if (this->sprinkler_ != nullptr && this->sprinkler_->active_valve().has_value() &&
        this->sprinkler_->active_valve().value() == this->zone_)
      this->sprinkler_->shutdown();
#endif
    this->parent_->stop_zone(this->zone_);
  }
  this->update_();
}
#endif

#ifdef USE_BUTTON
void ZoneFlowButton::press_action() {
  switch (this->kind_) {
    case ButtonKind::CLEAR_FAULTS:
      this->parent_->clear_faults();
      break;
    case ButtonKind::ACKNOWLEDGE_REPAIR:
      this->parent_->acknowledge_repair_press();
      break;
    case ButtonKind::STOP_ALL:
      this->parent_->stop_all();
      break;
    case ButtonKind::SELF_TEST:
      this->parent_->request_self_test();
      break;
  }
}
#endif

}  // namespace esphome::zoneflow
