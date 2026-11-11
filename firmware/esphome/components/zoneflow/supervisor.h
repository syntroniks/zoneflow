// SPDX-License-Identifier: Apache-2.0
// The safety layer of the ZoneFlow ESPHome package, framework-free.
//
// It owns the shared zf_core parts that do the work, unchanged and in the
// same roles as in the standalone firmware:
//   zf::hw::OutputStage  expander register sequence, read-backs, relay
//                        sequencing, weld and shorted-output checks,
//                        self-test, polling cycle, over-current latch
//                        (hardware design 19.6 to 19.8)
//   zf::ZoneDriver       the only code that switches outputs: per-zone
//                        maximum run time with rest, stagger, concurrency,
//                        master sequencing (spec section 2 rule 4)
//   zf::CurrentSense     RMS of the 100 ms windows of the two ADC channels
//   zf::CurrentMonitor   open, over-current and short-circuit verdicts with
//                        inrush blanking
//   zf::AcMonitor        24 VAC presence debounce
//
// What is new here is the glue the standalone firmware keeps in its
// zf::Controller, cut down to the hardware: which zone a fault belongs to,
// lockouts, three over-current trips per output per day, the master lockout,
// inhibits for no 24 VAC, maintenance and output-stage faults, the leak
// alarm, and the run budget kept over a software restart. The ESPHome
// component (zoneflow.h) feeds it inputs every 50 ms and publishes its state.
//
// No ESPHome or ESP-IDF header here: host tests drive it on the simulated
// board of the standalone firmware (firmware/zoneflow-fw/test/host).
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "zf/app/retained.hpp"
#include "zf/diag/current_monitor.hpp"
#include "zf/diag/current_sense.hpp"
#include "zf/diag/sensors.hpp"
#include "zf/events/event.hpp"
#include "zf/hw/output_stage.hpp"
#include "zf/zone/zone_driver.hpp"

namespace zfe {

// Why a zone is locked out or was stopped by diagnostics.
enum class ZoneFault : uint8_t {
  None,
  OpenCircuit,      // below the zone's current window: valve or wire open
  OverCurrent,      // above the window after inrush: shorted coil, wrong valve
  ShortCircuit,     // above the absolute limit
  OverCurrentTrip,  // the hardware latch tripped with this zone just switched on
  DcCurrent,        // one triac polarity not conducting
};
const char *to_string(ZoneFault f);
// The fault code the standalone firmware reports for the same condition.
zf::FaultCode fault_code(ZoneFault f);

// Faults that do not belong to one zone. Bits of Supervisor::general_faults().
enum GeneralFault : uint32_t {
  kFaultOutputStage = 1u << 0,      // a self-test or read-back failed (hardware design 19.7)
  kFaultCurrentWhileIdle = 1u << 1, // current with every output off
  kFaultOverCurrentTrip = 1u << 2,  // latch tripped, no zone to blame
  kFaultMasterLockedOut = 1u << 3,  // repeated faults on the master valve
  kFaultMaster = 1u << 4,           // a current fault attributed to the master
  kFaultDcCurrent = 1u << 5,        // DC component, no zone to blame
  kFaultLeak = 1u << 6,             // flow with every valve shut
  // Conditions: they clear themselves.
  kCondNoAc = 1u << 16,
  kCondCurrentSenseUnavailable = 1u << 17,
  kCondSensorSupply = 1u << 18,
  kCondMainsSignal = 1u << 19,
  kCondNotCommissioned = 1u << 20,  // no stored detector release times (hardware design 19.7, step 9)
};
inline constexpr uint32_t kConditionMask = 0xFFFF0000u;

struct ZoneSettings {
  bool enabled = true;
  uint32_t max_run_sec = 3600;  // clamped again by the driver to zf::kHardMaxRunSec
  uint16_t min_ma = 80;         // hardware design 19.3 item 10: 80 to 700 mA per output
  uint16_t max_ma = 700;
};

struct SupervisorConfig {
  uint8_t zone_count = 8;  // zones the YAML declares; limited further by the variant strap
  uint8_t max_concurrent = 1;
  bool master_enabled = false;  // same default as the standalone firmware
  uint32_t master_pre_open_ms = 1000;
  uint32_t master_post_close_ms = 2000;
  uint32_t inter_zone_delay_ms = 0;
  uint32_t min_rest_sec = 1800;  // spec section 2 rule 4 default
  std::array<ZoneSettings, zf::kMaxZones> zones{};
  uint16_t master_min_ma = 80;
  uint16_t master_max_ma = 700;
  bool current_enabled = true;
  uint32_t settle_ms = 500;         // inrush blanking
  uint16_t idle_threshold_ma = zf::kDefaultIdleMaxMa;  // hardware design 19.2
  uint16_t short_circuit_ma = 3000;
  uint8_t confirm_samples = 3;
  uint8_t master_fault_lockout_count = 3;
  float leak_threshold_lpm = 0.5f;
  uint32_t leak_detect_sec = 60;
  zf::hw::OutputStageConfig stage{};
};

struct SupervisorInputs {
  bool ac_present = false;     // an AC_ZC edge within 25 ms
  uint32_t mains_milli_hz = 0;  // 0 = unknown
  // A raw window from the ADC (seq != 0) is processed with zf::CurrentSense.
  // With seq == 0, `reading` is taken as it is (host tests, simulated board).
  zf::CurrentWindowStats window{};
  zf::CurrentReading reading{};
  bool current_sense_available = false;
  bool sensor_supply_on = false;
  float flow_lpm = -1.0f;  // < 0: no flow meter
  // Rule 16 and contract 19.4: no peripheral signal routed to the keep-alive pin.
  bool keepalive_pin_plain = true;
};

class Supervisor final : private zf::IZoneDriverListener {
 public:
  static constexpr uint32_t kTripBlameMs = 500;  // hardware design 19.8
  static constexpr uint8_t kTripsPerDay = 3;
  static constexpr int64_t kDayMs = 24ll * 3600 * 1000;
  static constexpr uint32_t kMinMainsMilliHz = 45000;
  static constexpr uint32_t kMaxMainsMilliHz = 66000;
  static constexpr uint32_t kFlowPollMinIntervalMs = 60000;

