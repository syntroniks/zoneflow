# ZoneFlow revision A: PCB layout

!!! warning "No board has been made"
    This layout exists as files. Nothing in it has been fabricated, soldered,
    powered or measured. Where this page says a rule "holds" or a check
    "passes", a program compared numbers in a file, and section 1 names the
    program. Section 13 lists what I think is weak, and section 14 what a
    person must look at before anyone orders a board.

!!! note "Changed by the layout review"
    The [independent review](hardware-layout-review.md) changed this board
    and routed it again. This page describes the board after the review.
    The reviewer rewrote sections 1, 2, 6, 7.14 to 7.16, 8, 10, 12 and 15 and
    corrected numbers elsewhere. Sections 13 and 14 are the author's lists as
    written before the review; a note on each item says what became of it.

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
| Schematic against the design netlist, pin for pin | `hwflow sch` | 353 components and 200 nets match pin for pin; the fitted parts of both builds match |
| Electrical rules of the schematic | `kicad-cli sch erc` | No error, no warning |
| Design rules of the board, with the zones filled again, errors and warnings | `kicad-cli pcb drc` | No error, no warning |
| Board against schematic: every footprint, every pad's net | `kicad-cli pcb drc --schematic-parity` | No difference |
| Connections not routed | the same DRC run | None |
| Mechanical contract, revision 3 | `hwflow mech` | Agrees: outline, 4 holes, 26 placed parts, height zones for 294 parts, keep-outs, bottom side, edge, soldering access |
| Layout rules of the fail-safe analysis | `hwflow.layoutrules` | Hold: 278 keep-away distances, one spacing, one angle, one pad ban |
| Placement files against JLCPCB's library footprints, on demand | `make cpl-check` | 312 and 267 placements, none misfits |

Two settings shape the DRC result, and a reviewer should know them:

- **One check is switched off: `lib_footprint_mismatch`.** The builder takes
  59 strokes off footprint silkscreen outlines where they lay over
  a neighbour's pad (section 11). KiCad then reports each such footprint as
  different from its library copy. Nothing else in a footprint can differ: the
  board is built from the libraries on every run. This is the only check
  switched off, and no single violation is excluded.
- **Four rules are added** in `design/rules.kicad_dru`: no track of another
  net within 1 mm of a keep-alive track; the pads of the high-impedance nodes
  keep the usual pad clearance while their tracks keep 0.6 mm; the two pads
  of the solder jumper JP1 may be 0.3 mm apart, which is how a jumper is
  made; and the fab limits of `hardware/rules/jlcpcb.kicad_dru`, with no
  hole under 0.3 mm among them.
- **A clearance that KiCad's footprint for U1 carried is removed.** It was
  0.2 mm and outranked the 0.4 mm of the net class. The builder prints the
  footprint when it does that.

DRC is run on the board as `pcbbuild finish` leaves it: the stored autorouter
session, then three changes that section 10 describes (tracks widened to their
class width, four pieces of autorouted copper taken out, seventeen connections
closed by a second, small router and by hand). The autorouter's own numbers
are there too.

The reports of this build are in `hardware/zoneflow-a/output/reports/`:
`drc.rpt`, `erc.rpt`, `layout-rules.md`, `routing.md`, `placement-check.md`.

## 2. Stack-up and what the fab must be told

Four copper layers, 1.6 mm, JLCPCB's standard FR-4 build with 1 oz outer and
0.5 oz inner copper. **No special option is needed.** The fab limits used as
DRC minimums are in `hardware/rules/jlcpcb.toml`, with the page they were read
from.

| Layer | Use |
|---|---|
| F.Cu | Every part except C1 and K1. Signals. The 24 VAC entry. Ground pour in what is left |
| In1.Cu | Ground, one plane. No track is on it, and the build fails if one is |
| In2.Cu | 3.3 V, one plane over the logic area. The autorouter may not use the layer, and the build fails if an autorouted track is on it. Eleven patched connections pass under an obstacle there, 114 mm in all (section 10). Under the triacs: the shunt return plane. A copy of the valve fan-out. The nine opto-output lines, by hand |
| B.Cu | The valve fan-out. Nine heat islands under the triacs. The valve-common run. The pairs and buses of section 7. The USB supply. Autorouted signals. Ground pour in what is left. C1 and K1 |

An earlier state of this layout had the valve fan-out on In2.Cu only and
needed 1 oz inner copper to survive a shorted output with the over-current
latch failed. That is gone: the fan-out is on B.Cu now (section 7.3), and the
calculation in section 6 holds with the default stack-up.

