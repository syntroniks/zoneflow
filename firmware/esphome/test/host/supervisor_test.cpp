// SPDX-License-Identifier: Apache-2.0
// The ESPHome package's safety glue on the simulated rev A board of the
// standalone firmware's host tests (fake_board.hpp): two keep-alive
// detectors, the over-current latch, a register-level TCA9539 held in reset
// while DRV_EN is low, the valve-common relay and the valve current.
#include <gtest/gtest.h>

#include "fake_board.hpp"
#include "supervisor.h"

namespace {

using zf::StartError;
using zf::StopReason;
using zf::ValveState;
using zfe::Supervisor;
using zfe::SupervisorConfig;
using zfe::ZoneFault;

struct Rig {
  zf::test::FakeClock clock;
  zf::OutputEnableGuard guard{zf::OutputEnableMode::Toggle, 500};
  zf::test::FakeBoard board{guard, clock};
  Supervisor sup;
  float flow_lpm = -1.0f;
  bool ticking = true;  // false: the ESPHome main loop is stalled

  static SupervisorConfig defaults() {
    SupervisorConfig c;
    for (auto &z : c.zones)
      z.max_run_sec = 600;
    c.min_rest_sec = 1800;
    return c;
  }
  explicit Rig(const SupervisorConfig &cfg = defaults(), const zf::RetainedRecord *retained = nullptr)
      : sup(board, guard, clock, zf::test::revALayout(), cfg) {
    clock.mono = 0;
    sup.begin(retained);
  }
  zfe::SupervisorInputs inputs() const {
    zfe::SupervisorInputs in;
    in.ac_present = board.acPresent;
    in.mains_milli_hz = 50000;
    in.reading = board.window();
    in.current_sense_available = true;
    in.sensor_supply_on = true;
    in.flow_lpm = flow_lpm;
    return in;
  }
  // 5 ms keep-alive interrupt periods, a control tick every 50 ms.
  void run(uint64_t ms) {
    for (uint64_t t = 0; t < ms; t += 5) {
      clock.advanceMs(5);
      board.step5ms();
      if (ticking && clock.mono % 50 == 0)
        sup.tick(inputs());
    }
  }
  template<typename Pred> bool run_until(Pred pred, uint64_t limit_ms) {
    for (uint64_t t = 0; t < limit_ms && !pred(); t += 5)
      run(5);
    return pred();
  }
  bool idle() const { return sup.stage().phase == zf::StagePhase::Idle; }
  bool zone_driven(std::size_t z) const { return board.valveDriven(z); }
  bool master_driven() const { return board.valveDriven(8); }
};

TEST(Supervisor, StartupSelfTestPassesAndLeavesTheKeepAliveStopped) {
  Rig r;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  EXPECT_FALSE(r.sup.hardware_fault());
  EXPECT_EQ(r.sup.stage().zonesFitted, 8);
  EXPECT_EQ(r.sup.zones_available(), 8);
  EXPECT_FALSE(r.board.kaHigh());
  EXPECT_FALSE(r.board.driveEnabled());
  EXPECT_GE(r.sup.stage().selfTestsPassed, 1u);
}

TEST(Supervisor, ZoneRunsWithMasterAndRelayThenEverythingStops) {
  Rig r;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  ASSERT_TRUE(r.sup.start_zone(0, 30).ok());
  EXPECT_EQ(r.sup.zone_state(0), ValveState::Waiting);
  // Self-test before the run (pin-high test, start, weld, relay), then the
  // master, then the zone 1 s later.
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(0) == ValveState::Open; }, 8000));
  EXPECT_TRUE(r.zone_driven(0));
  EXPECT_TRUE(r.master_driven());
  EXPECT_TRUE(r.board.relayClosed());
  r.run(31000);
  EXPECT_EQ(r.sup.zone_state(0), ValveState::Off);
  EXPECT_EQ(r.sup.zone_last_stop(0), StopReason::Completed);
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  EXPECT_FALSE(r.board.relayClosed());
  EXPECT_FALSE(r.board.driveEnabled());
  EXPECT_FALSE(r.sup.any_latched_fault());
  EXPECT_EQ(r.sup.zone_last_run_ma(0), 300);
}

TEST(Supervisor, UntilStoppedMeansTheZonesMaximumAndThenItRests) {
  SupervisorConfig c = Rig::defaults();
  c.zones[1].max_run_sec = 60;
  Rig r(c);
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  ASSERT_TRUE(r.sup.start_zone(1, 0).ok());  // the sprinkler component times the run
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(1) == ValveState::Open; }, 8000));
  r.run(61000);
  EXPECT_EQ(r.sup.zone_state(1), ValveState::Off);
  EXPECT_EQ(r.sup.zone_last_stop(1), StopReason::MaxRunTime);
  // No automation gets around the limit: the zone rests.
  EXPECT_EQ(r.sup.start_zone(1, 30).error, StartError::Resting);
  EXPECT_GT(r.sup.zone_rest_remaining_ms(1), 1700u * 1000u);
}

