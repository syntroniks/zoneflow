# Status and roadmap

This page says what exists, what was checked and how, and what cannot be
known until a prototype is built. It was written on 2026-11-10 and last
brought up to date on 2026-11-12 from the repository at that date, by the
[repository audit](design/repository-audit.md).

## Where the project is

ZoneFlow is a **paper design**. Every part of it has been designed and
reviewed on paper, and the software has been tested on a PC. Nothing has been
built, printed, flashed or measured.

**Do not order boards yet.** The revision A layout is finished and has had an
independent review. Three things stand between it and a prototype order: a
person goes through the [pre-order review](hardware/review/index.md), a
sample display panel confirms the flat cable's contact side and pin order,
and the owner decides.

| Area | Done | Checked by | Not done |
|---|---|---|---|
| Circuit | Rev A design with part numbers, calculations and SPICE simulations; independent reviews [A1](design/hardware-review-a1.md) and [A2](design/hardware-review-a2.md), the decisions on their findings, and the [answers](design/hardware-answers-a2.md) to the firmware's questions | Calculation, ngspice simulation, fault injection into a simulation built from the netlist, review against data sheets | Bench tests (below) |
| Schematic | 20-page KiCad 10 schematic, generated from `hardware/netlist.yaml` | ERC with no violations; netlist parity, pin for pin; both builds' BOMs against the netlist; page lint ([what was checked](hardware/schematic.md#what-was-checked)) | The datasheet checks of the pre-order review: whether each pin number in the netlist is the maker's, [issue #11](https://github.com/syntroniks/zoneflow/issues/11) |
| PCB | Four-layer layout of revision A, generated from text: placement, hand-routed power, valve and fail-safe copper, the rest autorouted; Gerbers, drill, BOM and placement files for both builds | DRC with schematic parity and JLCPCB's limits; the mechanical contract; the layout rules of the fail-safe analysis ([what was checked](design/hardware-layout.md#1-what-was-checked-and-the-result)); the placement files against JLCPCB's library footprints; an [independent review](design/hardware-layout-review.md) of the files, which changed the board | The seven-item checklist the review leaves for a person, [issue #11](https://github.com/syntroniks/zoneflow/issues/11); a sample display panel; a board, [issue #33](https://github.com/syntroniks/zoneflow/issues/33) |
| Enclosure | Parametric model, 8 printed parts, drilling template, a printable [test-fit kit](enclosure/test-fit.md) generated from the board file | Interference and clearance check on the models (`enclosure/tools/check.py`) | A print, a fit test, a thermal test, [issue #9](https://github.com/syntroniks/zoneflow/issues/9) |
| Standalone firmware | Zone driver, scheduler, self-tests, web interface, REST API, MQTT, OTA, display interface | Host unit tests; a [soak simulation](design/firmware-architecture.md) of both firmwares on a simulated board; `idf.py build` in CI; display simulator; web interface against a mock; [browser demo](try-it.md) on simulated hardware | Any run on a board, [issue #6](https://github.com/syntroniks/zoneflow/issues/6) |
| ESPHome package | Package and two external components on the shared C++ code | Compiles for the board; host tests of the logic | Any run on a board |
| Release tooling | Web installer, release workflow, `zfflash` | CI checks of image layout and checksums | A published release, [issue #8](https://github.com/syntroniks/zoneflow/issues/8) |
| Cost | A [landed-cost estimate](hardware/cost.md) for 5 to 100 units of both builds, from dated price snapshots | The arithmetic by unit tests; the page against its data files in CI | A quote from the assembler; a real order |
| Documentation | This site | `mkdocs build --strict` and an internal link check in CI; a [repository audit](design/repository-audit.md) of 2026-11-12 | Photos and measured values, after a prototype |

## What only a prototype can answer

The [hardware design](design/hardware-design.md#20-design-risk-register)
keeps a register of 36 risks that only a bench test can close, and a
[bring-up plan](design/hardware-design.md#21-bring-up-test-plan) of 18 steps
for the first board. In short:

| Question | Risk numbers |
|---|---|
| Do the triacs, the processor module and the regulator stay within their temperature limits with two zones and the master on for two hours in a 50 °C room, in the closed enclosure? | 1, 2, 3, 19 |
| Does an output survive a dead short on 20, 40 and 75 VA transformers, cold and hot, and does the relay survive breaking the fault current? | 4, 9, 16 |
| Does the over-current cut-off trip on real shorts and not on switching noise? | 5 |
| How much surge do the valve, power and sensor terminals survive? | 6, 18, 25 |
| Do small loads (a 50 mA pump-start relay coil) and real solenoids switch cleanly, hot and cold? | 7, 8 |
| Do both keep-alive detectors enable and release within their windows over temperature, with a slow or lopsided keep-alive, and does each single fault behave as the analysis says? | 10, 22, 23, 27, 28, 29 |
| Are the limits of the self-test's stop test, and the value each board stores for it, right for real boards? Does a board built with an open bleed resistor show at bring-up? | 30, 34 |
| Are the buck regulator, the supervisor and the current measurement stable and accurate with WiFi transmitting, and does the input resistor survive 1000 power-ups at the peak of 32 VAC? | 11, 12, 24, 26 |
| What does the board's half-wave load do to a small transformer? | 13 |
| WiFi range in the enclosure, and a pre-compliance emissions scan | 14 |
| Does the display panel match its drawing, and is 1.6 mA enough to see an output LED through the light pipe? | 15, 20 |
| How fast does a fuse open at the fault current a USB ground path would carry? | 17 |
| What happens if the buck regulator's switch fails short, or the 3.3 V regulator does? | 21, 32 |
| Are the module's eFuses as its data sheet says, and can one pin be read as a level and by the ADC at once? | 31, 33 |

The firmware has its own list of things to measure on the first boards: the
self-test limits, the longest control-loop stall during a flash write, the
time a login takes, the encoder feel, and the QR code under a phone camera.
They are in [firmware architecture](design/firmware-architecture.md) and
[display interface](design/display-ui.md#12-what-is-verified).

## Roadmap

The order is fixed by what each step needs from the one before it.

1. **Review before fabrication**, [issue #11](https://github.com/syntroniks/zoneflow/issues/11). The circuit reviews and the [layout review](design/hardware-layout-review.md) are written and their findings are in the design. What is open is the part only a person can do: the [review walkthrough](hardware/review/index.md) leads the owner through the layout review's pre-order checklist and records the result. A sample display panel must confirm the flat cable before an order.
2. **Enclosure test fit**, [issue #9](https://github.com/syntroniks/zoneflow/issues/9): print the enclosure and the [test-fit kit](enclosure/test-fit.md). This needs no board and can be done now.
3. **First prototypes**, [issue #33](https://github.com/syntroniks/zoneflow/issues/33): a small batch, assembled by JLCPCB, for the bring-up plan and the risk register, after the owner decides to order.
4. **Firmware on hardware**, [issue #6](https://github.com/syntroniks/zoneflow/issues/6): set the self-test limits and current thresholds from measurements, tune the display and the encoder.
5. **First firmware release**, [issue #8](https://github.com/syntroniks/zoneflow/issues/8).
6. **This manual corrected from the prototype**: photos, measured values, the notes marked "to be completed" on each page.

Options kept open for later, from the [specification](design/system-spec.md):
a board profile for the OpenSprinkler firmware, and a Matter irrigation
endpoint.

## How to follow

- [Progress log, issue #1](https://github.com/syntroniks/zoneflow/issues/1).
- [Open issues](https://github.com/syntroniks/zoneflow/issues).
- [Contributing](contributing.md): reviews of the circuit, the schematic, the layout and the firmware are the most useful help now.
