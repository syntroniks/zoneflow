# ZoneFlow revision A: PCB layout

!!! warning "No board has been made"
    This layout exists as files. Nothing in it has been fabricated, soldered,
    powered or measured. Where this page says a rule "holds" or a check
    "passes", a program compared numbers in a file, and section 1 names the
    program. Section 13 lists what I think is weak, and section 14 what a
    person must look at before anyone orders a board.

The board is `hardware/zoneflow-a/`. It is generated from text: the placement,
every hand-routed track and every zone are written by
`design/layout.py` into `design/board.toml`, and
[the toolchain](hardware-toolchain.md) builds the KiCad board from that. Run
`make` in the board directory to rebuild everything on this page.

Coordinates on this page are those of the
[mechanical contract](mechanical.md), `hardware/mechanical.yaml`: origin at the
bottom-left corner of the board seen from the component side, x along the
terminal edge, y up towards the antenna. Lengths are in millimetres.

[![Top side render](../hardware/zoneflow-a/zoneflow-a-top.png)](../hardware/zoneflow-a/zoneflow-a-top.png)

Top copper and silkscreen:
[front](../hardware/zoneflow-a/zoneflow-a-front.svg),
[back](../hardware/zoneflow-a/zoneflow-a-back.svg),
[In1.Cu](../hardware/zoneflow-a/zoneflow-a-In1.Cu.svg),
[In2.Cu](../hardware/zoneflow-a/zoneflow-a-In2.Cu.svg),
[all layers as a PDF](../hardware/zoneflow-a/zoneflow-a-board.pdf),
[bottom render](../hardware/zoneflow-a/zoneflow-a-bottom.png),
[interactive BOM](../hardware/zoneflow-a/zoneflow-a-ibom.html).

## 1. What was checked, and the result

`make` runs these in order and stops at the first that fails.

| Check | Program | Result |
|---|---|---|
| Schematic against the design netlist, pin for pin | `hwflow sch` | @@PARITY@@ |
| Electrical rules of the schematic | `kicad-cli sch erc` | @@ERC@@ |
| Design rules of the board, with the zones filled again, errors and warnings | `kicad-cli pcb drc` | @@DRC@@ |
| Board against schematic: every footprint, every pad's net | `kicad-cli pcb drc --schematic-parity` | @@PCBPARITY@@ |
| Connections not routed | the same DRC run | @@UNCONNECTED@@ |
| Mechanical contract, revision 3 | `hwflow mech` | @@MECH@@ |
| Layout rules of the fail-safe analysis | `hwflow.layoutrules` | @@RULES@@ |

Two settings shape the DRC result, and a reviewer should know them:

- **One check is switched off: `lib_footprint_mismatch`.** The builder takes
  @@SILK_STROKES@@ strokes off footprint silkscreen outlines where they lay over
  a neighbour's pad (section 11). KiCad then reports each such footprint as
  different from its library copy. Nothing else in a footprint can differ: the
  board is built from the libraries on every run. This is the only check
  switched off, and no single violation is excluded.
- **Three rules are added** in `design/rules.kicad_dru`: no track of another
  net within 1 mm of a keep-alive track; the pads of the high-impedance nodes
  keep the usual pad clearance while their tracks keep 0.6 mm; and the fab
  limits of `hardware/rules/jlcpcb.kicad_dru`.

The autorouter's own numbers are in section 10.

## 2. Stack-up and what the fab must be told

Four copper layers, 1.6 mm, JLCPCB's standard FR-4 build with 1 oz outer and
0.5 oz inner copper. **No special option is needed.** The fab limits used as
DRC minimums are in `hardware/rules/jlcpcb.toml`, with the page they were read
from.

| Layer | Use |
|---|---|
| F.Cu | Every part except C1 and K1. Signals. The 24 VAC entry. Ground pour in what is left |
| In1.Cu | Ground, one plane. No track is routed on it |
| In2.Cu | 3.3 V pour over the logic area. Under the triacs: the shunt return plane. A copy of the valve fan-out. The opto-output lines |
| B.Cu | The valve fan-out. Nine heat islands under the triacs. The valve-common run. The pairs and buses of section 7. Ground pour in what is left. C1 and K1 |

An earlier state of this layout had the valve fan-out on In2.Cu only and
needed 1 oz inner copper to survive a shorted output with the over-current
latch failed. That is gone: the fan-out is on B.Cu now (section 7.3), and the
calculation in section 6 holds with the default stack-up.

The smallest hole is 0.2 mm: the twelve thermal vias in the ground pad of the
module footprint, which is KiCad's stock footprint. The rules file records
0.2 mm as JLCPCB's smallest hole without a surcharge. Every via this layout
adds is 0.3 mm or larger.

## 3. Floor plan

