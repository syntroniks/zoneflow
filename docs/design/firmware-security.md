# ZoneFlow firmware: network security

Status: **phase 2, reviewed in phase 3**. Covers the standalone firmware's
network surface: WiFi provisioning, the REST API and event stream, the web UI,
OTA and MQTT (4.8, added after the review). None of it has run on a board. The logic named here is covered by
host tests (`firmware/zoneflow-fw/test/host`); the ESP-IDF glue around it is
only compiled. Section 8 lists what the phase 3 security review found and
changed (findings F1 to F7); section 7 lists the risks that remain.

## 1. Setting

- The controller sits on a home LAN, usually without a certificate
  authority, a DNS name or a static address. It is reached as
  `http://zoneflow-xxxx.local/` (mDNS) or by IP address.
- There is no cloud service. Nothing listens on the internet unless the owner
  forwards a port, which the documentation tells them not to do.
- The person who installs it has physical access: they see the display and
  can read the USB serial console.

## 2. What is worth protecting

| Asset | Why |
|---|---|
| Valve control | Unwanted watering wastes water and can flood. The zone driver caps each zone's on-time at its maximum (at most 4 h), added up across runs until the zone has rested (system spec section 2), whatever the API asks. A restart resets that count (section 7). |
| Schedule and configuration | Silent changes stop watering or waste water. |
| WiFi credentials | Give access to the owner's network. |
| Admin password and sessions | Give all of the above. |
| Firmware | A malicious image controls the outputs. The hardware keep-alive gate (spec section 2) still turns outputs off when firmware stops toggling, but a malicious image can keep toggling. |
| Availability | Scheduled watering must continue without the network, and nothing on the network may stop it. |

## 3. Adversaries

| Adversary | In scope | Can |
|---|---|---|
| **A1. A web page** the owner visits on a LAN computer | Yes | Make the browser send requests to the controller (CSRF), or point its own DNS name at the controller's address (DNS rebinding) and read the answers. |
| **A2. Another device on the LAN** (compromised IoT gadget, guest) | Yes | Send any request, from as many addresses as it likes (IPv4 aliases, IPv6). Read plain-HTTP traffic of other LAN clients where the network allows it (shared WPA2-PSK, hub, ARP spoofing). |
| **A3. Someone in radio range during setup** | Yes | Connect to the BLE service or the setup access point, sniff the air, record a WPA2 handshake for offline guessing. |
| **A4. Someone holding the device** | No | Read the flash (credentials and keys are not encrypted, see section 7), reflash it. |
| **A5. The internet** | No, by deployment | Only if the owner exposes the controller. The Host check below still refuses requests that do not name the device. |

## 4. Controls

### 4.1 Provisioning (A3)

- **Setup code.** At first boot the device draws 10 characters from a
  32-symbol alphabet (no 0/O/1/I; 50 bits, 5 bits from each RNG byte) from
  the hardware RNG with WiFi started, and stores the code itself in NVS
  (the display needs it). The serial console prints it **only while
  provisioning runs**. The display shows it only inside the QR code, and
  only while BLE provisioning runs (`DisplayNetworkPort` in
  `main/display_ports.hpp`); during SoftAP setup the owner needs the serial
  console or a printed label. The API never returns it. A factory reset keeps it, so a printed label
  stays valid.
- **BLE: ESP-IDF unified provisioning, Security 2.** SRP6a with the setup
  code as password, salt and verifier from `esp_srp_gen_salt_verifier` at
  first boot; the session is AES-256-GCM. A radio eavesdropper learns neither
  the setup code nor the WiFi password; an active attacker without the code
  cannot complete the session and gets one guess per attempt. A passive BLE
  observer learns the device name `ZoneFlow-XXXX` (two MAC bytes) and the BLE
  address. The stored code means the verifier adds nothing against A4.
- **SoftAP fallback.** The setup access point uses the setup code as its
  WPA2 passphrase (the ESP-IDF SoftAP scheme sets WPA/WPA2 mixed mode). The
  captive setup page sends the WiFi password and the admin password as plain
  HTTP inside that link. Whoever knows or recovers the setup code can read
  them: see section 7, item 6.
