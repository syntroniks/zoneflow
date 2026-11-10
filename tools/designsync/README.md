# designsync

Keeps the four files of the rev A paper design in step:

| File | Role |
|---|---|
| `hardware/netlist.yaml` | The source. Every component, its part and the net on every pin. |
| `docs/design/hardware-design.md` | Component tables, the test pad table and the cost summary are written from the netlist and the BOM. |
| `hardware/bom-preliminary.csv` | Reference and quantity columns are written from the netlist. Maker, price, stock and notes are entered by a person from a distributor page. |
| `hardware/pinmap.yaml` | Checked against the module and expander pins in the netlist. |

## Use

From the repository root:

    make -C hardware design-check     # changes nothing, exits 1 on any disagreement
    make -C hardware design-sync      # rewrites the generated parts

or directly, `uv run tools/designsync/designsync.py check`. The script names
its one dependency, PyYAML, in its header, and `uv` fetches it. CI runs the
check on every pull request that touches `hardware/`, the design document or
this tool: `.github/workflows/hardware.yml`.

## Changing the circuit

1. Edit `hardware/netlist.yaml`.
2. For a part number that is new to the design, add a line to the BOM by hand, with the LCSC number, maker, price and stock from a page you read. The tool refuses to invent one.
3. Run `make -C hardware design-sync`, then `make -C hardware design-check`.
4. Update the prose of the design document and, for a GPIO or expander change, `hardware/pinmap.yaml`.

## What is generated in the design document

Regions between two comment lines:

    <!-- designsync:begin table block="B valve common relay" -->
    ...
    <!-- designsync:end -->

| Region | Content |
|---|---|
| `table block="..."` | One row per component of that netlist block, in netlist order. `channel="1"` keeps only the designators of one output channel. |
| `testpads` | Every `TP` component and its net. |
| `cost` | BOM line counts, parts cost per board, the most expensive lines and the lines with the least stock. |

Do not edit inside a region. Everything outside is prose and is not touched.

## What is checked

- **Netlist.** Required fields, no duplicate reference, a known build variant (`all`, `8`, `dnf`), no net with a single connection, one value and package per LCSC number, an LCSC number on every fitted part except test pads, jumpers and pad-only connectors.
- **Document.** Every generated region equals what the netlist and BOM give. Every netlist block has a table.
- **BOM.** One line per LCSC number in the netlist, with the same references, quantities for both builds, value and package. No line that matches nothing. A line for a loose part, such as `J3 plug` or `cell`, follows the build variant of the part it belongs to.
- **Pin map.** Every GPIO entry against the net on that module pin, every module pin that carries a net, the enable pin, and all sixteen expander bits. An entry gives `net:` where the net name differs from the signal name.

## The cost figure

Quantity times the BOM price for the largest of 10, 100 and 1000 pieces that
the order of that line reaches. JLCPCB has more price breaks than the three
the BOM records, so its quote will be a little lower.