| Region | What is there | Why there |
|---|---|---|
| y 0 to 7.5 | USB-C, the four terminal headers, Qwiic | Fixed by the contract |
| y 7.6 to 8.9 | Nothing but the terminal labels | Kept free of pads so that every terminal has its name behind it |
| x 8 to 34, y 7.5 to 18 | The two fuse holders | Fixed. Nothing else is allowed in the fuse well |
| x 9 to 33, y 18 to 40 | Surge clamp, rectifier, surge resistor, fuse F5, buck converter, clamp D25 | Between the fuses and the pads of C1 |
| x 33 to 39, y 9 to 39 | Valve-common TVS, shunt, input monitor divider | Beside the first triac column, between the fuse in the return leg and the shunt plane |
| x 39 to 103, y 9 to 40 | Nine output columns, 7.1 mm apart | Above the output terminals. Each column, from the terminal up: TVS and snubber, PTC, triac, gate resistors |
| x 103 to 113, y 5 to 57 | Qwiic ESD and PTC, sensor TVS and series resistors, the flow input clamp | The field side of the sensor inputs stays at the board edge |
| y 39.3 to 44.7 | The LED row, with the two resistors of each LED | Fixed. Only parts under 1 mm are allowed under the light pipes |
| x 21 to 107, y 45 to 57 | Nine opto-triacs, each above its LED | The contract's LED pitch, 9.5 mm, is also the smallest pitch the opto-triac body allows |
| x 1 to 21, y 45 to 57 | 5 V OR-ing diodes, 3.3 V regulator | Next to the buck output and the USB supply |
| x 28 to 42, y 57 to 68 | Current-sense amplifier and the ADC series resistors | Between the two buttons, away from the buck |
| x 55 to 79, y 57 to 67 | Sensor supply switch and its protection | |
| x 1 to 57, y 68 to 110, under the display | Expander, fail-safe gate, relay driver and keep-alive detector B, display support, reset and boot buttons | All under 5 mm. Half of this area is empty and carries the board name |
| x 58 to 79, y 68 to 77 | Real-time clock | Next to the positive contact of the cell holder |
| x 79 to 113, y 57 to 74 | Coin cell holder | The only place that is not under the display carrier and takes a 32 x 16 mm part |
| x 93 to 113, y 74 to 90 | Keep-alive detector A, the comparators, the ADC filter capacitors | Under the module. The keep-alive enters at the corner next to module pin 10 |
| x 79 to 93, y 102 to 109 | Module support: 3.3 V decoupling, reset network, supervisor, encoder switch filter | The only free ground beside the module |
| Bottom, x 1 to 9, y 17 to 38 | K1 | See section 4 |
| Bottom, x 10 to 37, y 23 to 37 | C1 | Fixed |

The triac columns and the opto-triacs are on different pitches, 7.1 and
9.5 mm. A column cannot be 9.5 mm wide: nine of them would not fit between the
C1 pads and the right edge. An opto-triac cannot be on 7.1 mm: its body is up
to 8.89 mm long. So each gate line and opto-output line steps sideways between
its column and its opto-triac, by up to 17 mm, in the strip under the LED row.

## 4. Changes to the mechanical contract

The layout changed four things, and the contract is at revision 3 with them.
The enclosure was rebuilt against revision 3 and its interference check
passes: @@ENCLOSURE@@.

| Part | Revision 2 | Revision 3 | Why |
|---|---|---|---|
| K1 | Suggested at (50, 22), bottom side | (4.8, 27.85), bottom side, long axis along y | At (50, 22) its four pins come up between the output triacs, where the top side has no room for them |
| C1 | Can 21.5 long | Rubycon 63ZLJ470M12.5X20, can 22.0 long at most. Pads unchanged | The hardware design changed the part (section 4.5) |
| BT1 | (95.0, 64.0) | (95.0, 65.3), positive contact at the left | 1.3 mm up, to clear the opto-triac row |
| J6 | y = 1.625 | y = 2.63 | Its two mounting lands were 0.7 mm beyond the board edge |

## 5. Net classes

`design/circuit.py` assigns nets to classes by voltage domain;
`design/board.toml` gives each class its numbers.

| Class | Nets | Track | Clearance | Via |
|---|---|---|---|---|
| AC24 | 24 VAC in, both legs; the valve common; the nine outputs; the nine triac tabs; the rectifier output | 1.0 | 0.5 | 1.0 / 0.5 |
| AC24S | Gates, opto outputs, snubber mid-points: at 24 VAC potential, milliamps | 0.3 | 0.5 | 0.6 / 0.3 |
| Shunt | `ISNS`, the common return of the triacs, within 3 V of ground | 1.0 | 0.25 | 1.0 / 0.5 |
| HVDC | Rectified input before and behind F5 (up to 45 V), buck switch and bootstrap nodes | 0.5 | 0.4 | 0.8 / 0.4 |
| Field | Sensor terminals, sensor supply output | 0.4 | 0.5 | 0.8 / 0.4 |
| Power | Supply rails, relay coil | 0.4 | 0.2 | 0.8 / 0.4 |
| USB | D+ and D- | 0.25 | 0.2 | 0.6 / 0.3 |
| HiZ | The nodes where a leak holds a keep-alive detector on: `KA_N1`, `KA_N2`, `KA_INP`, `RLY_G2`, `KB_N1` | 0.25 | 0.6 | 0.6 / 0.3 |
| Default | Everything else | 0.2 | 0.2 | 0.6 / 0.3 |

The 0.5 mm around the 24 VAC and field nets is a design choice, not a number
from a standard. The supply is a Class 2 transformer, 45 V peak at most, and
the surge parts clamp at 77 V. I did not look up a creepage table.

