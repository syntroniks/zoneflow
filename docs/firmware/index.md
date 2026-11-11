# Choosing a firmware

!!! warning "Neither option has run on hardware"
    Both are written, build in CI and pass their host tests. No board
    exists yet. See [issue #6](https://github.com/syntroniks/zoneflow/issues/6)
    and [issue #8](https://github.com/syntroniks/zoneflow/issues/8).

ZoneFlow has two firmware options. Both run on the same board, read the same
pin map, and use the same C++ code for the parts that matter for safety: the
output stage and its self-tests, the keep-alive, the zone driver with the
maximum run time, and the current checks. You can switch later by flashing
the other one.

| | Standalone firmware | ESPHome package |
|---|---|---|
| Choose it if | You want a controller that works on its own | Home Assistant already runs your house |
| Needs a hub | No | Home Assistant, for setup and control |
| Needs a cloud service | No | No |
| Schedules without the network | Yes | Yes |
| WiFi setup | Phone app over Bluetooth LE, or a setup access point | Improv over USB or Bluetooth LE |
| Display | Full menus: programs, settings, faults, network | Status screen and manual runs |
| Programs | 16, up to 4 start times each, per-zone run times | 3, two start times each, a zone list and a run-time percentage |
| Day rules | Weekdays, every N days, odd, even | Every day, weekdays, weekends, odd, even, every 2 or 3 days, any set of weekdays |
| Control from elsewhere | Own web interface, REST API, MQTT with Home Assistant discovery | Home Assistant entities and automations |
| Run history | Kept on the controller | Home Assistant's history |
| Updates | Over the network with automatic rollback | ESPHome OTA, safe mode after repeated boot failures |
| Customising | Settings in the web interface and API | Edit the YAML, add any ESPHome component |
| Guide | [Display](../guide/display.md), [web interface](standalone.md), [MQTT](mqtt.md) | [ESPHome](esphome.md) |

[ESPHome, which firmware should I use](esphome.md#which-firmware-should-i-use)
has the same comparison from the ESPHome side, and
[how it differs from the standalone firmware](esphome.md#how-it-differs-from-the-standalone-firmware)
explains what is shared.

## Installing and updating

- [Installing firmware](web-installer.md): flash either option over USB from a Chromium browser, with `zfflash`, or with `esptool`.
- [Updating firmware](releases.md): what a release contains, updates over the network and over USB, rollback, recovery.

Both firmwares read pin numbers from one generated file only: a C++ header
for the standalone firmware, a substitutions file for the ESPHome package.
Both are generated from `hardware/pinmap.yaml`.
