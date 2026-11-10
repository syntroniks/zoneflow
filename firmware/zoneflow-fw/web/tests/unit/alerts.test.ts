import { describe, expect, it } from 'vitest';
import type { Status } from '../../src/api/client.ts';
import { deriveAlerts } from '../../src/state/alerts.ts';

const name = (id: number) => `Zone ${id}`;
const until = () => 'Tue 07:00';

describe('deriveAlerts', () => {
  it('is empty for a healthy controller', () => {
    expect(deriveAlerts({ time_status: 'synced', faults: { active: [], zones: [] }, sensors: { current_sense_available: true } }, name, until)).toEqual([]);
  });

  it('explains a zone fault in plain language, names the zone and offers clear', () => {
    const s: Status = { faults: { active: ['zone_open_circuit'], zones: [{ zone: 3, fault: 'zone_open_circuit', locked_out: true }] } };
    const [a] = deriveAlerts(s, name, until);
    expect(a?.title).toBe('No current through a valve on Zone 3');
    expect(a?.detail).toMatch(/Check the wire/);
    expect(a?.clearable).toBe(true);
    expect(a?.severity).toBe('error');
  });

  it('does not offer clear for conditions that clear themselves', () => {
    const [a] = deriveAlerts({ faults: { active: ['ac_lost'], zones: [] } }, name, until);
    expect(a?.clearable).toBe(false);
  });

  it('reports resting zones, rain delay, the sensor and an unset clock, errors first', () => {
    const s: Status = {
      time_status: 'unset',
      zones: [{ id: 2, resting: true, rest_remaining_s: 600 }],
      rain_delay: { active: true, until_utc: 1, remaining_s: 10 },
      inhibit: { sensor: true },
      master: { enabled: true, locked_out: true },
      faults: { active: [], zones: [] },
      sensors: { current_sense_available: false },
    };
    const ids = deriveAlerts(s, name, until).map((a) => a.id);
    expect(ids[0]).toBe('master');
    expect(ids).toEqual(expect.arrayContaining(['blind', 'time', 'rain', 'sensor', 'rest:2']));
  });
});
