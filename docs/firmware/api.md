# API reference

!!! warning "Not tested on hardware"
    The API is implemented in the standalone firmware and checked by host
    unit tests only. No board exists yet, so no request has been sent to a real
    controller. Tracked in [issue #6](https://github.com/syntroniks/zoneflow/issues/6).

The standalone firmware serves a REST API and a live event stream on the
local network. The ESPHome package does not have this API; it uses the
ESPHome native API.

The full reference is the OpenAPI 3.1 document
[`openapi.json`](openapi.json), rendered at the end of this page. A host test
(`test/host/web_openapi_test.cpp`) fails if a documented route is not
registered in the firmware, or a registered route is missing from the
document, or their security, body type or body limit differ.

## Finding the controller

- `http://zoneflow-xxxx.local/` where `xxxx` is the last four hex digits of
  the WiFi MAC address (mDNS).
- mDNS services `_http._tcp` and `_zoneflow._tcp` on port 80. The
  `_zoneflow._tcp` TXT record holds `api=/api/v1`, `fw=<version>` and
  `id=<mac>`.
- Or the IP address your router assigned.

## Conventions

- Base path `/api/v1`. JSON in and out, UTF-8.
- **Zone numbers are 1-based**, as printed on the terminal block: zone 1 is
  `Z1`. Program durations (`durations_s`) are an array where index 0 is zone 1.
- Times are Unix seconds UTC (`*_utc`); the controller's local time zone is a
  POSIX TZ string (`GET /api/v1/time`).
- Commands that change what the valves do (run, stop, pause, rain delay, ...)
  answer **202 Accepted**: the command is queued for the control task, which
  applies it within about 50 ms. Watch the event stream or `GET /status` for
  the result.
- Errors have one shape:

    ```json
    {"error": {"code": "invalid_config", "message": "must be 1 to 14400", "path": "max_run_s"}}
    ```

    `code` is stable and meant for programs; `path` names the offending field
    when there is one.

| Status | Meaning |
|---|---|
| 400 | Invalid body or parameter |
| 401 | No valid session (`unauthorized`), or no admin password yet (`password_not_set`) |
| 403 | Host header not this device (`bad_host`), cross-origin write (`bad_origin`), missing CSRF header (`csrf`) |
| 404 | No such route, zone or program |
| 405 | Method not allowed; see the `Allow` header |
| 409 | Refused in the current state: zone disabled or locked out, no 24 VAC, update running, too many programs |
| 413 | Body larger than the route allows (checked before the body is read) |
| 415 | Wrong `Content-Type` |
| 429 | Too many failed password attempts; see `Retry-After` |
| 503 | Command queue full; see `Retry-After` |

## Authentication

Every request that changes something needs a session. Reads are open unless
"protect reads" is on. The reasoning is in the
[firmware security notes](../design/firmware-security.md).

1. **First access.** Set the admin password with the setup code shown on the
   controller's display or serial console:

    ```sh
    curl -X POST http://zoneflow-ab12.local/api/v1/auth/setup \
         -H 'Content-Type: application/json' \
         -d '{"setup_code":"K7Q2M9XW4R","password":"a long password"}'
    ```

    The answer is a session, as for login.

2. **Log in** to get a session:

    ```sh
    curl -X POST http://zoneflow-ab12.local/api/v1/auth/login \
         -H 'Content-Type: application/json' -d '{"password":"a long password"}'
    # {"token":"4f1c...","csrf":"9a07...","idle_timeout_s":86400}
    ```

3. **Scripts** send the token as a bearer token. No CSRF header is needed:

    ```sh
    curl -X POST http://zoneflow-ab12.local/api/v1/zones/1/run \
         -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
         -d '{"duration_s":300}'
    ```

4. **Browsers** get the same token as the `zf_session` cookie (HttpOnly,
   SameSite=Strict). With the cookie, every `POST`, `PUT`, `PATCH` and
   `DELETE` must also send the `csrf` value as the `X-CSRF-Token` header. After
   a page reload, `GET /api/v1/auth` returns the token again for a valid
   cookie session.

Sessions live in RAM: a restart, including the one after a firmware update,
signs everyone out. Five wrong passwords from one address lock that address
out for 1 s, doubling with each further failure up to 15 minutes.

## Live event stream

`GET /api/v1/events` is a
[Server-Sent Events](https://html.spec.whatwg.org/multipage/server-sent-events.html)
stream (`text/event-stream`).

**Why SSE and not WebSocket.** The stream only flows from the controller to
the client; commands go through the REST routes, where authentication, CSRF
checks and validation already live. SSE is plain HTTP, so the same session
cookie or bearer token and the same Host check apply with no second protocol.
Browsers reconnect on their own (`EventSource`), and the firmware needs no
frame parser or masking code. The cost is that each stream holds one HTTP
connection; the controller allows 3 at a time and answers 503 to a fourth.

Messages:

| `event:` | When | `data:` |
|---|---|---|
| `state` | On connect, then when the snapshot changes (at most every 500 ms) and at least every 30 s | Same body as `GET /api/v1/status` |
| `event` | Each controller event | `{"type": "zone_changed", "utc": ..., "mono_ms": ..., ...}` (below) |
| `network` | Network status changed | Same body as `GET /api/v1/network` |
| `ota` | Update phase or progress changed (progress at most once a second) | The `ota` object of `GET /api/v1/firmware` |

A comment line (`: keep-alive`) is sent after 15 s without messages. Every
message has an increasing `id:`; the counter restarts with the device.

Controller event types and their extra fields:

| `type` | Fields |
|---|---|
| `zone_changed` | `zone` (1-based), `state` (`off`, `waiting`, `open`), `reason` |
| `master_changed` | `on` |
| `queue_changed` | (read the next `state` message) |
| `program_started` | `program_id`, `source`, `late` |
| `program_skipped` | `program_id`, `reason` |
| `program_missed` | `program_id`, `scheduled_utc` |
| `sensor_changed` | `sensor` (`rain`, `spare`, `ac`), `active` |
| `fault_raised`, `fault_cleared` | `code`, `zone` (1-based or null) |
| `rain_delay_changed` | `until_utc` (null when cleared) |
| `config_applied` | |
| `time_status_changed` | `status` (`unset`, `rtc`, `synced`) |
| `maintenance_changed` | `on` |

The stream is fed from the controller's event bus through a queue of 32
events; the bus handler never waits. If a slow client lets the queue fill,
events are dropped and the next `state` message carries the full picture.

```js
const es = new EventSource("/api/v1/events");
es.addEventListener("state", (m) => render(JSON.parse(m.data)));
es.addEventListener("event", (m) => console.log(JSON.parse(m.data)));
```

## Firmware updates

- `POST /api/v1/firmware/upload` with the raw `zoneflow-fw.bin` as
  `application/octet-stream`.
- `POST /api/v1/firmware/pull` with `{"url": "https://..."}` to download a
  release asset.

Both are refused with 409 `zones_active` while watering, unless `force` is
given (`?force=true` for upload, `"force": true` for pull). Before writing,
the controller enters maintenance: every valve closes, the queue is cleared
and starts are blocked. After a restart the new image must pass a self-test
within 60 s or the bootloader returns to the previous one. Details:
[firmware architecture, section 13](../design/firmware-architecture.md#13-ota-zf_ota).

## Reference

<swagger-ui src="openapi.json"/>
