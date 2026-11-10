// Turns the Vite build (dist/) into the firmware's www/ directory, which
// idf.py packs into the LittleFS "www" partition image
// (docs/firmware/web-ui-contract.md):
//  - every text file gets a gzip copy next to it (the device does not
//    compress; it sends <file>.gz when the browser accepts gzip, and the
//    plain file otherwise);
//  - file names must be lower case without spaces, paths under 64 bytes;
//  - the gzip size of what a browser downloads must stay under the budget.
//   node scripts/pack.ts              copy to ../www and check
//   node scripts/pack.ts --check-only check dist/ only
import { cpSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { constants, gzipSync } from 'node:zlib';

const BUDGET_GZIP_BYTES = 150 * 1024;
const MAX_PATH_BYTES = 63;
const dist = fileURLToPath(new URL('../dist', import.meta.url));
const www = fileURLToPath(new URL('../../www', import.meta.url));
const checkOnly = process.argv.includes('--check-only');

function walk(dir: string): string[] {
  return readdirSync(dir).flatMap((n) => {
    const p = join(dir, n);
    return statSync(p).isDirectory() ? walk(p) : [p];
  });
}

const errors: string[] = [];
const rows: [string, number, number][] = [];
let totalGz = 0;
let totalRaw = 0;
const files = walk(dist).filter((f) => !f.endsWith('.gz'));
for (const f of files) {
  const rel = '/' + relative(dist, f).split('\\').join('/');
  if (rel !== rel.toLowerCase() || /\s/.test(rel)) errors.push(`${rel}: file names must be lower case without spaces`);
  if (Buffer.byteLength(rel + '.gz') > MAX_PATH_BYTES) errors.push(`${rel}: path too long for LittleFS (${MAX_PATH_BYTES} bytes with .gz)`);
  const raw = readFileSync(f);
  const gz = gzipSync(raw, { level: constants.Z_BEST_COMPRESSION });
  totalRaw += raw.length;
  totalGz += Math.min(gz.length, raw.length);
  rows.push([rel, raw.length, gz.length]);
}

const kb = (n: number) => `${(n / 1024).toFixed(1)} KB`;
console.log('file'.padEnd(44), 'raw'.padStart(10), 'gzip'.padStart(10));
for (const [rel, raw, gz] of rows.sort()) console.log(rel.padEnd(44), kb(raw).padStart(10), kb(gz).padStart(10));
console.log('total'.padEnd(44), kb(totalRaw).padStart(10), kb(totalGz).padStart(10), ` (budget ${kb(BUDGET_GZIP_BYTES)})`);
if (totalGz > BUDGET_GZIP_BYTES) errors.push(`gzip total ${kb(totalGz)} is over the ${kb(BUDGET_GZIP_BYTES)} budget`);
if (!files.some((f) => relative(dist, f) === 'index.html')) errors.push('dist/index.html is missing');

if (errors.length) {
  for (const e of errors) console.error(`error: ${e}`);
  process.exit(1);
}
if (checkOnly) process.exit(0);

rmSync(www, { recursive: true, force: true });
mkdirSync(www, { recursive: true });
cpSync(dist, www, { recursive: true });
for (const [rel, raw, gz] of rows) {
  if (gz < raw * 0.9) writeFileSync(join(www, rel + '.gz'), gzipSync(readFileSync(join(dist, rel)), { level: constants.Z_BEST_COMPRESSION }));
}
console.log(`wrote ${www}`);