The smallest hole is 0.3 mm. KiCad's stock module footprint has twelve
0.2 mm vias in its ground pad; `design/footprints.py` drills them 0.3 mm in
0.7 mm lands, because JLCPCB charges for a smaller hole
([review](hardware-layout-review.md), finding L9). DRC fails on any hole
under 0.3 mm. The narrowest track is 0.10 mm, for 0.4 mm of `ZD_Z5_N` where
the autorouter left a pin; three more pieces of 0.2 mm each are 0.12, 0.15
and 0.19 mm wide (section 10). The rules file records 0.10 mm as the fab's
limit.

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
| Four output vias, 0.3 mm drill, 18 um plating | 4 x 0.0180 mm² | 9 K | 33 K |
| Two such vias, as the layout had before its review | 2 x 0.0180 mm² | 37 K | 131 K |
| Three A1 vias, one 0.3 mm and two 0.4 mm drill, 18 um | 0.065 mm² | 11 K | 40 K |

Working for the lane with its copy: 0.9 mm x (0.035 + 0.0175) mm = 4.725e-8 m²;
34 x 1.72e-8 / ((4.725e-8)² x 3.45e6) = 5.85e-7 / 7.70e-9 = 76 K.
A via barrel is pi x (hole + plating) x plating: pi x 0.318 x 0.018 =
0.0180 mm² for a 0.3 mm hole. The 18 um is the "Average Hole Plating
Thickness" of JLCPCB's capabilities page, read 2026-11-11. It is an average
and not a promised minimum.

What this says:

- **The worst case no longer depends on the inner copper.** A short with the
  over-current latch failed takes a fan-out lane from 70 °C to about 146 °C
  with both layers carrying, and to about 241 °C if the copy on In2.Cu carried
  nothing. Both are for a fraction of a second and below soldering
  temperature. The copy is margin, not a requirement.
- **The vias are no longer the weakest part of the path.** The layout had
  two 0.3 mm vias per output, and with 18 um of plating they heated by 131 K
  in the worst case. The review made them four, in a row 0.8 mm apart at the
  end of the lane, and added a third via at each A1 lead. The lane is the
  hottest piece again.
- Two conductors in parallel share a current by their cross-sections only if
  they are equally long. The lane and its copy are the same shape, joined at
  the terminal pin and at the two vias.

**The supply rails.** The review routed these by hand on the outer layers
(section 7.14). The currents are the peak column of the rail budget, hardware
design section 4.2.

| Path | Copper | Peak current | Rise | Drop |
|---|---|---|---|---|
| `V_BUCK`, clamp D25 to the OR-ing diode D3: the whole board except the relay coil | F.Cu, 9.1 mm at 0.8 mm and 6.5 mm at 0.55 mm | 0.73 A | 1.8 K in the narrow piece | 11 milliohm, 8 mV |
| `V5`, D3 to the regulator U3: the whole 3.3 V load | F.Cu, 6.8 mm at 0.8 mm and 9.9 mm at 0.7 mm | 0.50 A | 0.5 K | 11 milliohm, 6 mV |
| `VBUS`, USB connector to the OR-ing diode D4 | B.Cu, 42 mm at 0.7 mm, two vias at each end | 0.58 A if the board runs from USB alone with every consumer at its limit; 0.22 A typical | 0.7 K at the peak | 30 milliohm, 17 mV |
| `V_BUCK` to the sensor supply switch U11 | B.Cu, 61 mm at 0.5 mm | 0.16 A, the highest limit of U11 | 0.2 K | 68 milliohm, 11 mV |

Working for the narrow piece of the first row: 0.55 mm is 21.65 mil, times
1.378 is 29.8 mil²; 29.8^0.725 = 11.7; 0.048 x 2.754 x 11.7 = 1.55 A for
10 K; at 0.733 A, 10 x (0.733 / 1.55)^(1 / 0.44) = 1.8 K. The 0.55 mm is
where `V_BUCK` and `V5` pass side by side through the 2.1 mm between the
anode pad of D4 and the pins of U3.

## 7. What is routed by hand

Everything in this section is written out in `design/layout.py` as tracks,
vias and zones, laid before the autorouter runs and locked. These nets are
taken out of the autorouter's netlist (`[route] exclude`), so it can neither
add to them nor move them. They are 104 of the 200 nets.

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

- **Output net.** From the four vias at the end of the fan-out lane (7.3),
  0.3 mm drill in a row 0.8 mm apart, a 1.2 mm track on F.Cu into the lower
  PTC pad. A second 1.2 mm link joins the TVS and the
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
- **A1.** Three vias beside the A1 lead, one 0.3 mm and two 0.4 mm drill,
  into the shunt plane on In2.Cu. A 0.6 mm track to the gate resistor.
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
  four vias under the column join the two.
- Five lanes carry eight of the nine. The last output, zone 8, starts on a
  sixth lane level with the terminal pins, for the 5 mm right of its own pin
  where no other pin is, and steps up before the next terminal block begins.
  `layout.py` fails if that lane is used anywhere else.

### 7.4 Shunt return

`ISNS` is a plane on In2.Cu under all nine triacs, 68.7 x 21.5 mm. The valve
current goes from each A1 lead down three vias, through the plane, and up two
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
  amplifier, and I did not estimate what couples into it. The review did:
  about 0.2 mV at the switching frequency before two filters
  ([review](hardware-layout-review.md), section 8).

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
- **Ground of U1.** The exposed pad has six vias, 0.3 mm drill, into the
  ground plane and the ground pour on B.Cu, and a 0.5 mm track on F.Cu ties
  pin 1 to it. Pin 1 and the ground pad of C2 have a second via each. The
  review added these: the pad had no via before (finding L2).
