# Firmware

!!! warning "Neither option has run on hardware"
    Both are written, build in CI and pass their host tests. No board exists yet. See
    [issue #6](https://github.com/syntroniks/zoneflow/issues/6), [issue #7](https://github.com/syntroniks/zoneflow/issues/7) and [issue #8](https://github.com/syntroniks/zoneflow/issues/8).

There are two options. Both use the same hardware, the same pin map and the same C++
code for the safety-critical parts (the output stage, the zone driver and the current
checks). The [ESPHome page](esphome.md#which-firmware-should-i-use) has a longer
comparison.

| | ZoneFlow firmware | ESPHome package |
|---|---|---|
| Base | ESP-IDF | ESPHome |
| Needs a hub | No | Home Assistant |
| Needs a cloud service | No | No |
| Schedules without the network | Yes | Yes |
| Page | [Standalone firmware](standalone.md) | [ESPHome](esphome.md) |

The [web installer](web-installer.md) flashes a release over USB from a Chromium
browser. [Releases and updating](releases.md) covers updates, rollback and
recovery.
The REST API of the standalone firmware is covered in the
[API reference](api.md).

Firmware reads pin numbers from one generated file only: a C++ header for the
standalone firmware, a substitutions file for the ESPHome package. Both are generated
from `hardware/pinmap.yaml`.
