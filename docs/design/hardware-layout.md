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
| Schematic against the design netlist, pin for pin | `hwflow sch` | 348 components and 199 nets match pin for pin; the fitted parts of both builds match |
| Electrical rules of the schematic | `kicad-cli sch erc` | No error, no warning |
| Design rules of the board, with the zones filled again, errors and warnings | `kicad-cli pcb drc` | No error, no warning |
| Board against schematic: every footprint, every pad's net | `kicad-cli pcb drc --schematic-parity` | No difference |
| Connections not routed | the same DRC run | None |
| Mechanical contract, revision 3 | `hwflow mech` | Agrees: outline, 4 holes, 26 placed parts, height zones for 289 parts, keep-outs, bottom side, edge, soldering access |
| Layout rules of the fail-safe analysis | `hwflow.layoutrules` | Hold: 269 keep-away distances, one spacing, one angle, one pad ban |

Two settings shape the DRC result, and a reviewer should know them:

- **One check is switched off: `lib_footprint_mismatch`.** The builder takes
  59 strokes off footprint silkscreen outlines where they lay over
  a neighbour's pad (section 11). KiCad then reports each such footprint as
  different from its library copy. Nothing else in a footprint can differ: the
  board is built from the libraries on every run. This is the only check
  switched off, and no single violation is excluded.
- **Three rules are added** in `design/rules.kicad_dru`: no track of another
  net within 1 mm of a keep-alive track; the pads of the high-impedance nodes
  keep the usual pad clearance while their tracks keep 0.6 mm; and the fab
  limits of `hardware/rules/jlcpcb.kicad_dru`.

DRC is run on the board as `pcbbuild finish` leaves it: the stored autorouter
session, then three changes that section 10 describes (tracks widened to their
class width, two pieces of autorouted copper taken out, eighteen connections
closed by a second, small router). The autorouter's own numbers are there too.

The reports of this build are in `hardware/zoneflow-a/output/reports/`:
`drc.rpt`, `erc.rpt`, `layout-rules.md`, `routing.md`.

## 2. Stack-up and what the fab must be told

Four copper layers, 1.6 mm, JLCPCB's standard FR-4 build with 1 oz outer and
0.5 oz inner copper. **No special option is needed.** The fab limits used as
DRC minimums are in `hardware/rules/jlcpcb.toml`, with the page they were read
from.

| Layer | Use |
|---|---|
| F.Cu | Every part except C1 and K1. Signals. The 24 VAC entry. Ground pour in what is left |
| In1.Cu | Ground, one plane. No track is on it, and the build fails if one is |
| In2.Cu | 3.3 V pour over the logic area, shared with the logic signals the autorouter put there. Under the triacs: the shunt return plane. A copy of the valve fan-out. The opto-output lines |
| B.Cu | The valve fan-out. Nine heat islands under the triacs. The valve-common run. The pairs and buses of section 7. A second 3.3 V pour over the logic area. Ground pour along the keep-alive net and in what is left. C1 and K1 |

An earlier state of this layout had the valve fan-out on In2.Cu only and
needed 1 oz inner copper to survive a shorted output with the over-current
latch failed. That is gone: the fan-out is on B.Cu now (section 7.3), and the
calculation in section 6 holds with the default stack-up.

The smallest hole is 0.2 mm: the twelve thermal vias in the ground pad of the
module footprint, which is KiCad's stock footprint. The rules file records
0.2 mm as JLCPCB's smallest hole without a surcharge. Every via this layout
adds is 0.3 mm or larger. The narrowest track is 0.12 mm, in four pieces
under 0.3 mm long that the autorouter necked down at a pin (section 10); the
rules file records 0.10 mm as the fab's limit.

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
passes: 59 of 59 pairs of solids are clear of each other.

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

**The supply rails.** These were left to the autorouter in the Power class,
0.4 mm, and section 10 says what was done to them afterwards. The currents are
the peak column of the rail budget, hardware design section 4.2. By the formula
above a 0.4 mm track carries 1.23 A for 10 K on an outer layer (21.7 mil²) and
0.37 A on In2.Cu (17.5 um, 10.85 mil²).

