# Bill of materials

!!! warning "Paper design"
    No board has been made. Stock and prices are what the distributor pages
    showed on 2026-11-10 and will have moved.

The specification asks for a BOM with an LCSC part number on every line,
preferring JLCPCB basic parts. There are two kinds of BOM file in the
repository, and a check that they agree.

| File | What it is | Who writes it |
|---|---|---|
| [`hardware/bom-preliminary.csv`](https://github.com/syntroniks/zoneflow/blob/main/hardware/bom-preliminary.csv) | One line per part number, with the maker, the maker's part number, JLCPCB basic or extended, stock, three price breaks, quantities for both builds and notes | A person, from distributor pages. `tools/designsync` writes the reference and quantity columns from the netlist |
| [`hardware/zoneflow-a/output/zoneflow-a-bom-jlcpcb.csv`](https://github.com/syntroniks/zoneflow/blob/main/hardware/zoneflow-a/output/zoneflow-a-bom-jlcpcb.csv) | The 8-zone build in the format JLCPCB's assembly order takes: comment, designators, footprint, LCSC number, quantity. 92 lines | KiCad, from the schematic |
| [`hardware/zoneflow-a/output/zoneflow-a-bom-jlcpcb-4-zone.csv`](https://github.com/syntroniks/zoneflow/blob/main/hardware/zoneflow-a/output/zoneflow-a-bom-jlcpcb-4-zone.csv) | The same for the 4-zone build. 91 lines | KiCad, from the schematic, variant `4-zone` |

The cost estimate and the reasoning behind each part are in
[hardware design section 18](../design/hardware-design.md#18-bill-of-materials-and-cost-estimate).

## What keeps them in step

Both come from [`hardware/netlist.yaml`](https://github.com/syntroniks/zoneflow/blob/main/hardware/netlist.yaml).

- `make -C hardware design-check` fails if the preliminary BOM and the netlist
  disagree on a part number, a reference or a quantity.
- The same command fails if a JLCPCB BOM lists a part the netlist does not fit
  in that build, misses one it does, or carries a different LCSC number or
  footprint.
- Continuous integration runs it on every pull request.

## Parts that the assembly house does not fit

The JLCPCB BOMs list every part of the build. These are hand-soldered after
assembly, or plugged in, and must be taken out of the assembly order or
ordered as loose parts:

- C1, the bulk capacitor, through-hole on the bottom side.
- K1, the relay, through-hole inserted from the bottom side.
- J1 to J4, the terminal headers.
- DS1, the display panel, which plugs into J7.

A placement file that leaves them out comes with the layout,
[issue #5](https://github.com/syntroniks/zoneflow/issues/5).