- **The output beyond D25** is by hand since the review: section 7.14. The
  top of the feedback divider, R3, and the ripple capacitor C7 have a track
  of their own from the pad of D25, 0.3 mm wide, that carries no load
  current. The converter regulates the voltage at that pad.

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
Between the connector and the resistors the autorouter laid them, 24 and
30 mm on F.Cu through the ESD array, with no via. The array U2 stands at
(3.3, 17.7) since the review, so that neither line passes the screw head at
hole C.

This is not an impedance-controlled pair. The review calculated it from the
usual microstrip formulas: about 60 ohm each line and 98 ohm differential
over the 3.3 V plane, which is now whole under all of it. It is long because the contract puts the connector in one corner
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
- On B.Cu the nearest copper is the 3.3 V plane on In2.Cu, 0.2 mm above, and
  not the ground plane. Since the review the autorouter may not use In2.Cu.
  One patched track, `U0TXD`, crosses under the bus there for 3.8 mm, where
  six autorouted tracks did.

### 7.11 ADC lines and the flow input

Two more nets of the list in section 9 had to cross the keep-alive to get
from where they start to the module, so they are routed by hand the other way
round:

- **`ADC_LO` and `ADC_HI`.** The series resistors R46 and R47 are at the
  amplifier outputs. From there the two lines run on B.Cu east along y = 67,
  south of everything the keep-alive touches, then north under the comparator
  block and the module to pins 39 and 38. 129 and 109 mm. The filter
  capacitors C30 and C31 sit on the way, 23 mm of track before the pins. The
  hardware design, section 12.5, wants them at the pins. There is no room
  beside pins 38 and 39: the board edge is 1.5 mm away. The layer above the
  two lines is the 3.3 V plane since the review. Three patched nets cross
  them on In2.Cu, at four places, where twenty autorouted nets did.
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

### 7.14 The 5 V chain and the USB supply

By hand since the review, on the outer layers. Section 6 has the currents
and the calculation for each width.

| From | To | Layer | Width |
|---|---|---|---|
| D25, the clamp at the buck output | OR-ing diode D3, anode | F.Cu | 0.8, then 0.55 past the regulator's pins |
| D25 | R3 and C7, the feedback sense | F.Cu | 0.3, its own track |
| D25 | Relay coil, K1 pin A1, and D5 | F.Cu | 0.5 |
| D3 anode | Sensor supply switch U11 and C47 | B.Cu, along y = 63.7 | 0.5 |
| D3 and D4, cathodes (`V5`) | TP3, C10 | F.Cu | 1.0 and 0.8 |
| C10 | Regulator U3, pin 3 | F.Cu | 0.7 |
| U3, pin 2 and tab (3.3 V) | The 3.3 V plane | F.Cu bar under the body, five vias | 2.0 |
| J5, the four `VBUS` pins | ESD array U2, C13 | F.Cu and B.Cu | 0.5 |
| J5 | OR-ing diode D4, anode | B.Cu, round hole C and up the left edge inside K1 | 0.7, two vias at each end |

- **Where it is narrow.** `V_BUCK` and `V5` pass side by side through the
  2.1 mm between the anode pad of D4 and the pins of U3. `V_BUCK` is 0.55 mm
  there for 6.5 mm.
- **The regulator's output.** Pin 2 and the tab of U3 are one net. The
  builder's plane-via placer uses a via of the same net again when one is
  within reach, so it gave the tab none of its own and joined it by a 0.3 mm
  stub to the via of C12, 3.8 mm away. The whole 3.3 V load reached the plane
  through the one via at pin 2 and that stub, and the tab had only its own
  land to lose heat to. The bar and its five vias are the review's.

### 7.15 `VIN_MON` and the relay read-back

Two lines into ADC pins of the module, by hand since the review.

- **`VIN_MON`.** R9, R10 and C9 stand in one column, so the tap is one
  1.6 mm track. From C9 the line goes left on F.Cu, down a via at
  (29.2, 39.6), and on B.Cu up x = 27.4 to y = 56.6, right along y = 56.6
  under the opto-triacs to x = 98.3, and up x = 98.3 under the comparator
  block to a via at (98.3, 89.4) beside module pin 15. 137 mm and four vias.
  The two middle vias, at (98.3, 64.9) and (98.3, 69.6), step over the ADC
  lines. The 3.3 V plane is above it all the way, but for one patched track
  that crosses it. The line was 120 mm through ten vias, laid by the patch
  router.
- **`RLY_B_RB`.** From R94 and R95 at (56, 69) up x = 56.2 on F.Cu, across on
  B.Cu at y = 82.07 to x = 75.9, and up on F.Cu to module pin 7. 71 mm and
  two vias. The source is 50 kohm.

### 7.16 Over-voltage detector U13

