// The web UI names faults with the same words as the display, ESPHome and
// the manual. The one table is zf_core's zf/fault_text.hpp; this test reads
// it and holds src/i18n/en.ts to it.
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { en } from '../../src/i18n/en.ts';
import { faultTitleFor } from '../../src/state/alerts.ts';

const header = readFileSync(new URL('../../../components/zf_core/include/zf/fault_text.hpp', import.meta.url), 'utf8');
const rows = [...header.matchAll(/X\((\w+), "([a-z_]+)", "([^"]+)", "([^"]*)"\)/g)].map((m) => ({ code: m[2]!, title: m[3]!, zoneTitle: m[4]! }));
const table = en as Record<string, string>;

describe('fault text', () => {
  it('reads every row of the C++ table', () => {
    const api = JSON.parse(readFileSync(new URL('../../../../../docs/firmware/openapi.json', import.meta.url), 'utf8'));
    const codes: string[] = api.components.schemas.Faults.properties.active.items.enum;
    expect(rows.map((r) => r.code).sort()).toEqual([...codes].sort());
  });

  it.each(rows)('$code: same title as zf/fault_text.hpp', ({ code, title, zoneTitle }) => {
    expect(table[`fault_${code}`]).toBe(title);
    if (zoneTitle) expect(table[`fault_${code}_zone`]).toBe(zoneTitle.replace('%s', '{zone}'));
    else expect(table).not.toHaveProperty(`fault_${code}_zone`);
  });

  it('names the zone the way the display does', () => {
    expect(faultTitleFor('zone_open_circuit', ['Roses'])).toBe('Roses: no current');
    expect(faultTitleFor('zone_open_circuit', [])).toBe('Valve drew no current');
    expect(faultTitleFor('ac_lost', [])).toBe('No 24 V AC supply');
  });
});