TEST(Supervisor, BackToBackRunsShareOneBudget) {
  SupervisorConfig c = Rig::defaults();
  c.zones[0].max_run_sec = 60;
  Rig r(c);
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  ASSERT_TRUE(r.sup.start_zone(0, 40).ok());
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(0) == ValveState::Open; }, 8000));
  r.run(41000);
  ASSERT_EQ(r.sup.zone_state(0), ValveState::Off);
  const zf::StartResult second = r.sup.start_zone(0, 40);
  ASSERT_TRUE(second.ok());
  EXPECT_LE(second.acceptedSec, 21u);  // 60 s per zone, not per run
}

TEST(Supervisor, OpenValveIsStoppedAndLockedOutUntilCleared) {
  // Without a master valve. With one, the board measures master plus zone
  // (one shunt), so a 250 mA master alone already fills the summed window of
  // 160 to 1400 mA: an open zone is then found only with its min_current set
  // near its measured current. docs/firmware/esphome.md says so.
  SupervisorConfig c = Rig::defaults();
  c.master_enabled = false;
  Rig r(c);
  r.board.valveMa[2] = 10;  // below the 80 mA window
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  ASSERT_TRUE(r.sup.start_zone(2, 120).ok());
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_fault(2) != ZoneFault::None; }, 15000));
  EXPECT_EQ(r.sup.zone_fault(2), ZoneFault::OpenCircuit);
  EXPECT_EQ(r.sup.zone_state(2), ValveState::Off);
  EXPECT_TRUE(r.sup.zone_locked_out(2));
  EXPECT_FALSE(r.zone_driven(2));
  EXPECT_EQ(r.sup.start_zone(2, 60).error, StartError::LockedOut);
  r.sup.clear_faults();
  EXPECT_FALSE(r.sup.zone_locked_out(2));
  EXPECT_TRUE(r.sup.start_zone(2, 60).ok());
}

TEST(Supervisor, ShortedValveStopsEverythingAndLocksTheZoneOut) {
  Rig r;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  ASSERT_TRUE(r.sup.start_zone(1, 120).ok());
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(1) == ValveState::Open; }, 8000));
  r.board.valveMa[1] = 3500;  // above the 3 A short-circuit limit, below the hardware trip
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_fault(1) != ZoneFault::None; }, 2000));
  EXPECT_EQ(r.sup.zone_fault(1), ZoneFault::ShortCircuit);
  EXPECT_EQ(r.sup.output_mask(), 0);
  EXPECT_TRUE(r.sup.zone_locked_out(1));
}

TEST(Supervisor, HardwareTripBlamesTheZoneJustSwitchedOnAndLocksItAfterThree) {
  Rig r;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  for (int trip = 1; trip <= 3; ++trip) {
    ASSERT_TRUE(r.sup.start_zone(3, 120).ok()) << "trip " << trip;
    ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(3) == ValveState::Open; }, 10000));
    r.run(50);
    const uint32_t trips = r.sup.stage().tripSeq;
    r.board.trip();  // the comparator fires on the zone's inrush
    ASSERT_TRUE(r.run_until([&] { return r.sup.stage().tripSeq != trips; }, 1000));
    EXPECT_EQ(r.sup.zone_fault(3), ZoneFault::OverCurrentTrip);
    EXPECT_EQ(r.sup.output_mask(), 0);
    // The stage clears the latch by holding the keep-alive low for 1.5 s.
    ASSERT_TRUE(r.run_until([&] { return !r.board.latched && r.idle(); }, 10000)) << "trip " << trip;
    EXPECT_EQ(r.sup.zone_locked_out(3), trip == 3) << "trip " << trip;
  }
  EXPECT_FALSE(r.sup.hardware_fault());
  EXPECT_EQ(r.sup.start_zone(3, 60).error, StartError::LockedOut);
}

TEST(Supervisor, DetectorFaultAtStartupNeedsAConfirmedRepair) {
  Rig r;
  r.board.b.stuckOn = true;  // relay detector stuck on: B_RB low with the keep-alive stopped
  ASSERT_TRUE(r.run_until([&] { return r.sup.hardware_fault(); }, 10000));
  EXPECT_TRUE(r.sup.repair_needed());
  EXPECT_EQ(r.sup.start_zone(0, 60).error, StartError::Inhibited);
  r.sup.clear_faults();
  EXPECT_TRUE(r.sup.hardware_fault()) << "one press must not clear a detector fault";
  EXPECT_FALSE(r.sup.acknowledge_repair(false));
  r.board.b.stuckOn = false;
  EXPECT_TRUE(r.sup.acknowledge_repair(true));
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  EXPECT_TRUE(r.sup.start_zone(0, 60).ok());
}