## 6. Current and the width of the tracks that carry it

**Steady current.** The formula is the IPC-2221 chart fit as it is usually
quoted, I = k x dT^0.44 x A^0.725, with I in amperes, dT the rise in kelvin, A
the cross-section in square mils, k = 0.048 for an outer layer and 0.024 for an
inner one. I did not re-read the standard; the formula is from memory and is
conservative for a board with planes. 1 oz copper is 35 um, 1.378 mil.

| Track | Where | Cross-section | Current for 10 K | At the working current |
|---|---|---|---|---|
| 2.0 mm, outer | 24 VAC entry, valve common, return to the fuse | 108.5 mil² | 3.95 A | 3 A, the fuse rating: 5.3 K. 1.5 A, two zones and the master valve: 1.1 K |
| 1.0 mm, outer | Rectifier feed, `VIN_DC` | 54.3 mil² | 2.39 A | Below 0.5 A average |
| 1.2 mm, outer | Fan-out vias to the PTC | 65.1 mil² | 2.73 A | 0.5 A: 0.2 K |
| 0.9 mm, outer | One valve output on B.Cu, without its copy on In2.Cu | 48.8 mil² | 2.22 A | 0.5 A, one valve: 0.3 K. 1.1 A, the PTC hold current: 2.0 K |

Working: 2.0 mm is 78.74 mil, times 1.378 is 108.5 mil²; 108.5^0.725 = 29.9;
0.048 x 10^0.44 x 29.9 = 0.048 x 2.754 x 29.9 = 3.95 A. For another current
the rise is 10 x (I / I10)^(1 / 0.44).

**A short circuit.** The hardware design, section 6.6, gives the energy the
output path must pass: 9.6 A²s when the triac or the relay ends the fault
within two half-cycles, and up to 34 A²s if neither does and the PTC has to.
The rise of a conductor with no time to lose heat is
dT = I²t x rho / (A² x c), with rho = 1.72e-8 ohm m and c = 3.45e6 J/(m³ K)
for copper.

| Conductor | Cross-section | 9.6 A²s | 34 A²s |
|---|---|---|---|
| 2.0 mm outer track | 0.070 mm² | 10 K | 35 K |
| 1.2 mm outer link, vias to PTC and TVS to PTC | 0.042 mm² | 27 K | 96 K |
| 0.9 mm fan-out lane on B.Cu, 35 um, alone | 0.0315 mm² | 48 K | 171 K |
| The same with its copy on In2.Cu, 17.5 um | 0.0473 mm² | 21 K | 76 K |
| Two output vias, 0.3 mm drill, 25 um plating assumed | 2 x 0.0236 mm² | 22 K | 76 K |
| The same with 18 um plating | 2 x 0.0170 mm² | 42 K | 147 K |
| Two A1 vias, 0.3 and 0.4 mm drill, 25 um | 0.055 mm² | 16 K | 56 K |

Working for the lane with its copy: 0.9 mm x (0.035 + 0.0175) mm = 4.725e-8 m²;
34 x 1.72e-8 / ((4.725e-8)² x 3.45e6) = 5.85e-7 / 7.70e-9 = 76 K.

What this says:

- **The worst case no longer depends on the inner copper.** A short with the
  over-current latch failed takes a fan-out lane from 70 °C to about 146 °C
  with both layers carrying, and to about 241 °C if the copy on In2.Cu carried
  nothing. Both are for a fraction of a second and below soldering
  temperature. The copy is margin, not a requirement.
- **The two output vias are now the weakest part of the path**, and they
  depend on plating thickness, which I assumed. With 18 um they reach 70 + 147
  = 217 °C in the worst case. I did not find a plating thickness that JLCPCB
  promises.
- Two conductors in parallel share a current by their cross-sections only if
  they are equally long. The lane and its copy are the same shape, joined at
  the terminal pin and at the two vias.

## 7. What is routed by hand

Everything in this section is written out in `design/layout.py` as tracks,
vias and zones, laid before the autorouter runs and locked. These nets are
taken out of the autorouter's netlist (`[route] exclude`), so it can neither
add to them nor move them. They are: @@HAND_NETS@@ nets of @@ALL_NETS@@.

### 7.1 24 VAC entry

| From | To | Layer | Width |
|---|---|---|---|
| J1 pin 1 | F4 | F.Cu | 2.0 |
| J1 pin 2 | F1 | F.Cu | 2.0 |
| F4 (`AC_L`) | Surge clamp D1 | F.Cu | 2.0 |
| F4 | Rectifier D2 | F.Cu | 1.0 |
| D1 | Relay contact, K1 pin 13 | F.Cu | 2.0 |
| K1 pin 14 (`COM`) | J2 pins 1 and 2 | B.Cu, under the fuse well | 2.0 |
| J2 pin 2 | Valve-common TVS D23 | F.Cu | 1.5 |
| F1 (ground side) | D23 and the shunt R37 | F.Cu | 2.0 |

The stubs to the solder jumper JP1 are 0.5 mm wide, because its two pads are
0.3 mm apart and a wider track would come closer to the other net than the
pads are. JP1 is bridged only on a board built without K1.

