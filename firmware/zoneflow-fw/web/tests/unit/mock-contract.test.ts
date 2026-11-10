// The mock controller answers in the shapes docs/firmware/openapi.json
// documents, so the UI is tested against what the firmware sends (the
// firmware is held to the same schemas by web_openapi_schema_test.cpp).
import { describe, expect, it } from 'vitest';
import { Device } from '../../mock/device.ts';
import { check, responseSchema } from './schema-check.ts';

const UTC = 1781500320;

function busyDevice(): Device {
  const d = new Device({ scenario: 'demo', utc: UTC, frozen: true });
  d.runProgram(2, 'schedule');
  d.raiseFault('zone_open_circuit', 4);
  d.setRainDelay(UTC + 7200);
  d.pause();
  return d;
}

describe('mock answers match the OpenAPI schemas', () => {
  const views: [string, (d: Device) => unknown][] = [
    ['/status', (d) => d.statusJson()],
    ['/queue', (d) => d.queueJson()],
    ['/sensors', (d) => d.sensorsJson()],
    ['/faults', (d) => d.faultsJson()],
    ['/rain-delay', (d) => d.rainJson()],
    ['/time', (d) => d.timeJson()],
    ['/firmware', (d) => d.firmwareJson()],
    ['/info', (d) => d.infoJson()],
    ['/config', (d) => d.config],
    ['/mqtt', (d) => d.mqttJson()],
    ['/history', (d) => d.historyPage({ limit: 100 })],
    ['/zones/{zone}', (d) => d.zoneJson(0)],
  ];
  for (const scenario of ['demo', 'fresh'] as const) {
    it.each(views)(`${scenario}: GET %s`, (route, view) => {
      const d = scenario === 'demo' ? busyDevice() : new Device({ scenario, utc: UTC, frozen: true });
      expect(check(responseSchema(route), view(d))).toEqual([]);
    });
  }

  it('records a finished run in the history as the firmware does', () => {
    const d = new Device({ scenario: 'demo', utc: UTC, frozen: true });
    const before = d.history.length;
    d.runZone(2, 60);
    d.stopAll();
    const run = d.history.slice(before).find((r) => r['type'] === 'run');
    expect(run).toMatchObject({ zone: 2, source: 'manual_zone', end: 'requested', planned_s: 60, current_ma: 255 });
    expect(check(responseSchema('/history'), d.historyPage({ limit: 5 }))).toEqual([]);
  });

  it('pages the history newest first', () => {
    const d = new Device({ scenario: 'demo', utc: UTC, frozen: true });
    const p1 = d.historyPage({ limit: 10 }) as { records: { id: number }[]; next_before: number };
    expect(p1.records).toHaveLength(10);
    expect(p1.records[0]!.id).toBeGreaterThan(p1.records[9]!.id);
    const p2 = d.historyPage({ limit: 10, before: p1.next_before }) as { records: { id: number }[] };
    expect(p2.records[0]!.id).toBe(p1.records[9]!.id - 1);
  });
});

describe('schema checker', () => {
  it('reports a missing required field and a wrong enum value', () => {
    const d = new Device({ scenario: 'demo', utc: UTC, frozen: true });
    const q = d.queueJson();
    delete q['paused_s'];
    expect(check(responseSchema('/queue'), q).join()).toMatch(/paused_s: required/);
    const z = { ...d.zoneJson(0), state: 'flooding' };
    expect(check(responseSchema('/zones/{zone}'), z).join()).toMatch(/not in enum/);
  });
});
