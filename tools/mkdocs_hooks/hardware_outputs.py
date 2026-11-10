# SPDX-License-Identifier: Apache-2.0
"""MkDocs hook: publish the generated schematic of the rev A board with the site.

The schematic PDF and the per-sheet SVGs are generated into
``hardware/zoneflow-a/output/`` and committed there. This hook adds them to
the site under ``hardware/zoneflow-a/``, so that the documentation can show
them without a second copy in ``docs/``. A file that is missing is skipped;
the page that links to it then fails the strict build, which is the intent.
"""

from pathlib import Path

from mkdocs.structure.files import File

OUTPUT = Path(__file__).resolve().parents[2] / "hardware" / "zoneflow-a" / "output"
SITE_PATH = "hardware/zoneflow-a"


def on_files(files, config):
    sources = sorted(OUTPUT.glob("schematic-svg/*.svg")) + [OUTPUT / "zoneflow-a-schematic.pdf"]
    for src in sources:
        if src.is_file():
            rel = src.relative_to(OUTPUT).as_posix()
            files.append(File.generated(config, f"{SITE_PATH}/{rel}", abs_src_path=str(src)))
    return files