| Path | Copper | Peak current | Rise |
|---|---|---|---|
| `V_BUCK`, clamp D25 to the OR-ing diode D3: the whole board except the relay coil | F.Cu, 0.4 mm, 17 mm | 0.73 A | 3 K |
| `V5`, D3 to the regulator U3: the whole 3.3 V load | F.Cu, 0.4 mm, 17 mm | 0.50 A | 1.3 K |
| `V_BUCK` to the sensor supply switch U11 | In2.Cu, 0.4 mm, 53 mm | 0.16 A, the highest limit of U11 | 1.4 K |
| `V5` to the backlight, the comparator and the sensor pull-ups | In2.Cu, 0.4 mm, 158 mm | 0.08 A | 0.3 K |
| `VBUS`, USB connector to the OR-ing diode D4 | 0.4 mm: 29 mm on B.Cu, 25 mm on F.Cu, 19 mm on In2.Cu | 0.58 A if the board runs from USB alone with every consumer at its limit; 0.22 A typical | 27 K at the peak, 3 K typical, in the 19 mm on In2.Cu |

Working for the first row: 10 x (0.733 / 1.23)^(1 / 0.44) = 10 x 0.596^2.27 =
3.1 K. Its resistance is 1.72e-8 x 0.017 / (0.4e-3 x 35e-6) = 21 milliohm,
15 mV at 0.73 A.

None of these is at risk by this calculation. They are narrower and longer
than I would draw them by hand, and the `VBUS` piece on the inner layer is the
one I would change first. See section 13.

## 7. What is routed by hand

Everything in this section is written out in `design/layout.py` as tracks,
vias and zones, laid before the autorouter runs and locked. These nets are
taken out of the autorouter's netlist (`[route] exclude`), so it can neither
add to them nor move them. They are 99 of the 199 nets.

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
- **The pair passes under the buck converter**: on B.Cu, between the board
  and the can of C1, with the pins of U1 and the switch node 1.6 mm above on
  F.Cu and the ground plane between them. It is a pair into a difference
  amplifier, and I did not estimate what couples into it.

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
- **Not by hand: the output beyond D25.** From the pad of D25 the autorouter
  took `V_BUCK` on to the OR-ing diode, the relay coil and the sensor switch,
  0.4 mm wide (section 6). The top of the feedback divider, R3, and the ripple
  capacitor C7 hang on that track 6 mm from the pad of D25, and not on the pad
  of the output capacitor. The converter therefore regulates the voltage at
  that point: 6 mm of 0.4 mm track is 7.6 milliohm, 5.5 mV below the capacitor
  at 0.73 A.

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
module pin. 0.25 mm tracks 0.2 mm apart, 169 and 166 mm long.
Between the connector and the resistors the autorouter laid them, 21 and
29 mm on F.Cu through the ESD array.

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
  and R29, at the corner of the module. R33 is 12 mm of track from the pin.
- On B.Cu under the right edge of the encoder and left along y = 78.6 to R89
  at detector B. 74 mm of track and three vias in all.
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
- Length from module pin to connector pin: 61 mm (chip select) to 72 mm
  (reset); clock 65 mm, data 62 mm, backlight 80 mm. Section 15.3 says
  "within about 40 mm". The direct distance is 42 mm; the way round is longer.
  The bus is specified at 15 MHz.
- On B.Cu the nearest copper is the 3.3 V pour on In2.Cu, 0.2 mm above, and
  not the ground plane. The autorouter was not kept off In2.Cu along the bus
  and crossed it there with six tracks. Five of them cut that pour under all
  five lanes, and a return current has to go round each cut (section 13).

### 7.11 ADC lines and the flow input

Two more nets of the list in section 9 had to cross the keep-alive to get
from where they start to the module, so they are routed by hand the other way
round:

