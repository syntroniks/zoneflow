// Mock ZoneFlow controller: REST API, event stream and static files.
//
//   node mock/server.ts [--port 8787] [--scenario demo|fresh] [--static dist]
//                       [--utc 1780300000] [--frozen] [--speed 10]
//
// The demo scenario has a password ("zoneflow"), six named zones and three
// programs. The fresh scenario has no password: use the setup code
// K7Q2M9XW4R. Test hooks live under /__mock/ (see the bottom of the file).
import { createReadStream, existsSync, statSync } from 'node:fs';
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http';
import { extname, join, normalize, resolve } from 'node:path';
import { randomBytes } from 'node:crypto';
import { parseArgs } from 'node:util';
import { parseClockTime, parseIsoDate } from '../src/lib/civil.ts';
import { TimeZone } from '../src/lib/tz.ts';
import { Device, SETUP_CODE, type DeviceOptions, type Json } from './device.ts';

const { values: args } = parseArgs({
  options: {
    port: { type: 'string', default: '8787' },
    host: { type: 'string', default: '127.0.0.1' },
    scenario: { type: 'string', default: 'demo' },
    static: { type: 'string' },
    utc: { type: 'string' },
    frozen: { type: 'boolean', default: false },
    speed: { type: 'string', default: '1' },
    quiet: { type: 'boolean', default: false },
  },
});

// The device's Content-Security-Policy for HTML (zf_web web.cpp, kUiCsp), so
// a page that would break on the device breaks here too.
const CSP =
  "default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline'; connect-src 'self'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'";
const MIME: Record<string, string> = {
  '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json',
  '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon', '.webmanifest': 'application/manifest+json', '.txt': 'text/plain', '.woff2': 'font/woff2',
};

const baseOpts = (): DeviceOptions => ({
  scenario: args.scenario === 'fresh' ? 'fresh' : 'demo',
  utc: args.utc ? Number(args.utc) : undefined,
  frozen: args.frozen,
  speed: Number(args.speed),
});
const dev = new Device(baseOpts());
const streams = new Set<ServerResponse>();
let streamSeq = 0;

// ---- helpers -----------------------------------------------------------------

class HttpError extends Error {
  status: number;
  code: string;
  path?: string;
  headers: Record<string, string>;
  constructor(status: number, code: string, message: string, path?: string, headers: Record<string, string> = {}) {
    super(message);
    this.status = status;
    this.code = code;
    this.path = path;
    this.headers = headers;
  }
}
const bad = (path: string, message: string) => new HttpError(400, 'invalid_config', message, path);

