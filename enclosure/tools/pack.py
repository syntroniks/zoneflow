"""Pack the print files into one archive, the same bytes every time.

    python3 tools/pack.py build/zoneflow-test-fit-print-files.zip

The archive holds every STL and 3MF of the enclosure and of the test-fit kit,
the print plates, the board as KiCad models it (view only), the check report
and a list of what to print. `make zip` in enclosure/ writes it, and the
documentation build (tools/mkdocs_hooks/hardware_outputs.py) writes the same
archive into the site, so that it is one link in the test-fit guide.

Only the standard library is used.
"""

from __future__ import annotations

import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
OUT = HERE / "output"
NAME = "zoneflow-test-fit-print-files"

# What goes in, as patterns under enclosure/output/.
PATTERNS = [
    "zoneflow-*.stl", "zoneflow-*.3mf",
    "test-fit/zoneflow-testfit-*.stl", "test-fit/zoneflow-testfit-*.3mf",
    "test-fit/view-only/*.stl", "test-fit/view-only/*.3mf", "test-fit/view-only/*.txt",
    "test-fit/stand-ins.md", "plates/*.3mf", "check-report.md",
]
EXTRA = [(HERE.parent / "hardware" / "zoneflow-a" / "output" / "zoneflow-a.step", "test-fit/view-only/zoneflow-a.step")]

README = """ZoneFlow test-fit print files
=============================

Nothing in this archive has been printed by the project. Printer accuracy
varies. Treat the first print as a test and report what you find:
https://github.com/syntroniks/zoneflow/issues/9

The guide, with what to check at each step and the gap to expect:
https://syntroniks.github.io/zoneflow/enclosure/test-fit/

What to print
-------------
plates/                 one 3MF per print batch, parts laid out for a 220 x 220 mm bed
  zoneflow-plate-1-test-fit-kit.3mf   stand-in board, C1, K1, four plug pieces, display panel
  zoneflow-plate-2-body.3mf           base, display carrier, knob, two button caps
  zoneflow-plate-3-front.3mf          fascia and wiring cover
  zoneflow-plate-4-clear.3mf          light-pipe comb
zoneflow-*.stl, .3mf    the eight enclosure parts, each in print orientation
test-fit/zoneflow-testfit-*.stl, .3mf
                        the seven pieces of the test-fit kit, each in print orientation.
                        Print zoneflow-testfit-plug-7p twice.

Not for printing
----------------
test-fit/view-only/     the board as KiCad models it, as STL, 3MF and STEP. Its bodies
                        are not all closed solids and some part models are missing.
                        Look at it; print the test-fit board.

Reference
---------
check-report.md         interference and clearance check of all of the above
test-fit/stand-ins.md   every stand-in on the test-fit board and its source

Licence: CERN-OHL-P-2.0.
"""


def files() -> list[tuple[Path, str]]:
    found = []
    for pattern in PATTERNS:
        found += [(p, p.relative_to(OUT).as_posix()) for p in sorted(OUT.glob(pattern))]
    found += [(src, dest) for src, dest in EXTRA if src.is_file()]
    return found


def build(dest: Path) -> int:
    """Write the archive. Returns the number of files in it."""
    entries = files()
    dest.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(dest, "w") as z:
        def add(name: str, data: bytes) -> None:
            info = zipfile.ZipInfo(f"{NAME}/{name}", date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data, compresslevel=9)

        add("README.txt", README.encode("utf-8"))
        for src, name in entries:
            add(name, src.read_bytes())
    return len(entries) + 1


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    n = build(Path(argv[1]))
    print(f"{argv[1]}: {n} files, {Path(argv[1]).stat().st_size / 1e6:.1f} MB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