### 7.2 One output column

Nine identical columns, written once as a group. From the terminal upward:

- **Output net.** From the two vias of the fan-out lane (7.3) a 1.2 mm track
  on F.Cu into the lower PTC pad. A second 1.2 mm link joins the TVS and the
  PTC.
- **T2.** A 5 mm wide link from the upper PTC pad to the triac tab. Two vias,
  0.5 mm drill, beside the PTC pad and four, 0.3 mm drill, in the tab land
  take it to the island on B.Cu. **The four vias are in the land and are not
  plugged**; some solder will run into them. A 0.3 mm track on F.Cu, in the
  gap between this PTC and the next, takes T2 down to the snubber resistor.
- **Island.** One zone per triac on B.Cu, 6.6 x 23.1 mm, 1.52 cm². With the
  tab land, the PTC land and the link on F.Cu, 0.52 cm², each tab has
  2.04 cm². The hardware design, section 6.7, asks for 2 cm² or more. The
  islands were 2.0 cm² on the bottom alone before the fan-out moved to B.Cu
  and took the strip below them.
- **A1.** Two vias beside the A1 lead, 0.3 and 0.4 mm drill, into the shunt
  plane on In2.Cu. A 0.6 mm track to the gate resistor.
- **Gate.** 0.3 mm, from the gate lead under the two resistors to its via.

### 7.3 Valve fan-out

The nine output pins are within 32 mm of each other; the nine columns span
64 mm. Each output therefore runs sideways by 6 to 31 mm before it reaches
its column.

- On **B.Cu**, between the terminal row and the heat islands: 0.9 mm tracks,
  0.5 mm apart, the full 24 VAC clearance. An output enters on the lowest free
  lane and steps up one lane whenever an earlier output has turned up to its
  column, so no two cross. `layout.py` computes the lanes and checks that no
  two centre lines come closer than 1.4 mm; DRC checks the result again.
- The **same tracks again on In2.Cu**. The through-hole terminal pin and the
  two vias under the column join the two.
- Five lanes carry eight of the nine. The last output, zone 8, starts on a
  sixth lane level with the terminal pins, for the 5 mm right of its own pin
  where no other pin is, and steps up before the next terminal block begins.
  `layout.py` fails if that lane is used anywhere else.

### 7.4 Shunt return

`ISNS` is a plane on In2.Cu under all nine triacs, 68.7 x 21.5 mm. The valve
current goes from each A1 lead down two vias, through the plane, and up two
vias, 0.5 mm drill, at the shunt R37. A plane this wide does not depend on
its copper weight. A separate strip on F.Cu, 1.0 mm, joins the ground-side
pads of every TVS and snubber capacitor and reaches R37 by a 0.5 mm track; it
carries surge and snubber current only. The `ISNS` test pad is at the far end
of that strip.

### 7.5 Kelvin sense of the shunt

The hardware design, section 10.2, wants the amplifier to sense R37 at its
pads, with a ground sense that is its own trace and not the plane.

- Each sense track starts at its own via at the left end of a pad of R37.
- Both run on B.Cu, 0.6 mm apart: left under C1, up at x = 18.4 and 19.0, right
  at y = 58 to the amplifier. About 105 mm each. They take this way round
  because the direct way crosses the gate lines.
- A rule area keeps the ground pour on B.Cu away from both tracks along their
  whole length, and one on F.Cu does the same at the amplifier.
- The ground sense via at the amplifier end passes through the ground plane on
  In1.Cu. A rule area there keeps the plane off it. So the ground sense meets
  ground at one place only, the via at the shunt pad.
- R40 and R44, the two resistors on the ground sense, get no plane via.

### 7.6 Buck converter, fuse F5 and clamp D25

- **F5** stands on end between the pads of C1 and the input capacitors.
  `VIN_DC` goes from the positive pad of C1 to the top pad of F5, 6 mm,
  1.5 mm wide. The bottom pad of F5 is level with the joined pads of C2 and
  C3: `VIN_U1` is 3 mm of 1.5 mm track from the fuse to the capacitors and
  2 mm more to pin 2 of U1.
- F5 is 2.7 mm tall and C1 is soldered from the top side, so the body of F5
  keeps 3.5 mm from the centre of each C1 pad. That is why it stands on end:
  lying between the pad and the capacitors it would be 1.8 mm from the pad.
  To make room the converter moved 3.5 mm to the left.
- **D25** is above the output pad of L1: its cathode pad is 1.9 mm from the
  inductor pad, joined by a 2 mm track. Its anode has four vias, 0.4 mm drill,
  into the ground plane.
- **Loop of the converter.** C2 and C3 are at pins 1 and 2 of U1. The
  bootstrap capacitor is at pins 7 and 8. The switch node is pin 8, the
  bootstrap capacitor and the inductor pad, and one 0.25 mm track to the ripple
  resistor R6, which sits at the feedback pin with C7 and C8.
- **Output capacitors.** C6 is under the output pad of L1. C5 is beside C6 and
  reaches the pad by a 0.5 mm track 4 mm long, between the pads of L1 and C6.
  That track is the weak point of this block: see section 13.