[Answers A2](hardware-answers-a2.md) added U13, a TPS3701, with R13, R100,
C55 and C56. It stands above the test pad of `OC_TRIP_N`, the net its output
pulls low, 8 mm from pin 1 of U7.

- **`OV_DIV`** joins pin 4 of U13, C55, R100 and R13 with 5.0 mm of track on
  F.Cu and no via. The node is 99 kohm and its threshold is 0.4 V, so
  `board.toml` keeps the switch node, the keep-alive and the six display
  lines 10 mm from it in plan view, and the build measures that.
- **C56** is at pin 5. Pins 2 and 3 share a ground via.
- **`OC_TRIP_N`** goes from pin 6 round the left of U13 to TP10, 7.4 mm.

## 8. Ground and planes

- **One ground.** In1.Cu is ground and nothing else. The only openings are the
  clearances of vias and through-hole pins and one 1.4 mm square at the Kelvin
  sense via (7.5). The AC return and the logic ground are the same net: the
  shunt R37 and the fuse F1 join them at one place, beside the terminal
  blocks, and the valve current does not use the plane at all. It returns
  through the `ISNS` plane on In2.Cu to the shunt.
- **How the plane is kept free.** The autorouter is told that In1.Cu is a
  power layer with a ground plane (toolchain document, 13.9), and
  `pcbbuild finish` fails if the routed board has an autorouted track on it. **This was
  not so until late in this work.** The setting I had used to keep the router
  off the layer was not obeyed, and every routing before the last had signal
  tracks on the ground plane, 2.2 m of them in one run. No check noticed: a
  pour flows round what is on its layer and DRC is content. I found it by
  listing the layers of the autorouted tracks, which the routing report now
  does on every build.
- **Plane vias.** Every surface-mount pad on ground or 3.3 V gets a 0.3 mm
  stub and a via, 0.6 mm with a 0.3 mm drill, placed by the builder at the
  nearest free point beside the pad: 120 of them. Left out and done by hand:
  the exposed pad of U1 (six vias), the tab and pin 2 of U3 (five), the pads
  the Kelvin sense uses, a few pads at connectors, and five pads the placer
  has no point for (C25, U2, U13 and R35). The module's ground pad has the
  twelve vias of its footprint, drilled 0.3 mm.
- **Where vias stand close, their clearance holes in the plane join.** The
  escape vias beside the expander U5 are 1.3 mm apart, and four of them in a
  row leave a slot 5 mm long in the ground plane; three shorter ones are
  beside it. The signals there are I2C and static outputs.
- **3.3 V is a plane on In2.Cu.** One zone over the logic area and down both
  edges. The autorouter is told that In2.Cu is a power layer with a 3.3 V
  plane, as In1.Cu is one with a ground plane, and it routes on F.Cu and
  B.Cu only. What is on In2.Cu beside the plane is laid by hand: the shunt
  plane, the copies of the fan-out, and the nine opto-output lines, each of
  which cuts a slot 9 mm long under its opto-triac. On the routed board the
  plane is one piece of 71 cm². Eleven connections that the patch router or
  the reviewer closed pass under an obstacle on this layer: 114 mm of slot in
  all, the longest 21 mm (section 10). Before the
  review the autorouter used In2.Cu: its 2.4 m of track there cut the 3.3 V
  pour into 21 pieces, a second pour on B.Cu was in 15, and ten tracks from
  the patch router joined what the vias did not.
- **Ground on B.Cu.** B.Cu has no 3.3 V pour any more. Ground fills it
  wherever nothing else is, on both sides of the keep-alive track too, and
  ten vias 1.5 mm from that track tie the pour to the plane.
- **Pours on F.Cu.** Ground in whatever is left, clearance 0.3 mm, thermal
  relief on pads (0.3 mm gap, 0.4 mm spokes). Solid connection on the inner
  planes and on the heat islands.
- **Stitching.** Apart from the ten vias at the keep-alive there is no
  stitching pattern. The plane vias are the stitching: one per ground pad. An
  outer-layer pour fragment that touches no pad or via of its net is removed
  by the zone fill.
- **The valve block has no ground pour on B.Cu.** The fan-out and the islands
  take it. Ground there is the In1.Cu plane, above the shunt plane.
- **What a track on B.Cu runs over.** In JLCPCB's standard four-layer build
  the two inner layers are about 1 mm apart, with the core between them, and
  each is 0.2 mm from its outer layer. I did not read the build's figures for
  this order; these are the usual ones. So a track on B.Cu is far from the
  ground plane and close to In2.Cu, which over the logic area is the 3.3 V
  plane. The decoupling capacitors tie that plane to ground for fast
  currents. The hand-routed buses of section 7 all run under it.

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
| 1 mm from any other signal | No track or via of any net but ground within 1 mm of a `KA` track or via on the same layer. Smallest found: 1.04 mm, `BTN_BACK` and `ENC_B` | `[[keepaway]]`, scope "layer" |
| R33 at the module pin | R33 is at the corner of the module, 12 mm of track from the pin. **Not at the pin**: see below | |
| Away from the listed nets on every layer | No copper of a listed net, on any layer, within 1 mm of `KA` copper in plan view. Not even a crossing on another layer. Smallest found: 1.07 mm, the pads of the USB pins, see below | `[[keepaway]]`, scope "plan" |

