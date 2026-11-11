// SPDX-License-Identifier: Apache-2.0
// ZoneFlow browser demo: runs the firmware module (device.cpp, compiled to
// WebAssembly), draws the controller and the garden, and answers the web
// interface in the frame from the firmware's own REST API.
import createModule from './zoneflow-demo.js';
import { buildGarden } from './garden.js';
import { describeEvent, describeHardware } from './narrate.js';

const STORE_KEY = 'zoneflow-demo/1';
const DEVICE_HOST = '192.168.1.47';
const DEMO_PASSWORD = 'zoneflow';
const SETUP_CODE = 'K7Q2M9XW4R'; // kSetupCode in device.cpp
const TICK_MS = 50;
const $ = (id) => document.getElementById(id);

// ---- the firmware module ---------------------------------------------------------------------------

let m; // the Emscripten module
function cstr(text) {
  const size = m.lengthBytesUTF8(text) + 1;
  const p = m._malloc(size);
  m.stringToUTF8(text, p, size);
  return p;
}
function withString(text, fn) {
  const p = cstr(text);
  try {
    return fn(p);
  } finally {
    m._free(p);
  }
}
const dev = {
  boot: (options) => withString(JSON.stringify(options), (p) => m._zf_boot(p)) === 1,
  step: (ticks) => m._zf_step(ticks),
  world: () => JSON.parse(m.UTF8ToString(m._zf_world())),
  narrate: () => JSON.parse(m.UTF8ToString(m._zf_narrate())),
  set: (what, index, value) => withString(what, (p) => m._zf_set(p, index, value)),
  turn: (detents) => m._zf_turn(detents),
  key: (key, down) => m._zf_key(key, down ? 1 : 0),
  request: (req) => JSON.parse(withString(JSON.stringify(req), (p) => m.UTF8ToString(m._zf_request(p)))),
  streamOpen: (req) => JSON.parse(withString(JSON.stringify(req), (p) => m.UTF8ToString(m._zf_stream_open(p)))),
  streamClose: (id) => m._zf_stream_close(id),
  streamPoll: (now) => JSON.parse(m.UTF8ToString(m._zf_stream_poll(now))),
  storage: () => JSON.parse(m.UTF8ToString(m._zf_storage())),
  storageSerial: () => m._zf_storage_serial(),
};

// ---- state of the page -----------------------------------------------------------------------------

let world = null; // last zf_world()
let running = false; // the device is up (false while it restarts)
let speed = 1;
let skipping = null; // { fromUtc } while "next watering" runs
let cookie = ''; // the phone's cookie jar: "zf_session=..."
let savedSerial = -1;
let lastKeepAliveUtc = 0;
let lastControlUse = 0;
const conditions = { rain: false, wifi: false, ac: false, leak: false, triac: false };
const wires = Array(8).fill('ok'); // ok | cut | short
const sinks = new Set(); // open event streams of the web interface

// ---- time zone ---------------------------------------------------------------------------------------

async function posixZone() {
  let iana = 'UTC';
  try {
    iana = Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';
    const table = await (await fetch('tz.json')).json();
    const hit = table.find(([name]) => name === iana);
    if (hit) return hit[1];
  } catch {
    // fall through to a fixed offset
  }
  // Not in the table: today's offset without daylight-saving rules.
  const east = -new Date().getTimezoneOffset();
  if (east === 0) return 'UTC0';
  const abs = Math.abs(east);
  const hh = String(Math.floor(abs / 60)).padStart(2, '0');
  const mm = abs % 60 ? ':' + String(abs % 60).padStart(2, '0') : '';
  // POSIX signs are reversed: UTC+2 is written "-2".
  return `<${east > 0 ? '+' : '-'}${hh}${mm.replace(':', '')}>${east > 0 ? '-' : ''}${Number(hh)}${mm}`;
}

// ---- requests from the web interface -----------------------------------------------------------

