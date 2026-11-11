// SPDX-License-Identifier: Apache-2.0
#include "supervisor.h"

#include <algorithm>
#include <climits>
#include <cstdio>

#include "zf/fault_text.hpp"

namespace zfe {
namespace {
constexpr int64_t kNoTrip = INT64_MIN;
}  // namespace

const char *to_string(ZoneFault f) {
  switch (f) {
    case ZoneFault::None:
      return "none";
    case ZoneFault::OpenCircuit:
      return "open_circuit";
    case ZoneFault::OverCurrent:
      return "over_current";
    case ZoneFault::ShortCircuit:
      return "short_circuit";
    case ZoneFault::OverCurrentTrip:
      return "over_current_trip";
    case ZoneFault::DcCurrent:
      return "dc_current";
  }
  return "unknown";
}

zf::FaultCode fault_code(ZoneFault f) {
  switch (f) {
    case ZoneFault::None:
      return zf::FaultCode::None;
    case ZoneFault::OpenCircuit:
      return zf::FaultCode::ZoneOpenCircuit;
    case ZoneFault::OverCurrent:
      return zf::FaultCode::ZoneOverCurrent;
    case ZoneFault::ShortCircuit:
      return zf::FaultCode::ShortCircuit;
    case ZoneFault::OverCurrentTrip:
      return zf::FaultCode::OverCurrentTrip;
    case ZoneFault::DcCurrent:
      return zf::FaultCode::ZoneDcCurrent;
  }
  return zf::FaultCode::None;
}

namespace {

std::string title_of(zf::FaultCode c) {
  const zf::FaultText *t = zf::faultText(c);
  return t != nullptr ? std::string(t->title) : std::string(zf::toString(c));
}

// "Zone 3: no current"; the plain title for a fault that has no zone form.
std::string zone_title_of(zf::FaultCode c, const std::string &label) {
  const zf::FaultText *t = zf::faultText(c);
  if (t == nullptr || t->zoneTitle.empty())
    return title_of(c);
  const std::string fmt(t->zoneTitle);
  const std::size_t at = fmt.find("%s");
  return at == std::string::npos ? fmt : fmt.substr(0, at) + label + fmt.substr(at + 2);
}

}  // namespace

std::string zone_fault_text(ZoneFault f, const std::string &label) { return zone_title_of(fault_code(f), label); }

std::string faults_text(const Supervisor &s, uint8_t zone_count,
                        const std::array<const char *, zf::kMaxZones> &labels) {
  std::string out;
  auto add = [&out](const std::string &t) {
    if (!out.empty())
      out += ", ";
    out += t;
  };
  static const std::string kMaster = "Master valve";
  if (s.hardware_fault()) {
    const zf::HwCheck check = s.stage().fault;
    std::string t = title_of(zf::faultCodeFor(check));
    const std::string step = zf::hwCheckStep(check);  // "2b"; empty for a check that is not a step
    add(step.empty() ? t : t + " (step " + step + ")");
  }
  const uint32_t g = s.general_faults();
  if (g & kFaultCurrentWhileIdle)
    add(title_of(zf::FaultCode::CurrentWhileIdle));
  if (g & kFaultOverCurrentTrip)
    add(title_of(zf::FaultCode::OverCurrentTrip));
  if (g & kFaultMaster)
    add(zone_fault_text(s.master_fault() == ZoneFault::None ? ZoneFault::OverCurrent : s.master_fault(), kMaster));
  if (g & kFaultMasterLockedOut)
    add(title_of(zf::FaultCode::MasterLockedOut));
  if (g & kFaultDcCurrent)
    add(zone_fault_text(ZoneFault::DcCurrent, kMaster));
  if (g & kFaultLeak)
    add(title_of(zf::FaultCode::FlowWithoutZones));
  if (g & kCondNoAc)
    add(title_of(zf::FaultCode::AcLost));
  if (g & kCondCurrentSenseUnavailable)
    add(title_of(zf::FaultCode::CurrentSenseUnavailable));
  if (g & kCondSensorSupply)
    add(title_of(zf::FaultCode::SensorSupply));
  if (g & kCondMainsSignal)
    add(title_of(zf::FaultCode::MainsSignal));
  if (g & kCondNotCommissioned)
    add(title_of(zf::FaultCode::NotCommissioned));
  for (uint8_t z = 0; z < zone_count && z < zf::kMaxZones; ++z) {
    const std::string label =
        labels[z] != nullptr && labels[z][0] != '\0' ? std::string(labels[z]) : "Zone " + std::to_string(z + 1);
    if (s.zone_fault(z) != ZoneFault::None)
      add(zone_fault_text(s.zone_fault(z), label));
    else if (s.zone_locked_out(z))
      add(label + ": locked");
  }
  if (s.maintenance())
    add("Updating: watering is off");
  return out.empty() ? "No faults" : out;
}

Supervisor::Supervisor(zf::hw::IExpanderBus &bus, zf::OutputEnableGuard &guard, const zf::IClock &clock,
                       const zf::hw::ExpanderLayout &layout, const SupervisorConfig &cfg,
                       zf::IDetectorReferenceStore *reference)
    : clock_(clock),
      cfg_(cfg),
      stage_(bus, guard, clock, layout, cfg.stage, reference),
      driver_(stage_, guard, clock) {
  driver_.setListener(this);
  for (auto &t : trip_at_ms_)
    t.fill(kNoTrip);
  zf::CurrentMonitorConfig mc;
  mc.enabled = cfg_.current_enabled;
  mc.settleMs = cfg_.settle_ms;
  mc.idleMaxMa = cfg_.idle_threshold_ma;
  mc.shortCircuitMa = cfg_.short_circuit_ma;
  mc.confirmSamples = cfg_.confirm_samples;
  for (std::size_t z = 0; z < zf::kMaxZones; ++z)
    mc.zone[z] = {cfg_.zones[z].min_ma, cfg_.zones[z].max_ma};
  mc.master = {cfg_.master_min_ma, cfg_.master_max_ma};
  monitor_.configure(mc);
}

zf::ZoneDriverConfig Supervisor::driver_config() const {
  zf::ZoneDriverConfig d;
  uint8_t zones = std::min<uint8_t>(cfg_.zone_count, zf::kMaxZones);
  if (board_zones_ != 0)
    zones = std::min(zones, board_zones_);
  d.zoneCount = zones;
  d.maxConcurrent = std::clamp<uint8_t>(cfg_.max_concurrent, 1, zf::kMaxConcurrentZonesLimit);
  d.masterEnabled = cfg_.master_enabled;
  d.masterPreOpenMs = cfg_.master_pre_open_ms;
  d.masterPostCloseMs = cfg_.master_post_close_ms;
  d.interZoneDelayMs = cfg_.inter_zone_delay_ms;
  d.minRestSec = cfg_.min_rest_sec;
  d.staggerMs = zf::ZoneDriver::kMinStaggerMs;  // hardware design 19.1 rule 10
  d.zeroCrossSwitching = false;  // AC_ZC is sampled at 1 ms here, too coarse to switch on a crossing
  for (std::size_t z = 0; z < zf::kMaxZones; ++z) {
    d.zoneEnabled[z] = z < zones && cfg_.zones[z].enabled;
    d.maxRunSec[z] = cfg_.zones[z].max_run_sec;
  }
  return d;
}

void Supervisor::begin(const zf::RetainedRecord *retained) {
  driver_.init(driver_config());
  if (retained == nullptr)
    return;
  // The run budget and the latched output-stage fault survive a software
  // restart (firmware security residual risk 10, hardware design 19.7).
  std::array<zf::ZoneBudget, zf::kMaxZones> budget{};
  for (std::size_t z = 0; z < zf::kMaxZones; ++z)
    budget[z] = {retained->burstMs[z], retained->closedForMs[z]};
  driver_.restoreBudget(budget);
  if (retained->hwFault != 0 && retained->hwFault < static_cast<uint8_t>(zf::HwCheck::Count))
    stage_.restoreFault(static_cast<zf::HwCheck>(retained->hwFault));
  const auto now = static_cast<int64_t>(clock_.monoMs());
  for (std::size_t b = 0; b <= zf::kMaxZones; ++b) {
    for (std::size_t k = 0; k < zf::kTripsKept; ++k) {
      const uint32_t age = retained->tripAgeSec[b][k];
      trip_at_ms_[b][k] = age == 0 ? kNoTrip : now - static_cast<int64_t>(age - 1) * 1000;
    }
  }
}

void Supervisor::save_retained(zf::RetainedRecord &r) const {
  const auto budget = driver_.exportBudget();
  for (std::size_t z = 0; z < zf::kMaxZones; ++z) {
    r.burstMs[z] = budget[z].burstMs;
    r.closedForMs[z] = budget[z].closedForMs;
  }
  r.hwFault = static_cast<uint8_t>(stage_status_.fault);
  const auto now = static_cast<int64_t>(clock_.monoMs());
  for (std::size_t b = 0; b <= zf::kMaxZones; ++b) {
    for (std::size_t k = 0; k < zf::kTripsKept; ++k) {
      const int64_t at = trip_at_ms_[b][k];
      r.tripAgeSec[b][k] =
          at == kNoTrip || now - at >= kDayMs ? 0u : static_cast<uint32_t>((now - at) / 1000 + 1);
    }
  }
  zf::seal(r);
}

void Supervisor::tick(const SupervisorInputs &in) {
  const zf::MonoMs now = clock_.monoMs();

  // Valve current: a new 100 ms window every other tick.
  bool fresh = false;
  if (in.window.seq != 0) {
    if (in.window.seq != last_window_seq_) {
      last_window_seq_ = in.window.seq;
      // With the keep-alive stopped the expander is in reset and the relay
      // open: the true current is zero, so the window teaches the floor.
      const bool idle =
          stage_status_.phase == zf::StagePhase::Idle || stage_status_.phase == zf::StagePhase::NoAc;
      reading_ = current_sense_.process(in.window, idle, cfg_.idle_threshold_ma);
      fresh = true;
    }
  } else if (in.reading.seq != 0 && in.reading.seq != last_window_seq_) {
    last_window_seq_ = in.reading.seq;
    reading_ = in.reading;
    fresh = true;
  }
  if (fresh) {
    const int32_t ma = reading_.valid ? reading_.ma : -1;
    if (ma != current_ma_)
      changed();
    current_ma_ = ma;
  }

  // Output stage first (hardware design 19.7): it may withdraw power, trip
  // or fault before the driver switches anything this tick.
  zf::StageTickInput st;
  st.acPresent = in.ac_present;
  st.current = reading_;
  st.currentSenseAvailable = in.current_sense_available && cfg_.current_enabled;
  st.idleThresholdMa = cfg_.idle_threshold_ma;
  st.sensorSupplyOn = in.sensor_supply_on;
  st.keepAlivePinPlain = in.keepalive_pin_plain;
  stage_.tick(st);
  handle_stage(now);

  if (ac_.update(in.ac_present, now)) {
    set_flag(kCondNoAc, !ac_.present());
    update_inhibit();
  }
  if (!ac_.present() && (general_ & kCondNoAc) == 0)
    set_flag(kCondNoAc, true);  // absent since boot
  set_flag(kCondMainsSignal, ac_.present() && in.mains_milli_hz != 0 &&
                                 (in.mains_milli_hz < kMinMainsMilliHz || in.mains_milli_hz > kMaxMainsMilliHz));
  set_flag(kCondCurrentSenseUnavailable, cfg_.current_enabled && !in.current_sense_available);
  if (stage_status_.sensorSupplyKnown)
    set_flag(kCondSensorSupply, in.sensor_supply_on && stage_status_.sensorSupplyFault);
  // 19.7, the limits of step 9: a board with no stored release times says so.
  set_flag(kCondNotCommissioned, !stage_status_.commissioned);

  if (in.flow_lpm >= 0.0f)
    handle_flow(in.flow_lpm, now);

  // While the stage tests itself with every valve off, its own checks (weld,
  // shorted output) judge the current, not the idle check.
  const uint16_t mask = driver_.outputMask();
  const bool stage_judges = mask == 0 && stage_status_.phase != zf::StagePhase::Idle &&
                            stage_status_.phase != zf::StagePhase::NoAc;
  if (ac_.present() && fresh && !stage_judges && cfg_.current_enabled && in.current_sense_available) {
    handle_current(
        monitor_.evaluate(current_ma_, mask, driver_.lastOpenedBit(), driver_.lastOutputChangeMs(), now));
  }
  if (fresh && in.window.seq != 0)
    handle_dc(reading_, now);

  // The stage withdrew power because 24 VAC went away: say so, before the
  // driver finds power gone and reports a plain fault.
  if (stage_status_.phase == zf::StagePhase::NoAc && (driver_.anyOutputOn() || driver_.activeCount() > 0))
    driver_.stopAll(zf::StopReason::AcLost);
  driver_.tick();

  if (driver_.outputMask() != last_mask_) {
    last_mask_ = driver_.outputMask();
    changed();
  }
}

void Supervisor::handle_stage(zf::MonoMs now) {
  const zf::OutputStageStatus st = stage_.status();
  if (st.phase != last_phase_) {
    last_phase_ = st.phase;
    changed();
  }
  stage_status_ = st;
  if (st.zonesFitted != 0 && st.zonesFitted != board_zones_) {
    board_zones_ = st.zonesFitted;
    driver_.reconfigure(driver_config());  // zones the board does not have refuse to start
    changed();
  }
  if (st.faultSeq != stage_fault_seq_) {
    stage_fault_seq_ = st.faultSeq;
    if (st.fault != zf::HwCheck::None) {
      // The stage has already stopped the keep-alive. Bring the driver in
      // line; nothing starts until a person acknowledges.
      driver_.stopAll(zf::StopReason::Fault);
      set_flag(kFaultOutputStage, true);
    }
    changed();
  }
  const bool faulted = st.phase == zf::StagePhase::Fault;
  if (faulted != hw_fault_) {
    hw_fault_ = faulted;
    if (!faulted)
      set_flag(kFaultOutputStage, false);
    update_inhibit();
  }
  if (st.tripSeq != stage_trip_seq_) {
    stage_trip_seq_ = st.tripSeq;
    on_trip(now);
  }
}

void Supervisor::handle_current(const zf::CurrentVerdict &v) {
  const uint8_t zone = v.outputBit < zf::kMaxZones ? v.outputBit : zf::kNoZone;
  switch (v.fault) {
    case zf::CurrentFault::None:
      return;
    case zf::CurrentFault::ShortCircuit:
    case zf::CurrentFault::OverCurrent:
      if (v.outputBit == zf::ZoneDriver::kMasterBit)
        master_fault_in_session_ = true;
      driver_.stopAll(zf::StopReason::Fault);  // everything off first
      if (zone != zf::kNoZone) {
        driver_.setLockout(zone, true);
        zone_fault_[zone] =
            v.fault == zf::CurrentFault::ShortCircuit ? ZoneFault::ShortCircuit : ZoneFault::OverCurrent;
      } else {
        master_fault_ = v.fault == zf::CurrentFault::ShortCircuit ? ZoneFault::ShortCircuit : ZoneFault::OverCurrent;
        set_flag(kFaultMaster, true);
      }
      if (v.outputBit == zf::ZoneDriver::kMasterBit)
        record_master_fault();
      break;
    case zf::CurrentFault::OpenCircuit:
      if (zone != zf::kNoZone) {
        driver_.stop(zone, zf::StopReason::Fault);
        driver_.setLockout(zone, true);
        zone_fault_[zone] = ZoneFault::OpenCircuit;
      } else {
        driver_.stopAll(zf::StopReason::Fault);
        master_fault_ = ZoneFault::OpenCircuit;
        set_flag(kFaultMaster, true);
      }
      break;
    case zf::CurrentFault::CurrentWhileIdle:
      // Every output is already off in firmware: report it so a person
      // checks the output stage.
      set_flag(kFaultCurrentWhileIdle, true);
      break;
  }
  changed();
}

void Supervisor::handle_dc(const zf::CurrentReading &r, zf::MonoMs now) {
  // One triac polarity not conducting (hardware design 19.2), confirmed over
  // several windows after the inrush.
  const uint16_t mask = driver_.outputMask();
  if (!cfg_.current_enabled || !r.valid || !r.dcComponent || mask == 0 ||
      now - driver_.lastOutputChangeMs() < cfg_.settle_ms) {
    dc_windows_ = 0;
    return;
  }
  if (++dc_windows_ < std::max<uint8_t>(cfg_.confirm_samples, 1))
    return;
  dc_windows_ = 0;
  const uint8_t bit = driver_.lastOpenedBit();
  if (bit < zf::kMaxZones && (mask & (1u << bit)) != 0) {
    driver_.stop(bit, zf::StopReason::Fault);
    driver_.setLockout(bit, true);
    zone_fault_[bit] = ZoneFault::DcCurrent;
  } else {
    driver_.stopAll(zf::StopReason::Fault);
    set_flag(kFaultDcCurrent, true);
  }
  changed();
}

void Supervisor::on_trip(zf::MonoMs now) {
  // Hardware design 19.8. The latch has already removed gate drive and
  // released the relay; the output stage clears it by holding the keep-alive
  // low. The output switched on within 500 ms is the faulted one; without
  // one, every output that was on is suspect. Three trips in a day and the
  // output stays off until a person clears the faults.
  const uint16_t mask = driver_.outputMask();
  const uint8_t last = driver_.lastOpenedBit();
  uint16_t faulted = 0;
  if (last <= zf::ZoneDriver::kMasterBit && (mask & (1u << last)) != 0 &&
      now - driver_.openedAtMs(last) <= kTripBlameMs)
    faulted = static_cast<uint16_t>(1u << last);
  const uint16_t marked = faulted != 0 ? faulted : mask;
  const bool master = (faulted & (1u << zf::ZoneDriver::kMasterBit)) != 0;
  if (master)
    master_fault_in_session_ = true;
  driver_.stopAll(zf::StopReason::Fault);
  for (uint8_t z = 0; z < zf::kMaxZones; ++z) {
    if ((marked & (1u << z)) == 0)
      continue;
    if (record_trip(z, now))
      driver_.setLockout(z, true);
    if ((faulted & (1u << z)) != 0 || driver_.lockedOut(z))
      zone_fault_[z] = ZoneFault::OverCurrentTrip;
  }
  const bool master_limit =
      (marked & (1u << zf::ZoneDriver::kMasterBit)) != 0 && record_trip(zf::ZoneDriver::kMasterBit, now);
  if (master || faulted == 0)
    set_flag(kFaultOverCurrentTrip, true);
  if (master || master_limit)
    record_master_fault();
  update_inhibit();
  changed();
}

bool Supervisor::record_trip(uint8_t bit, zf::MonoMs now) {
  auto &times = trip_at_ms_[bit];
  const auto t = static_cast<int64_t>(now);
  std::size_t oldest = 0;
  for (std::size_t i = 1; i < times.size(); ++i) {
    if (times[i] < times[oldest])
      oldest = i;
  }
  times[oldest] = t;
  std::size_t today = 0;
  for (int64_t at : times)
    today += at != kNoTrip && t - at < kDayMs ? 1u : 0u;
  return today >= kTripsPerDay;
}

void Supervisor::record_master_fault() {
  master_fault_in_session_ = true;
  if (master_faults_ < 0xFF)
    ++master_faults_;
  if (master_faults_ >= cfg_.master_fault_lockout_count && !driver_.masterLockedOut()) {
    // No program retries into a shorted master circuit.
    driver_.setMasterLockout(true);
    set_flag(kFaultMasterLockedOut, true);
    update_inhibit();
  }
  changed();
}

void Supervisor::handle_flow(float lpm, zf::MonoMs now) {
  // Flow with every valve shut: a leak, a stuck valve or a hose left open.
  const bool leaking = driver_.outputMask() == 0 && lpm > cfg_.leak_threshold_lpm;
  if (!leaking) {
    leak_timing_ = false;
  } else if (!leak_timing_) {
    leak_timing_ = true;
    leak_since_ms_ = now;
  } else if (now - leak_since_ms_ >= static_cast<zf::MonoMs>(cfg_.leak_detect_sec) * 1000u) {
    set_flag(kFaultLeak, true);
  }
  // Flow that starts with every valve shut, or stops while one is open, may
  // be the sensor supply failing: run the polling cycle now (hardware design
  // 19.6), at most once a minute.
  const bool flowing = lpm > 0.0f;
  if (flowing != flowing_) {
    flowing_ = flowing;
    const bool unexplained = flowing ? driver_.outputMask() == 0 : driver_.outputMask() != 0;
    if (unexplained && (!flow_poll_armed_ || now - last_flow_poll_ms_ >= kFlowPollMinIntervalMs)) {
      flow_poll_armed_ = true;
      last_flow_poll_ms_ = now;
      stage_.requestPoll();
    }
  }
}

void Supervisor::update_inhibit() {
  const bool ac_ok = ac_.present();
  driver_.setInhibit(!ac_ok || maintenance_ || hw_fault_);
  if (driver_.activeCount() > 0 || driver_.masterOn() || driver_.masterPending()) {
    if (maintenance_)
      driver_.stopAll(zf::StopReason::Maintenance);
    else if (!ac_ok)
      driver_.stopAll(zf::StopReason::AcLost);
  }
  changed();
}

void Supervisor::set_flag(uint32_t bit, bool on) {
  const uint32_t before = general_;
  general_ = on ? (general_ | bit) : (general_ & ~bit);
  if (general_ != before)
    changed();
}

zf::StartResult Supervisor::start_zone(uint8_t zone, uint32_t duration_sec) {
  if (zone >= zf::kMaxZones)
    return {zf::StartError::InvalidZone, 0};
  const uint32_t sec = duration_sec == 0 ? driver_.effectiveMaxRunSec(zone) : duration_sec;
  const zf::StartResult r = driver_.start(zone, sec);
  changed();
  return r;
}

void Supervisor::stop_zone(uint8_t zone) {
  if (zone < zf::kMaxZones && driver_.state(zone) != zf::ValveState::Off) {
    driver_.stop(zone, zf::StopReason::Requested);
    changed();
  }
}

void Supervisor::stop_all() {
  driver_.stopAll(zf::StopReason::Requested);
  changed();
}

void Supervisor::set_maintenance(bool on) {
  if (on == maintenance_)
    return;
  maintenance_ = on;
  update_inhibit();
}

bool Supervisor::any_latched_fault() const {
  if ((general_ & ~kConditionMask) != 0 || hardware_fault())
    return true;
  for (std::size_t z = 0; z < zf::kMaxZones; ++z) {
    if (driver_.lockedOut(static_cast<zf::ZoneIndex>(z)))
      return true;
  }
  return false;
}

void Supervisor::clear_faults() {
  driver_.clearLockouts();
  monitor_.rearm();  // a current fault whose cause is still there is raised again
  zone_fault_.fill(ZoneFault::None);
  master_fault_ = ZoneFault::None;
  master_faults_ = 0;
  for (auto &t : trip_at_ms_)
    t.fill(kNoTrip);
  leak_timing_ = false;
  const bool stage_cleared = stage_.acknowledgeFault(false);
  general_ &= kConditionMask | (stage_cleared ? 0u : static_cast<uint32_t>(kFaultOutputStage));
  handle_stage(clock_.monoMs());
  update_inhibit();
}

bool Supervisor::acknowledge_repair(bool repaired) {
  if (!stage_.acknowledgeFault(repaired))
    return false;
  set_flag(kFaultOutputStage, false);
  handle_stage(clock_.monoMs());
  update_inhibit();
  return true;
}

void Supervisor::onZoneChanged(zf::ZoneIndex zone, zf::ValveState state, zf::StopReason reason) {
  (void) reason;
  if (zone >= zf::kMaxZones)
    return;
  if (state == zf::ValveState::Open) {
    monitor_.beginRun(zone);
  } else if (state == zf::ValveState::Off) {
    const uint16_t ma = monitor_.runMeanMa(zone);
    if (ma != 0)
      last_run_ma_[zone] = ma;
  }
  changed();
}

void Supervisor::onMasterChanged(bool on) {
  const zf::MonoMs now = clock_.monoMs();
  if (on) {
    master_on_since_ms_ = now;
    master_fault_in_session_ = false;
  } else if (!master_fault_in_session_ && now - master_on_since_ms_ >= cfg_.settle_ms) {
    master_faults_ = 0;  // a clean master session resets the count
  }
  changed();
}

}  // namespace zfe
