# ZoneFlow firmware (ESP-IDF)

Standalone firmware for the ZoneFlow irrigation controller, system spec
section 4.1. Target: ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal PSRAM),
ESP-IDF v5.5.6.

> **Status: phase 3 (MQTT), never run on hardware.** No board exists yet. What has
> been checked is listed under "What is verified" below. Everything else,
> including every pin number, is provisional.

Design and interfaces: [`docs/design/firmware-architecture.md`](../../docs/design/firmware-architecture.md).
Network security: [`docs/design/firmware-security.md`](../../docs/design/firmware-security.md).
REST API: [`docs/firmware/api.md`](../../docs/firmware/api.md) and
[`docs/firmware/openapi.json`](../../docs/firmware/openapi.json).

## Build the firmware

Install ESP-IDF v5.5.6 ([Espressif guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32s3/get-started/)), then:

```sh
. ~/esp/esp-idf-v5.5.6/export.sh   # wherever you installed it
cd firmware/zoneflow-fw
idf.py set-target esp32s3          # first time only
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

`sdkconfig` is generated from `sdkconfig.defaults` and is not committed.
Board options (output-enable type, keep-alive period, control period) are in
`idf.py menuconfig` under "ZoneFlow board".

The first build downloads two managed components from the ESP-IDF component
registry (`espressif/mdns` 1.14.1, `joltwallet/littlefs` 1.22.3) into
`managed_components/` (not committed).

Web UI files placed in `www/` (with an `index.html`) are packed into
`build/www.bin` and flashed to the `www` partition; see
[`docs/firmware/web-ui-contract.md`](../../docs/firmware/web-ui-contract.md).

## Run the host tests

No ESP-IDF needed. Needs CMake 3.24 or newer, a C++20 compiler and network
access on the first run (GoogleTest 1.17.0 and cJSON 1.7.19 are fetched; a
system GoogleTest is used if found). Besides `zf_core`, the suite builds the
pure-logic files of `zf_net`, `zf_web` and `zf_ota` and checks the route
table against `docs/firmware/openapi.json`.

```sh
firmware/zoneflow-fw/test/host/run.sh
```

The script builds with AddressSanitizer and UndefinedBehaviorSanitizer and
runs every test through CTest. Extra arguments go to `ctest`.

## Format

```sh
cd firmware/zoneflow-fw
git ls-files '*.cpp' '*.hpp' | xargs uvx --from clang-format==23.1.4 clang-format -i
```

CI checks formatting with the same clang-format version.

## Layout

| Path | Contents |
|---|---|
| `components/zf_core/` | Pure C++ logic, no ESP-IDF headers: zone driver, scheduler, time zones, config, events, diagnostics, controller. Builds for the device and for host tests. |
| `components/zf_board/` | Board header. The only place GPIO numbers and board constants appear. |
| `components/zf_platform/` | ESP-IDF code behind the core interfaces: GPIO outputs, OE keep-alive timer interrupt, persist writer task, zero-cross input, sensors (GPIO, PCNT, ADC), NVS, I2C, PCF8563 RTC, SNTP, panic/brownout/restart hooks, event logger. |
| `components/zf_net/` | WiFi station with backoff, BLE (Security 2) and SoftAP provisioning, hostname, mDNS, captive DNS, re-provision and factory reset. |
| `components/zf_web/` | HTTP(S) server, REST API under `/api/v1`, Server-Sent Events stream, admin authentication, static files from LittleFS, built-in fallback and setup pages. |
| `components/zf_ota/` | Firmware upload and HTTPS pull, web UI image upload, post-boot self-test and rollback. |
| `components/zf_mqtt/` | MQTT client (esp-mqtt), topic tree, commands, Home Assistant discovery. Logic in `mqtt_logic.cpp` (host-tested). |
| `components/zf_display` | Stub with the planned work written in its header. |
| `main/app_main.cpp` | Wires everything together and starts the tasks. |
| `test/host/` | GoogleTest suites and fakes for the core. |
| `test/broker/` | Optional broker test: the MQTT logic and the core against a local Mosquitto (`run.sh`; not in CI). |
| `partitions.csv` | 16 MB layout: NVS, two 4 MB OTA slots, 7.75 MB LittleFS `www` for web and display assets, 128 KB core dump. |

## What is verified

- Host tests: `test/host/run.sh`, all suites pass under ASan and UBSan.
- `idf.py build` with ESP-IDF v5.5.6 for esp32s3 completes with no warnings
  in ZoneFlow code. Image size 1 561 027 bytes (37.2 % of a 4 MB OTA slot).
- Broker test: `test/broker/run.sh` builds Mosquitto 2.1.2 and runs the
  MQTT logic with the real core against it; all checks pass. It does not
  run the ESP-IDF MQTT glue. See
  [`docs/firmware/mqtt.md`](../../docs/firmware/mqtt.md#what-has-been-tested).
- Nothing has run on an ESP32-S3. Pin assignments, the output-enable
  timing, the current-sense scaling and the PCF8563 driver are untested
  against hardware. So are WiFi, provisioning with a phone, mDNS, the HTTP
  server, HTTPS, the event stream, OTA and the MQTT client: only their logic is unit-tested,
  the ESP-IDF glue is compiled but has never executed.
