// Smoke test of the WebAssembly device, run by Node:
//   node sim/web/test/smoke.mjs <path to zoneflow-demo.js>
// Boots the demo garden, logs in and runs a zone through the REST API, and
// checks that the simulated valve opens, the display draws and a cut wire
// raises a fault. It proves the module works; it is not a test of the
// firmware logic, which test/host covers.
import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';

const modulePath = process.argv[2];
if (!modulePath) {
  console.error('usage: node smoke.mjs <zoneflow-demo.js>');
  process.exit(2);
}
const { default: create } = await import(pathToFileURL(modulePath).href);
const m = await create();

function cstr(text) {
  const n = m.lengthBytesUTF8(text) + 1;
  const p = m._malloc(n);
  m.stringToUTF8(text, p, n);
  return p;
}
function callJson(fn, arg) {
  const p = cstr(JSON.stringify(arg));
  const out = m.UTF8ToString(m[fn](p));
  m._free(p);
  return JSON.parse(out);
}
function boot(options) {
  const p = cstr(JSON.stringify(options));
  const ok = m._zf_boot(p);
  m._free(p);
  return ok;
}
const world = () => JSON.parse(m.UTF8ToString(m._zf_world()));
function set(what, index, value) {
  const p = cstr(what);
  m._zf_set(p, index, value);
  m._free(p);
}
let cookie = '';
let csrf = '';
function request(method, path, body) {
  const res = callJson('_zf_request', {
    method,
    path: '/api/v1' + path,
    body: body === undefined ? '' : JSON.stringify(body),
    content_type: body === undefined ? '' : 'application/json',
    host: '192.168.1.47',
    origin: 'http://192.168.1.47',
    cookie,
    csrf,
  });
  for (const [name, value] of res.headers ?? []) {
    if (name.toLowerCase() === 'set-cookie') cookie = value.split(';')[0];
  }
  return { status: res.status, json: res.body ? JSON.parse(res.body) : null, action: res.action };
}

const utc = Date.UTC(2026, 10, 10, 13, 32) / 1000;
assert.equal(boot({ utc, tz: 'CET-1CEST,M3.5.0,M10.5.0/3' }), 1);

// The display draws once, then reports no change.
const frame = m._zf_frame();
assert.notEqual(frame, 0, 'first frame');
const lit = m.HEAPU8.subarray(frame, frame + 320 * 240 * 4).some((v, i) => i % 4 !== 3 && v > 40);
assert.ok(lit, 'the display shows something');

// Reads are open; writes need the session.
assert.equal(request('GET', '/status').status, 200);
assert.equal(request('POST', '/zones/1/run', { duration_s: 120 }).status, 401);
const login = request('POST', '/auth/login', { password: 'zoneflow' });
assert.equal(login.status, 200);
csrf = login.json.csrf;

const run = request('POST', '/zones/1/run', { duration_s: 120 });
assert.equal(run.status, 202, JSON.stringify(run.json));
m._zf_step(400); // 20 s: the start-up self-test, then the one before the run (step 2 holds 2 s)
let w = world();
assert.ok(w.zones[0].led && w.zones[0].water, 'zone 1 is watering');
assert.ok(w.relay, 'the valve-common relay is closed');
assert.ok(w.current_ma > 200, 'the valve draws current');
assert.ok(w.flow_lpm > 0, 'the flow meter turns');

// The event stream sends the state at once and events after.
const stream = callJson('_zf_stream_open', { host: '192.168.1.47', cookie });
assert.equal(stream.status, 200);
assert.match(stream.first, /event: state\ndata: \{/);
request('POST', '/queue/stop');
m._zf_step(100);
const polled = JSON.parse(m.UTF8ToString(m._zf_stream_poll(1000)));
assert.match(polled.text, /event: (event|state)/);
w = world();
assert.ok(!w.zones[0].water && !w.relay, 'stopped: valve shut, relay open');

// A cut wire: the zone opens but draws nothing, and the firmware says so.
set('cut', 2, 1);
assert.equal(request('POST', '/zones/3/run', { duration_s: 120 }).status, 202);
m._zf_step(20 * 30);
const faults = request('GET', '/faults').json;
assert.ok(JSON.stringify(faults).includes('open'), 'open-circuit fault: ' + JSON.stringify(faults));

// Storage survives a restart.
const stored = JSON.parse(m.UTF8ToString(m._zf_storage()));
assert.ok(Object.keys(stored.nvs).length > 0 && Object.keys(stored.web).length > 0);
assert.equal(boot({ utc: utc + 600, storage: stored }), 1);
assert.equal(request('GET', '/zones').json.zones?.[0]?.name ?? request('GET', '/zones').json[0]?.name, 'Front lawn');

// Speed: how long a simulated hour takes.
const t0 = performance.now();
m._zf_step(20 * 3600);
const perHour = performance.now() - t0;
console.log(`ok. One simulated hour takes ${perHour.toFixed(0)} ms here.`);
