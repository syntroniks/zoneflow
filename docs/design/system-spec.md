# ZoneFlow system specification

Status: **v0.6 draft**. This file is the source of truth for hardware,
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
| Sensors | Rain sensor (dry contact, NO or NC). Flow meter (pulse, dry contact or open collector, up to 200 Hz). One spare general-purpose sensor input. A current-limited sensor supply terminal for powered flow meters. |
| Diagnostics | Measure total solenoid current. Detect open valve, shorted valve and loss of 24 VAC. A shorted valve or wire must not destroy the output; the board shuts the zone off and reports it. |
| Local UI | One status LED per output, a power/status LED, a colour display large enough for a real menu interface (zone status, manual run, schedule viewing and editing, setup), and controls to drive it without a phone. |
| Timekeeping | Battery-backed or supercap-backed RTC so schedules survive a power cut with no network. |
| Install | Indoor or garage wall mount. Not weatherproof. |
| Operating temperature | -20 to +50 °C ambient, non-condensing. The electronics must be rated for the temperature this gives inside the enclosure. |
| Manufacture | Machine-assembled SMT (JLCPCB-class). Through-hole parts are limited to terminal blocks and parts with no SMT equivalent. |
| Quantity | Tens of units per batch. |

## 2. Safety rules that override everything else

1. **Outputs are off by default.** With the MCU in reset, unpowered, crashed or
   being flashed, every valve output is off. This is enforced in hardware, not
   only in firmware.
2. **No strapping pins on outputs.** A valve output never shares an ESP32
   strapping pin or a pin that glitches at boot.
3. **A hardware output-enable** gates all output drivers. Firmware must keep
   toggling a keep-alive signal; when the toggling stops (crash, hang, reset,
   flashing), gate drive to every triac is removed within about one second,
   with no firmware involved. The same signal holds the output expander in
   reset, so no output state survives a restart. A second detector on the
   same keep-alive signal, sharing no part with the first, releases the relay
   of rule 6 in about the same time. No single component fault in either
   detector, or in the logic behind them, may leave a valve on after the
   keep-alive stops. Firmware tests both detectors before and after every
   run.
4. **Maximum run time.** Firmware enforces a per-zone maximum run time that a
   schedule or API call cannot exceed. The limit holds per zone, not per run:
   on-time adds up across back-to-back runs (several programs, repeated manual
   runs, pause and resume) until the zone has been closed for a minimum rest
   time. A zone that has been open for its maximum then stays closed for that
   rest time before it can open again. The rest time is configurable, with a
   default of 30 minutes.
5. Only Class 2 (low voltage, power limited) circuits are on the board. No
   mains.
6. **A hardware cut-off in the valve common.** A relay feeds the COM
   terminals. It is closed only while the output-enable is active, the
   relay's own keep-alive detector is active and firmware has asked for it. It
   removes power from every valve when an output device has failed short,
   which removing gate drive cannot do.

## 3. Hardware architecture

### 3.1 Baseline decisions