- The under-voltage divider R7, R8 is beside F5 and stays on `VIN_DC`, before
  the fuse. Its tap reaches pin 3 through the 1 mm slot between the pins of U1
  and the input capacitors, by hand.

### 7.7 Gate and opto-output lines

Eighteen lines from the tops of the columns to the opto-triacs. Sideways in the
strip under the LED row, then up under each opto-triac to a via beside its
pin. The opto-output lines are on In2.Cu and the gate lines on B.Cu, 0.3 mm
wide in lanes 0.8 mm apart: on one layer the eighteen would need more lanes
than the strip has. On In2.Cu each line cuts a slot 9 mm long in the 3.3 V
pour under its opto-triac.

### 7.8 USB

The series resistors R97 and R98 and the pads of the unfitted capacitors are
at the ESD array U2, next to the connector. From their upper ends D+ and D-
run as a pair on B.Cu: up the left edge between the pins of K1, across under
the display at y = 95, one step down past the debug header, to a via at each
module pin. 0.25 mm tracks 0.2 mm apart, @@USB_LEN@@.

This is not an impedance-controlled pair and I did not calculate its
impedance. It is long because the contract puts the connector in one corner
and the module in the opposite one. The interface is full speed, 12 Mbit/s,
where a run of this length is short against the edge rate; it has not been
tried. The resistors were beside the module before; there they were 4 mm from
the keep-alive pin, which section 9 forbids.

### 7.9 Keep-alive

See section 9 for the rules. The route:

- From module pin 10 a 2 mm stub on F.Cu under the module to a via.
- On B.Cu down the inside of the module's left pin row to the via between R33
  and R29, at the corner of the module. R33 is 10 mm of track from the pin.
- On B.Cu under the right edge of the encoder and left along y = 78.6 to R89
  at detector B. @@KA_LEN@@ in all.
- `[[guard]]` in `board.toml` lays a rule area on each side of every `KA`
  segment longer than 4 mm, 1 mm wide, where no track and no via may be. The
  ground pour fills both.

### 7.10 Display bus

The hardware design, section 15.3, asks for the bus by hand, and section 8.5
forbids the display clock and data near the keep-alive. The keep-alive now
lies between the module pins and the connector, so the direct way would cross
it. The bus goes round the top of module pin 10 instead:

- A via above each of module pins 18 to 22, then five lanes on B.Cu, 0.4 mm
  apart, up between the keep-alive guard and the thermal vias of the module's
  ground pad, and left under the module's pin row and the parts beside it.
- Past the mounting hole the lanes spread to 1 mm, each ends in a via, and
  the bus continues on F.Cu down the strip the display cable lies over and
  under the connector J7 to its pins.
- The backlight line goes round the other side of the ground pad, joins as
  the top lane, and ends at R61.
- Length from module pin to connector pin: @@SPI_LEN@@. Section 15.3 says
  "within about 40 mm". The direct distance is 42 mm; the way round is longer.
  The bus is specified at 15 MHz.
- On B.Cu the reference plane is the 3.3 V pour on In2.Cu, not ground. The
  autorouter is kept off In2.Cu nowhere along the bus, so it may have cut
  that pour under it: section 10 says what it did.

### 7.11 ADC lines and the flow input

Two more nets of the list in section 9 had to cross the keep-alive to get
from where they start to the module, so they are routed by hand the other way
round:

- **`ADC_LO` and `ADC_HI`.** The series resistors R46 and R47 are at the
  amplifier outputs. From there the two lines run on B.Cu east along y = 67,
  south of everything the keep-alive touches, then north under the comparator
  block and the module to pins 39 and 38. @@ADC_LEN@@. The filter capacitors
  C30 and C31 sit on the way, 18 mm before the pins. The hardware design,
  section 12.5, wants them at the pins. There is no room beside pins 38 and
  39: the board edge is 1.5 mm away.
- **`SENS_FLOW`.** The clamp and filter of the flow input (D16, C45, R79) are
  at the right edge, above the sensor series resistors. From R79 the line
  goes up the right edge on B.Cu, round the top of the ADC lines under the
  module, and down to pin 5. @@FLOW_LEN@@. The rain and auxiliary inputs keep
  their clamps above the display cable and are autorouted: they are not on
  the list.

### 7.12 LED row

The opto LED supply `V_DRV` is one 0.3 mm bus under the LED row with a stub to
each of the eighteen resistors. In each LED cell the three short links are
written out too.

## 8. Ground and planes

- **One ground.** In1.Cu is ground and nothing else. The only openings are the
  clearances of vias and through-hole pins and one 1.4 mm square at the Kelvin
  sense via (7.5). The autorouter is not given this layer.
- **Plane vias.** Every surface-mount pad on ground or 3.3 V gets a 0.3 mm
  stub and a via, 0.6 mm with a 0.3 mm drill, placed by the builder at the
  nearest free point beside the pad: @@PLANE_VIAS@@ of them. The exposed pad of
  U1, the pads the Kelvin sense uses and a few pads at connectors are left
  out and done by hand. The module's ground pad has the twelve thermal vias
  of its footprint.