- **Provisioning routes only on the setup access point** (F1).
  `GET /provision/networks` and `POST /provision/wifi` need no session; the
  WPA2 passphrase is the proof. The station joins the home network before
  SoftAP provisioning ends (the provisioning manager stops about 30 s after
  the station gets an address), so the glue marks each request by the
  socket's local address, and only connections to the access point's own
  address may use these routes (403 `setup_ap_only` otherwise). The plain
  HTTP exception while HTTPS is on and the captive redirect use the same
  per-connection test.
- **Windows** (F3). An unprovisioned device advertises BLE for 10 minutes,
  then switches to the access point until it is set up. Re-provisioning a
  configured device keeps the old credentials and gives up after 15 minutes.
  A phone that has completed the Security 2 handshake, or a client that has
  joined the WPA2 access point, holds the window open, for at most 30 minutes
  from its start. A bare BLE connection does not hold it; it can still occupy
  the one BLE connection slot until the window ends. BLE memory is released
  after provisioning ends and at every boot that does not provision.

### 4.2 Authentication (A2)

- **One admin password**, 8 to 64 bytes, no control characters. Set at first
  access with the setup code (`POST /api/v1/auth/setup`) or on the SoftAP
  setup page. Proving the setup code means physical access, so a LAN device
  cannot claim an unconfigured controller.
- **Storage.** PBKDF2-HMAC-SHA-256, 16-byte random salt, 10 000 iterations,
  stored as `pbkdf2-sha256$<iterations>$<salt>$<hash>` in NVS (namespace
  `zf_web`). Cost per check: 2 x 10 000 + 4 SHA-256 compressions in
  portable C++ (`crypto.cpp`), the same code the host tests check against
  published vectors. Not timed on an ESP32-S3. Time a login on the first
  board; if it takes more than about a second, move the hash to mbedTLS and
  the SHA accelerator rather than lowering the count. The iteration count is
  stored per record, so it can change without breaking existing passwords.
  Password-keyed HMAC state and PBKDF2 intermediates are zeroed after use.
- **Every state-changing route needs a session**, except login, setup and
  the two provisioning routes of 4.1. Reads (`GET`) are open by default and
  can be closed with `protect_reads` (`PUT /api/v1/security`). The route
  table marks each route Public, Read, Admin or Provisioning, and a host test
  checks that only login and setup are public writes.
- **Sessions.** 128-bit random token, held in RAM only (a restart signs
  everyone out), at most 8, idle timeout 24 h, absolute 30 days. A password
  change signs out every other session. Sent as cookie `zf_session`
  (HttpOnly, SameSite=Strict, Secure under HTTPS) or as
  `Authorization: Bearer <token>` for scripts. Tokens come only from the
  server, so a planted cookie cannot fix a session. With `protect_reads` on,
  an open event stream ends with the session that opened it, checked once a
  second (F5).
- **Rate limiting** of password and setup-code attempts: per client address,
  5 free failures, then a lock of 1 s doubling per failure up to 15 min; in
  total, 30 failures within 5 minutes lock every client out for 60 s, which
  bounds guessing from many addresses to about 30 tries per 5 minutes. The
  last 8 addresses that proved the password since boot are exempt from the
  total lock (F2), so a LAN device cycling through addresses cannot keep the
  owner out; new addresses it can (section 7). Answers 429 with Retry-After.

### 4.3 Browser attacks (A1)

- **CSRF.** Four independent layers:
  1. The session cookie is `SameSite=Strict`: browsers do not attach it to
     cross-site requests.
  2. A write that authenticates with the cookie must carry the session's
     CSRF token in `X-CSRF-Token`. A cross-site page cannot read the token.
  3. Request bodies must be `application/json` (or
     `application/octet-stream` for images). An HTML form cannot send either,
     and a script on another origin would need a CORS preflight, which the
     device never approves (it sends no CORS headers and registers no
     `OPTIONS` handler).
  4. A write whose `Origin` header names another host is refused (403
     `bad_origin`), login included. `Origin: null` is refused.
