# PCB layout

!!! warning "Laid out, not built"
    The revision A board is placed, routed and passes the checks named below.
    No board has been made. Nobody but its author has reviewed the layout.
    **Do not order boards yet**: see [ordering boards](ordering.md).

The board is 114 x 110 mm, four copper layers, 1.6 mm, with every part on the
top side except the bulk capacitor and the relay. It is in
[`hardware/zoneflow-a/`](https://github.com/syntroniks/zoneflow/tree/main/hardware/zoneflow-a).
Open `zoneflow-a.kicad_pro` in KiCad 10 to look at it, or use the files here.

[![Top side of the board, rendered](zoneflow-a/zoneflow-a-top.png)](zoneflow-a/zoneflow-a-top.png)

[![Bottom side of the board, rendered](zoneflow-a/zoneflow-a-bottom.png)](zoneflow-a/zoneflow-a-bottom.png)

The renders come from KiCad's 3D viewer. Four project footprints use a
stand-in model and four have none, so some parts show as lands only.

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

These are checks on files. The [layout document](../design/hardware-layout.md)
says what each found, what was routed by hand and why, the current
calculation for the valve paths, and what its author thinks is weak.

## The terminal labels

Left to right, as printed on the board: `24V~` `24V~` | `COM` `COM` `MV` `Z1`
`Z2` `Z3` `Z4` | `Z5` `Z6` `Z7` `Z8` | `5V` `FLOW` `GND` `RAIN` `GND` `AUX`
`GND`. A ground symbol right of the second `24V~` marks the terminal that
becomes the board's ground. See [wiring](../getting-started/wiring.md).