- **`ADC_LO` and `ADC_HI`.** The series resistors R46 and R47 are at the
  amplifier outputs. From there the two lines run on B.Cu east along y = 67,
  south of everything the keep-alive touches, then north under the comparator
  block and the module to pins 39 and 38. 121 and 109 mm. The filter
  capacitors C30 and C31 sit on the way, 23 mm of track before the pins. The
  hardware design, section 12.5, wants them at the pins. There is no room
  beside pins 38 and 39: the board edge is 1.5 mm away. Twenty autorouted nets
  cross under the two lines on In2.Cu.
- **`SENS_FLOW`.** The clamp and filter of the flow input (D16, C45, R79) are
  at the right edge, above the sensor series resistors. From R79 the line
  goes up the right edge on B.Cu, round the top of the ADC lines under the
  module, and down to pin 5. 78 mm. The rain and auxiliary inputs keep
  their clamps above the display cable and are autorouted: they are not on
  the list.

### 7.12 LED row

The opto LED supply `V_DRV` is one 0.3 mm bus under the LED row with a stub to
each of the eighteen resistors. In each LED cell the three short links are
written out too.

### 7.13 Flash-over points at the field terminals

The specification asks for PCB spark gaps on field terminals "where layout
allows", and the hardware design, section 13.4, makes that a layout task for
every terminal of J2, J3 and J4.

- A ground rail runs on B.Cu below the terminal row. From it a tooth, 0.4 mm
  wide, points at the pad of every terminal that is not ground: fifteen of
  them. The solder mask is open over the tip of each tooth, and over the pad
  as on any pad.
- The tooth ends 0.6 mm from the pad. That is the smallest gap that keeps the
  0.5 mm clearance this layout gives the 24 VAC nets; a narrower gap would be
  bare copper at 24 VAC and ground closer together than anything else on the
  board.
- **I did not calculate when such a gap fires.** The figure usually quoted
  for air, 3 kV/mm, gives about 2 kV for 0.6 mm. The TVS at each terminal
  clamps at 77 V, so in a surge the TVS acts and the gap does nothing. The gap
  is the place a flash-over happens first if a TVS has failed open, and not a
  protection part. A gap narrow enough to fire before a part is damaged would
  be about 0.1 mm, and would sit across a 24 VAC supply that can feed an arc.
  I left that out and say so.
- The 24 VAC input terminals have none: the design does not ask for them
  there, and that input has the surge clamp D1 behind fuses.

## 8. Ground and planes

- **One ground.** In1.Cu is ground and nothing else. The only openings are the
  clearances of vias and through-hole pins and one 1.4 mm square at the Kelvin
  sense via (7.5). The AC return and the logic ground are the same net: the
  shunt R37 and the fuse F1 join them at one place, beside the terminal
  blocks, and the valve current does not use the plane at all. It returns
  through the `ISNS` plane on In2.Cu to the shunt.
- **How the plane is kept free.** The autorouter is told that In1.Cu is a
  power layer with a ground plane (toolchain document, 13.9), and
  `pcbbuild finish` fails if the routed board has a track on it. **This was
  not so until late in this work.** The setting I had used to keep the router
  off the layer was not obeyed, and every routing before the last had signal
  tracks on the ground plane, 2.2 m of them in one run. No check noticed: a
  pour flows round what is on its layer and DRC is content. I found it by
  listing the layers of the autorouted tracks, which the routing report now
  does on every build.
- **Plane vias.** Every surface-mount pad on ground or 3.3 V gets a 0.3 mm
  stub and a via, 0.6 mm with a 0.3 mm drill, placed by the builder at the
  nearest free point beside the pad: 125 of them. One ground pad, pin 2 of
  R35, has no free point beside it and is joined by the copper on F.Cu. The
  exposed pad of U1, the pads the Kelvin sense uses and a few pads at
  connectors are left out and done by hand. The module's ground pad has the
  twelve thermal vias of its footprint.
- **Where vias stand close, their clearance holes in the plane join.** The
  escape vias beside the expander U5 are 1.3 mm apart, and four of them in a
  row leave a slot 5 mm long in the ground plane; three shorter ones are
  beside it. The signals there are I2C and static outputs.
