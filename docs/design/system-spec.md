# ZoneFlow system specification

Status: **v0.1 draft**. This file is the source of truth for hardware,
firmware, enclosure and documentation work. Change it by pull request; do not
let a subsystem drift from it silently.

## 1. Product summary

ZoneFlow is an open-source irrigation controller for standard 24 VAC sprinkler
valves. One board drives up to 8 zones plus a master valve or pump-start relay.
It connects over WiFi and Bluetooth LE, works without a cloud service, and
keeps watering on schedule when the network is down.

| Item | Requirement |
|---|---|
| Supply | 24 VAC nominal from a Class 2 sprinkler transformer, 50/60 Hz. Must survive 32 VAC RMS continuous (unloaded transformer, high line). |
| Zones | 8 switched 24 VAC outputs. A 4-zone build is an assembly variant of the same PCB (parts not fitted). |
| Master / pump | 1 additional switched 24 VAC output. |
| Load per output | 0.5 A continuous, 1.5 A inrush for 100 ms. Up to 2 zones plus master on at once (1.5 A total continuous). |
| Radio | WiFi 2.4 GHz and Bluetooth LE, PCB-antenna module with FCC/CE modular approval. |
| Sensors | Rain sensor (dry contact, NO or NC). Flow meter (pulse, dry contact or open collector, up to 200 Hz). One spare general-purpose sensor input. |
| Diagnostics | Measure total solenoid current. Detect open valve, shorted valve and loss of 24 VAC. |
| Local UI | One status LED per output, a power/status LED, a small OLED, and controls for manual run without a phone. |
| Timekeeping | Battery-backed or supercap-backed RTC so schedules survive a power cut with no network. |
| Install | Indoor or garage wall mount. Not weatherproof. |
| Manufacture | Machine-assembled SMT (JLCPCB-class). Through-hole parts are limited to terminal blocks and parts with no SMT equivalent. |
| Quantity | Tens of units per batch. |

## 2. Safety rules that override everything else

1. **Outputs are off by default.** With the MCU in reset, unpowered, crashed or
   being flashed, every valve output is off. This is enforced in hardware, not
   only in firmware.
2. **No strapping pins on outputs.** A valve output never shares an ESP32
   strapping pin or a pin that glitches at boot.
3. **A hardware output-enable** gates all output drivers. Firmware must toggle
   or assert it deliberately; a stuck firmware loses it (watchdog-style).
4. **Maximum run time.** Firmware enforces a per-zone maximum run time that a
   schedule or API call cannot exceed.
5. Only Class 2 (low voltage, power limited) circuits are on the board. No
   mains.

## 3. Hardware architecture

### 3.1 Baseline decisions

| Block | Decision | Notes |
|---|---|---|
| MCU module | ESP32-S3-WROOM-1 (PCB antenna, at least 8 MB flash) | Enough GPIO without an expander, native USB, mature in ESP-IDF and ESPHome. |
| Output stage | One isolated AC switch per output | Baseline is an opto-triac driver plus triac. The hardware lead may choose another isolated AC switch if the prior-art research supports it. |
| Logic supply | Bridge rectifier, bulk capacitor, buck converter rated at least 60 V input, 3.3 V rail | 32 VAC RMS gives about 45 V peak; leave margin. |
| USB | USB-C, native ESP32-S3 USB for flashing and logs | USB alone powers the logic so the board can be flashed on a desk. Valve outputs stay unpowered without 24 VAC. |
| Protection | Fuse or PTC on AC input, surge clamp on AC input, clamp or snubber on every output, ESD/TVS on every field terminal | Valve wires run tens of metres outdoors. |
| Current sense | One isolated measurement of total valve current | Hall sensor or current transformer; hardware lead decides. |
| AC presence | A signal that tells firmware 24 VAC is present | Lets firmware tell "USB only" from "valves powered". |
| RTC | I2C RTC with backup | |
| Display | 128x64 monochrome OLED on I2C | |
| Controls | Rotary encoder with push switch, plus one Back/Manual button | Hardware lead may substitute 3-4 buttons if it assembles better. |
| Expansion | One I2C connector (Qwiic/STEMMA QT pinout) | Future soil sensors, displays. |
| Terminals | Pluggable screw terminal blocks | 24VAC x2, COM x2, MV, Z1-Z8, sensor inputs with ground. |
| PCB | 2 or 4 layers, JLCPCB standard capabilities | Prefer JLCPCB basic parts; every BOM line carries an LCSC part number. |

