# Schematic

!!! warning "Paper design"
    The schematic is captured and passes the checks named below. No board has
    been made and nothing has been measured. The board laid out from it is on
    the [PCB layout](layout.md) page.

The revision A schematic is a KiCad 10 project in
[`hardware/zoneflow-a/`](https://github.com/syntroniks/zoneflow/tree/main/hardware/zoneflow-a).
It is generated from text: nobody draws it in the KiCad editor. Open
`zoneflow-a.kicad_pro` in KiCad 10 to look at it, or read the exports here.

- [Schematic PDF, 20 pages](zoneflow-a/zoneflow-a-schematic.pdf)
- The circuit, part by part, with every calculation: [hardware design](../design/hardware-design.md)
- What each footprint was checked against: [`design/footprints.md`](https://github.com/syntroniks/zoneflow/blob/main/hardware/zoneflow-a/design/footprints.md)

## Block diagram

The first page. Each box is one sheet, and a pin name on a box is the net name.

[![Block diagram of ZoneFlow revision A](zoneflow-a/schematic-svg/zoneflow-a.svg)](zoneflow-a/schematic-svg/zoneflow-a.svg)

## Sheets

| Sheet | Content |
|---|---|
| [Power](zoneflow-a/schematic-svg/zoneflow-a-Power.svg) | 24 VAC input, fuses, surge clamp, half-wave rectifier, bulk capacitor, 100 V buck converter to 5.28 V, input monitor |
| [USB](zoneflow-a/schematic-svg/zoneflow-a-USB.svg) | USB-C receptacle, ESD array, power OR-ing of USB and the buck output, 3.3 V regulator |
| [MCU](zoneflow-a/schematic-svg/zoneflow-a-MCU.svg) | ESP32-S3-WROOM-1 module, supervisor, reset and boot buttons, status LED |
| [Failsafe](zoneflow-a/schematic-svg/zoneflow-a-Failsafe.svg) | Keep-alive detector A, drive-enable gate, over-current comparator and latch |
| [CurrentSense](zoneflow-a/schematic-svg/zoneflow-a-CurrentSense.svg) | Shunt, two amplifier ranges, the comparator reference voltages, zero-cross detector |
| [Expander](zoneflow-a/schematic-svg/zoneflow-a-Expander.svg) | 16-bit I/O expander, valve-common relay and keep-alive detector B |
| [Z1](zoneflow-a/schematic-svg/zoneflow-a-Z1.svg), [Z2](zoneflow-a/schematic-svg/zoneflow-a-Z2.svg), [Z3](zoneflow-a/schematic-svg/zoneflow-a-Z3.svg), [Z4](zoneflow-a/schematic-svg/zoneflow-a-Z4.svg), [Z5](zoneflow-a/schematic-svg/zoneflow-a-Z5.svg), [Z6](zoneflow-a/schematic-svg/zoneflow-a-Z6.svg), [Z7](zoneflow-a/schematic-svg/zoneflow-a-Z7.svg), [Z8](zoneflow-a/schematic-svg/zoneflow-a-Z8.svg), [MV](zoneflow-a/schematic-svg/zoneflow-a-MV.svg) | The 24 VAC output channel: PTC, triac, snubber, TVS, opto-triac and indicator LED. One sheet, used nine times |
| [Sensors](zoneflow-a/schematic-svg/zoneflow-a-Sensors.svg) | Rain, flow and spare inputs with their protection, and the switched sensor supply |
| [RTC](zoneflow-a/schematic-svg/zoneflow-a-RTC.svg) | Real-time clock, crystal and coin cell |
| [Display](zoneflow-a/schematic-svg/zoneflow-a-Display.svg) | Display connector and backlight switch, encoder and the two buttons |
| [Connectors](zoneflow-a/schematic-svg/zoneflow-a-Connectors.svg) | Field terminals, Qwiic connector, debug header, test pads, mounting holes |

References in the output channel are 101 to 111 style. The first digit is the
channel and 9 is the master valve: R301 is in zone 3 and Q901 is the master
valve triac.

## Build variants

The schematic as drawn is the 8-zone board. The KiCad variant `4-zone` does
not fit channels 5 to 8 (44 parts), the terminal header J3 and the strap
resistor R28. Firmware reads the strap and runs four zones.

## What was checked

`make` in `hardware/zoneflow-a/` runs these and stops at the first failure.
The results are from the build of 2026-11-10 with KiCad 10.0.8.

| Check | What it proves | Result |
|---|---|---|
| Netlist parity | The netlist KiCad extracts from the sheets equals [`hardware/netlist.yaml`](https://github.com/syntroniks/zoneflow/blob/main/hardware/netlist.yaml) pin for pin: reference, value, footprint, LCSC number and the net on every pin | 353 components and 200 nets match |
| Build parity | KiCad's BOM for the 8-zone and the 4-zone build lists exactly the parts the netlist fits in that build | 2 builds match |
| Page lint | No text overlaps other text, no line crosses text, no symbol overlaps a symbol, nothing is off the page | 20 pages, no collision |
| ERC | KiCad's electrical rules check, errors and warnings, with no check switched off | 0 violations |

These checks compare files. They do not show that the circuit is right; the
[design review](../design/hardware-review-a1.md) is about that. They show
that the schematic is the reviewed netlist and that it is readable.

Continuous integration repeats the comparison of the committed netlist and
BOMs with `netlist.yaml` on every pull request, and a second job regenerates
the schematic with KiCad 10 and fails if the committed files differ.

## Changing the circuit

1. Edit `hardware/netlist.yaml`. For a part number that is new, add it to
   `hardware/bom-preliminary.csv` and to `hardware/zoneflow-a/design/parts.toml`.
2. If a part or a pin is new, place or wire it in
   `hardware/zoneflow-a/design/circuit.py`. A value or part-number change
   needs no drawing change.
3. Run `make -C hardware design-sync` and then `make` in
   `hardware/zoneflow-a/`. Commit the generated files with the change.

The flow is described in [hardware toolchain](../design/hardware-toolchain.md).
