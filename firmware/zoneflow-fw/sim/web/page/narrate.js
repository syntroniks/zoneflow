// SPDX-License-Identifier: Apache-2.0
// Plain sentences for the log, from the firmware's own events (the JSON of
// the event stream) and the state of its output stage. Wording only: every
// fact comes from the firmware.

const FAULTS = {
  zone_open_circuit: 'the valve draws no current. A broken wire or a dead solenoid',
  zone_over_current: 'the valve draws too much current',
  short_circuit: 'short circuit',
  current_while_idle: 'current flows although every valve is off',
  ac_lost: 'the 24 VAC supply is gone',
  flow_without_zones: 'water flows with every valve shut. A leak, or a valve stuck open',
  config_invalid: 'the stored settings were invalid',
  time_invalid: 'the clock is not set',
  storage_error: 'storage error',
  master_locked_out: 'the master valve is locked out after repeated faults',
  current_sense_unavailable: 'the current measurement is not available',
  over_current_trip: 'the hardware over-current comparator tripped and removed gate drive',
  keepalive_detector: 'a keep-alive detector failed its self-test',
  output_expander: 'the output expander does not answer',
  relay_welded: 'the valve-common relay does not open',
  output_shorted: 'an output device conducts with no valve switched on. The relay keeps the valve common off',
  trip_latch_stuck: 'the over-current latch does not clear',
  sensor_supply: 'the sensor supply is overloaded',
};

const SKIPS = {
  rain_delay: 'a rain delay is set',
  sensor_inhibit: 'the rain sensor is wet',
  no_ac: 'there is no 24 VAC',
  maintenance: 'an update is running',
};

const words = (code) => String(code ?? '').replaceAll('_', ' ');

function zoneLabel(e, world) {
  const name = world?.zones?.[e.zone - 1]?.name;
  return name ? `Zone ${e.zone} (${name})` : `Zone ${e.zone}`;
}

function programLabel(e, world) {
  const name = world?.programs?.[e.program_id];
  return name ? `Program “${name}”` : `Program ${e.program_id}`;
}

/** One log line for an event, or null for events not worth a line. */
export function describeEvent(e, world) {
  switch (e.type) {
    case 'zone_changed':
      if (e.state === 'open') return { text: `${zoneLabel(e, world)}: valve open.`, tone: 'good' };
      if (e.state === 'off') {
        const why = {
          completed: 'its time is up',
          requested: 'stopped on request',
          fault: 'shut off after a fault',
          ac_lost: '24 VAC lost',
          max_run_time: 'the maximum run time was reached',
          shutdown: 'the controller is shutting down',
        }[e.reason];
        const tone = e.reason === 'fault' || e.reason === 'ac_lost' ? 'bad' : e.reason === 'max_run_time' ? 'safety' : '';
        return { text: `${zoneLabel(e, world)}: valve closed${why ? ', ' + why : ''}.`, tone };
      }
      return null;
    case 'run_ended': {
      const parts = [`${e.actual_s} s of ${e.planned_s} s`];
      if (e.current_ma) parts.push(`${e.current_ma} mA`);
      if (typeof e.litres === 'number') parts.push(`${e.litres.toFixed(1)} L`);
      return { text: `Run recorded for zone ${e.zone}: ${parts.join(', ')}.`, tone: '' };
    }
    case 'program_started':
      return {
        text: `${programLabel(e, world)} started ${e.source === 'schedule' ? 'by the schedule' : 'by hand'}${e.late ? ', late' : ''}.`,
        tone: 'good',
      };
    case 'program_skipped':
      return { text: `${programLabel(e, world)} skipped: ${SKIPS[e.reason] ?? words(e.reason)}.`, tone: 'safety' };
    case 'program_missed':
      return { text: `${programLabel(e, world)} missed its start time.`, tone: 'safety' };
    case 'fault_raised':
      return {
        text: `Fault${e.zone != null ? ` on zone ${e.zone}` : ''}: ${FAULTS[e.code] ?? words(e.code)}.`,
        tone: 'bad',
      };
    case 'fault_cleared':
      return { text: `Fault cleared: ${words(e.code)}${e.zone != null ? ` on zone ${e.zone}` : ''}.`, tone: 'good' };
    case 'sensor_changed':
      if (e.sensor === 'rain') {
        return { text: e.active ? 'The controller sees the rain sensor wet.' : 'The controller sees the rain sensor dry.', tone: '' };
      }
      return null;
    case 'rain_delay_changed':
      return { text: e.until_utc ? 'Rain delay set.' : 'Rain delay cleared.', tone: '' };
    case 'command_rejected':
      return { text: `The controller refused a command: ${words(e.reason)}.`, tone: 'safety' };
    case 'config_applied':
      return { text: 'Settings applied.', tone: '' };
    default:
      return null;
  }
}

/** Lines for a change of the output stage (fail-safe self-test, relay). */
export function describeHardware(before, now) {
  if (!now || !before || before.phase === now.phase) return [];
  const out = [];
  const passed = now.self_tests_passed > before.self_tests_passed;
  if (passed) {
    const d = now.detectors;
    out.push({
      text: `Fail-safe self-test passed: with the keep-alive stopped, gate drive fell away in ${d.release_drive_ms} ms and the relay in ${d.release_relay_ms} ms.`,
      tone: 'safety',
    });
  }
  switch (now.phase) {
    case 'starting':
      out.push({ text: 'A run is asked for. First the output stage tests that it can still switch everything off.', tone: 'safety' });
      break;
    case 'startup_test':
      out.push({ text: 'Start-up self-test of the output stage.', tone: 'safety' });
      break;
    case 'ready':
      out.push({ text: 'The valve-common relay closes. Valves may now be switched.', tone: '' });
      break;
    case 'stopping':
      // The start-up test passes through this phase too; only a run ends here.
      if (before.phase !== 'ready') break;
      out.push({ text: 'Nothing left to water: the relay opens and the stop test runs.', tone: '' });
      break;
    case 'trip_clear':
      out.push({ text: 'Over-current latch set. The keep-alive is held low to clear it.', tone: 'bad' });
      break;
    case 'fault':
      out.push({ text: 'The output stage failed a check. Nothing runs until someone acknowledges it.', tone: 'bad' });
      break;
    case 'no_ac':
      out.push({ text: 'No 24 VAC: nothing is tested and nothing runs.', tone: 'bad' });
      break;
    default:
      break;
  }
  return out;
}