function apiCall(method, pathAndQuery, { body = '', contentType = '', csrf = '', authorization = '' } = {}) {
  const [path, query = ''] = pathAndQuery.split('?');
  const res = dev.request({
    method,
    path,
    query,
    body,
    content_type: contentType,
    cookie,
    csrf,
    authorization,
    host: DEVICE_HOST,
    origin: `http://${DEVICE_HOST}`,
  });
  for (const [name, value] of res.headers ?? []) {
    if (name.toLowerCase() === 'set-cookie') {
      const pair = value.split(';')[0];
      cookie = /Max-Age=0\b/i.test(value) || pair.endsWith('=') ? '' : pair;
    }
  }
  return res;
}

let demoSignedIn = false;
function signIn() {
  cookie = '';
  demoSignedIn = false;
  if (!world?.password_set) return false;
  const res = apiCall('POST', '/api/v1/auth/login', {
    body: JSON.stringify({ password: DEMO_PASSWORD }),
    contentType: 'application/json',
  });
  demoSignedIn = res.status === 200;
  return demoSignedIn;
}

// Under the phone: what a visitor needs to get in.
function phoneHint(w) {
  if (!w.password_set) {
    return `This controller has no password yet. The web interface asks for its setup code: ${SETUP_CODE}`;
  }
  return demoSignedIn ? `Signed in for you. The demo password is “${DEMO_PASSWORD}”.` : 'Protected by the password you set.';
}

const online = () => running && !conditions.wifi;
const networkDelay = () => new Promise((done) => setTimeout(done, 20 + Math.random() * 40));

// What the frame's fetch, XMLHttpRequest and EventSource talk to (shim.js).
window.zoneflowDemo = {
  async request({ method, url, headers = {}, body = '', binary = false }) {
    await networkDelay();
    if (!online()) {
      await new Promise((done) => setTimeout(done, 600));
      throw new TypeError('Failed to fetch');
    }
    const res = apiCall(method, url, {
      body: binary ? '' : body,
      contentType: headers['content-type'] ?? '',
      csrf: headers['x-csrf-token'] ?? '',
      authorization: headers['authorization'] ?? '',
    });
    if (res.action) setTimeout(() => deviceAction(res.action), 350);
    const out = {};
    for (const [name, value] of res.headers ?? []) {
      if (name.toLowerCase() !== 'set-cookie') out[name] = value;
    }
    out['Content-Type'] = res.content_type || 'application/json';
    return { status: res.status, headers: out, body: res.body ?? '' };
  },
  // sink: { open(), data(text), fail(permanent) }. Returns a close function.
  openStream(sink) {
    let id = 0;
    let closed = false;
    const entry = {
      sink,
      get id() {
        return id;
      },
      drop(permanent) {
        if (closed) return;
        closed = true;
        sinks.delete(entry);
        sink.fail(permanent);
      },
    };
    networkDelay().then(() => {
      if (closed) return;
      if (!online()) return entry.drop(false);
      const res = dev.streamOpen({ host: DEVICE_HOST, cookie });
      if (res.status !== 200) return entry.drop(true);
      id = res.id;
      sinks.add(entry);
      sink.open();
      sink.data(res.first);
    });
    return () => {
      if (closed) return;
      closed = true;
      sinks.delete(entry);
      if (id) dev.streamClose(id);
    };
  },
  navigated(path) {
    $('phone-url').textContent = 'zoneflow.local' + (path === '/' ? '' : path);
  },
};

function pumpStreams() {
  if (!running) return;
  const res = dev.streamPoll(performance.now());
  for (const id of res.closed ?? []) {
    for (const entry of [...sinks]) if (entry.id === id) entry.drop(false);
  }
  if (res.text) for (const entry of sinks) entry.sink.data(res.text);
}

function dropStreams() {
  for (const entry of [...sinks]) entry.drop(false);
}

// ---- restart, reset, storage -------------------------------------------------------------------