The list, and where each net ends up:

| Net of the list | Nets checked | Nearest to `KA`, plan view |
|---|---|---|
| PSRAM lines, GPIO35 to 37 | The three unconnected module pads 28 to 30 | 14.2 mm |
| `TFT_BL` | `TFT_BL`, the gate `BL_G` and the cathode `TFT_LEDK` it switches | 3.0 mm |
| `U0TXD` | `U0TXD_MCU`, `U0TXD` | 8.9 mm |
| `AC_ZC` | `AC_ZC` | 14.9 mm |
| `SENS_FLOW` | `SENS_FLOW`, `N_FLOW`, `T_FLOW` | 5.5 mm |
| `ADC_LO`, `ADC_HI` | Those and `ISENSE_LO`, `ISENSE_HI` before the series resistors | 3.6 mm, `ISENSE_LO` |
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
| `KA_INP`, comparator input of A | 2.2 mm, 3.3 V |

This check found three faults while I worked. In my own placement: a 3.3 V
pad 0.28 mm from TP8, and a 3.3 V plane via 0.55 mm from `KA_N1`. Both were
moved. In the autorouter's work: a `V_DRV` track 0.86 mm from the gate node
of Q2. That copper is taken out after the routing is imported and laid again
(section 10). The autorouter did the same on the routing of the review, and
the check stopped the build again.
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

This section describes the routing that the [review](hardware-layout-review.md)
made. The first routing gave the autorouter three layers, and its 2.4 m of
track on In2.Cu cut the 3.3 V pour into 21 pieces. This one gives it two.

Freerouting 2.5.0 was given the 96 nets that are not routed by hand, two
layers (F.Cu and B.Cu) and no fan-out stage, with its usage reporting off and
no network. In1.Cu was declared a ground plane and In2.Cu a 3.3 V plane. The
stored run ended after 29 passes and 2381 s. It does not give the same board
twice, so nothing below was done by routing again. Each step acts on that one
result after it is imported, in `pcbbuild finish`
([toolchain](hardware-toolchain.md), 13.10, 13.11 and 13.13). The numbers per
net are in `output/reports/routing.md`.

Track on the finished board, in millimetres:

| Layer | By hand | Patches | Autorouter |
|---|---|---|---|
| F.Cu | 1811 | 334 | 2733 |
| In1.Cu | 0 | 0 | 0 |
| In2.Cu | 444 | 113 | 0 |
| B.Cu | 1903 | 480 | 1650 |

641 vias: 386 by hand or beside a plane pad, 164 by the autorouter, 91 in
patches.

What it did badly, and what was done:

1. **It could not finish on two layers.** The hand-routed buses on B.Cu are
   long walls, and a signal can cross one only on F.Cu, where the parts are.
   17 connections were left open. They were closed in three ways:

    | How | Connections | Nets |
    |---|---|---|
    | Patch router, F.Cu and B.Cu | 5 | `SENS_PWR_EN` twice, `T_RAIN`, `PGOOD`, `ISENSE_LO` |
    | Patch router, with In2.Cu as a last resort | 7 | `U0TXD`, `ZD_Z8_N`, `ZD_Z2_N`, `STATUS_LED`, `RLY_A`, `SENS_5V`, `N_RAIN` |
    | By hand with the same search, `design/patches-hand.toml` | 5 | `VB` at TP15, `SA_N` at U8, `ZD_MV_N` twice, `I2C0_SDA` to the clock chip |

2. **The patches are long and change layer often.** A maze router keeps the
   clearances and knows nothing else:

    | Net | Length | Vias | Of it on In2.Cu |
    |---|---|---|---|
    | `STATUS_LED` | 161 mm | 7 | 8.5 mm |
    | `N_RAIN` | 156 mm | 13 | 20.7 mm |
    | `SENS_PWR_EN` | 126 mm | 12 | none |
    | `ZD_Z8_N` | 87 mm | 9 | 9.3 mm |
    | `RLY_A` | 71 mm at 0.4 mm | 10 | 12.4 mm |
    | `SENS_5V` | 62 mm at 0.4 mm | 4 | 18.5 mm |
    | `ZD_Z2_N` | 59 mm | 6 | 1.6 mm |
    | `I2C0_SDA` | 58 mm | 8 | 13.5 mm |
    | `ZD_MV_N` | 33 mm | 5 | 12.7 mm |
    | `ISENSE_LO` | 26 mm | 1 | none |
    | `VB` | 17 mm | 6 | 6.4 mm |
    | `U0TXD` | 16 mm | 3 | 3.8 mm |
    | `PGOOD` | 12 mm | 2 | none |
    | `SA_N` | 12 mm | 3 | 7.1 mm |
    | `T_RAIN` | 22 mm at 0.4 mm | 1 | none |

   All are levels, slow inputs or supplies of a few tens of milliamperes.
   None is a current path of section 6.
