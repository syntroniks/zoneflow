# hwflow

Python package behind the ZoneFlow hardware flow. It turns a circuit written
as Python into a KiCad 10 project and drives KiCad's own tools from there.

Read `docs/design/hardware-toolchain.md` for how to use it. Run it through
`make` in a board directory such as `hardware/toolchain-test/`.

| Module | Runs under | Job |
|---|---|---|
| `design`, `symlib`, `schwriter`, `project`, `netlist`, `cli` | `uv run` | circuit model, `.kicad_sch` writer, project files, netlist check |
| `pcbbuild` | system Python with `pcbnew` | board outline, placement, pre-routes, zones, DSN export, session import |
| `route` | either | Freerouting download, run and session cache |
| `fab`, `report` | either | JLCPCB BOM and pick-and-place CSV, build summary |

Tests: `uv run pytest` in this directory. `tests/test_kicad_roundtrip.py`
calls `kicad-cli` and is skipped when it is not installed.
