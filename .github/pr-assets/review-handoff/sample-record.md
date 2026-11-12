# Pre-order review of revision A, 2026-11-11

!!! warning "One person's look at design files"
    No board has been made. Each decision below is the owner's answer to a step of the
    [review walkthrough](../../hardware/review/index.md): a look at the board file, a print, a
    datasheet or an order page. A pass does not show that the board works.

Recorded by `reviewwalk record` from a hand-off file of the walkthrough. The decisions as data: [`2026-11-11-pre-order.json`](2026-11-11-pre-order.json).

| | |
|---|---|
| Board file | `hardware/zoneflow-a/zoneflow-a.kicad_pcb` |
| SHA-256 of the board file | `dc9a8c8055d5005cff9c28093b499e9143985e990f8f2122e0469c5dfdc7f1cf` |
| Commit the walkthrough page was built from | `fd6bfc462dc3a061607a085f14156c3c83aea4f4 with local changes` |
| Tracked in | [issue #33](https://github.com/syntroniks/zoneflow/issues/33) |

| Steps | Pass | Accepted | Fail | Rejected | Not sure | Not answered |
|---|---|---|---|---|---|---|
| 47 | 31 | 4 | 5 | 0 | 6 | 1 |

Not every step is passed or accepted. The board is not ready to order.

## Needs a change (5)

### Ground round the gate node of Q2

- **Step:** `k-q2-gate`, In KiCad
- **Decision:** fail
- **Owner's note:** Note for k-q2-gate: looked at it twice.
- **Remedy:** Not chosen. The agent proposes one.
- **Parts:** Q2, D22, C50, RN2
- **Nets:** `/Expander/RLY_G2`, `V_DRV`, `3V3`
- **Position:** (53.6, 72.2) mm from the lower left corner of the board. KiCad's default display shows (143.6, 87.8)
- **Files a change would be made in:** `hardware/zoneflow-a/design/layout.py`, `docs/design/hardware-layout.md`
- **Pictures:** `hardware/zoneflow-a/output/review/k-q2-gate-gate.svg`
- **Comes from:** Layout review, section 17, item 7, third place. Findings L22

### The terminal labels as the installer reads them

- **Step:** `k-silk`, In KiCad
- **Decision:** fail
- **Owner's note:** Note for k-silk: looked at it twice.
- **Remedy:** Chosen from the list (`fix-label`): Move or narrow the label named in the note, in `design/layout.py`.
- **Parts:** J1, J2, J3, J4
- **Position:** (57, 8) mm from the lower left corner of the board. KiCad's default display shows (147, 152)
- **Files a change would be made in:** `hardware/zoneflow-a/design/layout.py`, `docs/design/hardware-layout.md`
- **Pictures:** `hardware/zoneflow-a/output/review/k-silk-left.svg`, `hardware/zoneflow-a/output/review/k-silk-right.svg`
- **Comes from:** Layout review, section 17, item 6. Findings L10, L15

### Q1, Q2 and Q4, the N-channel MOSFET AO3400A

- **Step:** `s-ao3400`, With the datasheets
- **Decision:** fail
- **Owner's note:** Note for s-ao3400: looked at it twice.
- **Remedy:** Not chosen. The agent proposes one.
- **Parts:** Q1, Q2, Q4
- **Position:** (53.6, 72.2) mm from the lower left corner of the board. KiCad's default display shows (143.6, 87.8)
- **Files a change would be made in:** `hardware/netlist.yaml`, `hardware/zoneflow-a/design/parts.toml`, `hardware/bom-preliminary.csv`, `docs/design/hardware-design.md`
- **Pictures:** `hardware/zoneflow-a/output/review/s-ao3400-pads.svg`
- **Comes from:** Layout review, section 4: nine makers' sheets not opened. Findings L23

### The USB-C receptacle J5

- **Step:** `p-j5`, On the 1:1 print
- **Decision:** fail
- **Owner's note:** Note for p-j5: looked at it twice.
- **Remedy:** Chosen from the list (`footprint`): Correct the footprint in `design/footprints.py` and its row in `design/footprints.md`, from the maker's drawing.
- **Parts:** J5
- **Position:** (8, 2.67) mm from the lower left corner of the board. KiCad's default display shows (98, 157.33)
- **Files a change would be made in:** `hardware/zoneflow-a/design/footprints.py`, `hardware/zoneflow-a/design/footprints.md`, `hardware/mechanical.yaml`
- **Pictures:** `hardware/zoneflow-a/output/review/p-j5-lands.svg`
- **Comes from:** Layout review, section 17, item 1. Findings L23

### Placement preview: D25, cathode bar at the left

- **Step:** `o-prev-d25`, On JLCPCB's order pages
- **Decision:** fail
- **Owner's note:** Note for o-prev-d25: looked at it twice.
- **Remedy:** Not chosen. The agent proposes one.
- **Parts:** D25
- **Nets:** `V_BUCK`
- **Files a change would be made in:** `hardware/rules/jlcpcb-cpl-rotations.csv`, `docs/hardware/ordering.md`
- **Pictures:** `hardware/zoneflow-a/output/review/o-prev-d25-d25.svg`
- **Comes from:** Layout review, section 17, item 5, and section 16. Findings L23, L1

## Not sure: ask the owner (6)

### Two I2C conductors 0.2 mm from the node behind R29

- **Step:** `k-ka-n0`, In KiCad
- **Decision:** not sure
- **Owner's note:** Note for k-ka-n0: looked at it twice.
- **Parts:** C22, R29
- **Nets:** `/Failsafe/KA_N0`, `/QW_SDA`, `/I2C0_SCL`
- **Position:** (104.9, 88.3) mm from the lower left corner of the board. KiCad's default display shows (194.9, 71.7)
- **Files a change would be made in:** `hardware/zoneflow-a/design/layout.py`, `docs/design/hardware-layout.md`
- **Pictures:** `hardware/zoneflow-a/output/review/k-ka-n0-node.svg`
- **Comes from:** Layout review, section 17, item 7, second place, and section 6. Findings L22

### Accept or reject: the display bus is 61 to 72 mm long

- **Step:** `k-dev-j7`, In KiCad
- **Decision:** not sure
- **Owner's note:** Note for k-dev-j7: looked at it twice.
- **Parts:** J7, U4
- **Nets:** `/TFT_SCLK`, `/TFT_MOSI`, `/TFT_CS`, `/TFT_DC`, `/TFT_RST`, `/KA`
- **Position:** (80, 90) mm from the lower left corner of the board. KiCad's default display shows (170, 70)
- **Files a change would be made in:** `hardware/netlist.yaml`, `hardware/mechanical.yaml`, `hardware/zoneflow-a/design/layout.py`, `docs/design/hardware-layout.md`
- **Pictures:** `hardware/zoneflow-a/output/review/k-dev-j7-bus.svg`
- **Comes from:** Finding L17. Layout document, section 14, item 4. Findings L17

### U6, the quad comparator LM2901

- **Step:** `s-u6`, With the datasheets
- **Decision:** not sure
- **Owner's note:** Note for s-u6: looked at it twice.
- **Parts:** U6
- **Position:** (108, 78.9) mm from the lower left corner of the board. KiCad's default display shows (198, 81.1)
- **Files a change would be made in:** `hardware/netlist.yaml`, `hardware/zoneflow-a/design/parts.toml`, `hardware/bom-preliminary.csv`, `docs/design/hardware-design.md`
- **Pictures:** `hardware/zoneflow-a/output/review/s-u6-pads.svg`
- **Comes from:** Layout review, section 4: nine makers' sheets not opened. Findings L23

### U2 and U9, the ESD arrays USBLC6-2SC6

- **Step:** `s-usblc6`, With the datasheets
- **Decision:** not sure
- **Owner's note:** Note for s-usblc6: looked at it twice.
- **Parts:** U2, U9
- **Position:** (3.3, 17.7) mm from the lower left corner of the board. KiCad's default display shows (93.3, 142.3)
- **Files a change would be made in:** `hardware/netlist.yaml`, `hardware/zoneflow-a/design/parts.toml`, `hardware/bom-preliminary.csv`, `docs/design/hardware-design.md`
- **Pictures:** `hardware/zoneflow-a/output/review/s-usblc6-u2.svg`, `hardware/zoneflow-a/output/review/s-usblc6-u9.svg`
- **Comes from:** Layout review, section 4: checked "from memory". Findings L23

### Assembly options: top side only, rails on the two long sides

- **Step:** `o-assembly`, On JLCPCB's order pages
- **Decision:** not sure
- **Owner's note:** Note for o-assembly: looked at it twice.
- **Files a change would be made in:** `hardware/zoneflow-a/design/layout.py`, `docs/hardware/ordering.md`
- **Pictures:** `hardware/zoneflow-a/output/review/o-assembly-edges.svg`
- **Comes from:** Layout review, section 17, item 3. Finding L20. Findings L20

### Placement preview: every other polarised part against the pin 1 table

- **Step:** `o-prev-rest`, On JLCPCB's order pages
- **Decision:** not sure
- **Owner's note:** Note for o-prev-rest: looked at it twice.
- **Files a change would be made in:** `hardware/rules/jlcpcb-cpl-rotations.csv`, `docs/hardware/ordering.md`
- **Pictures:** `hardware/zoneflow-a/output/review/assembly-top.svg`
- **Comes from:** Layout review, section 17, item 5, and section 5, "What this does not prove". Findings L1, L23

## Not answered (1)

### Two questions for JLCPCB that nobody has asked

- **Step:** `o-ask`, On JLCPCB's order pages
- **Decision:** not answered
- **Owner's note:** none
- **Parts:** C1, K1
- **Position:** (20, 28) mm from the lower left corner of the board. KiCad's default display shows (110, 132)
- **Files a change would be made in:** `docs/design/mechanical.md`, `docs/hardware/ordering.md`
- **Pictures:** `hardware/zoneflow-a/output/review/o-ask-access.svg`
- **Comes from:** Layout review, section 17, closing paragraph. Layout document, section 14, items 2 and 8. Findings L6

## Passed and accepted: change nothing (35)

- pass: Open the board and run the design rule check yourself (`k-open`)
- pass: Look at one valve output on all four layers (`k-valve`)
- pass: The keep-alive track from module pin 10 to R33 and R29 (`k-ka-pin10`)
- pass: Six vias under the buck converter (`k-u1-vias`)
- pass: No copper under the two screw heads (`k-screws`)
- accepted: Five tracks under the display carrier's leg at hole B (`k-hole-b`)
- pass: The 17 connections the autorouter could not close (`k-patches`)
- accepted: Accept or reject: R33 is 12 mm from the pin it should sit at (`k-dev-r33`)
- accepted: Accept or reject: the ADC filter capacitors are 19 mm from their pins (`k-dev-adc`)
- accepted: Three risks the review left open: see each and accept it (`k-open-risks`)
- pass: D25, the 6 V clamp: which pad is the cathode (`s-d25`)
- pass: U11, the sensor supply switch TPS2553 (`s-u11`)
- pass: U5, the I/O expander TCA9539 (`s-u5`)
- pass: U7, the fail-safe logic 74HC132 (`s-u7`)
- pass: U8, the current-sense amplifier MCP6002 (`s-u8`)
- pass: U10, the clock PCF8563 (`s-u10`)
- pass: U3, the 3.3 V regulator LDL1117 (`s-u3`)
- pass: Q3, the P-channel MOSFET AO3401A (`s-ao3401`)
- pass: The six BAT54S diode pairs (`s-bat54s`)
- pass: Print both sides at true size and check the scale (`p-scale`)
- pass: The four terminal headers J1 to J4 (`p-headers`)
- pass: The display connector J7 (`p-j7`)
- pass: The encoder SW3 (`p-sw3`)
- pass: The coin cell holder BT1 (`p-bt1`)
- pass: The relay K1 and the capacitor C1 on the bottom print (`p-bottom`)
- pass: The display panel's flat cable against J7 (`h-display`)
- pass: Measure the parts KiCad has no model for (`h-heights`)
- pass: Run the two commands that guard the order (`o-commands`)
- pass: Board options that match the calculations (`o-options`)
- pass: The four copper layers in the right order (`o-layers`)
- pass: Take six lines out of the assembly (`o-bom`)
- pass: Placement preview: the eleven LEDs, cathode at the left (`o-prev-leds`)
- pass: Placement preview: the nine triacs, tab towards the terminals (`o-prev-triacs`)
- pass: Placement preview: the nine opto-triacs, pin 1 at the lower left (`o-prev-optos`)
- pass: Placement preview: the module, the display connector and the cell holder (`o-prev-big3`)
