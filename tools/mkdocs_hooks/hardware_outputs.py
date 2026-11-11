# SPDX-License-Identifier: Apache-2.0
"""MkDocs hook: publish generated hardware and enclosure files with the site.

The schematic PDF, the per-sheet SVGs, the board renders and plots, the
assembly drawings, the JLCPCB BOMs and placement files are generated into
``hardware/zoneflow-a/output/``; the preliminary BOM is
``hardware/bom-preliminary.csv``; the drilling template and the print files
(STL and 3MF of the enclosure and of the test-fit kit, the print plates, the
view-only board model) are generated into ``enclosure/output/``. All are
committed. This hook adds them to the site, so that the documentation can
show and link them without a second copy in ``docs/``. A file that is missing
is skipped; the page that links to it then fails the strict build or the link
check, which is the intent.

It also packs the print files into one archive with ``enclosure/tools/pack.py``,
the same archive ``make zip`` in ``enclosure/`` writes, and publishes it as
``enclosure/print/zoneflow-test-fit-print-files.zip``.
"""

import importlib.util
from pathlib import Path

from mkdocs.structure.files import File

ROOT = Path(__file__).resolve().parents[2]
HW_OUTPUT = ROOT / "hardware" / "zoneflow-a" / "output"
ENC_OUTPUT = ROOT / "enclosure" / "output"
# Print files, published under enclosure/print/ with their path below enclosure/output/.
PRINT_PATTERNS = ("zoneflow-*.stl", "zoneflow-*.3mf", "test-fit/zoneflow-testfit-*.stl", "test-fit/zoneflow-testfit-*.3mf",
                  "test-fit/view-only/*.stl", "test-fit/view-only/*.3mf", "plates/*.3mf")
PRINT_ZIP = ROOT / "enclosure" / "build" / "zoneflow-test-fit-print-files.zip"

# (source, path on the site)
EXTRA = [
    (ROOT / "hardware" / "bom-preliminary.csv", "hardware/bom-preliminary.csv"),
    (ROOT / "enclosure" / "output" / "drill-template.pdf", "enclosure/drill-template.pdf"),
    (ROOT / "enclosure" / "output" / "drill-template.svg", "enclosure/drill-template.svg"),
    # The STEP of the board, next to the STL and 3MF of the same export.
    (HW_OUTPUT / "zoneflow-a.step", "enclosure/print/test-fit/view-only/zoneflow-a.step"),
]


def on_files(files, config):
    # Pictures, documents and tables a reader opens in a browser: the schematic, the board renders,
    # the copper plots, the assembly drawings, the interactive BOM, the BOMs and placement files.
    # Gerbers, drill files and reports are linked on the repository instead.
    sources = sorted({src for pattern in ("schematic-svg/*.svg", "*.pdf", "*.png", "*.svg", "*.html", "*.csv")
                      for src in HW_OUTPUT.glob(pattern)})
    pairs = [(src, f"hardware/zoneflow-a/{src.relative_to(HW_OUTPUT).as_posix()}") for src in sources]
    prints = sorted({src for pattern in PRINT_PATTERNS for src in ENC_OUTPUT.glob(pattern)})
    pairs += [(src, f"enclosure/print/{src.relative_to(ENC_OUTPUT).as_posix()}") for src in prints]
    if prints:
        spec = importlib.util.spec_from_file_location("zoneflow_enclosure_pack", ROOT / "enclosure" / "tools" / "pack.py")
        pack = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(pack)
        pack.build(PRINT_ZIP)
        pairs.append((PRINT_ZIP, f"enclosure/print/{PRINT_ZIP.name}"))
    for src, dest in pairs + EXTRA:
        if src.is_file():
            files.append(File.generated(config, dest, abs_src_path=str(src)))
    return files
