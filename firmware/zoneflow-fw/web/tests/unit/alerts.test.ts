import { describe, expect, it } from 'vitest';
import { deriveAlerts } from '../../src/state/alerts.ts';
import { baseStatus, zoneStatus } from './fixtures.ts';

const name = (id: number) => `Zone ${id}`;
const until = () => 'Tue 07:00';

describe('deriveAlerts', () => {
  it('is empty for a healthy controller', () => {
    expect(deriveAlerts(baseStatus(), name, until)).toEqual([]);
  });

  it('explains a zone fault in plain language, names the zone and offers clear', () => {
    const s = { ...baseStatus(), faults: { active: ['zone_open_circuit' as const], zones: [{ zone: 3, fault: 'zone_open_circuit', locked_out: true }] } };
    const [a] = deriveAlerts(s, name, until);
    expect(a?.title).toBe('Zone 3: no current');
    expect(a?.detail).toMatch(/Check the wire/);
    expect(a?.clearable).toBe(true);
    expect(a?.severity).toBe('error');
  });

  it('does not offer clear for conditions that clear themselves', () => {
    const [a] = deriveAlerts({ ...baseStatus(), faults: { active: ['ac_lost'], zones: [] } }, name, until);
    expect(a?.clearable).toBe(false);
  });

  it('a board not yet commissioned is a warning that clears itself', () => {
    const [a] = deriveAlerts({ ...baseStatus(), faults: { active: ['not_commissioned'], zones: [] } }, name, until);
    expect(a?.severity).toBe('warning');
    expect(a?.clearable).toBe(false);
    expect(a?.title).toBe('Safety timing not learned');
  });

  it('a fault of a safety layer that needs a repair is not cleared with one press', () => {
    for (const [code, step] of [['keepalive_bridged', '2b'], ['relay_gate_bleed', '5b']] as const) {
      const hardware = { check: code === 'relay_gate_bleed' ? 'gate_bleed' : 'float_drive_enabled', step, repair_needed: true };
      const [a] = deriveAlerts({ ...baseStatus(), faults: { active: [code], zones: [], hardware } }, name, until);
      expect(a?.clearable).toBe(false);
      expect(a?.severity).toBe('error');
    }
  });

  it('reports resting zones, rain delay, the sensor and an unset clock, errors first', () => {
    const b = baseStatus();
    const s = {
      ...b,
      time_status: 'unset' as const,
      zones: [zoneStatus(2, { resting: true, rest_remaining_s: 600 })],
      rain_delay: { active: true, until_utc: 1, remaining_s: 10 },
      inhibit: { ...b.inhibit, sensor: true },
      master: { ...b.master, enabled: true, locked_out: true },
      sensors: { ...b.sensors, current_sense_available: false },
    };
    const ids = deriveAlerts(s, name, until).map((a) => a.id);
    expect(ids[0]).toBe('master');
    expect(ids).toEqual(expect.arrayContaining(['blind', 'time', 'rain', 'sensor', 'rest:2']));
  });
});
