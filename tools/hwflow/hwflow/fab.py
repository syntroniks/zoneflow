# SPDX-License-Identifier: Apache-2.0
"""Turn KiCad's BOM and position exports into the CSV files JLCPCB's assembly service takes."""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path


def read_rotations(path: Path | None) -> list[tuple[re.Pattern, float, float, float]]:
    rows = []
    if path and path.exists():
        with open(path, newline="", encoding="utf-8") as fh:
            for row in csv.reader(r for r in fh if not r.startswith("#")):
                if not row or row[0] == "footprint_regex":
                    continue
                rows.append((re.compile(row[0]), float(row[1]), float(row[2] or 0), float(row[3] or 0)))
    return rows


def cmd_cpl(args) -> int:
    """KiCad position CSV (Ref,Val,Package,PosX,PosY,Rot,Side) -> JLCPCB CPL."""
    rotations = read_rotations(Path(args.rotations) if args.rotations else None)
    out_rows = []
    corrected = 0
    with open(args.pos, newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            rot = float(row["Rot"])
            x, y = float(row["PosX"]), float(row["PosY"])
            for pat, d_rot, dx, dy in rotations:
                if pat.search(row["Package"]):
                    rot = (rot + d_rot) % 360
                    x, y = x + dx, y + dy
                    corrected += 1
                    break
            out_rows.append({
                "Designator": row["Ref"],
                "Mid X": f"{x:.4f}mm",
                "Mid Y": f"{y:.4f}mm",
                "Layer": "Top" if row["Side"].lower() == "top" else "Bottom",
                "Rotation": f"{rot:g}",
            })
    with open(args.out, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        w.writeheader()
        w.writerows(out_rows)
    print(f"cpl: {len(out_rows)} placements, {corrected} with a rotation correction -> {args.out}")
    return 0


def cmd_bom(args) -> int:
    """KiCad grouped BOM CSV -> JLCPCB BOM (Comment, Designator, Footprint, LCSC Part #)."""
    merged: dict[tuple[str, str], dict] = {}
    missing = []
    with open(args.bom, newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            lcsc = row["LCSC"].strip()
            footprint = row["Footprint"].split(":")[-1]
            if not lcsc:
                missing.append(row["Reference"])
            # One line per orderable part: the same LCSC number can carry
            # different schematic values (two switches named RESET and BOOT).
            key = (lcsc or row["Reference"], footprint)
            line = merged.setdefault(key, {"Comment": [], "Designator": [], "Footprint": footprint,
                                           "LCSC Part #": lcsc, "Quantity": 0})
            if row["Value"] not in line["Comment"]:
                line["Comment"].append(row["Value"])
            line["Designator"] += row["Reference"].split(",")
            line["Quantity"] += int(row["Qty"])
    out_rows = [{**line, "Comment": " / ".join(line["Comment"]), "Designator": ",".join(line["Designator"])}
                for line in merged.values()]
    with open(args.out, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=["Comment", "Designator", "Footprint", "LCSC Part #", "Quantity"])
        w.writeheader()
        w.writerows(out_rows)
    if missing:
        sys.stderr.write("bom: no LCSC number for: " + "; ".join(missing) + "\n")
        return 1
    print(f"bom: {len(out_rows)} lines -> {args.out}")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hwflow.fab")
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("cpl")
    c.add_argument("pos")
    c.add_argument("out")
    c.add_argument("--rotations")
    c.set_defaults(fn=cmd_cpl)
    b = sub.add_parser("bom")
    b.add_argument("bom")
    b.add_argument("out")
    b.set_defaults(fn=cmd_bom)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())
