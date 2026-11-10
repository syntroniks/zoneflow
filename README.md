# ZoneFlow

[![docs](https://github.com/syntroniks/zoneflow/actions/workflows/docs.yml/badge.svg)](https://github.com/syntroniks/zoneflow/actions/workflows/docs.yml)

Open-source irrigation controller for 24 VAC sprinkler valves. Up to 8 zones
plus a master valve, WiFi and Bluetooth LE, no cloud required.

> **Status: early design.** Nothing here has been built or bench-tested yet.
> Do not order boards from this repository until a release says otherwise.

Documentation: <https://syntroniks.github.io/zoneflow/>

## What is planned

- 8 zone outputs and 1 master-valve/pump output, 24 VAC
- ESP32-S3 with WiFi and Bluetooth LE
- Rain sensor and flow meter inputs
- Valve current monitoring: open and shorted valve detection
- 2.0-2.4 inch colour display, rotary encoder and buttons, per-zone LEDs
- Real-time clock so schedules run without a network
- Two firmware options: standalone ZoneFlow firmware, or ESPHome
- 3D-printable indoor enclosure
- Designed for machine assembly in small batches

## Repository layout

| Path | Contents |
|---|---|
| `docs/` | Documentation site source (MkDocs) and design documents |
| `mkdocs.yml`, `pyproject.toml`, `uv.lock` | Site configuration and pinned docs dependencies |
| `docs/design/system-spec.md` | System specification, the source of truth |
| `hardware/` | KiCad project, BOM, fabrication outputs |
| `firmware/zoneflow-fw/` | Standalone firmware (ESP-IDF) |
| `firmware/esphome/` | ESPHome configuration |
| `enclosure/` | Parametric enclosure source and print files |
| `tools/` | Build and release scripts |

## Licences

Hardware and enclosure: CERN-OHL-P-2.0. Firmware and tools: Apache-2.0.
Documentation: CC-BY-4.0. See the `LICENSE` file in each directory.