function send(res: ServerResponse, status: number, body: unknown, headers: Record<string, string> = {}): void {
  const text = JSON.stringify(body);
  res.writeHead(status, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff', ...headers });
  res.end(text);
}

function readBody(req: IncomingMessage, limit = 16384): Promise<Buffer> {
  return new Promise((ok, fail) => {
    const chunks: Buffer[] = [];
    let n = 0;
    req.on('data', (c: Buffer) => {
      n += c.length;
      if (n > limit) fail(new HttpError(413, 'too_large', 'body too large'));
      else chunks.push(c);
    });
    req.on('end', () => ok(Buffer.concat(chunks)));
    req.on('error', fail);
  });
}

async function json(req: IncomingMessage): Promise<Json> {
  const ct = String(req.headers['content-type'] ?? '');
  if (!ct.startsWith('application/json')) throw new HttpError(415, 'unsupported_media_type', 'expected application/json');
  const buf = await readBody(req);
  if (!buf.length) return {};
  try {
    const v = JSON.parse(buf.toString('utf8'));
    if (typeof v !== 'object' || v === null || Array.isArray(v)) throw new Error();
    return v as Json;
  } catch {
    throw new HttpError(400, 'invalid_json', 'body is not a JSON object');
  }
}

function cookie(req: IncomingMessage, name: string): string | undefined {
  for (const part of String(req.headers.cookie ?? '').split(';')) {
    const [k, ...v] = part.trim().split('=');
    if (k === name) return v.join('=');
  }
  return undefined;
}

interface Auth {
  token?: string;
  viaCookie: boolean;
}
function session(req: IncomingMessage): Auth {
  const bearer = /^Bearer (.+)$/.exec(String(req.headers.authorization ?? ''))?.[1];
  if (bearer && dev.sessions.has(bearer)) return { token: bearer, viaCookie: false };
  const c = cookie(req, 'zf_session');
  if (c && dev.sessions.has(c)) return { token: c, viaCookie: true };
  return { viaCookie: false };
}

function newSession(res: ServerResponse, status: number): void {
  const token = randomBytes(16).toString('hex');
  const csrf = randomBytes(16).toString('hex');
  dev.sessions.set(token, { csrf, created: Date.now() });
  send(res, status, { token, csrf, idle_timeout_s: 86400 }, { 'Set-Cookie': `zf_session=${token}; Path=/; HttpOnly; SameSite=Strict; Max-Age=2592000` });
}

const failures = new Map<string, number>();
function checkRate(req: IncomingMessage): void {
  const ip = req.socket.remoteAddress ?? '';
  const f = failures.get(ip) ?? 0;
  if (f >= 5) throw new HttpError(429, 'rate_limited', 'too many failed attempts', undefined, { 'Retry-After': String(2 ** Math.min(f - 5, 10)) });
}
function fail(req: IncomingMessage): void {
  const ip = req.socket.remoteAddress ?? '';
  failures.set(ip, (failures.get(ip) ?? 0) + 1);
}

// ---- validation (subset of zf_core config.cpp) ---------------------------------

const WEEKDAYS = ['sun', 'mon', 'tue', 'wed', 'thu', 'fri', 'sat'];
function validateProgram(p: Json): void {
  if (p['name'] !== undefined && (typeof p['name'] !== 'string' || p['name'].length > 31)) throw bad('name', 'at most 31 characters');
  const starts = (p['starts'] ?? []) as unknown[];
  if (!Array.isArray(starts) || starts.length > 4) throw bad('starts', 'at most 4 start times');
  starts.forEach((s, i) => {
    if (typeof s !== 'string' || parseClockTime(s) === null) throw bad(`starts[${i}]`, 'must be a time HH:MM');
  });
  const d = (p['days'] ?? { type: 'weekdays' }) as Json;
  if (!['weekdays', 'interval', 'odd', 'even'].includes(String(d['type']))) throw bad('days.type', 'must be weekdays, interval, odd or even');
  if (d['type'] === 'weekdays' && d['weekdays'] !== undefined) {
    (d['weekdays'] as unknown[]).forEach((w, i) => {
      if (!WEEKDAYS.includes(String(w))) throw bad(`days.weekdays[${i}]`, 'must be one of sun, mon, tue, wed, thu, fri, sat');
    });
  }
  if (d['type'] === 'interval') {
    const every = Number(d['every'] ?? 1);
    if (!Number.isInteger(every) || every < 1 || every > 365) throw bad('days.every', 'must be 1 to 365');
    if (d['anchor'] !== undefined && parseIsoDate(String(d['anchor'])) === null) throw bad('days.anchor', 'must be a date YYYY-MM-DD');
  }
  const durs = (p['durations_s'] ?? []) as unknown[];
  if (!Array.isArray(durs) || durs.length > 8) throw bad('durations_s', 'at most 8 entries');
  durs.forEach((v, i) => {
    if (!Number.isInteger(v) || (v as number) < 0 || (v as number) > 14400) throw bad(`durations_s[${i}]`, 'must be 0 to 14400');
  });
}

function validateConfig(c: Json): void {
  if (c['schema'] === undefined) throw bad('schema', 'required');
  if (Number(c['schema']) > 2) throw new HttpError(400, 'unsupported_schema', `schema ${String(c['schema'])} is newer than this firmware supports (2)`, 'schema');
  const zc = Number(c['zone_count'] ?? 8);
  if (!Number.isInteger(zc) || zc < 1 || zc > 8) throw bad('zone_count', 'must be 1 to 8');
  if (c['timezone'] !== undefined && !TimeZone.parse(String(c['timezone']))) throw bad('timezone', 'not a valid POSIX TZ string');
  if (typeof c['device_name'] === 'string' && c['device_name'].length > 31) throw bad('device_name', 'at most 31 characters');
  ((c['zones'] ?? []) as Json[]).forEach((z, i) => {
    if (typeof z['name'] === 'string' && z['name'].length > 23) throw bad(`zones[${i}].name`, 'at most 23 characters');
    const m = z['max_run_s'];
    if (m !== undefined && (!Number.isInteger(m) || (m as number) < 1 || (m as number) > 14400)) throw bad(`zones[${i}].max_run_s`, 'must be 1 to 14400');
  });
  const s = (c['schedule'] ?? {}) as Json;
  const pct = s['seasonal_percent'];
  if (pct !== undefined && (!Number.isInteger(pct) || (pct as number) < 0 || (pct as number) > 250)) throw bad('schedule.seasonal_percent', 'must be 0 to 250');
  ((s['programs'] ?? []) as Json[]).forEach((p, i) => {
    try {
      validateProgram(p);
    } catch (e) {
      if (e instanceof HttpError) throw bad(`schedule.programs[${i}].${e.path}`, e.message);
      throw e;
    }
  });
}

/** Fills defaults like the firmware's parser (fields left out take defaults). */
function withDefaults(c: Json): Json {
  const base = new Device({ scenario: 'fresh' }).config;
  const out: Json = { ...base, ...c };
  out['schema'] = 2; // a schema 1 document is migrated, as the firmware does
  for (const k of ['master', 'driver', 'diagnostics', 'schedule', 'sensors', 'mqtt']) out[k] = { ...(base[k] as Json), ...((c[k] as Json) ?? {}) };
  const zones = (c['zones'] ?? []) as Json[];
  out['zones'] = (base['zones'] as Json[]).map((z, i) => ({ ...z, ...(zones[i] ?? {}) }));
  return out;
}

function programDefaults(p: Json, id: number): Json {
  return {
    id,
    name: p['name'] ?? `Program ${id}`,
    enabled: p['enabled'] ?? true,
    starts: p['starts'] ?? [],
    days: p['days'] ?? { type: 'weekdays', weekdays: WEEKDAYS },
    durations_s: Array.from({ length: 8 }, (_, i) => ((p['durations_s'] as number[] | undefined)?.[i] ?? 0)),
    seasonal: p['seasonal'] ?? true,
    obey_sensor: p['obey_sensor'] ?? true,
  };
}

function applyConfig(next: Json): void {
  validateConfig(next);
  const oldTz = dev.config['timezone'];
  dev.config = withDefaults(next);
  // drop queued runs of removed zones/programs, like Scheduler::configure
  dev.emit('config_applied');
  if (oldTz !== dev.config['timezone']) dev.changed();
  dev.changed();
}

// ---- routes ---------------------------------------------------------------------

type Handler = (req: IncomingMessage, res: ServerResponse, p: Record<string, string>, url: URL) => Promise<void> | void;
interface Route {
  method: string;
  pattern: RegExp;
  keys: string[];
  access: 'public' | 'read' | 'admin';
  fn: Handler;
}
const routes: Route[] = [];
function route(method: string, path: string, access: Route['access'], fn: Handler): void {
  const keys: string[] = [];
  const pattern = new RegExp('^' + path.replace(/\{(\w+)\}/g, (_, k: string) => (keys.push(k), '([^/]+)')) + '$');
  routes.push({ method, pattern, keys, access, fn });
}

const accepted = (res: ServerResponse, command: string, extra: Json = {}) => send(res, 202, { accepted: true, command, ...extra });
const zoneParam = (p: Record<string, string>) => {
  const z = Number(p['zone']);
  if (!Number.isInteger(z) || z < 1 || z > dev.zoneCount()) throw new HttpError(404, 'not_found', 'no such zone');
  return z;
};
const programParam = (p: Record<string, string>) => {
  const id = Number(p['id']);
  const prog = dev.programs().find((x) => x['id'] === id);
  if (!prog) throw new HttpError(404, 'not_found', 'no such program');
  return { id, prog };
};
const maintenanceGate = () => {
  if (dev.maintenance) throw new HttpError(409, 'busy', 'an update is running');
};

route('GET', '/auth', 'public', (req, res) => {
  const s = session(req);
  const body: Json = { password_set: dev.passwordHash !== null, authenticated: !!s.token, protect_reads: dev.security.protect_reads, setup_complete: dev.setupComplete };
  if (s.token && s.viaCookie) body['csrf'] = dev.sessions.get(s.token)!.csrf;
  send(res, 200, body);
});
route('POST', '/auth/login', 'public', async (req, res) => {
  const b = await json(req);
  checkRate(req);
  if (dev.passwordHash === null) throw new HttpError(401, 'password_not_set', 'password_not_set');
  if (b['password'] !== dev.passwordHash) {
    fail(req);
    throw new HttpError(401, 'bad_credentials', 'bad_credentials');
  }
  failures.delete(req.socket.remoteAddress ?? '');
  newSession(res, 200);
});
route('POST', '/auth/setup', 'public', async (req, res) => {
  const b = await json(req);
  checkRate(req);
  if (dev.passwordHash !== null) throw new HttpError(409, 'password_already_set', 'password_already_set');
  if (b['setup_code'] !== SETUP_CODE) {
    fail(req);
    throw new HttpError(401, 'bad_credentials', 'bad_credentials');
  }
  const pw = String(b['password'] ?? '');
  if (pw.length < 8 || pw.length > 64) throw new HttpError(400, 'weak_password', 'password must be 8 to 64 characters without control characters');
  dev.passwordHash = pw;
  newSession(res, 201);
});
route('POST', '/auth/logout', 'admin', (req, res) => {
  const s = session(req);
  if (s.token) dev.sessions.delete(s.token);
  send(res, 200, { logged_out: true }, { 'Set-Cookie': 'zf_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0' });
});
route('POST', '/auth/password', 'admin', async (req, res) => {
  const b = await json(req);
  checkRate(req);
  if (b['current_password'] !== dev.passwordHash) {
    fail(req);
    throw new HttpError(401, 'bad_credentials', 'bad_credentials');
  }
  const pw = String(b['new_password'] ?? '');
  if (pw.length < 8 || pw.length > 64) throw new HttpError(400, 'weak_password', 'password must be 8 to 64 characters without control characters');
  dev.passwordHash = pw;
  const keep = session(req).token;
  for (const k of [...dev.sessions.keys()]) if (k !== keep) dev.sessions.delete(k);
  if (keep) dev.sessions.delete(keep);
  newSession(res, 200);
});
route('GET', '/security', 'read', (_req, res) =>
  send(res, 200, { ...dev.security, password_set: dev.passwordHash !== null, sessions: dev.sessions.size, tls_fingerprint_sha256: '3F:9A:1C:77:0B:E2:54:D8:6A:11:90:C3:4E:2B:F7:85:0D:61:AA:39:C2:5E:73:B4:18:EF:06:9D:52:C0:8B:47' }),
);
route('PUT', '/security', 'admin', async (req, res) => {
  const b = await json(req);
  for (const k of Object.keys(b)) if (!['protect_reads', 'https', 'check_host'].includes(k)) throw new HttpError(400, 'invalid_body', `unknown field ${k}`, k);
  if (typeof b['https'] === 'boolean' && b['https'] !== dev.security.https) dev.security.restart_required = true;
  Object.assign(dev.security, b);
  send(res, 200, { ...dev.security, password_set: true, sessions: dev.sessions.size, tls_fingerprint_sha256: '' });
});
route('GET', '/info', 'read', (_q, res) => send(res, 200, dev.infoJson()));
route('GET', '/status', 'read', (_q, res) => send(res, 200, dev.statusJson()));
route('GET', '/zones', 'read', (_q, res) => send(res, 200, Array.from({ length: dev.zoneCount() }, (_, i) => dev.zoneJson(i))));
route('GET', '/zones/{zone}', 'read', (_q, res, p) => send(res, 200, dev.zoneJson(zoneParam(p) - 1)));
route('PATCH', '/zones/{zone}', 'admin', async (req, res, p) => {
  const z = zoneParam(p);
  const b = await json(req);
  for (const k of Object.keys(b)) if (!['name', 'enabled', 'max_run_s', 'default_run_s', 'current_min_ma', 'current_max_ma'].includes(k)) throw new HttpError(400, 'invalid_body', `unknown field ${k}`, k);
  const dr = b['default_run_s'];
  if (dr !== undefined && (!Number.isInteger(dr) || (dr as number) < 1 || (dr as number) > 14400)) throw bad('default_run_s', 'must be 1 to 14400');
  if (typeof b['name'] === 'string' && b['name'].length > 23) throw bad('name', 'at most 23 characters');
  const m = b['max_run_s'];
  if (m !== undefined && (!Number.isInteger(m) || (m as number) < 1 || (m as number) > 14400)) throw bad('max_run_s', 'must be 1 to 14400');
  if (b['current_min_ma'] !== undefined && b['current_max_ma'] !== undefined && Number(b['current_min_ma']) >= Number(b['current_max_ma'])) throw bad('current_max_ma', 'must be above current_min_ma');
  const next = structuredClone(dev.config);
  Object.assign((next['zones'] as Json[])[z - 1]!, b);
  applyConfig(next);
  send(res, 200, dev.zoneJson(z - 1));
});

function runVerdictOverride(res: ServerResponse, command: string, fields: Json): boolean {
  const v = dev.nextRunVerdict;
  if (!v) return false;
  dev.nextRunVerdict = null;
  const code = v.replace(/^reject:/, '');
  accepted(res, command, { confirmed: false, ...fields });
  setTimeout(() => dev.emit('command_rejected', { command, zone: fields['zone'] ?? null, program_id: fields['program_id'] ?? null, error: code }), 600);
  return true;
}

route('POST', '/zones/{zone}/run', 'admin', async (req, res, p) => {
  const z = zoneParam(p);
  const b = await json(req);
  const d = b['duration_s'];
  if (!Number.isInteger(d) || (d as number) < 1 || (d as number) > 14400) throw new HttpError(400, 'invalid_body', 'must be 1 to 14400', 'duration_s');
  const blocked = dev.checkZoneRun(z);
  if (blocked) throw new HttpError(409, blocked.code, blocked.code.replace(/_/g, ' '), undefined, blocked.retryAfter ? { 'Retry-After': String(blocked.retryAfter) } : {});
  if (runVerdictOverride(res, 'run_zone', { zone: z, duration_s: d })) return;
  if (dev.queue.length >= 64) throw new HttpError(409, 'queue_full', 'queue full');
  const clamped = dev.runZone(z, d as number);
  accepted(res, 'run_zone', { confirmed: true, zone: z, duration_s: clamped });
});
route('POST', '/zones/{zone}/stop', 'admin', (_q, res, p) => {
  const z = zoneParam(p);
  if (dev.queue[0]?.zone !== z - 1 || dev.queueState !== 'running') {
    throw new HttpError(409, dev.queue.some((e) => e.zone === z - 1) ? 'not_head' : 'not_running', 'zone is not the running entry');
  }
  dev.skip();
  accepted(res, 'skip');
});
route('GET', '/programs', 'read', (_q, res) => send(res, 200, dev.programs()));
route('POST', '/programs', 'admin', async (req, res) => {
  const b = await json(req);
  validateProgram(b);
  const list = dev.programs();
  if (list.length >= 16) throw new HttpError(409, 'too_many_programs', 'at most 16 programs');
  let id = Number(b['id'] ?? 0);
  if (id && list.some((x) => x['id'] === id)) throw new HttpError(409, 'exists', 'id in use', 'id');
  if (!id) id = Math.max(0, ...list.map((x) => Number(x['id']))) + 1;
  const next = structuredClone(dev.config);
  const prog = programDefaults(b, id);
  ((next['schedule'] as Json)['programs'] as Json[]).push(prog);
  applyConfig(next);
  send(res, 201, prog);
});
route('GET', '/programs/{id}', 'read', (_q, res, p) => send(res, 200, programParam(p).prog));
route('PUT', '/programs/{id}', 'admin', async (req, res, p) => {
  const { id } = programParam(p);
  const b = await json(req);
  if (b['id'] !== undefined && b['id'] !== id) throw new HttpError(400, 'invalid_body', 'must match the path', 'id');
  validateProgram(b);
  const next = structuredClone(dev.config);
  const list = (next['schedule'] as Json)['programs'] as Json[];
  const prog = programDefaults(b, id);
  list[list.findIndex((x) => x['id'] === id)] = prog;
  applyConfig(next);
  send(res, 200, prog);
});
route('DELETE', '/programs/{id}', 'admin', (_q, res, p) => {
  const { id } = programParam(p);
  const next = structuredClone(dev.config);
  const s = next['schedule'] as Json;
  s['programs'] = (s['programs'] as Json[]).filter((x) => x['id'] !== id);
  applyConfig(next);
  send(res, 200, { deleted: true });
});
route('POST', '/programs/{id}/run', 'admin', (_q, res, p) => {
  const { id } = programParam(p);
  if (runVerdictOverride(res, 'run_program', { program_id: id })) return;
  const err = dev.runProgram(id);
  if (err) throw new HttpError(err === 'unknown_program' ? 404 : 409, err, err.replace(/_/g, ' '));
  accepted(res, 'run_program', { confirmed: true });
});
route('GET', '/queue', 'read', (_q, res) => send(res, 200, dev.queueJson()));
route('POST', '/queue/pause', 'admin', (_q, res) => (dev.pause(), accepted(res, 'pause')));
route('POST', '/queue/resume', 'admin', (_q, res) => (dev.resume(), accepted(res, 'resume')));
route('POST', '/queue/skip', 'admin', (_q, res) => (dev.skip(), accepted(res, 'skip')));
route('POST', '/queue/stop', 'admin', (_q, res) => (dev.stopAll(), accepted(res, 'stop_all')));
route('GET', '/rain-delay', 'read', (_q, res) => send(res, 200, dev.rainJson()));
route('PUT', '/rain-delay', 'admin', async (req, res) => {
  const b = await json(req);
  if (dev.timeStatus === 'unset') throw new HttpError(409, 'time_not_set', 'the clock is not set');
  let until: number;
  if (b['hours'] !== undefined && b['until_utc'] === undefined) {
    const h = b['hours'];
    if (!Number.isInteger(h) || (h as number) < 1 || (h as number) > 720) throw new HttpError(400, 'invalid_body', 'must be 1 to 720', 'hours');
    until = dev.utc() + (h as number) * 3600;
  } else if (b['until_utc'] !== undefined && b['hours'] === undefined) {
    until = Number(b['until_utc']);
    if (!(until > dev.utc() && until <= dev.utc() + 30 * 86400)) throw new HttpError(400, 'invalid_body', 'must be within the next 30 days', 'until_utc');
  } else throw new HttpError(400, 'invalid_body', 'exactly one of hours and until_utc');
  dev.setRainDelay(until);
  accepted(res, 'set_rain_delay', { until_utc: until });
});
route('DELETE', '/rain-delay', 'admin', (_q, res) => (dev.setRainDelay(0), accepted(res, 'set_rain_delay')));
route('GET', '/sensors', 'read', (_q, res) => send(res, 200, dev.sensorsJson()));
route('GET', '/faults', 'read', (_q, res) => send(res, 200, dev.faultsJson()));
route('POST', '/faults/clear', 'admin', (_q, res) => (dev.clearFaults(), accepted(res, 'clear_faults')));
route('GET', '/config', 'read', (_q, res) => send(res, 200, dev.config));
route('PUT', '/config', 'admin', async (req, res) => {
  maintenanceGate();
  applyConfig(await json(req));
  send(res, 200, { applied: true });
});
route('GET', '/config/backup', 'read', (_q, res) =>
  send(res, 200, { format: 'zoneflow-backup', format_version: 1, created_utc: dev.utc(), firmware_version: '0.3.0-mock', hostname: 'zoneflow-ab12', config: dev.config }),
);
route('POST', '/config/restore', 'admin', async (req, res) => {
  const b = await json(req);
  if (b['format'] !== 'zoneflow-backup' || typeof b['config'] !== 'object') throw new HttpError(400, 'invalid_backup', 'not a ZoneFlow backup', 'format');
  applyConfig(b['config'] as Json);
  send(res, 200, { restored: true });
});
route('GET', '/time', 'read', (_q, res) => send(res, 200, dev.timeJson()));
route('PUT', '/time', 'admin', async (req, res) => {
  const b = await json(req);
  if (b['timezone'] !== undefined) {
    if (typeof b['timezone'] !== 'string' || !TimeZone.parse(b['timezone'])) throw new HttpError(400, 'invalid_config', 'not a valid POSIX TZ string', 'timezone');
    dev.config['timezone'] = b['timezone'];
    dev.emit('config_applied');
  }
  if (b['utc'] !== undefined) {
    const u = Number(b['utc']);
    if (!(u >= 1735689600 && u <= 4102444800)) throw new HttpError(400, 'invalid_body', 'out of range', 'utc');
    dev.setClock(u);
    dev.timeStatus = 'rtc';
    dev.faults.delete('time_invalid');
    dev.emit('time_status_changed', { status: 'rtc' });
  }
  dev.changed();
  send(res, 200, dev.timeJson());
});
route('GET', '/network', 'read', (_q, res) => send(res, 200, dev.network));
route('POST', '/network/reprovision', 'admin', async (req, res) => {
  const b = await json(req);
  if (b['transport'] !== undefined && !['ble', 'softap'].includes(String(b['transport']))) throw new HttpError(400, 'invalid_body', 'ble or softap', 'transport');
  accepted(res, 'reprovision');
  later(() => restart(), 500);
});
route('GET', '/provision', 'public', (_q, res) => send(res, 200, { active: false, transport: 'none', state: 'idle', provisioned: true, password_set: dev.passwordHash !== null, hostname: 'zoneflow-ab12', ip: '192.168.1.47' }));
route('POST', '/system/reboot', 'admin', (_q, res) => {
  accepted(res, 'reboot');
  later(() => restart(), 500);
});
route('POST', '/system/factory-reset', 'admin', async (req, res) => {
  const b = await json(req);
  if (b['confirm'] !== 'factory-reset') throw new HttpError(400, 'confirm_required', 'send {"confirm":"factory-reset"}', 'confirm');
  accepted(res, 'factory_reset');
  later(() => {
    dev.reset({ ...baseOpts(), scenario: 'fresh' });
    restart();
  }, 500);
});
route('GET', '/firmware', 'read', (_q, res) => send(res, 200, dev.firmwareJson()));
route('PUT', '/setup', 'admin', async (req, res) => {
  const b = await json(req);
  if (typeof b['complete'] !== 'boolean' || Object.keys(b).length !== 1) throw new HttpError(400, 'invalid_body', 'send {"complete": true|false}', 'complete');
  dev.setupComplete = b['complete'];
  send(res, 200, { setup_complete: dev.setupComplete });
});
route('GET', '/history', 'read', (_q, res, _p, url) => {
  const num = (k: string, min: number, max: number): number | undefined => {
    const v = url.searchParams.get(k);
    if (v === null) return undefined;
    if (!/^\d+$/.test(v) || Number(v) < min || Number(v) > max) throw new HttpError(400, 'invalid_body', `bad ${k}`, k);
    return Number(v);
  };
  send(res, 200, dev.historyPage({
    limit: num('limit', 1, 100) ?? 50, before: num('before', 1, 2 ** 52), after: num('after', 0, 2 ** 52),
    since: num('since', 0, 4102444800), zone: num('zone', 1, 8),
  }));
});
const MQTT_FIELDS = ['enabled', 'host', 'port', 'username', 'tls', 'ca', 'client_id', 'base_topic', 'discovery', 'discovery_prefix', 'ha_zone_entity'];
route('GET', '/mqtt', 'read', (_q, res) => send(res, 200, dev.mqttJson()));
route('PUT', '/mqtt', 'admin', async (req, res) => {
  const b = await json(req);
  const next = { ...(dev.config['mqtt'] as Json) };
  for (const [k, v] of Object.entries(b)) {
    if (k === 'password' || k === 'ca_cert') continue;
    if (!MQTT_FIELDS.includes(k)) throw new HttpError(400, 'invalid_body', 'unknown field', k);
    next[k] = v;
  }
  const port = Number(next['port']);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw bad('port', 'must be 1 to 65535');
  if (next['enabled'] === true && !String(next['host'] ?? '')) throw bad('host', 'required when enabled');
  if (/[#+]|^\/|\/$/.test(String(next['base_topic'] ?? ''))) throw bad('base_topic', 'no wildcards, no leading or trailing /');
  if (typeof b['ca_cert'] === 'string' && b['ca_cert'] && !String(b['ca_cert']).includes('-----BEGIN CERTIFICATE-----')) throw bad('ca_cert', 'not a PEM certificate');
  if ('password' in b) dev.mqttSecrets.password = typeof b['password'] === 'string' ? b['password'] : '';
  if ('ca_cert' in b) dev.mqttSecrets.ca = typeof b['ca_cert'] === 'string' ? b['ca_cert'] : '';
  dev.config['mqtt'] = next;
  dev.mqttChangedMono = dev.monoMs();
  dev.emit('config_applied');
  send(res, 200, dev.mqttJson());
});

async function receiveImage(req: IncomingMessage, res: ServerResponse, kind: 'fw' | 'ui', force: boolean): Promise<void> {
  if (String(req.headers['content-type']) !== 'application/octet-stream') throw new HttpError(415, 'unsupported_media_type', 'expected application/octet-stream');
  if (dev.ota['phase'] !== 'idle' && dev.ota['phase'] !== 'failed') throw new HttpError(409, 'busy', 'an update is running');
  const watering = dev.queueState !== 'idle' || dev.zones.some((z) => z.state !== 'off');
  if (watering && !force) throw new HttpError(409, 'zones_active', 'watering is running; add force');
  const total = Number(req.headers['content-length'] ?? 0);
  dev.stopAll();
  dev.maintenance = true;
  dev.emit('maintenance_changed', { on: true });
  dev.ota = { phase: 'preparing', error: 'none', source: 'upload', forced: watering, received_bytes: 0, total_bytes: total, new_version: kind === 'fw' ? '0.3.1' : '' };
  dev.sendOta();
  let got = 0;
  let first: Buffer | null = null;
  let lastSent = 0;
  for await (const chunk of req) {
    const c = chunk as Buffer;
    if (!first) first = c;
    got += c.length;
    dev.ota = { ...dev.ota, phase: 'receiving', received_bytes: got };
    if (Date.now() - lastSent > 250) {
      dev.sendOta();
      lastSent = Date.now();
    }
    await new Promise((r) => setTimeout(r, 15)); // flash writes are slow
  }
  const ok = kind === 'fw' ? first?.[0] === 0xe9 : first?.subarray(8, 16).toString('latin1') === 'littlefs';
  dev.maintenance = false;
  if (!ok) {
    dev.ota = { ...dev.ota, phase: 'failed', error: 'bad_image' };
    dev.sendOta();
    dev.emit('maintenance_changed', { on: false });
    throw new HttpError(400, 'bad_image', kind === 'fw' ? 'not an ESP32-S3 zoneflow-fw image' : 'not a LittleFS image');
  }
  dev.ota = { ...dev.ota, phase: 'verifying', received_bytes: got };
  dev.sendOta();
  accepted(res, kind === 'fw' ? 'firmware_upload' : 'assets_upload');
  later(() => {
    if (kind === 'fw') {
      dev.ota = { ...dev.ota, phase: 'rebooting' };
      dev.sendOta();
      later(() => restart(), 1500);
    } else {
      dev.ota = { phase: 'idle', error: 'none', source: 'none', forced: false, received_bytes: 0, total_bytes: 0, new_version: '' };
      dev.sendOta();
      dev.emit('maintenance_changed', { on: false });
    }
  }, 800);
}
route('POST', '/firmware/upload', 'admin', (req, res, _p, url) => receiveImage(req, res, 'fw', url.searchParams.get('force') === 'true'));
route('POST', '/firmware/assets', 'admin', (req, res) => receiveImage(req, res, 'ui', true));
route('POST', '/firmware/pull', 'admin', async (req, res) => {
  const b = await json(req);
  const url = String(b['url'] ?? '');
  if (!url.startsWith('https://')) throw new HttpError(400, 'invalid_body', 'https:// only', 'url');
  const watering = dev.queueState !== 'idle';
  if (watering && b['force'] !== true) throw new HttpError(409, 'zones_active', 'watering is running; add force');
  dev.stopAll();
  accepted(res, 'firmware_pull');
  dev.ota = { phase: 'receiving', error: 'none', source: 'pull', forced: watering, received_bytes: 0, total_bytes: 1_600_000, new_version: '0.3.1' };
  const step = setInterval(() => {
    if (dev.ota['source'] !== 'pull') return clearInterval(step);
    const r = Number(dev.ota['received_bytes']) + 200_000;
    if (r >= 1_600_000) {
      clearInterval(step);
      dev.ota = { ...dev.ota, phase: 'failed', error: 'download_failed', received_bytes: 1_600_000 };
    } else dev.ota = { ...dev.ota, received_bytes: r };
    dev.sendOta();
  }, 400);
});

/** Timers of simulated restarts and uploads; a mock reset cancels them. */
const timers = new Set<ReturnType<typeof setTimeout>>();
function later(fn: () => void, ms: number): void {
  const t = setTimeout(() => {
    timers.delete(t);
    fn();
  }, ms);
  timers.add(t);
}

/** A restart: sessions end, streams drop, the device is back after 2 s. */
let down = false;
function restart(): void {
  dev.sessions.clear();
  dev.stopAll();
  down = true;
  for (const s of streams) s.destroy();
  streams.clear();
  later(() => {
    down = false;
    dev.maintenance = false;
    dev.security.restart_required = false;
    dev.ota = { phase: 'idle', error: 'none', source: 'none', forced: false, received_bytes: 0, total_bytes: 0, new_version: '' };
    dev.bootMono = dev.monoMs();
  }, 2000);
}

// ---- event stream ------------------------------------------------------------------

function openStream(req: IncomingMessage, res: ServerResponse): void {
  if (streams.size >= 3) throw new HttpError(503, 'busy', 'too many streams', undefined, { 'Retry-After': '5' });
  res.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache', Connection: 'keep-alive', 'X-Accel-Buffering': 'no' });
  streams.add(res);
  let lastSeq = -1;
  let lastState = 0;
  let lastAny = Date.now();
  const write = (event: string, data: unknown) => {
    if (dev.streamMuted) return;
    res.write(`id: ${++streamSeq}\nevent: ${event}\ndata: ${JSON.stringify(data)}\n\n`);
    lastAny = Date.now();
  };
  const sendState = () => {
    lastSeq = dev.sequence;
    lastState = Date.now();
    write('state', dev.statusJson());
  };
  sendState();
  const off = dev.subscribe((event, data) => write(event, data));
  const timer = setInterval(() => {
    const now = Date.now();
    if ((dev.sequence !== lastSeq && now - lastState >= 500) || now - lastState >= 30000) sendState();
    else if (now - lastAny >= 15000 && !dev.streamMuted) {
      res.write(': keep-alive\n\n');
      lastAny = now;
    }
  }, 250);
  const done = () => {
    clearInterval(timer);
    off();
    streams.delete(res);
  };
  req.on('close', done);
  res.on('close', done);
}

// ---- dispatch -------------------------------------------------------------------------

async function api(req: IncomingMessage, res: ServerResponse, url: URL): Promise<void> {
  const path = url.pathname.slice('/api/v1'.length) || '/';
  const method = req.method ?? 'GET';
  if (path === '/events' && method === 'GET') {
    if (dev.security.protect_reads && !session(req).token) throw new HttpError(401, 'unauthorized', 'session required');
    return openStream(req, res);
  }
  const candidates = routes.filter((r) => r.pattern.test(path));
  if (!candidates.length) throw new HttpError(404, 'not_found', 'no such route');
  const r = candidates.find((x) => x.method === method);
  if (!r) throw new HttpError(405, 'method_not_allowed', 'method not allowed', undefined, { Allow: candidates.map((x) => x.method).join(', ') });
  const write = method !== 'GET';
  if (write) {
    const origin = req.headers.origin;
    if (origin && new URL(origin).host !== req.headers.host) throw new HttpError(403, 'bad_origin', 'cross-origin write');
  }
  const s = session(req);
  if (r.access === 'admin' || (r.access === 'read' && dev.security.protect_reads)) {
    if (dev.passwordHash === null) throw new HttpError(401, 'password_not_set', 'set the admin password first');
    if (!s.token) throw new HttpError(401, 'unauthorized', 'session required');
  }
  if (write && s.token && s.viaCookie && req.headers['x-csrf-token'] !== dev.sessions.get(s.token)?.csrf) {
    throw new HttpError(403, 'csrf', 'missing or wrong X-CSRF-Token');
  }
  const m = r.pattern.exec(path)!;
  const params: Record<string, string> = {};
  r.keys.forEach((k, i) => (params[k] = decodeURIComponent(m[i + 1] ?? '')));
  await r.fn(req, res, params, url);
}

const staticRoot = args.static ? resolve(args.static) : null;
function serveStatic(req: IncomingMessage, res: ServerResponse, url: URL): void {
  if (!staticRoot) {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('mock: no --static directory; use the Vite dev server for the UI');
    return;
  }
  let p = normalize(decodeURIComponent(url.pathname));
  if (p.includes('..')) {
    res.writeHead(400).end();
    return;
  }
  if (p.endsWith('/')) p += 'index.html';
  const gz = String(req.headers['accept-encoding'] ?? '').includes('gzip');
  const tryFile = (q: string): [string, boolean] | null => {
    const f = join(staticRoot, q);
    if (gz && existsSync(f + '.gz')) return [f + '.gz', true];
    if (existsSync(f) && statSync(f).isFile()) return [f, false];
    return null;
  };
  // Same rules as resolveStatic() in zf_web http_util.cpp.
  let hit = tryFile(p);
  let logical = p;
  if (!hit && !extname(p)) {
    hit = tryFile('/index.html');
    logical = '/index.html';
  }
  if (!hit) {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('not found');
    return;
  }
  const type = MIME[extname(logical)] ?? 'application/octet-stream';
  const headers: Record<string, string> = {
    'Content-Type': type,
    'Cache-Control': logical.startsWith('/assets/') ? 'public, max-age=31536000, immutable' : 'no-cache',
    'X-Content-Type-Options': 'nosniff',
    Vary: 'Accept-Encoding',
  };
  if (hit[1]) headers['Content-Encoding'] = 'gzip';
  if (type.startsWith('text/html')) headers['Content-Security-Policy'] = CSP;
  res.writeHead(200, headers);
  createReadStream(hit[0]).pipe(res);
}

/** Test hooks; not part of the device API. */
async function control(req: IncomingMessage, res: ServerResponse, url: URL): Promise<void> {
  const b = req.method === 'POST' ? await json(req).catch(() => ({}) as Json) : {};
  switch (url.pathname) {
    case '/__mock/reset':
      for (const t of timers) clearTimeout(t);
      timers.clear();
      down = false;
      dev.reset({ ...baseOpts(), ...(b as Partial<DeviceOptions>) });
      for (const s of streams) s.destroy();
      streams.clear();
      failures.clear();
      break;
    case '/__mock/clock':
      dev.setClock(Number(b['utc'] ?? dev.utc()), b['frozen'] as boolean | undefined);
      break;
    case '/__mock/fault':
      dev.raiseFault(String(b['code']), (b['zone'] as number | null) ?? null);
      break;
    case '/__mock/run':
      // Start watering without a session (to set up screenshots).
      if (b['program']) dev.runProgram(Number(b['program']), 'schedule');
      else dev.runZone(Number(b['zone']), Number(b['seconds'] ?? 600));
      if (b['elapsed_s']) {
        const head = dev.queue[0];
        if (head) dev.zones[head.zone]!.remainingMs -= Number(b['elapsed_s']) * 1000;
      }
      break;
    case '/__mock/set': {
      const s = b as Json;
      if (typeof s['rain'] === 'boolean') {
        dev.sensors.rainActive = s['rain'];
        dev.emit('sensor_changed', { sensor: 'rain', active: s['rain'] });
      }
      if (typeof s['spare'] === 'boolean') {
        dev.sensors.spareActive = s['spare'];
        dev.emit('sensor_changed', { sensor: 'spare', active: s['spare'] });
      }
      if (typeof s['time_status'] === 'string') dev.timeStatus = s['time_status'] as 'unset';
      if (typeof s['rest_zone'] === 'number') dev.zones[s['rest_zone'] - 1]!.restMs = Number(s['rest_s'] ?? 900) * 1000;
      if (typeof s['rain_delay_h'] === 'number') dev.setRainDelay(dev.utc() + s['rain_delay_h'] * 3600);
      if (typeof s['verdict'] === 'string') dev.nextRunVerdict = s['verdict'];
      if (typeof s['mute'] === 'boolean') dev.streamMuted = s['mute'];
      if (typeof s['protect_reads'] === 'boolean') dev.security.protect_reads = s['protect_reads'];
      if (typeof s['pause'] === 'boolean') s['pause'] ? dev.pause() : dev.resume();
      if (s['ota'] && typeof s['ota'] === 'object') {
        dev.ota = { ...dev.ota, ...(s['ota'] as Json) };
        dev.sendOta();
      }
      dev.changed();
      break;
    }
    case '/__mock/drop-streams':
      for (const s of streams) s.destroy();
      streams.clear();
      break;
    case '/__mock/state':
      return send(res, 200, { streams: streams.size, sessions: dev.sessions.size, status: dev.statusJson() });
    default:
      throw new HttpError(404, 'not_found', 'unknown mock hook');
  }
  send(res, 200, { ok: true });
}

const server = createServer((req, res) => {
  const url = new URL(req.url ?? '/', 'http://mock');
  const run = async () => {
    if (url.pathname.startsWith('/__mock/')) return control(req, res, url);
    if (down) {
      req.socket.destroy();
      return;
    }
    if (url.pathname.startsWith('/api/v1/') || url.pathname === '/api/v1') return api(req, res, url);
    return serveStatic(req, res, url);
  };
  run().catch((e: unknown) => {
    if (res.headersSent) return res.end();
    if (e instanceof HttpError) {
      send(res, e.status, { error: { code: e.code, message: e.message, ...(e.path ? { path: e.path } : {}) } }, e.headers);
    } else {
      console.error(e);
      send(res, 500, { error: { code: 'internal', message: String(e) } });
    }
  });
});

setInterval(() => dev.tick(), 200);
server.listen(Number(args.port), args.host, () => {
  if (!args.quiet) {
    console.log(`ZoneFlow mock on http://${args.host}:${args.port} (scenario ${args.scenario}${staticRoot ? `, serving ${staticRoot}` : ''})`);
    console.log(args.scenario === 'fresh' ? `setup code: ${SETUP_CODE}` : 'admin password: zoneflow');
  }
});
