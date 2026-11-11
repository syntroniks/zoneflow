# SPDX-License-Identifier: Apache-2.0
"""MkDocs hook: publish generated hardware and enclosure files with the site.

The schematic PDF, the per-sheet SVGs and the JLCPCB BOMs are generated into
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
    sources = sorted(HW_OUTPUT.glob("schematic-svg/*.svg")) + [
        HW_OUTPUT / "zoneflow-a-schematic.pdf",
        HW_OUTPUT / "zoneflow-a-bom-jlcpcb.csv",
        HW_OUTPUT / "zoneflow-a-bom-jlcpcb-4-zone.csv",
    ]
    pairs = [(src, f"hardware/zoneflow-a/{src.relative_to(HW_OUTPUT).as_posix()}") for src in sources]
    for src, dest in pairs + EXTRA:
        if src.is_file():
            files.append(File.generated(config, dest, abs_src_path=str(src)))
    return files