  // `reference` keeps this board's detector release times (hardware design
  // 19.7, the limits of step 9); nullptr keeps them in RAM only.
  Supervisor(zf::hw::IExpanderBus &bus, zf::OutputEnableGuard &guard, const zf::IClock &clock,
             const zf::hw::ExpanderLayout &layout, const SupervisorConfig &cfg,
             zf::IDetectorReferenceStore *reference = nullptr);

  // Before the first tick. `retained` is the record kept in RTC memory over
  // a software restart, or nullptr.
  void begin(const zf::RetainedRecord *retained);
  // Every control tick (50 ms). Output stage first, then diagnostics, then
  // the zone driver, which beats the keep-alive heartbeat.
  void tick(const SupervisorInputs &in);

  // ---- commands (control task) ----
  // duration_sec 0 means "until stopped", which is the zone's maximum run
  // time: the sprinkler component times the run and closes the zone.
  zf::StartResult start_zone(uint8_t zone, uint32_t duration_sec);
  void stop_zone(uint8_t zone);
  void stop_all();
  // OTA, shutdown: everything off, nothing starts until cleared.
  void set_maintenance(bool on);
  // One press: zone lockouts, latched faults and every output-stage fault
  // except a keep-alive detector fault (hardware design 19.7).
  void clear_faults();
  // The faults of steps 1, 2, 2b, 5b and 9: only with the repair confirmed.
  // The board then learns its detector release times again.
  bool acknowledge_repair(bool repaired);
  void request_poll() { stage_.requestPoll(); }
  // Any context: outputs off and the keep-alive guard tripped.
  void emergency_off() { driver_.emergencyOff(); }

