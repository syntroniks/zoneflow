# Web UI contract

For whoever writes the browser UI of the standalone firmware. It states what
the firmware provides and expects. Nothing here has run on hardware yet.

The UI that implements it is in `firmware/zoneflow-fw/web/` (developer guide
in its `README.md`); `npm run build` there writes `www/`.

## Where the files go

| Item | Contract |
|---|---|
| Build output | `firmware/zoneflow-fw/www/`. Not committed (`.gitignore`); the UI project writes it. |
| Entry point | `www/index.html` (or `index.html.gz`). Without it the firmware build skips the image and the device serves its built-in page. |
| Packing | `idf.py build` runs `littlefs_create_partition_image(www ...)` (component `joltwallet/littlefs` 1.22.3) and writes `build/www.bin`; `idf.py flash` writes it to the `www` partition. |
| Size | The `www` partition is 0x7C0000 bytes (7 936 KiB) in `partitions.csv` (WROOM-1-N16R8). It also holds display assets (fonts, images) from the display firmware, so agree on a directory split with that owner; LittleFS needs some space for metadata. |
| Update in the field | `POST /api/v1/firmware/assets` with `www.bin` as `application/octet-stream` (admin session). Same safety gate as a firmware update; no restart needed. A release should ship `zoneflow-fw.bin` and `www.bin`. |
| File names | Lower case, no spaces. Paths and names under 64 bytes (LittleFS default name limit; keep margin). |

## How files are served

- `GET /` serves `/index.html`.
- If the browser accepts gzip and `<file>.gz` exists, that is sent with
  `Content-Encoding: gzip`. **Ship pre-compressed files** (`app.js.gz` next to
  or instead of `app.js`); the device does not compress.
- A path without an extension that matches no file serves `/index.html`, so
  client-side routes such as `/zones/3` work on reload.
- Files under `/assets/` are sent with
  `Cache-Control: public, max-age=31536000, immutable`. **Put a content hash
  in every file name there** (Vite and most bundlers do by default).
  Everything else is `no-cache`.
- Content types come from the extension: html, js, mjs, css, json,
  webmanifest, svg, png, jpg, ico, webp, woff2, woff, txt, map.
- HTML is sent with this Content-Security-Policy:
  `default-src 'self'; img-src 'self' data:; style-src 'self' 'unsafe-inline';
  connect-src 'self'; frame-ancestors 'none'; base-uri 'self'; form-action 'self'`.
  **No inline scripts, no CDNs**: everything is served by the device, which
  may have no internet.
- Paths under `/api/v1/` are never served from files.

## The API and the stream

- REST API: [API reference](api.md) and `docs/firmware/openapi.json`. Use the
  same origin (relative URLs, `/api/v1/...`).
- Live updates: one `EventSource("/api/v1/events")`. Render from the `state`
  message (same body as `GET /api/v1/status`); use `event` messages for
  notifications. The device allows 3 streams in total, so open one per tab and
  close it when the tab is hidden for long.
- Commands answer 202 and take effect within about 50 ms; show the result
  from the next `state` message rather than from the 202. Run commands may
  answer 409 with the controller's reason (`zone_resting`, `locked_out`,
  `master_locked_out`, `already_queued`, ...); show it to the user. A 202 with
  `"confirmed": false` means no verdict yet: watch for `command_rejected`.
- Show the new safety state: a resting zone (`resting`, `rest_remaining_s`),
  a locked-out master (`master.locked_out`), `current_sense_available: false`
  (diagnostics blind, watering continues) and `over_current_tripped`. The
  lockouts and the trip clear with "clear faults"; the current-sense fault
  clears when readings return; a rest ends on its own.
- Show the valve board's state (`hardware`, `faults.hardware`): a failed
  self-test blocks every start (`inhibit.hardware`, run error
  `hardware_fault`). "Clear faults" acknowledges it, except when
  `faults.hardware.repair_needed` is true: then the user must confirm a
  repair with `POST /api/v1/faults/acknowledge-repair` and
  `{"repaired": true}`, never with one press.

## Authentication in the browser

1. On load call `GET /api/v1/auth`:
   - `password_set: false`: show a "set admin password" form that asks for
     the setup code (the display shows it under Setup, Network, Show setup
     code after a 3 s hold of the knob), then `POST /api/v1/auth/setup`.
   - `authenticated: false`: show the login form, `POST /api/v1/auth/login`.
   - `authenticated: true`: the answer includes `csrf`.
2. Keep the `csrf` value in memory (not localStorage) and send it as
   `X-CSRF-Token` on every `POST`, `PUT`, `PATCH` and `DELETE`. The session
   cookie is HttpOnly; the UI never handles the token itself.
3. Send JSON bodies with `Content-Type: application/json`. Other types get 415.
4. On 401 go back to step 1 (sessions end at every restart, including after
   an update). On 429 wait `Retry-After` seconds.
5. If `protect_reads` is true, every `GET` (and the event stream) needs the
   session too.

## Things the UI should offer

The API supports all of these; the device has no other place for them:

- Zone list with state, remaining time, run for N seconds, stop; rename and
  per-zone settings.
- Programs: list, create, edit, delete, run now. Validation errors carry
  `error.path` relative to the program (`starts[1]`), so they can be shown
  next to the field.
- Queue: pause, resume, skip, stop all. Rain delay. Sensor values. Faults and
  "clear faults".
- Time and time zone (POSIX TZ string; offer a list of common zones).
- Network status; "re-enter WiFi setup" (`POST /api/v1/network/reprovision`).
- Firmware: version, slots, upload, pull from URL, progress from the `ota`
  stream messages. Warn that a forced update stops watering.
- Backup download and restore. Security settings: password change,
  protect reads, HTTPS (with the certificate fingerprint and an honest
  explanation, see [firmware security](../design/firmware-security.md#5-https-plainly)).
- Reboot and factory reset, behind a confirmation.

## Built-in pages

While the partition is empty the device serves a small built-in page at `/`
(status, password setup or login, firmware and UI upload). During SoftAP
provisioning it serves its own setup page at `/setup` and redirects every
other host name there (captive portal). The UI does not need to cover WiFi
setup on the access point.