- **3.3 V has no plane of its own.** It is a pour on In2.Cu and a second pour
  on B.Cu, over the logic area and down both edges. With the ground plane
  kept free, the logic signals that need an inner layer take In2.Cu and cut
  its pour into pieces; the pour on B.Cu is cut by the buses of section 7.
  Every 3.3 V via reaches both, and what one pour does not join the other
  mostly does. On the routed board the pour on In2.Cu is in 21 pieces and the
  one on B.Cu in 15. Vias join most of them; ten tracks laid afterwards,
  189 mm in all, join the rest (section 10). DRC finds every 3.3 V pad
  connected. It does not say how well: section 13.
- **Ground on B.Cu.** Along the keep-alive net a corridor 4 mm wide is left
  out of the 3.3 V pour: there the ground pour stays, on both sides of the
  track, and ten vias 1.5 mm from the track tie it to the plane. An eleventh
  is written in the source and taken out after the routing is imported: it
  stands in the area under the encoder that the contract keeps free of
  wiring, and the stored routing was made with it. Ground also fills B.Cu
  below the logic area where nothing else is.
- **Pours on F.Cu.** Ground in whatever is left, clearance 0.3 mm, thermal
  relief on pads (0.3 mm gap, 0.4 mm spokes). Solid connection on the inner
  planes and on the heat islands.
- **Stitching.** Apart from the ten vias at the keep-alive there is no
  stitching pattern. The plane vias are the stitching: one per ground pad. An
  outer-layer pour fragment that touches no pad or via of its net is removed
  by the zone fill.
- **The valve block has no ground pour on B.Cu.** The fan-out and the islands
  take it. Ground there is the In1.Cu plane, above the shunt plane.
- **What a track on In2.Cu or B.Cu runs over.** In JLCPCB's standard four-layer
  build the two inner layers are about 1 mm apart, with the core between
  them, and each is 0.2 mm from its outer layer. I did not read the build's
  figures for this order; these are the usual ones. So a track on In2.Cu or
  B.Cu is far from the ground plane and close to whatever is on the other of
  those two layers, which is a pour in pieces. For the slow signals of this
  board I take that as acceptable. For the USB pair and the display bus it is
  one of the weak points of section 13.

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
| 1 mm from any other signal | No track or via of any net but ground within 1 mm of a `KA` track or via on the same layer. Smallest found: 1.03 mm, `OC_FAULT_N` | `[[keepaway]]`, scope "layer" |
| R33 at the module pin | R33 is at the corner of the module, 12 mm of track from the pin. **Not at the pin**: see below | |
| Away from the listed nets on every layer | No copper of a listed net, on any layer, within 1 mm of `KA` copper in plan view. Not even a crossing on another layer. Smallest found: 1.07 mm, the pads of the USB pins, see below | `[[keepaway]]`, scope "plan" |

The list, and where each net ends up:

| Net of the list | Nets checked | Nearest to `KA`, plan view |
|---|---|---|
| PSRAM lines, GPIO35 to 37 | The three unconnected module pads 28 to 30 | 14.2 mm |
| `TFT_BL` | `TFT_BL`, the gate `BL_G` and the cathode `TFT_LEDK` it switches | 3.0 mm |
| `U0TXD` | `U0TXD_MCU`, `U0TXD` | 4.8 mm |
| `AC_ZC` | `AC_ZC` | 14.9 mm |
| `SENS_FLOW` | `SENS_FLOW`, `N_FLOW`, `T_FLOW` | 5.5 mm |
| `ADC_LO`, `ADC_HI` | Those and `ISENSE_LO`, `ISENSE_HI` before the series resistors | 4.3 mm, `ISENSE_LO` |
| USB data | `USB_DP`, `USB_DM` and both behind the series resistors | 1.07 mm, the pads of module pins 13 and 14 |
| Display clock and data | `TFT_SCLK`, `TFT_MOSI` | 1.37 mm, `TFT_MOSI` |
| Buck switch node | `SW` | 49 mm |