- **DNS rebinding.** The `Host` header must be an IP literal, the device
  hostname or `<hostname>.local`; anything else gets 403 `bad_host`. A page
  that re-points its own name at the device sends its own name as Host and is
  refused, for reads and the event stream as well. Even with the check off,
  the browser would not send the device's cookie to that name. The check can
  be turned off (`check_host`) for setups that use another DNS name, such as
  a reverse proxy; then open reads are readable by such a page.
- **Headers.** API answers carry `Cache-Control: no-store` and
  `X-Content-Type-Options: nosniff`. HTML pages carry a Content-Security-Policy
  with `frame-ancestors 'none'`. The web UI image is trusted code: it runs in
  the device's origin, and its scripts are allowed by `script-src 'self'`.

### 4.4 Request handling

- **Caps before parsing.** Every route has a body cap in the route table
  (64 bytes for empty commands, 512 bytes for small objects, 2 KB for a
  program, 16 KB for a whole configuration). `Content-Length` is checked
  before the body is read; larger requests get 413 and the connection is
  closed. Firmware and UI images are streamed, never buffered. A chunked body
  counts as length 0 and is never read.
- **JSON depth.** Bodies nested deeper than 16 levels, or with a closing
  bracket that closes nothing, are refused before cJSON sees them (cJSON
  recurses once per level; the core review measured 64 bytes of stack per
  level on the ESP32-S3). A randomised host test compares the check with
  what cJSON parses (F7).
- **Stack.** Every request runs on the HTTP server task with a 12 KB stack
  (configuration parsing took about 4.6 KB in the core review). The smallest
  free stack seen is reported as `http_stack_free_min` in `GET /api/v1/info`.
- **One HTTP task.** `esp_http_server` handles one request at a time, so a
  client that sends its headers or body slowly holds up every other client
  (section 7). The control task is not affected (4.7).

### 4.5 Secrets in logs, answers, backups and dumps

- Never logged: the admin password, session tokens, the WiFi password, the
  TLS key. The setup code (and the QR payload that holds it) is logged only
  while provisioning runs.
- Never in an API answer or the event stream: the setup code, WiFi
  password, password hash, TLS key. `GET /network` and the `state` message
  show SSID, BSSID and addresses (open reads, section 7). Login and setup
  return the session token in the body for scripts as well as in the
  HttpOnly cookie.
- mDNS TXT records hold `api`, the firmware version and the MAC address.
- `GET /api/v1/config/backup` contains the configuration only. WiFi
  credentials, the password hash, sessions, the setup code and the TLS key are
  never part of it; there is no option to include them. A restored device
  needs WiFi provisioning and its own password.
- **Core dumps** go to the `coredump` partition (ELF, task stacks; DRAM is
  not captured). A stack can hold a secret that was in use at the crash, for
  example WiFi credentials during provisioning. A factory reset now erases
  the dump (F4); nothing serves it over the network.
- **Factory reset** erases NVS (configuration, WiFi credentials, password
  hash, web settings, TLS key) and the core dump. It keeps the setup code
  and SRP verifier, the web UI image and the previous firmware in the other
  OTA slot.

### 4.6 Firmware updates

- Only an admin session can upload or pull an image. Pulls accept `https://`
  URLs only and check the server against the ESP-IDF certificate bundle,
  host name included. Redirects are followed (at most 5) only to `https://`
  targets: ESP-IDF v5.5.6 `esp_http_client` refuses a downgrade with
  `ESP_ERR_HTTP_REDIRECT_DOWNGRADE`, and a relative `Location` is refused
  the same way.
- Before anything is written the project name (`zoneflow-fw`) is checked, and
  the chip id: by `checkImageHeader` for uploads and by `esp_https_ota`
  (`esp_ota_verify_chip_id`) for pulls. `esp_ota_end` checks the image
  hash. A truncated upload fails that check; power loss during an update
  leaves the running slot as the boot slot.
- **Images are not signed and older versions are accepted.** Anyone with
  the admin password can install any firmware that passes those checks,
  including an older one. Secure Boot v2 with anti-rollback would close this;
  it is not enabled (section 7).
- The web UI image (`POST /firmware/assets`) is checked only for the
  LittleFS magic. It is trusted code (4.3), and LittleFS parses it at mount.
- During an upload or pull the controller stays in maintenance, so no zone
  starts. A stalled upload keeps it there until the connection drops (three
  10 s receive timeouts in a row) or the client finishes.

