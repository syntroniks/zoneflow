// Generates src/api/schema.d.ts from docs/firmware/openapi.json.
//   node scripts/gen-api.ts          write the file
//   node scripts/gen-api.ts --check  fail if the checked-in file is stale (CI)
import { readFileSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import openapiTS, { astToString } from 'openapi-typescript';

const spec = new URL('../../../../docs/firmware/openapi.json', import.meta.url);
const out = fileURLToPath(new URL('../src/api/schema.d.ts', import.meta.url));

const header =
  '// GENERATED from docs/firmware/openapi.json by scripts/gen-api.ts. Do not edit.\n' +
  '// Regenerate with `npm run gen:api`; CI fails if this file is stale.\n\n';
const ast = await openapiTS(JSON.parse(readFileSync(spec, 'utf8')));
const text = header + astToString(ast);

if (process.argv.includes('--check')) {
  let current = '';
  try {
    current = readFileSync(out, 'utf8');
  } catch {
    // missing counts as stale
  }
  if (current !== text) {
    console.error('src/api/schema.d.ts is out of date with docs/firmware/openapi.json; run `npm run gen:api`.');
    process.exit(1);
  }
  console.log('API types match docs/firmware/openapi.json');
} else {
  writeFileSync(out, text);
  console.log(`wrote ${out}`);
}