The same check is made for `KA_N0`, the node behind the 220 ohm resistor R29:
to a bridge it is the keep-alive net. Its nearest listed nets are `TFT_BL` at
1.2 mm, `TFT_MOSI` at 1.45 mm and `ADC_HI` at 1.9 mm, each on another layer.

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
`KB_N1`, `KB_N0` or `KA_N1`: none has. TP8 is on `KA_N2`, 4.3 mm from the
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
| `RLY_G2`, gate of Q2 | 1.48 mm, `V_DRV` at the resistor array RN2 |
| `KB_N1` | 4.3 mm, 3.3 V |
| `KA_N1` | 1.22 mm, 3.3 V |
| `KA_N2`, reservoir of A | 4.3 mm, 3.3 V |
| `KA_INP`, comparator input of A | 2.0 mm, 3.3 V |

This check found three faults while I worked. In my own placement: a 3.3 V
pad 0.28 mm from TP8, and a 3.3 V plane via 0.55 mm from `KA_N1`. Both were
moved. In the autorouter's work: a `V_DRV` track 0.86 mm from the gate node
of Q2. That copper is taken out after the routing is imported and laid again
by a router that knows the rule (section 10).
What the check cannot see: the drain of Q2 is 0.5 mm from its gate, across
the SOT-23. That is the part, and the fault table of the hardware design has
that bridge.

### Rule 4: the two pumps apart

> Place the A pump (R29, C22, D7) and the B pump (R89, C51, D22) at least 10 mm
> apart.

`[[apart]]`: the nearest pads of the two groups are 42.4 mm apart. The
schematic note also asks for C51 at right angles to C22: `[[perpendicular]]`
finds 90 degrees.

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
| Current-sense block away from L1, the display clock and the antenna | The same | The amplifier is 39 mm from L1, 43 mm from the display clock and 80 mm from the antenna. **The Kelvin pair is not away from the buck converter**: section 13 |

## 10. What the autorouter did, and what was done about it

Freerouting 2.5.0 was given the 100 nets that are not routed by hand, three
layers (F.Cu, In2.Cu, B.Cu) and no fan-out stage. In1.Cu was declared a ground
plane. The stored run ended after 28 passes and 647 s. It does not give the
same board twice, so nothing below was done by routing again. Each step acts
on that one result after it is imported, in `pcbbuild finish`
([toolchain](hardware-toolchain.md), 13.10 and 13.11). The numbers per net are
in `output/reports/routing.md`.

Track on the finished board, in millimetres:

| Layer | By hand | Patch router | Autorouter |
|---|---|---|---|
| F.Cu | 1541 | 82 | 1645 |
| In1.Cu | 0 | 0 | 0 |
| In2.Cu | 459 | 60 | 2356 |
| B.Cu | 1648 | 215 | 686 |

518 vias: 334 by hand or beside a plane pad, 159 by the autorouter, 25 by the
patch router.

What it did badly, and what I did:

1. **It routed on the ground plane.** Every run before the last did, and
   section 8 says how I found it. This run has nothing on In1.Cu, and the
   build fails if a later one has.
2. **It narrowed tracks without saying so.** 310 segments in 61 nets, 354 mm,
   came back narrower than their net class. It used 0.15 mm where the class
   says 0.2 and 0.3 mm where it says 0.4, in pieces of `V_BUCK`, `V5` and
   `VBUS` among others. DRC has no objection to that. The build now sets
   each one to its class width and keeps the change
   wherever DRC stays clean. 19 segments, 16.9 mm, have no room and stay
   narrow:

    | Net | Stays narrow | What it carries |
    |---|---|---|
    | `T_FLOW` | 8.1 mm at 0.3 and 0.24 mm, class 0.4 | The flow sensor input, at the terminal: milliamps |
    | `BOOT` | 2.8 mm at 0.15 mm | A strapping pin |
    | `V_DRV` | 2.6 mm at 0.3 mm on In2.Cu, class 0.4 | The opto LED supply, 45 mA at most |
    | `FAULT` | 1.8 mm at 0.15 mm | A logic level |
    | `EN`, `I2C0_SCL`, `ZD_Z1_N`, `ZD_Z5_N` | Under 0.6 mm each at 0.12 mm, where the track leaves a pin | Logic levels |

