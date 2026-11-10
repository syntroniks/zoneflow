# ZoneFlow firmware: network security

Status: **phase 2 draft**. Covers the standalone firmware's network surface:
WiFi provisioning, the REST API and event stream, the web UI and OTA. None of
it has run on a board. The logic named here is covered by host tests
(`firmware/zoneflow-fw/test/host`); the ESP-IDF glue around it is only
compiled.

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
| Valve control | Unwanted watering wastes water and can flood. A single run is capped by the per-zone maximum and a hard 4 h limit in the zone driver (system spec section 2), whatever the API asks. |
| Schedule and configuration | Silent changes stop watering or waste water. |
| WiFi credentials | Give access to the owner's network. |
| Admin password and sessions | Give all of the above. |
| Firmware | A malicious image controls the outputs. The hardware keep-alive gate (spec section 2) still turns outputs off when firmware stops toggling, but a malicious image can keep toggling. |
| Availability | Scheduled watering must continue without the network. |

## 3. Adversaries

| Adversary | In scope | Can |
|---|---|---|
| **A1. A web page** the owner visits on a LAN computer | Yes | Make the browser send requests to the controller (CSRF), or point its own DNS name at the controller's address (DNS rebinding) and read the answers. |
| **A2. Another device on the LAN** (compromised IoT gadget, guest) | Yes | Send any request. Read plain-HTTP traffic of other LAN clients where the network allows it (shared WPA2-PSK, hub, ARP spoofing). |
| **A3. Someone in radio range during setup** | Yes | Connect to the BLE service or the setup access point, sniff the air. |
| **A4. Someone holding the device** | No | Read the flash (credentials and keys are not encrypted, see section 7), reflash it. |
| **A5. The internet** | No, by deployment | Only if the owner exposes the controller. The Host check below still refuses requests that do not name the device. |

## 4. Controls

### 4.1 Provisioning (A3)

- **Setup code.** At first boot the device draws 10 characters from a
  32-symbol alphabet (no 0/O/1/I; 50 bits) from the hardware RNG with the
  radio on, and stores it in NVS. It is shown on the display (display module)
  and printed to the serial console **only while provisioning runs**. The API
  never returns it. A factory reset keeps it, so a printed label stays valid.
- **BLE: ESP-IDF unified provisioning, Security 2.** SRP6a with the setup
  code as password; the device stores only the SRP salt and verifier
  (`esp_srp_gen_salt_verifier`, computed once at first boot). The session is
  AES-256-GCM. A radio eavesdropper learns neither the setup code nor the WiFi
  password; an active attacker without the code cannot complete the session.
- **SoftAP fallback.** The setup access point is WPA2 with the setup code as
  passphrase. The captive setup page and the Security 2 provisioning
  endpoints both run inside that encrypted link, so the spec's "credentials
  never sent in plain text" holds for the page too.
- **Windows.** An unprovisioned device advertises BLE for 10 minutes, then
  switches to the access point until it is set up. Re-provisioning a
  configured device keeps the old credentials and gives up after 15 minutes.
  BLE memory is released after provisioning ends and at every boot that does
  not provision.

### 4.2 Authentication (A2)

- **One admin password**, 8 to 64 bytes, no control characters. Set at first
  access with the setup code (`POST /api/v1/auth/setup`) or on the SoftAP
  setup page. Proving the setup code means physical access, so a LAN device
  cannot claim an unconfigured controller.
- **Storage.** PBKDF2-HMAC-SHA-256, 16-byte random salt, 10 000 iterations,
  stored as `pbkdf2-sha256$<iterations>$<salt>$<hash>` in NVS (namespace
  `zf_web`). Cost per check: 2 x 10 000 + 4 SHA-256 compressions. Not timed
  on an ESP32-S3; if a login takes more than about a second on hardware,
  move the hash to the SHA accelerator rather than lowering the count. The
  iteration count is stored per record, so it can change without breaking
  existing passwords.
- **Every state-changing route needs a session.** Reads (`GET`) are open by
  default and can be closed with `protect_reads` (`PUT /api/v1/security`).
  The route table marks each route Public, Read, Admin or Provisioning, and
  a host test checks that only login and setup are public writes.
- **Sessions.** 128-bit random token, held in RAM only (a restart signs
  everyone out), at most 8, idle timeout 24 h, absolute 30 days. A password
  change signs out every other session. Sent as cookie `zf_session`
  (HttpOnly, SameSite=Strict, Secure under HTTPS) or as
  `Authorization: Bearer <token>` for scripts.
- **Rate limiting** of password and setup-code attempts: per client address,
  5 free failures, then a lock of 1 s doubling per failure up to 15 min; in
  total, 30 failures within 5 minutes lock every client out for 60 s, which
  bounds guessing from many spoofed addresses. Answers 429 with Retry-After.

### 4.3 Browser attacks (A1)

