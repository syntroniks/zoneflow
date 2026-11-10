# hwflow

Python package behind the ZoneFlow hardware flow. It turns a circuit written
as Python into a KiCad 10 project and drives KiCad's own tools from there.

Read `docs/design/hardware-toolchain.md` for how to use it. Run it through
`make` in a board directory such as `hardware/toolchain-test/`.

| Module | Runs under | Job |
|---|---|---|
| `design`, `symlib`, `schwriter`, `project`, `netlist`, `cli` | `uv run` | circuit model, `.kicad_sch` writer, project files, netlist check |
| `capture`, `parity` | `uv run` | draw a schematic from a design netlist and a part table; compare KiCad's netlist and BOMs with that netlist pin for pin |
| `schlint` | `uv run` | plot every page and find text or symbols that collide |
| `symgen` | `uv run` | write a project-local symbol library |
| `fpgen` | system Python with `pcbnew` | write a project-local footprint library |
| `committed` | `uv run`, no KiCad | check a board's committed netlist and BOMs against the design netlist (CI) |
| `stable` | either | take the time of export out of PDFs, SVGs and reports |
| `plotcmp` | either | compare two SVG plots by what they draw; keep a committed plot that draws the same |
| `pcbbuild` | system Python with `pcbnew` | board outline, placement, pre-routes, zones, DSN export, session import |
| `route` | either | Freerouting download, run and session cache |
| `fab`, `report` | either | JLCPCB BOM and pick-and-place CSV, build summary |

Tests: `uv run pytest` in this directory. The tests that call `kicad-cli` or
read KiCad's symbol libraries are skipped when KiCad is not installed.
The one runtime dependency is PyYAML, for the design netlist.
