// All strings are in one table; check that every key used in the code exists.
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { describe, expect, it } from 'vitest';
import { en } from '../../src/i18n/en.ts';

function files(dir: string): string[] {
  return readdirSync(dir).flatMap((n) => {
    const p = join(dir, n);
    return statSync(p).isDirectory() ? files(p) : p.endsWith('.tsx') || p.endsWith('.ts') ? [p] : [];
  });
}

describe('string table', () => {
  const src = new URL('../../src', import.meta.url).pathname;
  it('has every key that t() is called with', () => {
    const missing: string[] = [];
    for (const f of files(src)) {
      for (const m of readFileSync(f, 'utf8').matchAll(/\bt\('([a-z0-9_]+)'/g)) if (!(m[1]! in en)) missing.push(`${f}: ${m[1]}`);
      for (const m of readFileSync(f, 'utf8').matchAll(/\btn\('([a-z0-9_]+)'/g)) {
        if (!(`${m[1]}_one` in en) || !(`${m[1]}_other` in en)) missing.push(`${f}: ${m[1]}_one/_other`);
      }
    }
    expect(missing).toEqual([]);
  });
  it('has a plain-language title and check for every fault code in the API', () => {
    const api = JSON.parse(readFileSync(new URL('../../../../../docs/firmware/openapi.json', import.meta.url), 'utf8'));
    const codes: string[] = api.components.schemas.Faults.properties.active.items.enum;
    for (const c of codes) {
      expect(en, c).toHaveProperty(`fault_${c}`);
      expect(en, c).toHaveProperty(`fault_${c}_check`);
    }
  });
  it('explains every run refusal code', () => {
    const api = JSON.parse(readFileSync(new URL('../../../../../docs/firmware/openapi.json', import.meta.url), 'utf8'));
    for (const c of api.components.schemas.RunRejection['x-error-codes'] as string[]) expect(en, c).toHaveProperty(`err_${c}`);
  });
});
