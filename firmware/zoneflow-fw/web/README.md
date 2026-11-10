# ZoneFlow web UI

The browser interface the standalone firmware serves on the local network.
Preact, TypeScript and Vite; no runtime dependencies beyond Preact and
`@preact/signals`. Everything is served by the controller: no CDN, no web
fonts, no analytics.

Contract with the firmware (where files go, how they are served, the API and
the stream): [`docs/firmware/web-ui-contract.md`](../../../docs/firmware/web-ui-contract.md).
User guide with screenshots: [`docs/firmware/standalone.md`](../../../docs/firmware/standalone.md).

## Commands

Node 22.18 or newer (CI uses Node 24). Scripts and the mock are TypeScript
run directly by Node (type stripping), so there is no extra build step for
them.

```sh
cd firmware/zoneflow-fw/web
npm ci
npm run mock            # mock controller on :8787 (demo: password "zoneflow")
npm run dev             # Vite on :5173, /api proxied to the mock
npm test                # unit tests (vitest)
npm run build           # type check, build, pack into ../www, check the size budget
npx playwright install chromium
npm run e2e             # end-to-end tests against the mock serving ../www
npm run screenshots     # regenerate docs/firmware/img/webui/*.png
```

`npm run build` is the one command the firmware needs: it writes
`firmware/zoneflow-fw/www/` (not committed), which `idf.py build` packs into
`build/www.bin` for the `www` partition. Without Node the firmware still
builds and serves its built-in page.

Mock options: `node mock/server.ts --scenario fresh` (no password; setup code
`K7Q2M9XW4R`), `--speed 10` (time runs ten times faster), `--utc <unix s>
--frozen` (fixed clock), `--static ../www` (serve the packed UI with the
device's Content-Security-Policy and gzip rules).

## Why Preact

Size matters twice: the files live in flash and are sent by a
microcontroller over WiFi, often to a phone with a weak signal in the garden.

| Option | Runtime, gzip | Notes |
|---|---|---|
| Preact 10 + signals | about 6 KB | React's component model and JSX, mature, tiny. Chosen. |
| Svelte 5 | about 3–15 KB, grows with components | Compiler output grows per component; fine, but less familiar to most contributors. |
| React 19 | about 45 KB | Too large for the benefit. |
| No framework | 0 KB | Twenty screens of forms and live state by hand would cost more code than the framework saves. |

The current build is 50 KB gzip in total (JavaScript 44 KB, CSS 6 KB, HTML
0.4 KB); `npm run build` prints the table and fails above 150 KB.

## Layout

| Path | Contents |
|---|---|
| `src/api/schema.d.ts` | Types generated from `docs/firmware/openapi.json` (`npm run gen:api`). CI fails if it is stale. |
| `src/api/client.ts` | Typed `call(method, path, {params, body})`, CSRF header, 401 handling, upload with progress. |
| `src/state/` | Signals store, the event stream (`live.ts`), commands (`actions.ts`), plain-language errors and alerts. |
| `src/lib/tz.ts`, `civil.ts` | Ports of the core's POSIX time zone and calendar code. |
| `src/lib/preview.ts` | Next-seven-days preview with the scheduler's rules. |
| `src/i18n/en.ts` | Every user-visible string. |
| `src/screens/`, `src/components/` | Screens and shared components. |
| `mock/` | Mock controller (`device.ts` simulation, `server.ts` HTTP, SSE and test hooks under `/__mock/`). |
| `scripts/` | API type generation, schedule vectors, packing, screenshot shrinking. |
| `tests/unit/`, `tests/e2e/` | vitest and Playwright. |

## Rules the code follows

- **The controller is the truth.** The UI renders the last `state` message of
  `/api/v1/events`. Countdowns run locally only while the stream is live; when
  it is lost (an error, or 40 s without a message, while the device sends
  `state` at least every 30 s) the page says so and greys the live values.
- **Optimistic display only for run commands**, which the API confirms or
  refuses (`confirmed`, 409, `command_rejected`). Everything else waits for
  the next `state`.
- **Session and CSRF as in the contract**: the CSRF token from login or
  `GET /api/v1/auth` is kept in memory and sent as `X-CSRF-Token` on every
  write; a 401 sends the user back to sign in. Reads work without signing in
  unless "protect reads" is on.
- **Times are the controller's local time** (its POSIX TZ string through
  `lib/tz.ts`), not the phone's.
- **Destructive actions ask first**: stop all is the exception, because
  stopping water must be one tap.
- **Strings** come from `src/i18n/en.ts` through `t()`; a unit test fails on a
  missing key or a fault code without a plain-language explanation.

## Schedule preview and shared vectors

`lib/preview.ts` follows `zf_core/src/scheduler.cpp`: day rules, sorted and
de-duplicated start times, DST gaps (a start in the gap runs at its end;
several starts in one gap run once) and repeats (first occurrence), seasonal
rounding, disabled zones, rain delay. `scripts/gen-vectors.ts` writes
`firmware/zoneflow-fw/test/vectors/schedule_preview.json` from it; the C++
host test `schedule_vectors_test.cpp` runs the real `Scheduler` over the same
cases, and `tests/unit/preview.test.ts` runs the TypeScript. Change one side
and the other fails. The preview lists planned starts; it does not model the
queue (a start whose program is still running is skipped by the device as
`already_queued`).
