# Firmware

!!! warning "No firmware exists yet"
    Neither option has been written or run on hardware. See
    [issue #6](https://github.com/syntroniks/zoneflow/issues/6), [issue #7](https://github.com/syntroniks/zoneflow/issues/7) and [issue #8](https://github.com/syntroniks/zoneflow/issues/8).

Two options are planned. Both use the same hardware and the same pin map.

| | ZoneFlow firmware | ESPHome package |
|---|---|---|
| Base | ESP-IDF | ESPHome |
| Needs a hub | No | Home Assistant |
| Needs a cloud service | No | No |
| Page | [Standalone firmware](standalone.md) | [ESPHome](esphome.md) |

Both can be flashed with the [web installer](web-installer.md) once it exists.
The REST API of the standalone firmware is covered in the
[API reference](api.md).

Firmware reads pin numbers from one board header only. It never scatters them
through the code.