TEST(Supervisor, StalledMainLoopStopsTheKeepAliveAndTheValves) {
  Rig r;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  ASSERT_TRUE(r.sup.start_zone(0, 300).ok());
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(0) == ValveState::Open; }, 8000));
  ASSERT_TRUE(r.zone_driven(0));
  // The ESPHome loop stops calling tick(): no heartbeat. The interrupt keeps
  // firing but stops making edges after 500 ms; detector A lets go 600 ms later
  // in this model (hardware: 0.45 to 0.9 s, 1.2 s if the pin stopped high).
  r.ticking = false;
  r.run(1400);
  EXPECT_FALSE(r.board.driveEnabled());
  EXPECT_FALSE(r.zone_driven(0));
  EXPECT_FALSE(r.master_driven());
  // When the loop comes back the stage finds drive lost and latches a fault.
  r.ticking = true;
  r.run(200);
  EXPECT_TRUE(r.sup.hardware_fault());
  EXPECT_EQ(r.sup.zone_state(0), ValveState::Off);
}

TEST(Supervisor, NoAcBlocksStartsAndStopsARun) {
  Rig r;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  ASSERT_TRUE(r.sup.start_zone(0, 300).ok());
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(0) == ValveState::Open; }, 8000));
  r.board.acPresent = false;
  r.run(300);
  EXPECT_EQ(r.sup.zone_state(0), ValveState::Off);
  EXPECT_EQ(r.sup.zone_last_stop(0), StopReason::AcLost);
  EXPECT_NE(r.sup.general_faults() & zfe::kCondNoAc, 0u);
  EXPECT_EQ(r.sup.start_zone(0, 60).error, StartError::Inhibited);
  r.board.acPresent = true;
  r.run(1000);
  EXPECT_EQ(r.sup.general_faults() & zfe::kCondNoAc, 0u);
  EXPECT_FALSE(r.sup.any_latched_fault()) << "loss of 24 VAC is a condition, not a latched fault";
}

TEST(Supervisor, MaintenanceStopsEverythingAndBlocksStarts) {
  Rig r;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  ASSERT_TRUE(r.sup.start_zone(4, 300).ok());
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(4) == ValveState::Open; }, 8000));
  r.sup.set_maintenance(true);  // OTA begins
  r.run(100);
  EXPECT_EQ(r.sup.output_mask(), 0);
  EXPECT_FALSE(r.zone_driven(4));
  EXPECT_EQ(r.sup.start_zone(4, 60).error, StartError::Inhibited);
}

TEST(Supervisor, NeverMoreThanThreeValveBitsLow) {
  SupervisorConfig c = Rig::defaults();
  c.max_concurrent = 2;
  Rig r(c);
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  EXPECT_TRUE(r.sup.start_zone(0, 100).ok());
  EXPECT_TRUE(r.sup.start_zone(1, 100).ok());
  EXPECT_EQ(r.sup.start_zone(2, 100).error, StartError::TooManyActive);
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(1) == ValveState::Open; }, 10000));
  EXPECT_TRUE(r.master_driven());
  r.run(5000);
  EXPECT_LE(r.board.maxValvesLow, 3);
  EXPECT_EQ(r.board.maxValvesLow, 3);
}

TEST(Supervisor, FourZoneBoardRefusesZonesItDoesNotHave) {
  Rig r;
  r.board.eightZone = false;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  EXPECT_EQ(r.sup.stage().zonesFitted, 4);
  EXPECT_EQ(r.sup.zones_available(), 4);
  EXPECT_FALSE(r.sup.start_zone(5, 60).ok());
  EXPECT_TRUE(r.sup.start_zone(3, 60).ok());
}

TEST(Supervisor, FlowWithEveryValveShutRaisesTheLeakAlarm) {
  Rig r;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  r.flow_lpm = 2.0f;
  r.run(30000);
  EXPECT_FALSE(r.sup.leak());
  r.run(31000);
  EXPECT_TRUE(r.sup.leak());
  r.flow_lpm = 0.0f;
  r.run(1000);
  EXPECT_TRUE(r.sup.leak()) << "latched until cleared";
  r.sup.clear_faults();
  EXPECT_FALSE(r.sup.leak());
}

TEST(Supervisor, FlowDuringAScheduledRunIsNoLeak) {
  Rig r;
  ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
  ASSERT_TRUE(r.sup.start_zone(0, 120).ok());
  ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(0) == ValveState::Open; }, 8000));
  r.flow_lpm = 8.0f;
  r.run(90000);
  EXPECT_FALSE(r.sup.leak());
}

TEST(Supervisor, RunBudgetAndLatchedFaultSurviveASoftwareRestart) {
  SupervisorConfig c = Rig::defaults();
  c.zones[0].max_run_sec = 60;
  zf::RetainedRecord rec{};
  {
    Rig r(c);
    ASSERT_TRUE(r.run_until([&] { return r.idle(); }, 10000));
    ASSERT_TRUE(r.sup.start_zone(0, 0).ok());
    ASSERT_TRUE(r.run_until([&] { return r.sup.zone_state(0) == ValveState::Open; }, 8000));
    r.run(61000);
    ASSERT_EQ(r.sup.zone_last_stop(0), StopReason::MaxRunTime);
    r.sup.save_retained(rec);
  }
  ASSERT_TRUE(zf::intact(rec));
  Rig after(c, &rec);
  EXPECT_EQ(after.sup.start_zone(0, 30).error, StartError::Resting) << "a restart must not cut the rest short";
}

}  // namespace