3. **Eleven of them cut a slot in the 3.3 V plane.** 114 mm in all. The
   patch router may use In2.Cu only when the two routing layers have no
   path, a millimetre there costs six elsewhere, and three areas are closed
   to it: 3 mm either side of the keep-alive, the Kelvin sense pair, and
   what the autorouter's keep-outs close. The review lists what each slot
   passes under ([review](hardware-layout-review.md), section 9).
4. **It boxed pads in.** With no fan-out stage it ran tracks round four pads
   on F.Cu and left no place for a via: the test pad TP15, pin 2 of the
   amplifier U8, the pull-up R96 and pin 8 of the expander U5. For each, one
   piece of autorouted copper is taken out after the import (`[[unroute]]`)
   and laid again beside the connection it made room for: `ZD_Z1_N` under
   TP15, `SA_P` at U8, `RLY_ON` at U5. `VB` reaches TP15 through a via in
   the pad, which is a test pad and is not soldered.
5. **It put a supply rail beside a node that must not see one.** It joined
   the four `V_DRV` pads of the resistor array RN2 with a bar 0.86 mm from
   the gate node of Q2, as it did in the first routing. Layout rule 3 of
   section 9 failed the build. The bar is taken out and the pads are joined
   through their own centres. The node is now 1.48 mm from the rail.
6. **It narrowed tracks without saying so.** 315 segments, 288 mm, came back
   narrower than their net class. DRC has no objection to that. The build
   sets each one to its class width and keeps the change wherever DRC stays
   clean. Seven segments, 1.0 mm, have no room and stay narrow:

    | Net | Stays narrow |
    |---|---|
    | `ZD_Z5_N` | 0.4 mm at 0.10 mm |
    | `I2C0_SDA` | 0.2 mm at 0.12 mm |
    | `ISENSE_LO` | 0.2 mm at 0.15 mm |
    | `USB_DP` | 0.2 mm at 0.19 mm, class 0.25 mm |

7. **Vias in pads.** None: no autorouted via has its centre inside a
   surface-mount pad.
8. **Wandering tracks.** Three nets are more than 2.5 times as long as their
   pins are apart, measured along the axes: `ENC_SW_SW`, 58 mm for 11 mm;
   `SP_B`, 39 mm for 7 mm; `ZD_Z1_N`, 105 mm for 42 mm. `ZC_DIV`, the
   zero-cross signal, is 152 mm from the 24 VAC input to the comparator
   under the module.
9. **Planes.** In1.Cu has no track. In2.Cu has no autorouted track, and the
   build fails if a later routing puts one there.

What the patch router did badly, found by the review and repaired in the
tool:

- **It did not know the keep-outs.** It read the rule areas of the board and
  not the rectangles of `board.toml` that keep the autorouter out. It laid
  `SENS_5V` under the screw head at hole D and two nets across the stretch
  of the keep-alive that the board closes on F.Cu. It now takes those
  rectangles as fences.
- **It did not know the rule areas of footprints.** Two patches crossed the
  "no wiring" areas of the encoder, which DRC reported. It reads them now.
- **It joins the two items DRC names, and no others.** For `I2C0_SDA` those
  lay on opposite sides of the keep-alive. That patch is by hand, to a pin
  on the right side.

What I looked at:

- **The tables** of `routing.md`.
- **Each copper layer** of the finished board as a plot, and the Gerber
  files rendered by a program that is not KiCad.
- **Closer plots** of the converter, the expander, the amplifier U8, the
  right edge and both ends of the keep-alive.
- **The 17 patched connections**, each from end to end.

I did not follow each of the 96 autorouted nets from pin to pin.

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
- **Reference designators.** 356 footprints, 354 of them on one side: the designators of the
  resistors, small capacitors, diodes and transistors do not fit between them.
  They are left off the silkscreen and are on the
  [assembly drawing](../hardware/zoneflow-a/zoneflow-a-assembly-top.pdf)
  instead. Connectors, ICs, switches, fuses and test pads keep theirs.
- **How it is kept clean.** The builder runs KiCad's DRC on the silkscreen
  and acts on every finding: a stroke of a footprint outline that lies over a
  neighbour's pad or over the board edge is removed; a designator is moved to
  the next of fourteen places round its part, and hidden if none is free; a
  label marked optional is dropped. It repeats until DRC finds nothing. A
  terminal label is not optional: if one collides the build fails. Since the
  review it also compares the box of every text with every other and with
  the board outline, because DRC lets two designators stand flush against
  each other and says nothing of one that lies beyond the edge. The terminal
  labels have letters 0.8 mm wide for the same reason. Result of this build:
  23 rounds, 59 strokes removed, 36 designators moved, 9 hidden, one optional
  label dropped.
- No text is under 1.0 mm high or 0.15 mm thick, the limits in the fab rules.

## 12. Fabrication outputs

