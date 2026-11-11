# ZoneFlow

[![docs](https://github.com/syntroniks/zoneflow/actions/workflows/docs.yml/badge.svg)](https://github.com/syntroniks/zoneflow/actions/workflows/docs.yml)

Open-source controller for 24 VAC sprinkler valves: 8 zones plus a master
valve or pump-start relay, a colour display with a knob, WiFi and Bluetooth
LE, and no cloud service.

> **Status: designed on paper. Not built. Not tested.**
> No ZoneFlow board has been made and no firmware has run on one. Every
> figure in this repository is a design value, a calculation or a simulation
> result. **Do not order boards**: the PCB layout is not finished. ZoneFlow
> has no safety, EMC or radio certification.

Documentation and manual: <https://syntroniks.github.io/zoneflow/>

![Enclosure render, front](docs/assets/enclosure/render-front.png)

## What exists

| Part | State |
|---|---|
| Circuit design | Written with calculations and simulations, reviewed once, findings resolved ([hardware design](docs/design/hardware-design.md)) |
| Schematic | KiCad 10, generated from `hardware/netlist.yaml`, ERC clean ([PDF](hardware/zoneflow-a/output/zoneflow-a-schematic.pdf)) |
| Bill of materials | LCSC number, stock and price on every line, checked 2026-11-10 |
| PCB layout | In progress ([#5](https://github.com/syntroniks/zoneflow/issues/5)) |
| Enclosure | Parametric OpenSCAD model with STL and 3MF exports; not printed |
| Standalone firmware | ESP-IDF; web interface, REST API, MQTT, OTA, display menus; host-tested, never run on a board |
| ESPHome package | Compiles for the board; host-tested, never run on a board |
| Release tooling | Web installer, release workflow, `zfflash`; no release yet |

## Features, as designed

- 8 zone outputs and 1 master-valve or pump-start output, 24 VAC, 0.5 A each continuous; a 4-zone build of the same board
- Powered from one Class 2 sprinkler transformer; input designed to survive 32 VAC
- Valve current measured: open valve, shorted valve, current with every valve off and loss of 24 VAC reported in plain words
- Hardware fail-safe: two keep-alive detectors and a relay in the valve common turn the valves off within about a second if the firmware stops
- Rain sensor, flow meter (with leak alarm) and spare inputs; current-limited 5 V supply for powered meters
- 2.0 inch colour display, rotary encoder and two buttons, one light per output
- Real-time clock with a CR2032 cell: schedules run without a network
- Two firmware options: standalone (ESP-IDF) or ESPHome
- 3D-printable indoor wall-mount enclosure with a separate wiring compartment

## Repository layout

| Path | Contents |
|---|---|
| `docs/` | Documentation site source (MkDocs): the user manual, and design documents in `docs/design/` |
| `docs/design/system-spec.md` | System specification, the source of truth |
| `hardware/` | KiCad project, netlist, pin map, BOM, simulations |
| `firmware/zoneflow-fw/` | Standalone firmware (ESP-IDF) and its web interface |
| `firmware/esphome/` | ESPHome package and its external components |
| `enclosure/` | Parametric enclosure source and print files |
| `tools/` | Build, release and documentation scripts |
| `mkdocs.yml`, `pyproject.toml`, `uv.lock` | Site configuration and pinned docs dependencies |

## Follow and contribute

- Progress log: [issue #1](https://github.com/syntroniks/zoneflow/issues/1)
- Status and roadmap: <https://syntroniks.github.io/zoneflow/status/>
- How to contribute: [docs/contributing.md](docs/contributing.md). Reviews of the circuit, the schematic and the firmware help most now.

## Licences

Hardware and enclosure: CERN-OHL-P-2.0. Firmware and tools: Apache-2.0.
Documentation: CC-BY-4.0. See the `LICENSE` file in each directory.
