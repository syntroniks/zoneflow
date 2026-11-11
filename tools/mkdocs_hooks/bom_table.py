# SPDX-License-Identifier: Apache-2.0
"""MkDocs hook: render hardware/bom-preliminary.csv as a table.

A page that contains the line ``<!-- zf:bom-table -->`` gets it replaced by
one table row per BOM line, with links to the LCSC page and the datasheet.
The table is built from the committed CSV at every build, so it cannot drift
from the file. A page with the marker and no CSV fails the build.
"""

import csv
from pathlib import Path

CSV = Path(__file__).resolve().parents[2] / "hardware" / "bom-preliminary.csv"
MARKER = "<!-- zf:bom-table -->"


def _cell(text):
    return str(text).replace("|", "\\|").replace("\n", " ").strip()


def _table():
    with CSV.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    out = [
        "| References | 8-zone | 4-zone | Part | Maker and part number | LCSC | JLCPCB | Stock | USD at 100 | Datasheet | Note |",
        "|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    for r in rows:
        lcsc = r["lcsc"].strip()
        if lcsc and r["source_url"].strip():
            lcsc = f"[{lcsc}]({r['source_url'].strip()})"
        ds = r["datasheet_url"].strip()
        ds = f"[{_cell(r['datasheet']) or 'link'}]({ds})" if ds else _cell(r["datasheet"]) or "none"
        part = f"{_cell(r['value'])}, {_cell(r['package'])}"
        maker = f"{_cell(r['manufacturer'])} {_cell(r['mpn'])}".strip()
        out.append(
            f"| {_cell(r['refs'])} | {_cell(r['qty_8zone'])} | {_cell(r['qty_4zone'])} | {part} | {maker} "
            f"| {lcsc or 'none'} | {_cell(r['jlcpcb_library'])} | {_cell(r['jlcpcb_stock'])} "
            f"| {_cell(r['usd_each_at_100'])} | {ds} | {_cell(r['note'])} |"
        )
    out.append("")
    out.append(f"{len(rows)} lines, read from `hardware/bom-preliminary.csv` when this page was built.")
    return "\n".join(out)


def on_page_markdown(markdown, page, config, files):
    if MARKER not in markdown:
        return markdown
    return markdown.replace(MARKER, _table())
