// SPDX-License-Identifier: Apache-2.0
// Puts the demo together in sim/web/dist/: the page, the firmware module
// and the web interface exactly as built for the controller (../../www).
// Run by build.sh, after the module and the interface are built.
import { cpSync, existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { TIME_ZONES } from '../../web/src/lib/timezones.ts';

const here = fileURLToPath(new URL('.', import.meta.url));
const www = join(here, '../../www');
const built = join(here, '../build-web');
const dist = join(here, 'dist');

for (const need of [join(www, 'index.html'), join(built, 'zoneflow-demo.wasm'), join(built, 'zoneflow-demo.js')]) {
  if (!existsSync(need)) {
    console.error(`missing ${need}: run sim/web/build.sh`);
    process.exit(1);
  }
}

rmSync(dist, { recursive: true, force: true });
mkdirSync(dist, { recursive: true });
cpSync(join(here, 'page'), dist, { recursive: true });
cpSync(join(built, 'zoneflow-demo.js'), join(dist, 'zoneflow-demo.js'));
cpSync(join(built, 'zoneflow-demo.wasm'), join(dist, 'zoneflow-demo.wasm'));

// The interface's files, without the gzip copies made for the controller:
// a web server compresses by itself.
cpSync(www, join(dist, 'ui'), { recursive: true, filter: (src) => !src.endsWith('.gz') });
cpSync(join(dist, 'ui', 'favicon.svg'), join(dist, 'favicon.svg'));

// The interface's own index.html with two changes: its files are under ui/
// here, and shim.js runs first (it must come before any tag with a URL).
let html = readFileSync(join(www, 'index.html'), 'utf8');
const before = html;
html = html.replaceAll('="/assets/', '="ui/assets/').replaceAll('="/favicon.svg"', '="ui/favicon.svg"');
html = html.replace(/<meta charset="utf-8"\s*\/?>/i, (tag) => `${tag}\n    <script src="shim.js"></script>`);
if (html === before || !html.includes('shim.js') || !html.includes('ui/assets/')) {
  console.error('www/index.html does not look as expected; app.html not written');
  process.exit(1);
}
if (/(src|href)="\//.test(html)) {
  console.error('app.html still has a URL from the server root');
  process.exit(1);
}
writeFileSync(join(dist, 'app.html'), html);
rmSync(join(dist, 'ui', 'index.html'));

writeFileSync(join(dist, 'tz.json'), JSON.stringify(TIME_ZONES));

function size(dir: string): number {
  return readdirSync(dir).reduce((sum, name) => {
    const p = join(dir, name);
    const s = statSync(p);
    return sum + (s.isDirectory() ? size(p) : s.size);
  }, 0);
}
console.log(`dist: ${(size(dist) / 1024).toFixed(0)} KB before compression`);