### 4.7 Network load and the control loop

- The control task runs on core 1 at priority 10. Every network task (WiFi,
  lwIP's TCP/IP task, HTTP and HTTPS servers, event stream, zf_net, captive
  DNS, mDNS, BLE host) runs on core 0. Before F6 the TCP/IP task (priority
  18) had no core affinity and could preempt the control task under load;
  it is now pinned to core 0.
- Network code reaches the controller only through the command queue
  (non-blocking), snapshot copies and event-bus handlers that store atomics
  or copy into a queue without waiting. Config writes hold locks the control
  task never takes. A flash write stalls both cores; only an admin can cause
  one, and the OE keep-alive keeps its edges through stalls shorter than
  500 ms.
- The task watchdog no longer watches the core 0 idle task (F6): a client
  that keeps the HTTP(S) server busy could otherwise turn load into a panic
  and restart. The control task and the core 1 idle task stay watched.
- Through a crash or a restart, outputs go off (panic wrapper, shutdown
  handler, hardware keep-alive). Manual runs and the queue are not restored.
  A scheduled start is stored as consumed within about 2.25 s of starting
  (2 s mailbox interval plus the writer's 250 ms poll), so only a crash loop
  faster than that could re-run it, and only within
  `schedule.missed_start_grace_min`. The review found no request that
  crashes the firmware.

### 4.8 MQTT (A2)

MQTT is off by default. When it is on, **whoever may publish to the
controller's command topics on the broker controls the valves**, exactly
like an admin session. The broker's authentication and access control are
the protection; the controller cannot tell clients apart.

- **Broker access.** Use a broker user for the controller and ACLs that let
  only it and Home Assistant use `<root>/#`. Mosquitto: `allow_anonymous
  false`, `password_file`, `acl_file`.
- **Routes.** `GET /api/v1/mqtt` is a Read route (needs a session when
  `protect_reads` is on) and `PUT /api/v1/mqtt` an Admin route; both go
  through `Api::admit()` like every other route: Host check, Origin check,
  CSRF header for cookie sessions, 8 KB body cap, JSON only. Neither is a
  Provisioning route, so the setup-access-point rule (F1) does not apply.
  Tests in `web_mqtt_api_test.cpp`.
- **Credentials.** The MQTT password and a custom CA certificate are
  write-only in the API (`PUT /api/v1/mqtt`; `GET` shows only
  `password_set`), stored in NVS namespace `zf_mqtt`, never logged and
  never in the configuration document or its backup. Copies the firmware
  owns are cleared with `secureZero` when done: the password in the parsed
  request body (on every return path of `Api::mqttPut`), the client's copy
  right after `esp_mqtt_client_init`, and the presence check at start.
  Not cleared: the request buffer in the HTTP glue (same as for the admin
  password at login), the NVS read buffer inside ESP-IDF, and esp-mqtt's
  own `strdup` copy, which it frees without clearing.
- **On the wire.** Plain MQTT (port 1883) sends the password and every
  message in clear text, readable by A2 on the LAN. `tls: true` encrypts
  and checks the broker against the ESP-IDF certificate bundle or a stored
  CA. There is no client certificate support.
- **What a command can do.** The same commands as the REST API, through the
  same queue and the same core limits: a run is clamped to the zone's
  maximum and run-time budget, locked-out and resting zones are refused.
  MQTT cannot change the configuration beyond a zone's default run time
  and a program's enabled flag, cannot install firmware (the Home
  Assistant `update` entity has no install command), and cannot reach the
  password, network or security settings.
- **Accidental starts.** Retained command messages and empty messages are
  ignored, so a stale retained "run" on the broker, or clearing one, starts
  nothing.
- **Floods.** At most 10 commands at once and 5 per second; the rest are
  answered `rate_limited`. Incoming payloads over 256 bytes and JSON nested
  more than two levels are refused before parsing.

## 5. HTTPS, plainly

The device can serve HTTPS with a certificate it creates itself (EC P-256,
`CN=<hostname>.local`, valid 2025 to 2049). Turn it on with
`PUT /api/v1/security {"https": true}` and restart; port 80 then only
redirects to HTTPS (except for clients of the setup access point). The key
is drawn only once WiFi is running, because the ESP32-S3 RNG is a true
random source only while WiFi or BT runs.

