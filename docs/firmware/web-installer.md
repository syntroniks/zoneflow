# Web installer

!!! note "Not available yet"
    This page is a placeholder. The installer is tracked in [issue #8](https://github.com/syntroniks/zoneflow/issues/8).
    There is no firmware to flash and no board to flash it to.

## What it will do

The project plans a browser-based installer on this site. It will use
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) and the WebSerial
API to flash a ZoneFlow board over USB-C, with no software to install.

You will choose one of the two firmware options:

- [Standalone firmware](standalone.md)
- [ESPHome](esphome.md)

The firmware images will come from release builds produced by CI. The
installer will not be built from anything that was not built and published by
the project's release process.

## What it needs

- A browser with WebSerial support. Chromium-based browsers have it. Others may not.
- A USB-C data cable.
- A ZoneFlow board. The ESP32-S3 has native USB, and USB alone powers the logic.

Until then, there is nothing to install. This page will be replaced when the
installer exists.
