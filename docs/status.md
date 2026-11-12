# Status and roadmap

This page says what exists, what was checked and how, and what cannot be
known until a prototype is built. It was written on 2026-11-10 from the
repository at that date.

## Where the project is

ZoneFlow is a **paper design**. Every part of it has been designed and
reviewed on paper, and the software has been tested on a PC. Nothing has been
built, flashed or measured.

| Area | Done | Checked by | Not done |
|---|---|---|---|
| Circuit | Rev A design with part numbers, calculations and SPICE simulations; independent review A1 and the decisions on its findings | Calculation, ngspice simulation, review against data sheets | Bench tests (below) |
| Schematic | 20-page KiCad 10 schematic, generated from `hardware/netlist.yaml` | ERC with no violations; netlist parity, pin for pin; both builds' BOMs against the netlist; page lint ([what was checked](hardware/schematic.md#what-was-checked)) | A second review before fabrication, [issue #11](https://github.com/syntroniks/zoneflow/issues/11) |
| PCB | Four-layer layout of revision A, generated from text: placement, hand-routed power, valve and fail-safe copper, the rest autorouted; Gerbers, drill, BOM and placement files for both builds | DRC with schematic parity and JLCPCB's limits; the mechanical contract; the layout rules of the fail-safe analysis ([what was checked](design/hardware-layout.md#1-what-was-checked-and-the-result)); the placement files against JLCPCB's library footprints; an [independent review](design/hardware-layout-review.md) of the files, which changed the board | The seven-item checklist the review leaves for a person, [issue #11](https://github.com/syntroniks/zoneflow/issues/11); a board |
| Enclosure | Parametric model, 8 printed parts, drilling template, a printable [test-fit kit](enclosure/test-fit.md) generated from the board file | Interference and clearance check on the models (`enclosure/tools/check.py`) | A print, a fit test, a thermal test, [issue #9](https://github.com/syntroniks/zoneflow/issues/9) |
| Standalone firmware | Zone driver, scheduler, self-tests, web interface, REST API, MQTT, OTA, display interface | Host unit tests; `idf.py build` in CI; display simulator; web interface against a mock; [browser demo](try-it.md) on simulated hardware | Any run on a board, [issue #6](https://github.com/syntroniks/zoneflow/issues/6) |
| ESPHome package | Package and two external components on the shared C++ code | Compiles for the board; host tests of the logic | Any run on a board |
| Release tooling | Web installer, release workflow, `zfflash` | CI checks of image layout and checksums | A published release, [issue #8](https://github.com/syntroniks/zoneflow/issues/8) |
| Documentation | This site | `mkdocs build --strict` and an internal link check in CI | Photos and measured values, after a prototype |

## What only a prototype can answer

The [hardware design](design/hardware-design.md#20-design-risk-register)
keeps a register of 27 risks that only a bench test can close, and a
[bring-up plan](design/hardware-design.md#21-bring-up-test-plan) of 18 steps
for the first board. In short:

| Question | Risk numbers |
|---|---|
| Do the triacs, the processor module and the regulator stay within their temperature limits with two zones and the master on for two hours in a 50 °C room, in the closed enclosure? | 1, 2, 3, 19 |
| Does an output survive a dead short on 20, 40 and 75 VA transformers, cold and hot, and does the relay survive breaking the fault current? | 4, 9, 16 |
| Does the over-current cut-off trip on real shorts and not on switching noise? | 5 |
| How much surge do the valve, power and sensor terminals survive? | 6, 18, 25 |
| Do small loads (a 50 mA pump-start relay coil) and real solenoids switch cleanly, hot and cold? | 7, 8 |
| Do both keep-alive detectors enable and release within their windows over temperature, and does each single fault behave as the analysis says? | 10, 22, 23, 27 |
| Are the buck regulator, the supervisor and the current measurement stable and accurate with WiFi transmitting, and does the input resistor survive 1000 power-ups at the peak of 32 VAC? | 11, 12, 24, 26 |
| What does the board's half-wave load do to a small transformer? | 13 |
| WiFi range in the enclosure, and a pre-compliance emissions scan | 14 |
| Does the display panel match its drawing, and is 1.6 mA enough to see an output LED through the light pipe? | 15, 20 |
| How fast does a fuse open at the fault current a USB ground path would carry? | 17 |
| What happens if the regulator's switch fails short? | 21 |

The firmware has its own list of things to measure on the first boards: the
self-test limits, the longest control-loop stall during a flash write, the
time a login takes, the encoder feel, and the QR code under a phone camera.
They are in [firmware architecture](design/firmware-architecture.md) and
[display interface](design/display-ui.md#12-what-is-verified).

## Roadmap

The order is fixed by what each step needs from the one before it.

1. **Review before fabrication**, [issue #11](https://github.com/syntroniks/zoneflow/issues/11): an independent review of the schematic and the layout. The [layout review](design/hardware-layout-review.md) is written; its human pre-order checklist is open.
2. **First prototypes**: a small batch, assembled by JLCPCB, for the bring-up plan and the risk register. There is no issue for this yet; follow the [progress log, issue #1](https://github.com/syntroniks/zoneflow/issues/1).
3. **Firmware on hardware**, [issue #6](https://github.com/syntroniks/zoneflow/issues/6): set the self-test limits and current thresholds from measurements, tune the display and the encoder.
4. **First firmware release**, [issue #8](https://github.com/syntroniks/zoneflow/issues/8), and the enclosure printed and fitted, [issue #9](https://github.com/syntroniks/zoneflow/issues/9).
6. **This manual corrected from the prototype**: photos, measured values, the notes marked "to be completed" on each page.

Options kept open for later, from the [specification](design/system-spec.md):
a board profile for the OpenSprinkler firmware, and a Matter irrigation
endpoint.

## How to follow

- [Progress log, issue #1](https://github.com/syntroniks/zoneflow/issues/1).
- [Open issues](https://github.com/syntroniks/zoneflow/issues).
- [Contributing](contributing.md): reviews of the circuit, the schematic and the firmware are the most useful help now.