- **CSRF.** Four independent layers:
  1. The session cookie is `SameSite=Strict`: browsers do not attach it to
     cross-site requests.
  2. A write that authenticates with the cookie must carry the session's
     CSRF token in `X-CSRF-Token`. A cross-site page cannot read the token.
  3. Request bodies must be `application/json` (or
     `application/octet-stream` for images). An HTML form cannot send either,
     and a script on another origin would need a CORS preflight, which the
     device never approves (it sends no CORS headers).
  4. A write whose `Origin` header names another host is refused (403
     `bad_origin`), login included.
- **DNS rebinding.** The `Host` header must be an IP literal, the device
  hostname or `<hostname>.local`; anything else gets 403 `bad_host`. A page
  that re-points its own name at the device sends its own name as Host and is
  refused, for reads as well. The check can be turned off (`check_host`) for
  setups that use another DNS name, such as a reverse proxy.
- **Headers.** API answers carry `Cache-Control: no-store` and
  `X-Content-Type-Options: nosniff`. HTML pages carry a Content-Security-Policy
  with `frame-ancestors 'none'`.

### 4.4 Request handling

- **Caps before parsing.** Every route has a body cap in the route table
  (64 bytes for empty commands, 512 bytes for small objects, 2 KB for a
  program, 16 KB for a whole configuration). `Content-Length` is checked
  before the body is read; larger requests get 413 and the connection is
  closed. Firmware and UI images are streamed, never buffered.
- **JSON depth.** Bodies nested deeper than 16 levels are refused before
  cJSON sees them (cJSON recurses once per level; the core review measured
  64 bytes of stack per level on the ESP32-S3).
- **Stack.** Every request runs on the HTTP server task with a 12 KB stack
  (configuration parsing took about 4.6 KB in the core review). The smallest
  free stack seen is reported as `http_stack_free_min` in `GET /api/v1/info`.

### 4.5 Secrets in logs and backups

- Never logged: the admin password, session tokens, the WiFi password, the
  TLS key. The setup code is logged only while provisioning runs.
- `GET /api/v1/config/backup` contains the configuration only. WiFi
  credentials, the password hash, sessions, the setup code and the TLS key are
  never part of it; there is no option to include them. A restored device
  needs WiFi provisioning and its own password.

### 4.6 Firmware updates

- Only an admin session can upload or pull an image. Pulls accept `https://`
  URLs only and check the server against the ESP-IDF certificate bundle.
- The image header is checked (ESP32-S3 chip id, project `zoneflow-fw`)
  before anything is written, and `esp_ota_end` checks the image hash.
- **Images are not signed.** Anyone with the admin password can install any
  firmware that passes those checks. Secure Boot v2 would close this; it is
  not enabled (section 7).

## 5. HTTPS, plainly

The device can serve HTTPS with a certificate it creates itself (EC P-256,
`CN=<hostname>.local`, valid 2025 to 2049). Turn it on with
`PUT /api/v1/security {"https": true}` and restart; port 80 then only
redirects to HTTPS (except during SoftAP setup).

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
  certificate.
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
| Host, Origin, CSRF, content type, size, auth order | `Api::admit` in `zf_web/src/api.cpp` | `web_api_test.cpp` |
| Password hash, rate limiter, sessions | `zf_web/src/auth.cpp`, `crypto.cpp` | `web_auth_test.cpp`, `crypto_test.cpp` (RFC 4231, RFC 7914 vectors) |
| Setup code, QR payload, provisioning windows | `zf_net/src/net_logic.cpp` | `net_logic_test.cpp` |
| Captive DNS parser | `captiveDnsAnswer` | `net_logic_test.cpp` |
| OTA header check and safety gate | `zf_ota/src/ota_logic.cpp` | `ota_logic_test.cpp` |
| No secrets in backup or network status | `Api::configBackup`, `Api::networkGet` | `web_api_test.cpp` |

## 7. Known gaps

1. **No flash or NVS encryption.** WiFi credentials (stored by the WiFi
   driver), the setup code, the SRP verifier, the password hash and the TLS
   key are in plain NVS. Anyone with the board can read them (A4). Production
   builds should enable flash encryption and NVS encryption; both burn eFuses
   and are irreversible, so they are not in `sdkconfig.defaults`.
2. **No Secure Boot.** See 4.6.
3. **One admin account**, no read-only users and no long-lived API tokens
   for integrations; a session lasts at most 30 days.
4. **Reads are open by default.** Status, configuration and network details
   are visible to any LAN device until `protect_reads` is on.
5. **Sessions end at restart**, including the restart after an update.
6. **An unprovisioned device advertises indefinitely** (BLE, then the access
   point). Someone in range who also has the setup code could provision it.
7. **Nothing here has been tested against a real browser, phone app or
   attacker.** The checks above are unit tests of the logic.