function save() {
  if (!running) return;
  try {
    localStorage.setItem(STORE_KEY, JSON.stringify(dev.storage()));
    savedSerial = dev.storageSerial();
  } catch {
    // private mode or full: the demo then starts over on reload
  }
}
function loadSaved() {
  try {
    const text = localStorage.getItem(STORE_KEY);
    return text ? JSON.parse(text) : null;
  } catch {
    return null;
  }
}

let zone = 'UTC0';
function applyConditions() {
  dev.set('rain', 0, conditions.rain ? 1 : 0);
  dev.set('wifi', 0, conditions.wifi ? 0 : 1);
  dev.set('ac', 0, conditions.ac ? 0 : 1);
  dev.set('leak', 0, conditions.leak ? 30 : 0);
  dev.set('triac', 0, conditions.triac ? 1 : 0);
  wires.forEach((state, z) => {
    dev.set('cut', z, state === 'cut' ? 1 : 0);
    dev.set('short', z, state === 'short' ? 1 : 0);
  });
}

function start(options) {
  const utc = world?.utc ?? Math.floor(Date.now() / 1000);
  const ok = dev.boot({ utc, tz: zone, ...options });
  if (!ok) throw new Error('the firmware did not start');
  applyConditions();
  world = dev.world();
  lastKeepAliveUtc = world.utc;
  running = true;
  signIn();
  save();
}

// kind: restart (keep storage), garden (demo garden), fresh (out of the box).
async function restart(kind, message) {
  if (!running) return;
  const storage = kind === 'restart' ? dev.storage() : null;
  running = false;
  skipping = null;
  setSpeed(1);
  dropStreams();
  $('screen-wrap').classList.add('booting');
  drawLeds(null);
  if (message) toast(message);
  if (kind !== 'restart') {
    try {
      localStorage.removeItem(STORE_KEY);
    } catch {
      // nothing stored
    }
    Object.keys(conditions).forEach((k) => (conditions[k] = false));
    wires.fill('ok');
    garden.reset();
    $('log').replaceChildren();
  }
  await new Promise((done) => setTimeout(done, 1100));
  start(kind === 'restart' ? { storage } : { fresh: kind === 'fresh' });
  lastHardware = null;
  $('screen-wrap').classList.remove('booting');
  syncConditionButtons();
  log(kind === 'fresh' ? 'A new controller powers up for the first time.' : 'The controller restarted.', 'safety');
  reloadPhone();
}

function deviceAction(action) {
  if (action === 'reboot') restart('restart', 'The controller is restarting…');
  else if (action === 'factory_reset') restart('fresh', 'Factory reset: everything is erased.');
  else if (action === 'reprovision') {
    toast('A real controller would now restart into WiFi setup. The simulator has no radio, so it stays connected.');
  }
}

// ---- the main loop ---------------------------------------------------------------------------------

let owed = 0;
let lastPump = performance.now();
function pump() {
  const now = performance.now();
  const elapsed = Math.min(now - lastPump, 250);
  lastPump = now;
  if (!running) return;
  if (skipping) {
    const until = performance.now() + 14;
    let found = false;
    let skipped = false;
    while (!found && !skipped && performance.now() < until) {
      dev.step(100);
      world = dev.world();
      found = world.zones.some((z) => z.led);
      // A start the controller held back is what the visitor waited for too.
      skipped = narrate().includes('program_skipped');
      if (world.utc - skipping.fromUtc > 8 * 86400) break;
    }
    if (found || skipped) {
      skipping = null;
      setSpeed(1);
      toast(
        found
          ? 'A watering has started. Time is back to normal speed.'
          : 'The controller skipped this start. The log says why. Time is back to normal speed.',
      );
    } else if (world.utc - skipping.fromUtc > 8 * 86400) {
      skipping = null;
      setSpeed(1);
      toast('Nothing ran in eight days. Check the programs, the rain sensor and the supply.');
    }
  } else {
    owed += elapsed * speed;
    const ticks = Math.floor(owed / TICK_MS);
    owed -= ticks * TICK_MS;
    if (ticks > 0) dev.step(ticks);
  }
  world = dev.world();

  if (world.restart) {
    const kind = world.restart === 'factory' ? 'fresh' : 'restart';
    restart(kind, kind === 'fresh' ? 'Factory reset: everything is erased.' : 'The controller is restarting…');
    return;
  }
  // Fast time must not sign the phone out (sessions end after 24 idle hours).
  if (cookie && world.utc - lastKeepAliveUtc > 3600) {
    lastKeepAliveUtc = world.utc;
    apiCall('GET', '/api/v1/auth');
  }
  pumpStreams();
  narrate();
}

