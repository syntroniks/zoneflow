# PCB layout

!!! warning "Laid out, not built"
    The revision A board is placed, routed and passes the checks named below.
    No board has been made. An
    [independent review](../design/hardware-layout-review.md) of the files
    found faults, fixed them, and leaves seven things for a person to check.
    **Do not order boards before that is done**: see
    [ordering boards](ordering.md).

The board is 114 x 110 mm, four copper layers, 1.6 mm, with every part on the
top side except the bulk capacitor and the relay. It is in
[`hardware/zoneflow-a/`](https://github.com/syntroniks/zoneflow/tree/main/hardware/zoneflow-a).
Open `zoneflow-a.kicad_pro` in KiCad 10 to look at it, or use the files here.

[![Top side of the board, rendered](zoneflow-a/zoneflow-a-top.png)](zoneflow-a/zoneflow-a-top.png)

[![Bottom side of the board, rendered](zoneflow-a/zoneflow-a-bottom.png)](zoneflow-a/zoneflow-a-bottom.png)

The renders come from KiCad's 3D viewer. KiCad has no model for the fuse
holders and fuses, the USB-C connector, the buttons, the encoder, the display
connector and the cell holder, so those show as lands only.

## Look at it

| What | File |
|---|---|
| Find a part or a net on the board | [Interactive BOM](zoneflow-a/zoneflow-a-ibom.html) |
| Every layer, one page each | [Board PDF](zoneflow-a/zoneflow-a-board.pdf) |
| Top copper and silkscreen | [SVG](zoneflow-a/zoneflow-a-front.svg) |
| Bottom copper and silkscreen | [SVG](zoneflow-a/zoneflow-a-back.svg) |
| Inner layer 1, ground | [SVG](zoneflow-a/zoneflow-a-In1.Cu.svg) |
| Inner layer 2, 3.3 V and the shunt return | [SVG](zoneflow-a/zoneflow-a-In2.Cu.svg) |
| Where every part goes, top | [Assembly drawing](zoneflow-a/zoneflow-a-assembly-top.pdf) |
| Where every part goes, bottom | [Assembly drawing](zoneflow-a/zoneflow-a-assembly-bottom.pdf) |
| 3D model | `hardware/zoneflow-a/output/zoneflow-a.step` in the repository |
| Gerber and drill files | `hardware/zoneflow-a/output/gerbers/` in the repository |

## Where things are

[![Top copper and silkscreen](zoneflow-a/zoneflow-a-front.svg)](zoneflow-a/zoneflow-a-front.svg)

- **Bottom edge**: USB-C, the four terminal blocks, Qwiic. Each terminal has its name on the board behind it.
- **Above the terminals, left**: the two fuses, then the 24 VAC supply: surge clamp, rectifier, buck converter.
- **Above the terminals, centre and right**: nine output columns, one per valve output. From the terminal up: surge clamp and snubber, resettable fuse, triac.
- **The row of LEDs**, and above it the nine opto-triacs that fire the triacs.
- **Centre**: current measurement, the two buttons, the coin cell, the clock.
- **Top left, under the display**: I/O expander, the fail-safe gate, the relay driver. The display plugs into the connector in the middle.
- **Top right**: the ESP32-S3 module with its antenna beyond the board edge, the encoder beside it, and under it the keep-alive detector.
- **Bottom side**: the bulk capacitor C1, lying down, and the relay K1.

## What was checked

`make` in the board directory runs every check and stops at the first that
fails. The results of the committed board are in
`hardware/zoneflow-a/output/reports/`.

| Check | What it compares |
|---|---|
| Design rules (`kicad-cli pcb drc`) | Clearances, widths, holes and silkscreen against JLCPCB's published limits and the board's own rules; the board against the schematic, pad by pad; every connection routed |
| Mechanical contract (`hwflow mech`) | The position and side of every part the enclosure depends on, the holes, the height zones, against `hardware/mechanical.yaml` |
| Layout rules (`hwflow.layoutrules`) | What the fail-safe analysis asks of the layout: which nets stay away from the keep-alive net on every layer, how far apart the two detectors are, which nets have no test pad |
| Placement files (`make cpl-check`, on demand) | Every placement against the footprint JLCPCB's library holds for the part: all pads on pads, pad 1 on pad 1, the cathode on the cathode |

These are checks on files. The [layout document](../design/hardware-layout.md)
says what each found, what was routed by hand and why, the current
calculation for the valve paths, and what its author thinks is weak. The
[layout review](../design/hardware-layout-review.md) is a second reader's
account: what it measured with its own programs, what it found and changed,
and what it could not verify.

## How it was routed

104 of the 200 nets are written out by hand in the source: the 24 VAC
paths, the valve outputs, the shunt return, the buck converter, the 5 V and
USB supplies, the USB pair, the display bus, the monitor lines into the ADC
and everything the fail-safe analysis has a rule for. An autorouter laid the
rest on the two outer layers. The two inner layers are planes, ground and
3.3 V, and the build fails if the autorouter puts a track on either.

The autorouter could not finish on two layers. The build corrects its result
on every run, and the review closed what was left:

- **Planes.** Nothing may be autorouted on either, and the build fails if
  something is.
- **Track widths.** The autorouter narrowed 288 mm of track below the width
  its net class asks for. The build widens every piece that has room; 1 mm
  stays narrow.
- **A guarded node.** It laid a supply rail 0.86 mm from a node of the
  fail-safe circuit that must keep 1 mm from every rail. That copper is taken
  out and laid again.
- **Open connections.** It left 17. A second, small router closed twelve and
  the reviewer five. Eleven of them pass under an obstacle on the 3.3 V
  layer, 114 mm in all, and several are long with many vias. They are slow
  signals. The [layout document](../design/hardware-layout.md#10-what-the-autorouter-did-and-what-was-done-about-it)
  lists each one.

## What the review changed

The [review](../design/hardware-layout-review.md) lists 30 findings. The
ones that would have cost a board:

- **Placement files.** 26 parts would have been placed a quarter turn off or
  beside their lands, the nine triacs and the nine opto-triacs among them.
  The correction table is rewritten and a new check compares every placement
  with JLCPCB's own footprint for the part.
- **Buck converter.** Its exposed pad had no via. It has six.
- **3.3 V.** The supply was two pours cut into 21 and 15 pieces. It is one
  plane now, with eleven short slots, and the regulator reaches it through
  five vias.
- **5 V and USB supply.** Autorouted at 0.4 mm before, by hand at 0.55 to
  1.0 mm now.
- **Short-circuit path.** Four vias per output where there were two.
- **Small holes.** Twelve 0.2 mm holes, which JLCPCB charges for, are
  0.3 mm now, and the design rules fail on a smaller one.

## What is still weak

The lists are in the
[layout document, section 13](../design/hardware-layout.md#13-my-own-review),
and in the
[review, section 16](../design/hardware-layout-review.md#16-what-i-could-not-verify).
The first few:

- **Nothing has been built.** Every result on this page is a check on files.
- **Heat.** Each triac has 2 cm² of copper, most of it on the bottom side
  behind six vias. Nobody has measured a triac temperature. The resettable
  fuse beside each triac is heated by it.
- **Placement.** The check proves the files agree with JLCPCB's library. It
  does not prove the library is how each part lies in its tape. Look at the
  assembler's preview.
- **Routing.** The logic area is autorouted on two layers and then patched.
  It passes every check and it is not tidy: one sensor input is 156 mm long
  with 13 vias.
- **Long analogue runs.** The ADC filter capacitors are 19 mm from their
  pins, and the current-sense pair passes under the buck converter.

## The terminal labels

Left to right, as printed on the board: `24V~` `24V~` | `COM` `COM` `MV` `Z1`
`Z2` `Z3` `Z4` | `Z5` `Z6` `Z7` `Z8` | `5V` `FLOW` `GND` `RAIN` `GND` `AUX`
`GND`. A ground symbol right of the second `24V~` marks the terminal that
becomes the board's ground. See [wiring](../getting-started/wiring.md).