3. **It left two nets unfinished and cut the 3.3 V pours into pieces that had
   no connection.** `PGOOD`, `VIN_MON` and nine links between pieces of pour
   were open. A small maze router (`hwflow.patchroute`) closed them, and a
   tenth link that its own tracks then opened:

    | Net | Closed with | Width |
    |---|---|---|
    | 3.3 V | Ten links, 6.8 to 36.9 mm, 189 mm and 11 vias in all | 0.4 mm, except one of 23 mm at 0.2 mm |
    | `VIN_MON` | 120 mm, 10 vias | 0.2 mm |
    | `PGOOD` | 10 mm, 2 vias | 0.2 mm |

4. **It put a supply rail beside a node that must not see one.** It joined
   the four `V_DRV` pads of the resistor array RN2 with a bar 0.86 mm from
   the gate node of Q2. Layout rule 3 of section 9 failed the build. The bar
   and its via are taken out; the patch router,
   which is given the rule, joined the four pads through their own centres
   and brought the rail in from the other side, 20 mm with one via. The node
   is now 1.48 mm from the rail.
5. **It fed the regulator through a neck.** `V5` from the OR-ing diodes to
   U3, the whole 3.3 V load, went through 1.7 mm of 0.3 mm track that cannot
   be widened where it lies. That piece is taken out with its via. The
   patch router laid 8.8 mm at 0.4 mm to the regulator in its place, and
   9.9 mm to the inner-layer branch that the via had fed.
6. **It boxed in the expander.** In earlier runs its own vias round the
   TSSOP left pins of U5 that nothing could reach. Every pin of U5 now has a
   stub and a via written by hand before it runs; the nine it did not use
   are removed afterwards.
7. **Vias in pads.** None: no autorouted via has its centre inside a
   surface-mount pad.
8. **Wandering tracks.** No net is more than 2.5 times as long as its pins
   are apart, measured along the axes. The longest it laid are the rain and
   auxiliary sensor inputs, 124 and 128 mm on In2.Cu, from the terminals to
   their clamps above the display cable.
9. **Plane splits.** In1.Cu has none. In2.Cu carries 2356 mm of autorouted
   track in 51 nets, which is what cuts its 3.3 V pour into 21 pieces
   (section 8). Twenty of those nets cross under the ADC lines, six under the
   USB pair and six under the display bus.

What I did not fix:

- **`VIN_MON`** is the tap of the input-voltage divider, 300 kohm over
  10 kohm with 100 nF across the lower resistor at the divider. The divider
  is at the 24 VAC entry and the module pin in the opposite corner, 113 mm
  away along the axes. The patch router took it there in 120 mm, changing
  layer ten times. It is a slow level read by the ADC and the capacitor is at
  the far end from the pin. I would lay it by hand before the next routing
  run, and I did not want to route again for it.
- **C25**, the 100 nF capacitor "at U8", is 7.5 mm from the supply pin of
  the op-amp it serves, and its via lands in a piece of pour that reaches
  nothing else. The 0.2 mm link of item 3, 23 mm long, is its only
  connection to that pin. U8 is a 1 MHz op-amp and has the pours at its pin;
  the capacitor does little where it is.
- **The supply rails stay as the autorouter laid them**, at the class width,
  with the currents of section 6. The top of the feedback divider senses
  6 mm down the output track (7.6).
- **The inner-layer tracks under the hand-routed buses** (item 9).

What I looked at:

- **The tables** of `routing.md`.
- **Each copper layer** of the finished board as a plot at 13 pixels per
  millimetre, all four.