- **Pours.** Ground on F.Cu and B.Cu in whatever is left, clearance 0.3 mm,
  thermal relief on pads (0.3 mm gap, 0.4 mm spokes). Solid connection on the
  inner planes and on the heat islands.
- **Stitching.** There is no separate stitching pattern. The plane vias are the
  stitching: one per ground pad. An outer-layer ground fragment that touches
  no ground pad or via is removed by the zone fill.
- **The valve block has no ground pour on B.Cu.** The fan-out and the islands
  take it. Ground there is the In1.Cu plane, above the shunt plane.
- **3.3 V.** A pour on In2.Cu over the logic area and down both edges. Signals
  the autorouter put on In2.Cu cut into it; the fill and the connectivity check
  show whether every 3.3 V via is still joined (section 10).

## 9. The layout rules of fail-safe review A2

[Review A2](hardware-review-a2.md) moved the keep-alive from module pin 31 to
pin 10 and wrote: "Every bridge fault in section 3 is a statement about what
is next to what." The rules are in the
[hardware design, section 8.5](hardware-design.md#85-single-fault-analysis),
"Layout rules that follow". This section goes through them. Each is stated in
`board.toml` and checked by `hwflow.layoutrules` on the finished board file on
every build; the numbers below are from
`output/reports/layout-rules.md`.

### Rule 1: the keep-alive route

> Route `KA` from module pin 10 to R33, R29 and R89 with ground on both sides,
> at least 1 mm from any other signal. Put R33 at the module pin. Keep the net
> away from every net in the list above, on every layer.

| Part of the rule | How it is met | Evidence |
|---|---|---|
| From pin 10 to R33, R29 and R89 | Section 7.9 | Netlist parity |
| Ground on both sides | A 1 mm strip each side where nothing but the ground pour may be | Rule areas `guard /KA` in the board; DRC |
| 1 mm from any other signal | No track or via of any net but ground within 1 mm of a `KA` track or via on the same layer. Smallest found: @@KA_SAME@@ | `[[keepaway]]`, scope "layer" |
| R33 at the module pin | R33 is at the corner of the module, 10 mm of track from the pin. **Not at the pin**: see below | |
| Away from the listed nets on every layer | No copper of a listed net, on any layer, within 1 mm of `KA` copper in plan view. Not even a crossing on another layer. Smallest found: @@KA_PLAN@@ | `[[keepaway]]`, scope "plan" |

The list, and where each net ends up:

| Net of the list | Nets checked | Nearest to `KA`, plan view |
|---|---|---|
| PSRAM lines, GPIO35 to 37 | The three unconnected module pads 28 to 30 | @@D_PSRAM@@ |
| `TFT_BL` | `TFT_BL`, the gate `BL_G` and the cathode `TFT_LEDK` it switches | @@D_BL@@ |
| `U0TXD` | `U0TXD_MCU`, `U0TXD` | @@D_TXD@@ |
| `AC_ZC` | `AC_ZC` | @@D_ZC@@ |
| `SENS_FLOW` | `SENS_FLOW`, `N_FLOW`, `T_FLOW` | @@D_FLOW@@ |
| `ADC_LO`, `ADC_HI` | Those and `ISENSE_LO`, `ISENSE_HI` before the series resistors | @@D_ADC@@ |
| USB data | `USB_DP`, `USB_DM` and both behind the series resistors | @@D_USB@@ |
| Display clock and data | `TFT_SCLK`, `TFT_MOSI` | @@D_SPI@@ |
| Buck switch node | `SW` | @@D_SW@@ |

The same check is made for `KA_N0`, the node behind the 220 ohm resistor R29:
to a bridge it is the keep-alive net.

Three things a reviewer should weigh:

1. **R33 is not at the pin.** Left of pin 10 is the encoder, 0.1 mm of
   courtyard away; nothing fits there. R33 is at the first place a part can
   be, the corner of the module, with R29. An open track between the pin and
   R33 leaves the detectors on R33, pulled low. An open track between R33 and
   a detector leaves that detector with no input. Both are off.
2. **The USB pins are 3.8 mm from pin 10 on the module edge**, pins 13 and 14.
   That is the module's pin order. The check finds the USB pads 1.07 mm from
   the keep-alive track in plan view: the track is on B.Cu under the module,
   the pads are on F.Cu, and the ground plane is between them. The USB tracks
   leave those pads to the left, away from it.
3. **The display bus runs beside the keep-alive for 4 mm** under the module,
   on the same layer: `TFT_MOSI` 1.4 mm away and `TFT_SCLK` 1.8 mm, with the
   guard strip and its ground pour between. There was no other way round the
   module's ground pad.

### Rule 2: no test pad on the pump nodes

> Put no test pad on `RLY_G2`, `KB_N1`, `KA_N1` or `KA_N2`'s diode side that a
> probe or a flux residue could bridge to a rail. TP8 on `KA_N2` stays, with
> ground around it.

`[[no_pad]]` fails the build if a part named `TP*` has a pad on `RLY_G2`,
`KB_N1`, `KB_N0` or `KA_N1`: @@NOPAD@@. TP8 is on `KA_N2`, 4.3 mm from the
nearest 3.3 V copper, with the ground pour round it.

### Rule 3: ground round the high-impedance nodes

> Surround the gate node of Q2 with ground. [...] The same for A's reservoir.

- The five nodes are a net class of their own, HiZ, with 0.6 mm clearance:
  the autorouter and the plane-via placement keep that distance, and the
  ground pour fills up to it.
- `[[keepaway]]` fails the build if copper of `3V3`, `V5`, `V_BUCK`, `VBUS` or
  `V_DRV`, pads included, is within 1 mm of one of them on the same layer.

| Node | Nearest rail copper |
|---|---|
| `RLY_G2`, gate of Q2 | @@HIZ_G2@@ |
| `KB_N1` | @@HIZ_KB@@ |
| `KA_N1` | @@HIZ_N1@@ |
| `KA_N2`, reservoir of A | @@HIZ_N2@@ |
| `KA_INP`, comparator input of A | @@HIZ_INP@@ |

This check found two faults in an earlier state of this layout: a 3.3 V pad
0.28 mm from TP8, and a 3.3 V plane via 0.55 mm from `KA_N1`. Both were moved.
What the check cannot see: the drain of Q2 is 0.5 mm from its gate, across
the SOT-23. That is the part, and the fault table of the hardware design has
that bridge.

### Rule 4: the two pumps apart

> Place the A pump (R29, C22, D7) and the B pump (R89, C51, D22) at least 10 mm
> apart.

`[[apart]]`: @@APART@@. The schematic note also asks for C51 at right angles to
C22: `[[perpendicular]]`, @@PERP@@.

### Rule 5: do not move the pin

> Do not move the keep-alive to another pin without redoing this list for its
> neighbours.

The pin is where the netlist puts it. The neighbours on the module edge are
`ENC_B` (pin 9) and `BTN_BACK` (pin 11), which the review found harmless.
Their pads are 0.37 mm from the pad of pin 10, as on every pin of this
module.

### The other notes of the review

| Note | Where | How it is met |
|---|---|---|
| D25 at the buck output with a short, wide path | The orchestrator's brief for this work | Section 7.6: 1.9 mm, 2 mm wide, four ground vias |
| F5 close to the bulk capacitor | The same | Section 7.6: 6 mm from the pad of C1 |
| `TFT_BL` must never run next to `KA` | Schematic note, display sheet | The table of rule 1 |
| Keep J7 within about 40 mm of module pins 18 to 22 | Schematic note, display sheet | **Not met**: section 7.10 |
| `KA` never next to `TFT_BL`, `AC_ZC`, `U0TXD` or the buck converter | Schematic note, MCU sheet | The table of rule 1 |
| No test pad on `RLY_G2`, `KB_N1` or `KB_N0` | Schematic note, expander sheet | Rule 2 |
| Kelvin connections at R37 | Schematic note, current-sense sheet | Section 7.5 |
| Current-sense block away from L1, the display clock and the antenna | The same | 40 mm from L1, 30 mm from the bus, 60 mm from the antenna |

## 10. What the autorouter did

@@AUTOROUTE@@

## 11. Silkscreen

- **Terminal labels.** Behind every terminal, in the strip the output columns
  were moved up 1 mm to clear: `24V~`, `24V~`, `COM`, `COM`, `MV`, `Z1` to
  `Z8`, `5V`, `FLOW`, `GND`, `RAIN`, `GND`, `AUX`, `GND`. They are the names
  of the [wiring page](../getting-started/wiring.md).
- **The ground leg.** Terminal 2 of the 24 VAC block is the board's ground.
  A ground symbol is drawn right of its label.
- **Fuse ratings.** `F4 3A T` and `F1 3A T` above the two holders.
- **Board name, revision, licence, status.** `ZoneFlow rev A`,
  `ENGINEERING PROTOTYPE`, `Not built, not tested`, `CERN-OHL-P-2.0`, in the
  open area under the display, and again in one line on the bottom side.
- **Reference designators.** 351 parts on one side: the designators of the
  resistors, small capacitors, diodes and transistors do not fit between them.
  They are left off the silkscreen and are on the
  [assembly drawing](../hardware/zoneflow-a/zoneflow-a-assembly-top.pdf)
  instead. Connectors, ICs, switches, fuses and test pads keep theirs.
- **How it is kept clean.** The builder runs KiCad's DRC on the silkscreen
  and acts on every finding: a stroke of a footprint outline that lies over a
  neighbour's pad or over the board edge is removed; a designator is moved to
  the next of fourteen places round its part, and hidden if none is free; a
  label marked optional is dropped. It repeats until DRC finds nothing. A
  terminal label is not optional: if one collides the build fails. Result of
  this build: @@SILK@@.
- No text is under 1.0 mm high or 0.15 mm thick, the limits in the fab rules.

## 12. Fabrication outputs

All in `hardware/zoneflow-a/output/`, committed.

| File | What |
|---|---|
| `gerbers/` | Eleven layers in Gerber X2, drill files for plated and unplated holes, a drill map, the job file |
| `zoneflow-a-bom-jlcpcb.csv`, `-4-zone.csv` | JLCPCB BOM for the 8-zone and the 4-zone build |
| `zoneflow-a-cpl-jlcpcb.csv`, `-4-zone.csv` | Placement file for each build, rotations corrected by `hardware/rules/jlcpcb-cpl-rotations.csv` |
| `zoneflow-a-pin1.csv` | Where pin 1 of every polarised part is, to compare with the assembler's preview |
| `zoneflow-a-assembly-top.pdf`, `-bottom.pdf` | Part outlines with every reference designator |
| `zoneflow-a-board.pdf` | Every layer, one page each |
| `zoneflow-a-schematic.pdf` | The schematic |
| `zoneflow-a.step` | 3D model of the assembled board |
| `zoneflow-a-top.png`, `-bottom.png` | Renders |
| `zoneflow-a-front.svg`, `-back.svg`, `-In1.Cu.svg`, `-In2.Cu.svg` | Flat views of each copper layer |
| `zoneflow-a-ibom.html` | Interactive BOM: select a part or a net and see where it is |
| `reports/` | ERC and DRC reports, the layout-rule table, a summary |

**The rotation check.** @@ROTATION@@

The placement files leave out the through-hole parts: the four terminal
headers, C1, K1 and the debug pads. C1 and K1 are on the bottom side and are
soldered by hand from the top.

## 13. My own review

What I think is sound:

- The current-carrying copper is written out with its calculation, and the
  calculation no longer leans on a fab option.
- The keep-alive rules are statements in a file and are measured on every
  build, on every layer. The check found real faults while I worked.
- The mechanical contract is checked: every constrained part, every height
  zone.
- One ground plane with nothing routed on it.

What is weak, most serious first:

1. **Nothing has been built.** Every claim here is a check on files.
2. **The output vias under a shorted output** (section 6): two 0.3 mm vias
   per output, with a plating thickness I assumed. More or larger vias fit
   only if the TVS moves.
3. **Heat.** 2.04 cm² of copper per triac tab against the 2 cm² the design
   asks for, most of it on the bottom, joined by six vias of which four are
   in the solder land. The design's own figure is for 1 cm² and has margin,
   but nobody has measured a triac temperature.
4. **Three long hand-routed runs on the bottom layer**: USB (@@USB_LEN@@), the
   display bus (@@SPI_LEN@@) and the ADC lines (@@ADC_LEN@@). Each is longer than
   it would be without the keep-alive rule, and each is my reading of what the
   signal tolerates, not a measurement. The ADC lines carry a 1 kohm source
   past the comparator block; the filter capacitor is 18 mm from the pin.
5. **Parts are tight where a hand has to reach.** The module and the encoder
   are 0.02 mm of courtyard apart. C1 and K1 are soldered from the top
   between other parts; the check keeps parts over 1.5 mm tall 2.5 mm from
   their pads, which is my number.
6. **The buck output.** C5 hangs on a 0.5 mm track. The feedback divider
   senses at the inductor pad, where C6 and D25 are. I would move C5 if the
   block had another 3 mm.
7. **Silkscreen.** @@SILK_WEAK@@
8. **The autorouted nets** (section 10) were inspected by reading tables and
   pictures, not by a second person.
9. **The 0.5 mm clearance of the 24 VAC nets** is a choice, not a standard.
10. **The fan-out passes 0.5 mm above the pins of the sensor terminal block**
    on the bottom side, and the flow input runs 0.6 mm from the right board
    edge. Both are within the rules; both are where a slipped screwdriver or a
    scored board edge would reach.

## 14. What a person must look at

Before a board is ordered:

1. **Open the board in KiCad** and look at the valve block on all four layers:
   the fan-out, the islands, the vias in the tab lands.
2. **Section 6.** Redo one row of each table. Decide whether two vias per
   output are enough, and ask the fab for its plating thickness.
3. **Section 9.** Read review A2, section 3, then look at the copper round
   module pin 10 and round Q2, D22 and D7, on the screen and in
   `output/reports/layout-rules.md`. The check measures distances; it does not
   know what flux does.
4. **The three rules I could not meet as written**: R33 not at the pin (rule
   1), J7 further than 40 mm of track from the module (7.10), C30 and C31
   18 mm from their pins (7.11). Accept each or ask for a change.
5. **The footprints.** `design/footprints.md` lists what was compared with
   which drawing. Print the board 1:1 and lay the terminal headers, the
   display connector, the encoder and the cell holder on it.
6. **The placement file against the assembler's preview**, part by part for
   the polarised ones, with `zoneflow-a-pin1.csv`.
7. **The silkscreen** as the installer sees it: the terminal labels with the
   plugs in, the ground symbol at terminal 2.
8. **C1 and K1**: can a soldering iron reach their pads from the top with the
   neighbours fitted? Ask the assembler whether it fits them.
9. **The antenna.** The module's antenna end is beyond the board edge. The
   flow input passes 2 mm below that edge under the module.

## 15. Rebuilding

```
make -C hardware/zoneflow-a          # everything, and it fails if a check fails
make -C hardware/zoneflow-a board-check   # the checks only: DRC, contract, layout rules
make -C hardware/zoneflow-a reroute  # throw the stored routing away and route again
```

The autorouter's result is stored in `route/` with the hash of what it was
made from, and reused while that does not change. Routing again takes about
@@ROUTE_TIME@@ and does not give the same tracks twice.