All in `hardware/zoneflow-a/output/`, committed.

| File | What |
|---|---|
| `gerbers/` | Eleven layers in Gerber X2, drill files for plated and unplated holes, a drill map, the job file |
| `zoneflow-a-bom-jlcpcb.csv`, `-4-zone.csv` | JLCPCB BOM for the 8-zone and the 4-zone build |
| `zoneflow-a-cpl-jlcpcb.csv`, `-4-zone.csv` | Placement file for each build, rotations and origins corrected by `hardware/rules/jlcpcb-cpl-rotations.csv` |
| `zoneflow-a-pin1.csv` | Where pin 1 of every polarised part is, to compare with the assembler's preview |
| `zoneflow-a-assembly-top.pdf`, `-bottom.pdf` | Part outlines with every reference designator |
| `zoneflow-a-board.pdf` | Every layer, one page each |
| `zoneflow-a-schematic.pdf` | The schematic |
| `zoneflow-a.step` | 3D model of the assembled board |
| `zoneflow-a-top.png`, `-bottom.png` | Renders |
| `zoneflow-a-front.svg`, `-back.svg`, `-In1.Cu.svg`, `-In2.Cu.svg` | Flat views of each copper layer |
| `zoneflow-a-ibom.html` | Interactive BOM: select a part or a net and see where it is |
| `reports/` | ERC and DRC reports, the layout-rule table (`layout-rules.md`), the routing table (`routing.md`), the placement check (`placement-check.md`), a summary |

The renders and the STEP model show lands only where KiCad has no 3D model:
the two fuse holders, F5, the PTC fuse F3 and the nine of the outputs, the USB-C connector, the reset
and boot buttons, the encoder, the two front buttons, the display connector
and the cell holder.

**The placement check.** The placement files take each part's position and
rotation from the board and correct both from
`hardware/rules/jlcpcb-cpl-rotations.csv`: a rotation and an offset per
footprint, tied to one LCSC number where two parts in one package differ.