- **Closer plots** of the converter, the expander and the area round C25.
- **The top render** at 47 pixels per millimetre, for the rotation check of
  section 12.

I did not follow each of the 100 nets from pin to pin.

## 11. Silkscreen

- **Terminal labels.** Behind every terminal, in the strip the output columns
  were moved up 1 mm to clear: `24V~`, `24V~`, `COM`, `COM`, `MV`, `Z1` to
  `Z8`, `5V`, `FLOW`, `GND`, `RAIN`, `GND`, `AUX`, `GND`. They are the names
  of the [wiring page](../getting-started/wiring.md).
- **The ground leg.** Terminal 2 of the 24 VAC block is the board's ground.
  A ground symbol is drawn right of its label.
- **Fuse ratings.** `F4 3A T` and `F1 3A T` above the two holders. The
  rating of F5, a soldered fuse, found no room and is not printed.
- **The cell.** `CR2032` above the holder, and `+` above its positive land,
  which is the left one.
- **Connectors and buttons.** `USB`, `QWIIC`, `RESET`, `BOOT`, `BACK`, `HOME`,
  and the six signal names at the debug pads.
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
  this build: 23 rounds, 59 strokes removed, 33 designators moved, 7 hidden,
  one optional label dropped.
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
| `reports/` | ERC and DRC reports, the layout-rule table (`layout-rules.md`), the routing table (`routing.md`), a summary |

The renders and the STEP model show lands only where KiCad has no 3D model:
the two fuse holders, F5, the PTC fuse F3 and the nine of the outputs, the USB-C connector, the reset
and boot buttons, the encoder, the two front buttons, the display connector
and the cell holder.

**The rotation check.** The placement files take each part's rotation from
the board and add a correction per package family from
`hardware/rules/jlcpcb-cpl-rotations.csv`.

I checked pad 1 of each of the 84 polarised or multi-pin parts on the top
side against KiCad's 3D model of the part. I rendered the top side at 47
pixels per millimetre, fitted the picture to the four screw holes (worst
error 0.02 pixel), drew a circle at pad 1 of every part of
`zoneflow-a-pin1.csv`, and looked at each one. The cathode band of the 25
power and signal diodes, the dot of every IC and of the 14 SOT-23 parts, the
corner mark of the module, the notch of the nine opto-triacs and the first
lead of the nine triacs are where pad 1 is.

Three groups it could not show:

- **The 11 LEDs.** Their model has a mark at the end away from pad 1 that I
  cannot read with certainty. Pad 1 is the cathode in the footprint and in
  the netlist.
- **The cell holder and the encoder.** They have no model.
- **The connectors.** They fit one way.

It does not check whether JLCPCB's library holds each part the way the
correction table assumes. The rows of the table are copied from
another project; one is set by analogy; the families without a row are placed
with KiCad's own rotation. **The rotation column is unverified until someone
compares the assembler's placement preview with `zoneflow-a-pin1.csv`, part
by part.**

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
- What the autorouter got wrong was found by measuring its result (tracks on
  the plane, narrowed tracks, a rail beside a guarded node), and each of the
  three is now a check or a step of the build.

What is weak, most serious first:

1. **Nothing has been built.** Every claim here is a check on files.
2. **The output vias under a shorted output** (section 6): two 0.3 mm vias
   per output, with a plating thickness I assumed. More or larger vias fit
   only if the TVS moves.
3. **Heat.** 2.04 cm² of copper per triac tab against the 2 cm² the design
   asks for, most of it on the bottom, joined by six vias of which four are
   in the solder land. The design's own figure is for 1 cm² and has margin,
   but nobody has measured a triac temperature.
4. **Three long hand-routed runs on the bottom layer**: USB (169 mm), the
   display bus (61 to 72 mm) and the ADC lines (121 and 109 mm). Each is
   longer than it would be without the keep-alive rule, and each is my
   reading of what the signal tolerates, not a measurement. The ADC lines
   carry a 1 kohm source past the comparator block; the filter capacitor is
   23 mm from the pin. None of the three runs over unbroken copper: the pour
   above them on In2.Cu is cut by autorouted tracks, 6 times under the USB
   pair, 6 under the display bus and 20 under the ADC lines, and the ground
   plane is 1 mm further away.