  // ---- state ----
  zf::ValveState zone_state(uint8_t zone) const { return driver_.state(zone); }
  uint32_t zone_remaining_ms(uint8_t zone) const { return driver_.remainingMs(zone); }
  uint32_t zone_rest_remaining_ms(uint8_t zone) const { return driver_.restRemainingMs(zone); }
  bool zone_locked_out(uint8_t zone) const { return driver_.lockedOut(zone); }
  ZoneFault zone_fault(uint8_t zone) const { return zone < zf::kMaxZones ? zone_fault_[zone] : ZoneFault::None; }
  // Which current fault set kFaultMaster: OpenCircuit, OverCurrent or ShortCircuit.
  ZoneFault master_fault() const { return master_fault_; }
  zf::StopReason zone_last_stop(uint8_t zone) const { return driver_.lastStopReason(zone); }
  // Mean current of the zone's last run with no other output changing; 0 if none.
  uint16_t zone_last_run_ma(uint8_t zone) const { return zone < zf::kMaxZones ? last_run_ma_[zone] : 0; }
  bool master_on() const { return driver_.masterOn(); }
  bool master_locked_out() const { return driver_.masterLockedOut(); }
  uint16_t output_mask() const { return driver_.outputMask(); }
  int32_t current_ma() const { return current_ma_; }
  const zf::CurrentReading &reading() const { return reading_; }
  int32_t noise_floor_ma() const { return current_sense_.noiseFloorMa(); }
  const zf::OutputStageStatus &stage() const { return stage_status_; }
  bool ac_present() const { return ac_.present(); }
  bool hardware_fault() const { return stage_status_.phase == zf::StagePhase::Fault; }
  bool repair_needed() const { return zf::hwCheckNeedsRepair(stage_status_.fault); }
  // Step 2b runs: the display, the backlight PWM and the PSRAM must be busy (rule 17).
  bool float_test() const { return stage_status_.floatTest; }
  bool commissioned() const { return stage_status_.commissioned; }
  bool maintenance() const { return maintenance_; }
  uint32_t general_faults() const { return general_; }
  bool leak() const { return (general_ & kFaultLeak) != 0; }
  // Anything a person must look at: a latched fault or a locked-out zone.
  bool any_latched_fault() const;
  // Zones the board has: the configured count, limited by the variant strap.
  uint8_t zones_available() const { return driver_.config().zoneCount; }
  // Increments whenever something above changed; the component publishes then.
  uint32_t change_seq() const { return change_seq_; }

  void save_retained(zf::RetainedRecord &r) const;

  zf::ZoneDriver &driver() { return driver_; }
  zf::hw::OutputStage &output_stage() { return stage_; }

 private:
  void onZoneChanged(zf::ZoneIndex zone, zf::ValveState state, zf::StopReason reason) override;
  void onMasterChanged(bool on) override;

  zf::ZoneDriverConfig driver_config() const;
  void handle_stage(zf::MonoMs now);
  void handle_current(const zf::CurrentVerdict &v);
  void handle_dc(const zf::CurrentReading &r, zf::MonoMs now);
  void on_trip(zf::MonoMs now);
  bool record_trip(uint8_t bit, zf::MonoMs now);
  void record_master_fault();
  void handle_flow(float lpm, zf::MonoMs now);
  void update_inhibit();
  void set_flag(uint32_t bit, bool on);
  void changed() { ++change_seq_; }

  const zf::IClock &clock_;
  SupervisorConfig cfg_;
  zf::hw::OutputStage stage_;
  zf::ZoneDriver driver_;
  zf::CurrentSense current_sense_;
  zf::CurrentMonitor monitor_;
  zf::AcMonitor ac_;

  zf::OutputStageStatus stage_status_{};
  uint32_t stage_fault_seq_ = 0;
  uint32_t stage_trip_seq_ = 0;
  uint8_t board_zones_ = 0;  // from the variant strap; 0 until read

  zf::CurrentReading reading_{};
  uint32_t last_window_seq_ = 0;
  int32_t current_ma_ = -1;
  uint8_t dc_windows_ = 0;

  std::array<ZoneFault, zf::kMaxZones> zone_fault_{};
  std::array<uint16_t, zf::kMaxZones> last_run_ma_{};
  std::array<std::array<int64_t, zf::kTripsKept>, zf::kMaxZones + 1> trip_at_ms_{};
  uint8_t master_faults_ = 0;
  ZoneFault master_fault_ = ZoneFault::None;
  bool master_fault_in_session_ = false;
  zf::MonoMs master_on_since_ms_ = 0;

  uint32_t general_ = 0;
  bool maintenance_ = false;
  bool hw_fault_ = false;

  bool leak_timing_ = false;
  zf::MonoMs leak_since_ms_ = 0;
  bool flowing_ = false;
  bool flow_poll_armed_ = false;
  zf::MonoMs last_flow_poll_ms_ = 0;

  uint32_t change_seq_ = 0;
  uint16_t last_mask_ = 0;
  zf::StagePhase last_phase_ = zf::StagePhase::None;
};

// Fault text in the words of the display and the web UI, from the one table
// in zf/fault_text.hpp. `label` names the zone: its name, or "Zone 3".
std::string zone_fault_text(ZoneFault f, const std::string &label);
// Every active fault and condition, joined with ", ". "No faults" if none.
// `labels[z]` is the zone's name; empty or null gives "Zone N".
std::string faults_text(const Supervisor &s, uint8_t zone_count,
                        const std::array<const char *, zf::kMaxZones> &labels);

}  // namespace zfe
