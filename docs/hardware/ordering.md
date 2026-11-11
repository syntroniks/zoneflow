# Ordering boards

!!! danger "Do not order boards yet"
    The files needed to order a board do not exist yet, and the design has
    never been built. A board ordered from what is in the repository today
    cannot be made.

## What is missing

| Missing | Why it matters | Tracked in |
|---|---|---|
| PCB layout | There are no copper layers, so there is nothing to fabricate. The schematic and the board outline exist; placement and routing do not. | [Issue #5](https://github.com/syntroniks/zoneflow/issues/5) |
| Gerber, drill and placement files | A fab and an assembler need these. They are generated from the layout. | [Issue #5](https://github.com/syntroniks/zoneflow/issues/5) |
| Review of the layout | The circuit was reviewed (review A1); the layout and the schematic as captured need an independent review before money is spent. | [Issue #11](https://github.com/syntroniks/zoneflow/issues/11) |
| A prototype and bench tests | Revision A is an engineering prototype until it has been bench-tested. Thermal, short-circuit, surge and fail-safe behaviour are unmeasured. See [status](../status.md#what-only-a-prototype-can-answer). | No issue yet; [progress log, #1](https://github.com/syntroniks/zoneflow/issues/1) |
| An answer on hand-soldered parts | The relay K1 and the bulk capacitor C1 are through-hole parts on the bottom side. Whether JLCPCB will solder them, or the builder does, has not been asked. | [Mechanical design section 6](../design/mechanical.md#6-c1-and-k1-who-fits-them) |

## What exists

- The [schematic](schematic.md), with ERC and a pin-for-pin check against the design netlist.
- The [bill of materials](bom.md) in JLCPCB's format for both builds, every line with an LCSC number, stock and price checked on 2026-11-10.
- The [board outline and fixed part positions](../design/mechanical.md): 114 x 110 mm, four layers.
- A [cost estimate](cost.md) from JLCPCB's published prices and its online board quote, dated 2026-11-11. It is not a quote for assembly.

## How ordering will work

This is the process the specification plans. Details will be added when the
files exist.

1. Download the release's fabrication package: Gerbers, drill files, the placement file and the JLCPCB BOM for the build you want.
2. Upload the Gerbers to a JLCPCB-class service: four layers, 1.6 mm, with the options the release names.
3. Choose assembly on the top side and upload the BOM and the placement file.
4. Leave out of the assembly order the parts that are fitted by hand: the bulk capacitor C1 and the relay K1 (unless the assembler fits them), the through-hole terminal headers J1 to J4 and their loose plugs, the display panel DS1 and the coin cell. The [BOM page](bom.md#parts-that-the-assembly-house-does-not-fit) lists them.
5. Order the hand-fitted parts and the display panel, which plugs in at final assembly.
6. Check any part the assembler proposes to substitute against the BOM. The display panel has no confirmed second source.

!!! note "To be completed"
    The exact fab options, the placement file, a checked quote and the
    assembly order steps come with the layout,
    [issue #5](https://github.com/syntroniks/zoneflow/issues/5), and are
    confirmed by the first prototype order.

## Stock

Stock was checked again on 2026-11-11 for the [cost estimate](cost.md#components).
No line was short of 100 eight-zone boards. The thinnest were the triac
(3,384 for 900 needed), the 48 V TVS diode (5,782 for 1,402) and the display
panel (413 for 100). Check stock again before you order.