5. **3.3 V has no solid plane.** It is two pours in 21 and 15 pieces, joined
   by vias and by ten tracks. Every pad is connected; I did not calculate the
   impedance from the regulator to any load, and the decoupling capacitor C25
   is connected so poorly that it does little (section 10).
6. **The autorouter laid the 5 V chain.** Buck output to OR-ing diode to
   regulator runs on 0.4 mm tracks, the USB feed is partly on the thin inner
   layer, and the feedback is sensed 6 mm down the load track. The numbers of
   section 6 say it is safe. I would not draw a power path this way, and it
   is the first thing I would do by hand in a revision.
7. **A maze router that knows only clearances closed two kinds of
   connection.** They are `VIN_MON`, 120 mm through ten vias, and the 3.3 V
   links (section 10).
8. **Parts are tight where a hand has to reach.** A frame land of the encoder
   is 1.6 mm from the nearest pad of the module, and the two courtyards are
   0.02 mm apart; both positions are fixed by the contract.
   C1 and K1 are soldered from the top
   between other parts; the check keeps parts over 1.5 mm tall 2.5 mm from
   their pads, which is my number.
9. **The Kelvin sense pair runs under the buck converter** (section 7.5),
   shielded by the ground plane and by nothing else. The other way round
   crosses the eighteen gate lines. If the low current range reads noise at
   the switching frequency, this is where to look.
10. **The buck output capacitors.** C5 hangs on a 0.5 mm track. I would move
    C5 if the block had another 3 mm.
11. **Silkscreen.** Seven designators found no place and are hidden: U1 and
    the test pads TP9, TP14, TP22, TP27, TP28 and TP29. A test pad without
    its name on the board is found only with the interactive BOM or the
    assembly drawing. Designators of neighbouring test pads stand so close
    that they read as one word (`TP6TP21`), and `SW2` stands against `BOOT`.
    DRC finds no overlap in any of them. The plus sign of the cell holder's
    own footprint lost a stroke to the cleaner and is now a short bar beside
    the positive land; the `+` I added is above that land.
12. **The autorouted nets** (section 10) were inspected by reading tables and
    pictures, not by a second person.
13. **The 0.5 mm clearance of the 24 VAC nets** is a choice, not a standard.
14. **The fan-out passes 0.5 mm above the pins of the sensor terminal block**
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
   23 mm from their pins (7.11). Accept each or ask for a change.
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
10. **Section 10, "What I did not fix".** Decide whether `VIN_MON`, C25 and
    the 5 V chain may stay as they are for a first prototype. Each wants a
    change to the hand-routed copper and then a new autorouter run, which
    gives a different board: every check and this whole review would be done
    again.
11. **`output/reports/routing.md`.** Every net the patch router touched and
    every track that stays narrower than its class is listed there. Find each
    on the board.
12. **The tools.** The board is what `tools/hwflow` makes of the sources. The
    width pass, the patch router and the layout-rule check are new in this
    work and have unit tests on made-up boards only. A reader who distrusts
    them can run `kicad-cli pcb drc --schematic-parity` on the committed
    board without any of them.

## 15. Rebuilding

```
make -C hardware/zoneflow-a          # everything, and it fails if a check fails
make -C hardware/zoneflow-a board-check   # the checks only: DRC, contract, layout rules
make -C hardware/zoneflow-a reroute  # throw the stored routing away and route again
make -C hardware/zoneflow-a patches  # after a new routing: close what it left open
```

The autorouter's result is stored in `route/` with the hash of what it was
made from, and reused while that does not change. Routing again takes about
eleven minutes and does not give the same tracks twice: the patches of
`design/patches.toml` and the two `[[unroute]]` boxes of `design/layout.py`
belong to the stored routing and have to be made again for a new one, and
sections 8 to 10 of this page describe the stored one.