| Block | Decision | Notes |
|---|---|---|
| MCU module | ESP32-S3-WROOM-1-N16R8 (PCB antenna, 16 MB flash, 8 MB PSRAM for the display interface) | Native USB, mature in ESP-IDF and ESPHome. With the SPI display the module has too few GPIOs for nine direct valve outputs, so the valve outputs are on an I2C expander that the output-enable holds in reset. |
| Output stage | One triac per output, OpenSprinkler-style topology | One 24 VAC leg is circuit ground, the other is valve COM. Each valve sits between COM and its triac; all triac returns share one path to ground through the current shunt. Triac rated at least 1 A RMS with a high surge rating; no MAC97-class parts. Each gate is fired from the triac's own T2 terminal through an opto-triac, because the well-rated triacs available are three-quadrant parts that positive logic drive cannot fire on both half-cycles. The opto-triac LEDs are powered through the fail-safe gate of section 2, not from an unguarded pin. See `prior-art.md` for the evidence and `hardware-design.md` section 6 for the analysis. |
| Logic supply | Half-wave rectifier, bulk capacitor sized for half-wave ripple at peak WiFi load, buck converter rated at least 60 V input, 5 V bus shared with USB, linear regulator to the 3.3 V rail. A clamp on the converter output and a fuse in its input, so that a converter whose switch has failed short cannot put the rectified supply on the 5 V and 3.3 V parts | 32 VAC RMS gives about 45 V peak, and the 45 V regulators used by older designs have no margin. A failed regulator must fail safe and must not destroy the board silently: with the logic over its ratings nothing can be said about the keep-alive. |
| USB | USB-C, native ESP32-S3 USB for flashing and logs | USB alone powers the logic so the board can be flashed on a desk. Valve outputs stay unpowered without 24 VAC. Logic ground is tied to one 24 VAC leg, so the design notes must state what happens when USB and 24 VAC are connected together and what the user must not do (for example earthing the other transformer leg). |
| Protection | A fuse in each leg of the AC input, surge clamp on AC input, surge clamp on the valve common. Per output: RC snubber, bidirectional TVS, and a series element (PTC, fuse or resistor) that limits fault current. Clamp on every sensor terminal. PCB spark gaps on field terminals where layout allows | Valve wires run tens of metres outdoors. Blown triacs from shorted or surge-hit wiring are the most common field failure in existing designs. |
| Current sense | One low-side shunt in the shared triac return, amplified around a bias voltage and sampled as a waveform by ADC1 | Measures total current; firmware computes RMS over whole mains cycles and attributes it to zones at turn-on. The bias keeps the signal out of the non-linear region of the ESP32 ADC near 0 V. A hardware comparator on the same signal latches and removes gate drive without firmware. |
| AC presence | A signal that tells firmware 24 VAC is present | Lets firmware tell "USB only" from "valves powered". |
| RTC | I2C RTC with coin-cell backup | |
| Valve common | Relay in the feed to the COM terminals, held by its own keep-alive detector | Safety rules 3 and 6. |
| Display | 2.0 to 2.4 inch colour IPS TFT, 320x240, ST7789-class controller on SPI, no touch, PWM-dimmable backlight | Chosen by the project owner: the small screens on existing controllers limit the local interface too much. |
| Controls | Rotary encoder with push switch, plus Back and Manual/Home buttons | The display has no touch, so these are the only local inputs. |
| Expansion | One I2C connector (Qwiic/STEMMA QT pinout) | Future soil sensors, displays. |
| Terminals | Pluggable screw terminal blocks | 24VAC x2, COM x2, MV, Z1-Z8, sensor inputs with ground. |
| PCB | 2 or 4 layers, JLCPCB standard capabilities | Prefer JLCPCB basic parts; every BOM line carries an LCSC part number. |

### 3.2 Pin map

The pin map lives in `hardware/pinmap.yaml`.
Firmware reads pin numbers only from one board header generated from that
file, and must not scatter pin numbers through the code.

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
- WiFi provisioning over Bluetooth LE (ESP-IDF unified provisioning, Security
  2), with a fallback access point. BLE memory is released after setup.
- No lock to a single access point or channel; reconnect works on mesh
  networks. Credentials are never sent or stored in plain text on the network.
- Local web UI served by the device, plus a documented REST API and a live
  status stream.
- MQTT with Home Assistant discovery.
- OTA update with rollback.
- Display interface (LVGL) driven by the encoder and buttons: home screen with
  zone status and next run, manual zone and program runs, schedule viewing and
  editing, rain delay, sensor and fault status, network setup and info.
  Backlight dims and times out.
- Flow and current diagnostics: per-zone learned current, fault reporting.
- Host-side unit tests for the scheduler and other pure logic.

### 4.2 ESPHome configuration

A maintained ESPHome package for users who run Home Assistant: all outputs,
sensors, display and controls mapped, using the ESPHome `sprinkler` component,
with Improv BLE provisioning. The stock component has no time-of-day
scheduler, flow sensing, fault detection or rain input, so the package adds an
on-device scheduler, flow metering with leak alarm, current-fault shut-off with
inrush blanking, rain lockout and a maximum-run-time guard. It drives the
hardware keep-alive signal.

### 4.3 Firmware tooling

A browser-based installer on the project site (WebSerial) that flashes either
firmware option onto a board over USB, and release builds produced by CI.

A browser demo on the project site: the standalone firmware's core, display
and REST API compiled to WebAssembly and run on simulated hardware, with the
web interface as the controller serves it. It lets people try the product
without a board. It is a simulation: it counts as none of the checks of
section 8, and the site says so wherever it offers the demo.

### 4.4 Options kept open, not in the first release

- A board profile for the OpenSprinkler firmware (GPL-3.0), which would bring
  its app and Home Assistant integration. Nothing in the hardware should
  prevent it.
- A Matter Water Valve / Irrigation System endpoint.

## 5. Enclosure

3D-printable indoor wall-mount enclosure, parametric source in the repo.

- Prints without supports on a 220 x 220 mm bed in PLA or PETG.
- Separate wiring compartment cover so the user wires valves without exposing
  the electronics.
- Window or opening for the display, access to the controls, light pipes or
  openings for the LEDs.
- Wall-mount keyholes, cable entry at the bottom, strain relief.
- Deliverables: source, STL and 3MF exports, STEP if the tool allows, assembly
  drawing or renders, print settings.

## 6. Documentation

Published with GitHub Pages from this repository.

- Landing page: what it is, features, photos or renders, how to get one.
- The browser demo of section 4.3, with a page that states what it simulates.
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
