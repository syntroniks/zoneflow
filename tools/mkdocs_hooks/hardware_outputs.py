# SPDX-License-Identifier: Apache-2.0
"""MkDocs hook: publish generated hardware and enclosure files with the site.

The schematic PDF, the per-sheet SVGs, the board renders and plots, the
assembly drawings, the JLCPCB BOMs and placement files are generated into
``hardware/zoneflow-a/output/``; the preliminary BOM is
``hardware/bom-preliminary.csv``; the drilling template is generated into
``enclosure/output/``. All are committed. This hook adds them to the site,
so that the documentation can show and link them without a second copy in
``docs/``. A file that is missing is skipped; the page that links to it then
fails the strict build or the link check, which is the intent.
"""

from pathlib import Path

from mkdocs.structure.files import File

ROOT = Path(__file__).resolve().parents[2]
HW_OUTPUT = ROOT / "hardware" / "zoneflow-a" / "output"

# (source, path on the site)
EXTRA = [
    (ROOT / "hardware" / "bom-preliminary.csv", "hardware/bom-preliminary.csv"),
    (ROOT / "enclosure" / "output" / "drill-template.pdf", "enclosure/drill-template.pdf"),
    (ROOT / "enclosure" / "output" / "drill-template.svg", "enclosure/drill-template.svg"),
]


def on_files(files, config):
    # Pictures, documents and tables a reader opens in a browser: the schematic, the board renders,
    # the copper plots, the assembly drawings, the interactive BOM, the BOMs and placement files.
    # Gerbers, drill files and reports are linked on the repository instead.
    sources = sorted({src for pattern in ("schematic-svg/*.svg", "*.pdf", "*.png", "*.svg", "*.html", "*.csv")
                      for src in HW_OUTPUT.glob(pattern)})
    pairs = [(src, f"hardware/zoneflow-a/{src.relative_to(HW_OUTPUT).as_posix()}") for src in sources]
    for src, dest in pairs + EXTRA:
        if src.is_file():
            files.append(File.generated(config, dest, abs_src_path=str(src)))
    return files