### 3.2 Pin map

The pin map lives in `hardware/pinmap.yaml` once the schematic exists.
Firmware reads pin numbers only from one board header generated from, or kept
in step with, that file. Until then firmware uses a provisional map and must
not scatter pin numbers through the code.

### 3.3 Hardware deliverables

- KiCad project (schematic and PCB) that passes ERC and DRC with no
  unexplained violations.
- Schematic PDF, fabrication outputs (Gerbers, drill, pick-and-place, BOM with
  LCSC numbers), STEP model of the assembled board, board renders.
- A design-notes document with the calculations: power budget, thermal check
  for output devices, regulator input range, protection part selection.
- A bring-up and test procedure.

## 4. Firmware

Two supported options. Both use the same hardware and pin map.

### 4.1 ZoneFlow firmware (standalone)

ESP-IDF based. Works with no hub and no cloud.

- Zone driver with the safety rules of section 2.
- Scheduler: programs with start times, weekday/interval/odd-even rules,
  per-zone durations, seasonal adjustment percentage, rain delay, sensor
  inhibit. Runs from the RTC when the network is down.
- Persistent configuration with versioned schema.
- WiFi provisioning over Bluetooth LE, with a fallback access point.
- Local web UI served by the device, plus a documented REST API and a live
  status stream.
- MQTT with Home Assistant discovery.
- OTA update with rollback.
- OLED and local controls: status, manual run, network info.
- Flow and current diagnostics: per-zone learned current, fault reporting.
- Host-side unit tests for the scheduler and other pure logic.

### 4.2 ESPHome configuration

A maintained ESPHome package for users who run Home Assistant: all outputs,
sensors, display and controls mapped, using the ESPHome `sprinkler` component,
with BLE provisioning.

### 4.3 Firmware tooling

A browser-based installer on the project site (WebSerial) that flashes either
firmware option onto a board over USB, and release builds produced by CI.

## 5. Enclosure

3D-printable indoor wall-mount enclosure, parametric source in the repo.

- Prints without supports on a 220 x 220 mm bed in PLA or PETG.
- Separate wiring compartment cover so the user wires valves without exposing
  the electronics.
- Window or opening for the OLED, access to the controls, light pipes or
  openings for the LEDs.
- Wall-mount keyholes, cable entry at the bottom, strain relief.
- Deliverables: source, STL and 3MF exports, STEP if the tool allows, assembly
  drawing or renders, print settings.

## 6. Documentation

Published with GitHub Pages from this repository.

- Landing page: what it is, features, photos or renders, how to get one.
- Getting started: wiring, first power-up, provisioning, first schedule.
- Hardware reference: schematic, BOM, design notes, ordering from a fab.
- Firmware guides for both options, API reference.
- Enclosure printing and assembly.
- Troubleshooting and safety notes.

## 7. Licences

| Part | Licence |
|---|---|
| Hardware and enclosure | CERN-OHL-P-2.0 |
| Firmware and tools | Apache-2.0 |
| Documentation | CC-BY-4.0 |

## 8. Design verification limits

This design is produced without physical prototypes. Every release states
which checks were done (ERC, DRC, simulation, calculation, firmware host
tests) and which need a bench: thermal behaviour, EMC, surge immunity, RF
performance. Revision A boards are engineering prototypes until bench-tested.