// ---- drawing -----------------------------------------------------------------------------------------

const canvas = $('screen');
const ctx = canvas.getContext('2d');
const image = ctx.createImageData(320, 240);
let shown = {};

function drawLeds(w) {
  const row = $('led-row').children;
  const mini = $('leds-mini').children;
  row[0].firstElementChild.classList.toggle('on', !!w);
  row[1].firstElementChild.classList.toggle('on', !!w?.master);
  row[1].classList.toggle('unused', !w?.master_enabled);
  for (let z = 0; z < 8; z++) {
    const on = !!w?.zones[z].led;
    row[z + 2].firstElementChild.classList.toggle('on', on);
    row[z + 2].classList.toggle('unused', !!w && !w.zones[z].enabled);
    mini[z].classList.toggle('on', on);
  }
}

function setText(id, text) {
  if (shown[id] !== text) {
    shown[id] = text;
    $(id).textContent = text;
  }
}

function draw() {
  requestAnimationFrame(draw);
  if (!running || !world) return;
  const p = m._zf_frame();
  if (p) {
    image.data.set(m.HEAPU8.subarray(p, p + 320 * 240 * 4));
    ctx.putImageData(image, 0, 0);
  }
  const w = world;
  // The backlight, as brightness of the picture. An illustration: a dimmed
  // panel still reads well, so the scale is not linear.
  const level = w.backlight <= 0 ? 0 : Math.pow(w.backlight / 100, 0.45);
  $('screen-dim').style.opacity = String(Math.min(0.96, 1 - level));
  $('screen-wrap').classList.toggle('asleep', w.backlight <= 0);
  drawLeds(w);

  const names = w.zones.map((z) => z.name).join('|') + w.zones.map((z) => z.enabled).join('');
  if (shown.names !== names) {
    shown.names = names;
    $('label-card').replaceChildren(
      ...w.zones.map((z, i) => {
        const li = document.createElement('li');
        li.className = z.enabled ? '' : 'off';
        const n = document.createElement('b');
        n.textContent = String(i + 1);
        li.append(n, z.enabled ? z.name : '');
        return li;
      }),
    );
    [...$('wire-chips').children].forEach((chip, z) => (chip.disabled = !w.zones[z].enabled));
  }
  garden.update(w, speed);

  setText('phone-hint', phoneHint(w));
  setText('m-ac-v', w.ac ? '24 VAC' : 'no 24 VAC');
  $('m-ac').classList.toggle('bad', !w.ac);
  setText('m-current-v', `${w.current_ma} mA`);
  $('m-current').classList.toggle('live', w.current_ma > 0 && !w.tripped);
  $('m-current').classList.toggle('bad', w.tripped);
  setText('m-flow-v', `${w.flow_lpm.toFixed(1)} L/min`);
  $('m-flow').classList.toggle('live', w.flow_lpm > 0);
  setText('m-total-v', `${Math.round(w.litres).toLocaleString()} L`);
  drawClock(w.utc);
}