The review rewrote that table and added a check, `make cpl-check`. JLCPCB
places the footprint that its own library holds for an LCSC number, with
that footprint's origin on the coordinate of the placement file. The check
takes that footprint from the JLCEDA/EasyEDA Official Library
(<https://lceda.cn/>, <https://easyeda.com>), puts it on the board as the
file says, and compares where its pads land: every pad on a pad, pad 1 on
pad 1, the cathode on pad 1 of a diode or an LED. On this build all 312
placements of the 8-zone file and all 267 of the 4-zone file fit. With the table as
it was, 26 placements were wrong, the nine triacs and the nine opto-triacs
among them ([review](hardware-layout-review.md), section 5). The check needs
the network once and is not part of `make`; its report is
`output/reports/placement-check.md`.

**The check against KiCad's models**, which I made before that. I checked pad 1 of each of the 84 polarised or multi-pin parts on the top
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

That check shows that each footprint agrees with KiCad's model of the part.
It could not show how JLCPCB's library holds the part, which is what
`make cpl-check` compares. **What stays unverified** is that the library
footprint is how the part lies in its tape: compare the assembler's
placement preview with `zoneflow-a-pin1.csv` before confirming an order.

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

What is weak, most serious first. This is the list as I wrote it before the
[independent review](hardware-layout-review.md). The lines marked "Review"
say what the review did about each item.

1. **Nothing has been built.** Every claim here is a check on files.
   *Review: still so.*
2. **The output vias under a shorted output** (section 6): two 0.3 mm vias
   per output, with a plating thickness I assumed. More or larger vias fit
   only if the TVS moves.
   *Review: changed. Four vias per output fit without moving the TVS, and the
   calculation uses JLCPCB's published 18 um.*
3. **Heat.** 2.04 cm² of copper per triac tab against the 2 cm² the design
   asks for, most of it on the bottom, joined by six vias of which four are
   in the solder land. The design's own figure is for 1 cm² and has margin,
   but nobody has measured a triac temperature.
   *Review: unchanged. The review expects a junction near 110 °C in the
   design's worst case and found that each PTC is heated by its triac
   (finding L25).*
4. **Three long hand-routed runs on the bottom layer**: USB (169 mm), the
   display bus (61 to 72 mm) and the ADC lines (121 and 109 mm). Each is
   longer than it would be without the keep-alive rule, and each is my
   reading of what the signal tolerates, not a measurement. The ADC lines
   carry a 1 kohm source past the comparator block; the filter capacitor is
   23 mm from the pin. None of the three runs over unbroken copper: the pour
   above them on In2.Cu is cut by autorouted tracks, 6 times under the USB
   pair, 6 under the display bus and 20 under the ADC lines, and the ground
   plane is 1 mm further away.
   *Review: the lengths stand. All three now run under one 3.3 V plane. One
   slot crosses under the display bus and four under the ADC lines, where 6
   and 20 autorouted tracks did, and none under the USB pair. The review
   argues the display bus and the ADC capacitors with numbers (its section
   13).*
5. **3.3 V has no solid plane.** It is two pours in 21 and 15 pieces, joined
   by vias and by ten tracks. Every pad is connected; I did not calculate the
   impedance from the regulator to any load, and the decoupling capacitor C25
   is connected so poorly that it does little (section 10).
   *Review: changed. In2.Cu is one 3.3 V plane with eleven short slots, the
   regulator reaches it through five vias, and C25 is at its pin.*
6. **The autorouter laid the 5 V chain.** Buck output to OR-ing diode to
   regulator runs on 0.4 mm tracks, the USB feed is partly on the thin inner
   layer, and the feedback is sensed 6 mm down the load track. The numbers of
   section 6 say it is safe. I would not draw a power path this way, and it
   is the first thing I would do by hand in a revision.
   *Review: changed. By hand on the outer layers, section 7.14.*
7. **A maze router that knows only clearances closed two kinds of
   connection.** They are `VIN_MON`, 120 mm through ten vias, and the 3.3 V
   links (section 10).
   *Review: changed. `VIN_MON` is by hand and the links are gone. What the
   patch router closed on the new routing is in section 10.*
8. **Parts are tight where a hand has to reach.** A frame land of the encoder
   is 1.6 mm from the nearest pad of the module, and the two courtyards are
   0.02 mm apart; both positions are fixed by the contract.
   C1 and K1 are soldered from the top
   between other parts; the check keeps parts over 1.5 mm tall 2.5 mm from
   their pads, which is my number.
   *Review: unchanged.*
9. **The Kelvin sense pair runs under the buck converter** (section 7.5),
   shielded by the ground plane and by nothing else. The other way round
   crosses the eighteen gate lines. If the low current range reads noise at
   the switching frequency, this is where to look.
   *Review: unchanged, with an estimate of 0.2 mV before the filters.*
10. **The buck output capacitors.** C5 hangs on a 0.5 mm track. I would move
    C5 if the block had another 3 mm.
    *Review: unchanged.*
11. **Silkscreen.** Seven designators found no place and are hidden: U1 and
    the test pads TP9, TP14, TP22, TP27, TP28 and TP29. A test pad without
    its name on the board is found only with the interactive BOM or the
    assembly drawing. Designators of neighbouring test pads stand so close
    that they read as one word (`TP6TP21`), and `SW2` stands against `BOOT`.
    DRC finds no overlap in any of them. The plus sign of the cell holder's
    own footprint lost a stroke to the cleaner and is now a short bar beside
    the positive land; the `+` I added is above that land.
    *Review: the cleaner now keeps texts 0.5 mm apart and on the board, so
    names no longer run together. Nine designators are hidden for it:
    section 11.*
12. **The autorouted nets** (section 10) were inspected by reading tables and
    pictures, not by a second person.
    *Review: the new routing was inspected the same way, by the reviewer.*
13. **The 0.5 mm clearance of the 24 VAC nets** is a choice, not a standard.
    *Review: it meets what the reviewer recalls of IPC-2221B for this
    voltage, and the review measured that the board keeps it.*
14. **The fan-out passes 0.5 mm above the pins of the sensor terminal block**
    on the bottom side, and the flow input runs 0.6 mm from the right board
    edge. Both are within the rules; both are where a slipped screwdriver or a
    scored board edge would reach.
    *Review: unchanged.*

## 14. What a person must look at

This is the list I wrote for the reviewer. The
[review](hardware-layout-review.md) went through it. What is left for a
person is the review's own checklist of seven items, in its section 17.

| Item below | What the review did |
|---|---|
| 1, 11 | Looked at every layer and at `routing.md` for the new routing, from renders of the board file and of the Gerber files. Nobody opened the KiCad window |
| 2 | Recalculated section 6 with JLCPCB's published plating and doubled the output vias |
| 3 | Measured the fail-safe distances with its own program. The copper round pin 10 and Q2 is item 7 of its checklist |
| 4 | Argued each of the three with numbers and accepted them (its section 13) |
| 5 | Compared pin order and polarity with the makers' documents. The 1:1 print is item 1 of its checklist |
| 6 | Wrote `make cpl-check`, which found 26 wrong placements, and fixed them. The preview is item 5 of its checklist |
| 7 | Item 6 of its checklist |
| 8 | Open. Nobody has asked the assembler |
| 9 | Checked: no copper in the antenna area, the ground plane whole under the module |
| 10 | Fixed all three |
| 12 | Unchanged. The review's own tools also have unit tests on made-up data only |

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
make -C hardware/zoneflow-a cpl-check     # placement files against JLCPCB's library; needs the network once
make -C hardware/zoneflow-a reroute  # throw the stored routing away and route again
make -C hardware/zoneflow-a patches  # after a new routing: close what it left open
```

The autorouter's result is stored in `route/` with the hash of what it was
made from, and reused while that does not change. Routing again takes
about 40 minutes and does not give the same tracks twice: the patches of
`design/patches.toml` belong to the stored routing and have to be made again
for a new one, and sections 8 to 10 of this page describe the stored one.