What it does:

- Encrypts the password, session token and all traffic on the LAN. A
  **passive** listener (A2 sniffing) learns nothing.
- Lets the owner pin the device: the SHA-256 fingerprint is in
  `GET /api/v1/security` and in the serial log. A browser that has accepted
  that certificate once warns if it changes.

What it does not do:

- **It is not trusted by browsers.** Every browser shows a warning on first
  visit. Clicking through without comparing the fingerprint gives no
  protection against an **active** attacker on the LAN who presents their own
  certificate. Port 80 sends no HSTS header, so such an attacker can also
  keep a browser on plain HTTP.
- It does not authenticate the device to scripts unless they pin the
  fingerprint.
- It costs memory: each TLS session needs tens of kilobytes of heap, so the
  HTTPS server accepts 4 connections.

Plain HTTP (the default) protects nothing on the wire. On a home WiFi network
with WPA2-PSK, anyone who knows the WiFi password and captures a client's
handshake can decrypt that client's traffic, including the admin password at
login.

## 6. Where it is implemented and tested

| Control | Code | Host tests |
|---|---|---|
| Route access levels, body caps | `zf_web/src/routes.cpp` | `web_http_test.cpp` (Routes), `web_openapi_test.cpp` |
| Host, Origin, CSRF, setup-AP connection, content type, size, auth order | `Api::admit` in `zf_web/src/api.cpp` | `web_api_test.cpp` |
| Event stream re-check | `Api::streamMayContinue`, `dropUnauthorisedStreams` in `web.cpp` | `web_api_test.cpp` |
| Password hash, rate limiter, sessions | `zf_web/src/auth.cpp`, `crypto.cpp` | `web_auth_test.cpp`, `crypto_test.cpp` (FIPS 180-2, RFC 4231, RFC 7914 and RFC 6070-style vectors) |
| JSON depth pre-check | `jsonDepthOk` in `json_map.cpp` | `web_http_test.cpp` (JsonDepth) |
| Static paths, Host check | `zf_web/src/http_util.cpp` | `web_http_test.cpp` |
| Setup code, QR payload, provisioning windows | `zf_net/src/net_logic.cpp` | `net_logic_test.cpp` |
| Captive DNS parser | `captiveDnsAnswer` | `net_logic_test.cpp` |
| OTA header check and safety gate | `zf_ota/src/ota_logic.cpp` | `ota_logic_test.cpp` |
| No secrets in backup or network status | `Api::configBackup`, `Api::networkGet` | `web_api_test.cpp` |
| MQTT password and CA write-only, outside the configuration | `Api::mqttGet`, `Api::mqttPut`, `zf_mqtt/src/mqtt.cpp` | `web_mqtt_api_test.cpp`, `mqtt_config_test.cpp` |
| MQTT command validation, retained and empty messages, rate limit | `zf_mqtt/src/mqtt_logic.cpp` | `mqtt_logic_test.cpp`; broker test `test/broker/run.sh` |
| History: Read access (follows `protect_reads`), query parameters range-checked, no secrets recorded (events only: no passwords, tokens, setup code or MQTT secrets); erased by a factory reset (NVS) | `Api::historyGet`, `historyFromEvent` | `web_api_test.cpp` (History*), `history_test.cpp` |
| First-run flag: Admin route, 64-byte body cap, one boolean, stored with the web settings (not in a backup) | `Api::setupPut` | `web_api_test.cpp` (SetupCompleteIsStoredAndNeedsASession), `web_openapi_test.cpp` |
| Setup-AP detection, core dump erase, task affinity, TLS key timing | `web.cpp`, `net.cpp`, `sdkconfig.defaults` | none; compiled with `idf.py build` |

## 7. Residual risks and known gaps

1. **No flash or NVS encryption.** WiFi credentials (stored by the WiFi
   driver), the setup code, the SRP verifier, the password hash and the TLS
   key are in plain NVS. Anyone with the board can read them (A4). Production
   builds should enable flash encryption and NVS encryption; both burn eFuses
   and are irreversible, so they are not in `sdkconfig.defaults`.
