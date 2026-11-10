import { describe, expect, it } from 'vitest';
import type { HistoryRecord } from '../../src/api/client.ts';
import { describeRecord, summarise } from '../../src/screens/history.tsx';

type Run = Extract<HistoryRecord, { type: 'run' }>;
const run = (zone: number, utc: number, actual: number, litres: number | null = null): Run => ({
  id: utc, utc, time_valid: true, type: 'run', zone, program_id: 2, source: 'schedule', end: 'completed',
  planned_s: actual, actual_s: actual, current_ma: 300, litres,
});

describe('history', () => {
  it('sums runtime and water per zone inside the window, ignoring unknown times', () => {
    const records: HistoryRecord[] = [
      run(1, 1000, 600, 50.5),
      run(1, 2000, 300, 20),
      run(2, 2500, 120),
      run(3, 10, 900, 99), // before the window
      { ...run(4, 3000, 60), time_valid: false },
      { id: 9, utc: 3000, time_valid: true, type: 'fault_raised', zone: 1, code: 'short_circuit' },
    ];
    expect(summarise(records, 500)).toEqual([
      { zone: 1, seconds: 900, litres: 70.5, runs: 2 },
      { zone: 2, seconds: 120, litres: null, runs: 1 },
    ]);
  });

  it('describes a cut-short run with what was planned and why it ended', () => {
    const d = describeRecord({ ...run(3, 1, 214), planned_s: 600, end: 'requested', source: 'manual_zone', litres: 4.1 });
    expect(d.text).toBe('Zone 3 watered (stopped)');
    expect(d.detail).toBe('3 min 34 s of 10 min · Manual run · 300 mA · 4.1 L');
    expect(d.tone).toBe('water');
  });

  it('describes faults, restarts and updates in plain words', () => {
    expect(describeRecord({ id: 1, utc: 1, time_valid: true, type: 'fault_raised', zone: 2, code: 'zone_open_circuit' }).text).toBe(
      'Fault: No current through a valve (Zone 2)',
    );
    const boot = describeRecord({ id: 2, utc: 1, time_valid: true, type: 'boot', reset_reason: 'brownout' });
    expect(boot.detail).toBe('After the supply voltage dropped');
    expect(boot.tone).toBe('warning');
    const ota = describeRecord({ id: 3, utc: 1, time_valid: true, type: 'ota', stage: 'failed', source: 'upload', error: 'bad_image' });
    expect(ota.text).toBe('Update failed');
    expect(ota.detail).toBe('not a firmware image');
  });
});
