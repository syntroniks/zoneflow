# SPDX-License-Identifier: Apache-2.0
"""Take the time of export out of KiCad's output files.

KiCad stamps every PDF, SVG and report with the second it was written. The
outputs of a board are committed, so a rebuild that changes nothing else would
still change every one of them. This replaces each stamp with the date in the
schematic's title block at 00:00:00, which is the same on every run.

    python -m hwflow.stable <root>.kicad_sch <output directory>

Only the standard library is used.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


def design_date(root_sheet: Path) -> str:
    """The date of the title block of the root sheet, "YYYY-MM-DD"."""
    m = re.search(r'\(title_block\b.*?\(date "(\d{4}-\d{2}-\d{2})"\)', Path(root_sheet).read_text(encoding="utf-8"),
                  flags=re.S)
    if not m:
        raise SystemExit(f"{root_sheet}: no date in the title block")
    return m.group(1)


def normalise(path: Path, date: str) -> bool:
    """Rewrite one file in place. True if it changed."""
    suffix = path.suffix.lower()
    raw = path.read_bytes()
    stamp = f"{date}T00:00:00".encode()
    if suffix == ".pdf":
        # Same length as KiCad's own stamp, so the PDF's cross-reference table stays valid.
        new = re.sub(rb"/CreationDate \(D:\d{4}:\d\d:\d\d:\d\d:\d\d:\d\d\)",
                     b"/CreationDate (D:" + date.replace("-", ":").encode() + b":00:00:00)", raw)
    elif suffix == ".svg":
        new = re.sub(rb"(<title>SVG Image created as [^<]*? date )\S+( </title>)", rb"\g<1>" + stamp + rb"\g<2>", raw)
    elif suffix == ".rpt":
        new = re.sub(rb"(report \()\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(, )", rb"\g<1>" + stamp + rb"\g<2>", raw, count=1)
    elif suffix == ".json":
        new = re.sub(rb'("date": ")\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(")', rb"\g<1>" + stamp + rb"\g<2>", raw, count=1)
    elif suffix in (".gbr", ".drl", ".gbrjob"):
        # Gerber X2 and Excellon headers, and the Gerber job file: "...CreationDate,2026-11-10T18:00:00-06:00",
        # "date 2026-11-10T18:00:00-0600".
        new = re.sub(rb"(CreationDate[,\"]:? ?\"?)\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d[+-]\d\d:?\d\d",
                     rb"\g<1>" + stamp + b"+00:00", raw)
        new = re.sub(rb"(date )\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d[+-]\d\d:?\d\d", rb"\g<1>" + stamp + b"+0000", new)
    else:
        return False
    if new != raw:
        path.write_bytes(new)
        return True
    return False


def main(argv=None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 2:
        sys.stderr.write(__doc__)
        return 2
    date = design_date(Path(args[0]))
    changed = sum(normalise(p, date) for p in sorted(Path(args[1]).rglob("*")) if p.is_file())
    print(f"stable: export time replaced by {date} in {changed} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