let clockFormat = null;
let dateFormat = null;
function drawClock(utc) {
  // The controller's own zone is the visitor's (posixZone), so the browser
  // can format its clock.
  clockFormat ??= new Intl.DateTimeFormat(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit' });
  dateFormat ??= new Intl.DateTimeFormat(undefined, { weekday: 'short', day: 'numeric', month: 'short' });
  const d = new Date(utc * 1000);
  setText('clock-time', clockFormat.format(d));
  setText('clock-date', dateFormat.format(d));
}

// ---- log ---------------------------------------------------------------------------------------------

let lastHardware = null;
function log(text, tone = '') {
  const list = $('log');
  const li = document.createElement('li');
  const time = document.createElement('time');
  time.textContent = world
    ? new Date(world.utc * 1000).toLocaleTimeString(undefined, { hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23' })
    : '';
  const span = document.createElement('span');
  span.textContent = text;
  if (tone) span.className = tone;
  li.append(time, span);
  list.prepend(li);
  while (list.children.length > 40) list.lastElementChild.remove();
}

// Writes the log. Returns the types of the events seen.
function narrate() {
  const n = dev.narrate();
  // At speed, only what matters: the stream of valve steps would bury it.
  const quiet = speed > 60 || skipping;
  for (const line of describeHardware(lastHardware, n.hardware)) if (!quiet) log(line.text, line.tone);
  lastHardware = n.hardware;
  for (const e of n.events ?? []) {
    tourEvent(e);
    const line = describeEvent(e, world);
    if (line && !(quiet && !line.tone)) log(line.text, line.tone);
  }
  return (n.events ?? []).map((e) => e.type);
}

// ---- controls of the controller ------------------------------------------------------------------

// A real hand at 600x would hold a button for minutes of controller time.
function usingControls() {
  lastControlUse = performance.now();
  if (speed !== 1 || skipping) {
    skipping = null;
    setSpeed(1);
  }
}

let knobAngle = 0;
function turn(detents) {
  if (!running || detents === 0) return;
  usingControls();
  dev.turn(detents);
  knobAngle += detents * 15;
  $('knob-cap').style.transform = `rotate(${knobAngle}deg)`;
}

// Holds a button for at least 150 ms (three control periods), or for as
// long as the finger stays down.
const held = {};
function keyDown(key, el) {
  if (!running || held[key]) return;
  usingControls();
  held[key] = { since: performance.now(), el };
  el?.classList.add('pressed');
  dev.key(key, true);
}
function keyUp(key) {
  const h = held[key];
  if (!h || h.releasing) return;
  h.releasing = true;
  setTimeout(() => {
    dev.key(key, false);
    h.el?.classList.remove('pressed');
    delete held[key];
  }, Math.max(0, 150 - (performance.now() - h.since)));
}
function tap(key, el) {
  keyDown(key, el);
  keyUp(key);
}

function bindHold(el, key) {
  el.addEventListener('pointerdown', (e) => {
    el.setPointerCapture(e.pointerId);
    keyDown(key, el);
  });
  el.addEventListener('pointerup', () => keyUp(key));
  el.addEventListener('pointercancel', () => keyUp(key));
  el.addEventListener('keydown', (e) => {
    if ((e.key === 'Enter' || e.key === ' ') && !e.repeat) keyDown(key, el);
  });
  el.addEventListener('keyup', (e) => {
    if (e.key === 'Enter' || e.key === ' ') keyUp(key);
  });
  el.addEventListener('click', (e) => e.preventDefault());
}

function bindKnob() {
  const knob = $('knob');
  let drag = null;
  const angleAt = (e) => {
    const r = knob.getBoundingClientRect();
    return (Math.atan2(e.clientY - (r.top + r.height / 2), e.clientX - (r.left + r.width / 2)) * 180) / Math.PI;
  };
  knob.addEventListener('pointerdown', (e) => {
    knob.setPointerCapture(e.pointerId);
    drag = { last: angleAt(e), sum: 0, turned: false };
  });
  knob.addEventListener('pointermove', (e) => {
    if (!drag) return;
    const a = angleAt(e);
    let d = a - drag.last;
    if (d > 180) d -= 360;
    if (d < -180) d += 360;
    drag.last = a;
    drag.sum += d;
    const detents = Math.trunc(drag.sum / 15);
    if (detents !== 0) {
      drag.sum -= detents * 15;
      drag.turned = true;
      turn(detents);
    }
  });
  const end = () => {
    if (drag && !drag.turned) tap(0, knob);
    drag = null;
  };
  knob.addEventListener('pointerup', end);
  knob.addEventListener('pointercancel', () => (drag = null));
  knob.addEventListener('keydown', (e) => {
    if ((e.key === 'Enter' || e.key === ' ') && !e.repeat) {
      e.preventDefault();
      tap(0, knob);
    }
  });
  let wheel = 0;
  const onWheel = (e) => {
    e.preventDefault();
    wheel += e.deltaMode === 0 ? e.deltaY : e.deltaY * 30;
    const detents = Math.trunc(wheel / 30);
    if (detents !== 0) {
      wheel -= detents * 30;
      turn(Math.max(-4, Math.min(4, detents)));
    }
  };
  knob.addEventListener('wheel', onWheel, { passive: false });
  $('screen-wrap').addEventListener('wheel', onWheel, { passive: false });
  $('screen-wrap').addEventListener('click', () => {
    // A dark screen: any control wakes it, and that first touch does nothing else.
    if (world?.backlight <= 0) turn(1);
  });
  $('turn-left').addEventListener('click', () => turn(-1));
  $('turn-right').addEventListener('click', () => turn(1));
  bindHold($('key-back'), 1);
  bindHold($('key-home'), 2);

  // Keyboard, while nothing that takes typing has the focus.
  const free = () => {
    const a = document.activeElement;
    return !a || a === document.body || $('device').contains(a);
  };
  const keyOf = (e) =>
    e.key === 'Backspace' || e.key === 'Escape' ? 1 : e.key === 'h' || e.key === 'H' || e.key === 'm' || e.key === 'M' ? 2 : -1;
  window.addEventListener('keydown', (e) => {
    if (!free() || e.metaKey || e.ctrlKey || e.altKey) return;
    if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') turn(-1);
    else if (e.key === 'ArrowRight' || e.key === 'ArrowDown') turn(1);
    else if (e.key === 'Enter' && document.activeElement === document.body) tap(0, knob);
    else if (keyOf(e) >= 0 && !e.repeat) keyDown(keyOf(e), keyOf(e) === 1 ? $('key-back') : $('key-home'));
    else return;
    e.preventDefault();
  });
  window.addEventListener('keyup', (e) => {
    if (keyOf(e) >= 0) keyUp(keyOf(e));
  });
}

// ---- time controls -----------------------------------------------------------------------------------

function setSpeed(value) {
  speed = value;
  owed = 0;
  for (const b of document.querySelectorAll('.speed button')) {
    b.setAttribute('aria-checked', String(Number(b.dataset.speed) === value && !skipping));
  }
  $('skip').classList.toggle('running', !!skipping);
  $('skip').lastElementChild.textContent = skipping ? 'Stop' : 'Next watering';
}

function bindTime() {
  for (const b of document.querySelectorAll('.speed button')) {
    b.addEventListener('click', () => {
      skipping = null;
      setSpeed(Number(b.dataset.speed));
    });
  }
  $('skip').addEventListener('click', () => {
    if (!running) return;
    if (skipping) {
      skipping = null;
      setSpeed(1);
      return;
    }
    if (world.zones.some((z) => z.led)) {
      toast('A zone is watering right now.');
      return;
    }
    skipping = { fromUtc: world.utc };
    setSpeed(speed);
  });
}

// ---- conditions ----------------------------------------------------------------------------------------

const conditionNotes = {
  rain: ['It rains. The rain sensor is wet: scheduled watering is held.', 'The rain has stopped. The sensor dries.'],
  wifi: ['The router is off. The phone cannot reach the controller; the schedule runs on.', 'WiFi is back.'],
  ac: ['The transformer is unplugged: no 24 VAC. The controller stays up on USB here.', '24 VAC is back.'],
  leak: ['A pipe leaks: 3 L/min flow with every valve shut.', 'The pipe is mended.'],
  triac: ['An output device has failed short. It will be found at the next self-test.', 'The output device is replaced.'],
};

function syncConditionButtons() {
  for (const b of document.querySelectorAll('[data-cond]')) {
    b.setAttribute('aria-pressed', String(conditions[b.dataset.cond]));
  }
  [...$('wire-chips').children].forEach((chip, z) => {
    chip.dataset.state = wires[z];
    chip.textContent = wires[z] === 'ok' ? String(z + 1) : `${z + 1} ${wires[z] === 'cut' ? 'cut' : 'short'}`;
    chip.title = `Wire to valve ${z + 1}: ${wires[z] === 'ok' ? 'whole' : wires[z] === 'cut' ? 'cut' : 'shorted'}`;
  });
  $('phone-bar').classList.toggle('offline', conditions.wifi);
}

function setCondition(name, on) {
  if (!running || conditions[name] === on) return;
  conditions[name] = on;
  applyConditions();
  if (name === 'wifi' && on) dropStreams();
  syncConditionButtons();
  log(conditionNotes[name][on ? 0 : 1], 'world');
  tourDone(name === 'rain' ? 'rain' : name === 'wifi' ? 'wifi' : '');
}

function setWire(z, state) {
  wires[z] = state;
  applyConditions();
  syncConditionButtons();
  const name = world?.zones[z].name ?? '';
  log(
    state === 'ok'
      ? `The wire to valve ${z + 1} is mended.`
      : state === 'cut'
        ? `The wire to valve ${z + 1} (${name}) is cut. Run the zone to see what the controller makes of it.`
        : `The wire to valve ${z + 1} (${name}) is shorted. Run the zone: the hardware comparator should trip.`,
    'world',
  );
}

function bindConditions() {
  for (const b of document.querySelectorAll('[data-cond]')) {
    b.addEventListener('click', () => setCondition(b.dataset.cond, !conditions[b.dataset.cond]));
  }
  const chips = $('wire-chips');
  for (let z = 0; z < 8; z++) {
    const chip = document.createElement('button');
    chip.type = 'button';
    chip.addEventListener('click', () => setWire(z, wires[z] === 'ok' ? 'cut' : wires[z] === 'cut' ? 'short' : 'ok'));
    chips.append(chip);
  }
  syncConditionButtons();
}

// ---- the tour ------------------------------------------------------------------------------------------

const tourSteps = [
  {
    id: 'manual',
    title: 'Water a zone at the wall',
    text: 'Press the round button under the display, pick a zone with the knob, press the knob. Before the valve opens the controller tests its own fail-safe: watch the log.',
    action: 'Show me the button',
    run: () => {
      showTab('controller');
      pulse($('key-home'));
    },
  },
  {
    id: 'program',
    title: 'Run a program from the phone',
    text: 'In the web interface tap Run, then start “Lawn morning”. The display, the lights and the garden follow, because all three look at the same controller.',
    action: 'Show me the phone',
    run: () => {
      showTab('phone');
      pulse(document.querySelector('.phone'));
    },
  },
  {
    id: 'rain',
    title: 'Make it rain',
    text: 'A wet rain sensor holds the schedule. Turn the rain on, then press Next watering to see a program skipped.',
    action: 'Make it rain',
    run: () => {
      showTab('garden');
      setCondition('rain', true);
    },
  },
  {
    id: 'fault',
    title: 'Cut a wire',
    text: 'Cut the wire to valve 3 and run that zone. The controller measures no current, shuts the zone off and reports it on the display and the phone.',
    action: 'Cut wire 3',
    run: () => {
      showTab('garden');
      setWire(2, 'cut');
      pulse($('wire-chips').children[2]);
    },
  },
  {
    id: 'wifi',
    title: 'Unplug the WiFi',
    text: 'The phone loses the controller. The controller does not care: the schedule, the knob and the safety checks need no network.',
    action: 'Switch WiFi off',
    run: () => setCondition('wifi', true),
  },
];

function tourDone(id) {
  if (id) document.querySelector(`#tour li[data-step="${id}"]`)?.classList.add('done');
}
function tourEvent(e) {
  if (e.type === 'zone_changed' && e.state === 'open' && performance.now() - lastControlUse < 90_000) tourDone('manual');
  if (e.type === 'program_started' && e.source === 'manual_program') tourDone('program');
  if (e.type === 'fault_raised' && e.zone != null) tourDone('fault');
}
function buildTour() {
  $('tour').replaceChildren(
    ...tourSteps.map((s) => {
      const li = document.createElement('li');
      li.dataset.step = s.id;
      const b = document.createElement('b');
      b.textContent = s.title;
      const p = document.createElement('p');
      p.textContent = s.text;
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = s.action;
      button.addEventListener('click', s.run);
      li.append(b, p, button);
      return li;
    }),
  );
}

// ---- page furniture --------------------------------------------------------------------------------

function pulse(el) {
  if (!el) return;
  el.classList.remove('pulse');
  void el.offsetWidth;
  el.classList.add('pulse');
  el.scrollIntoView({ block: 'nearest', behavior: 'smooth' });
}

let toastTimer;
function toast(text) {
  const el = $('toast');
  el.textContent = text;
  el.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => el.classList.remove('show'), 5200);
}

function showTab(name) {
  document.querySelector('.stage').dataset.tab = name;
  for (const b of document.querySelectorAll('.tabs button')) b.setAttribute('aria-pressed', String(b.dataset.tab === name));
}

function reloadPhone() {
  $('app').src = 'app.html?' + Date.now();
}

function buildStatic() {
  const row = $('led-row');
  for (const label of ['', 'M', '1', '2', '3', '4', '5', '6', '7', '8']) {
    const cell = document.createElement('span');
    if (label === '') cell.className = 'power';
    cell.append(document.createElement('i'), label);
    row.append(cell);
  }
  for (let z = 0; z < 8; z++) $('leds-mini').append(document.createElement('i'));
  for (const id of ['m-ac', 'm-current', 'm-flow', 'm-total']) $(id).querySelector('dd').id = id + '-v';

  for (const b of document.querySelectorAll('.tabs button')) b.addEventListener('click', () => showTab(b.dataset.tab));
  $('phone-reload').addEventListener('click', reloadPhone);

  const menu = $('menu');
  for (const item of menu.querySelectorAll('[data-menu]')) {
    item.addEventListener('click', () => {
      menu.open = false;
      const kind = item.dataset.menu;
      if (kind === 'restart') restart('restart', 'Power off, power on…');
      else if (kind === 'garden') restart('garden', 'Starting over with the demo garden.');
      else restart('fresh', 'Unboxing a new controller.');
    });
  }
  document.addEventListener('click', (e) => {
    if (menu.open && !menu.contains(e.target)) menu.open = false;
  });

  // The controller is drawn 500 px wide and scaled down to fit.
  const fit = $('device-fit');
  new ResizeObserver(() => fit.style.setProperty('--s', String(Math.min(1, fit.clientWidth / 500)))).observe(fit);
  const bar = document.querySelector('.bar');
  new ResizeObserver(() => document.documentElement.style.setProperty('--bar-h', bar.offsetHeight + 'px')).observe(bar);
}

// ---- start -------------------------------------------------------------------------------------------

const garden = buildGarden($('garden'));

async function main() {
  buildStatic();
  buildTour();
  bindConditions();
  bindTime();
  try {
    [m, zone] = await Promise.all([createModule(), posixZone()]);
    const saved = loadSaved();
    start(saved ? { storage: saved } : {});
  } catch (e) {
    console.error(e);
    $('splash-text').textContent =
      'The simulator could not start in this browser. It needs WebAssembly; a current Firefox, Chrome, Edge or Safari will do.';
    return;
  }
  bindKnob();
  setInterval(pump, 20);
  requestAnimationFrame(draw);
  setInterval(() => {
    if (running && dev.storageSerial() !== savedSerial) save();
  }, 2000);
  window.addEventListener('pagehide', save);
  reloadPhone();
  $('splash').classList.add('gone');
  log('The controller powers up and tests its fail-safe before anything may run.', 'safety');
}

main();