2. **No Secure Boot, no anti-rollback.** Update images are not signed, and an
   older image is accepted (4.6). The admin password is the only gate.
3. **Self-signed HTTPS, off by default.** See section 5.
4. **One admin account**, no read-only users and no long-lived API tokens
   for integrations; a session lasts at most 30 days.
5. **Reads are open by default.** Status, configuration and network details
   (SSID, BSSID, addresses) are visible to any LAN device until
   `protect_reads` is on.
6. **The setup code is a 50-bit secret used three ways and never changed.**
   It is the SoftAP WPA2 passphrase, the Security 2 password and the proof
   for `POST /auth/setup`, and it survives a factory reset. Someone who
   records the WPA2 handshake of a phone joining the setup access point can
   guess the passphrase offline. Assuming 2.5 million WPA2 guesses per
   second per GPU (an order-of-magnitude assumption, neither measured nor
   sourced here), 2^50 = 1.1e15 guesses take 4.5e8 s, about 14 GPU-years,
   half that on average. Success yields the code, and the decrypted setup page with the
   home WiFi password and the admin password. The access point also allows
   WPA (TKIP) clients. Options, not done: a longer code, a separate random
   passphrase per setup shown next to the code, a setup page that uses the
   Security 2 endpoints instead of plain HTTP, and a "full reset" that draws
   a new code.
7. **A LAN device can keep new clients from logging in.** With more than 16
   addresses it holds the total lock (4.2). Addresses that logged in since
   the last restart still get in, and open sessions keep working; anyone
   else waits until it stops, or uses the display.
8. **One slow client stalls the web server.** Headers (up to 1024 bytes) or
   a body sent a byte every few seconds keep the single HTTP task busy for
   hours; `esp_http_server` has no per-request deadline. Watering is not
   affected. A fix needs a receive override with a per-request deadline or
   a server change.
9. **Event stream slots.** With reads open, any LAN device can hold all 3
   stream slots; the owner's UI then gets 503 for the stream and must poll.
10. **A restart resets the run-time budget.** The zone driver keeps each
    zone's accumulated on-time and rest in RAM. An admin who restarts the
    controller (`POST /system/reboot`, an update, a factory reset,
    re-provisioning) can shorten the minimum rest from 60 s to the restart
    time. Holding the budget in RTC memory that survives a software reset
    would close it; that is a `zf_core` and `zf_platform` change.
11. **Unprovisioned devices advertise indefinitely** (BLE, then the access
    point). Someone in range who also has the setup code could provision it.
12. **PBKDF2 cost not measured** on the target (4.2).
13. **MQTT trusts the broker.** Anyone allowed to publish to the command
    topics can water; MQTT without TLS exposes the broker password on the
    LAN. No client certificates (4.8).
14. **Nothing here has been tested against a real browser, phone app or
    attacker.** The checks above are unit tests of the logic and a firmware
    build.

## 8. Security review (phase 3)

An independent review of the phase 2 network code. Each finding has a host
test that failed before the fix, except where the change is device glue
(marked). The pull request that made these changes has the full table.

| ID | Severity | Finding | Fix |
|---|---|---|---|
| F1 | High | Provisioning routes answered home-network clients for about 30 s after SoftAP setup; a LAN device could set the admin password if the owner left it empty | Routes need a connection to the setup AP's address (4.1) |
| F2 | Medium | A LAN device cycling through addresses kept the total login lock up and the owner out | Known clients exempt from the total lock (4.2) |
| F3 | Low | Any BLE connection, without the setup code, held the provisioning window open without limit | Session counted after Security 2; 30 min cap (4.1) |
| F4 | Low | Factory reset left the core dump, which can hold secrets | Erased on reset (glue) |
| F5 | Low | Event streams outlived logout, password change and expiry with `protect_reads` on | Re-checked once a second |
| F6 | Medium | Network load could preempt the control task (unpinned TCP/IP task) or reboot the controller (idle watchdog on core 0) | `sdkconfig.defaults` (not demonstrated on hardware) |
| F7 | Low | JSON depth check let unbalanced closers hide nesting (cJSON never parsed it) | Closer at depth 0 refused |
