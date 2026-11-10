---
title: ZoneFlow
hide:
  - toc
---

<div class="zf-hero" markdown>

<img src="assets/logo.svg" alt="" width="72" height="72">

# ZoneFlow

<p class="zf-lead">An open-source controller for 24 VAC sprinkler valves. Up to 8 zones plus a master valve or pump relay. WiFi and Bluetooth LE. No cloud required.</p>

</div>

!!! danger zf-status "Status: early design. Not built. Not bench-tested."
    - No board has been made. No firmware has run on one.
    - No test results exist. Every figure on this site is a requirement or a plan.
    - **Do not order boards yet.** A release will say when that changes.
    - ZoneFlow has no safety or radio certification.

    Read the [safety notes](safety.md) before you build anything from this
    project. Follow progress in the [tracking issue](https://github.com/syntroniks/zoneflow/issues/1).

## What it is

ZoneFlow is a controller for standard 24 VAC sprinkler valves. One board drives
up to 8 zones plus a master valve or pump-start relay. It keeps watering on
schedule when the network is down.

## Planned features

These come from the [system specification](design/system-spec.md). They are
requirements, not shipped features.

- 8 switched 24 VAC zone outputs. A 4-zone build is an assembly variant of the same board.
- 1 master-valve or pump-start output.
- Powered from a Class 2 sprinkler transformer, 24 VAC nominal, 50/60 Hz.
- ESP32-S3 module with WiFi (2.4 GHz) and Bluetooth LE.
- Rain sensor input and flow meter input. One spare sensor input.
- Valve current monitoring: detects an open valve, a shorted valve and loss of 24 VAC.
- Outputs are off by default, enforced in hardware.
- Maximum run time per zone, enforced in firmware.
- 2.0 to 2.4 inch colour display with a rotary encoder and buttons, and one LED per output.
- Battery-backed or supercap-backed real-time clock, so schedules survive a power cut with no network.
- USB-C for flashing and logs. USB alone powers the logic, so you can flash on a desk.
- I2C expansion connector (Qwiic/STEMMA QT pinout).
- 3D-printable indoor wall-mount enclosure.
- Designed for machine assembly in small batches.

## Two firmware options

Both use the same hardware.

<div class="grid cards" markdown>

**ZoneFlow firmware**

Standalone, ESP-IDF based. Works with no hub and no cloud.

- Scheduler with weekday, interval and odd-even rules, seasonal adjustment, rain delay and sensor inhibit
- WiFi provisioning over Bluetooth LE
- Local web UI and a documented REST API
- MQTT with Home Assistant discovery
- OTA update with rollback

[Standalone firmware](firmware/standalone.md)

**ESPHome package**

For people who already run Home Assistant.

- All outputs, sensors, display and controls mapped
- Uses the ESPHome `sprinkler` component
- BLE provisioning

[ESPHome package](firmware/esphome.md)

</div>

A browser-based [web installer](firmware/web-installer.md) flashes the standalone
firmware over USB once a release is published. The ESPHome package is not built
yet. See [Releases and updating](firmware/releases.md).

## Where to start

- [Safety notes](safety.md): read these first.
- [Getting started](getting-started/index.md): wiring, first power-up, provisioning, first schedule. Not written yet.
- [Hardware](hardware/index.md): what the board is meant to contain.
- [Design documents](design/index.md): the specification.
- [Contributing](contributing.md): how to help.

## Licences

| Part | Licence |
|---|---|
| Hardware and enclosure | CERN-OHL-P-2.0 |
| Firmware and tools | Apache-2.0 |
| Documentation | CC-BY-4.0 |
