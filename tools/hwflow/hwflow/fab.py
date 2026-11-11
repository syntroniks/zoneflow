# SPDX-License-Identifier: Apache-2.0
"""Turn KiCad's BOM and position exports into the CSV files JLCPCB's assembly service takes."""

from __future__ import annotations

import argparse
import csv
import math
import re
import sys
from pathlib import Path


def read_rotations(path: Path | None) -> list[tuple[re.Pattern, float, float, float, str]]:
    """Rows of the correction table: footprint pattern, rotation, dx, dy and, optionally, one LCSC number.

    A row with a number applies to that part only. The assembler's library holds one footprint per
    part number, and two parts in one package can lie differently in it.
    """
    rows = []
    if path and path.exists():
        with open(path, newline="", encoding="utf-8") as fh:
            for row in csv.reader(r for r in fh if not r.startswith("#")):
                if not row or row[0] == "footprint_regex":
                    continue
                rows.append((re.compile(row[0]), float(row[1]), float(row[2] or 0), float(row[3] or 0),
                             row[4].strip() if len(row) > 4 else ""))
    return rows


def read_part_numbers(path: Path | None) -> dict[str, str]:
    """Designator -> LCSC number, from the JLCPCB BOM this package writes."""
    numbers: dict[str, str] = {}
    if path and path.exists():
        with open(path, newline="", encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                for ref in row["Designator"].split(","):
                    numbers[ref.strip()] = row.get("LCSC Part #", "").strip()
    return numbers


def correct(rotations, package: str, lcsc: str, x: float, y: float, rot: float) -> tuple[float, float, float, bool]:
    """The placement the assembler needs for a part that KiCad has at x, y, turned by rot.

    The first row that fits is used. Its offset is in the footprint's own frame, x to the right and
    y up with the footprint at rotation 0, so it turns with the part. This is for the top side.
    """
    for pat, d_rot, dx, dy, only in rotations:
        if pat.search(package) and (not only or only == lcsc):
            a = math.radians(rot)
            return (x + dx * math.cos(a) - dy * math.sin(a), y + dx * math.sin(a) + dy * math.cos(a),
                    (rot + d_rot) % 360, True)
    return x, y, rot, False


def cmd_cpl(args) -> int:
    """KiCad position CSV (Ref,Val,Package,PosX,PosY,Rot,Side) -> JLCPCB CPL."""
    rotations = read_rotations(Path(args.rotations) if args.rotations else None)
    numbers = read_part_numbers(Path(args.bom) if args.bom else None)
    skip = {r for r in (args.exclude or "").split(",") if r}
    if args.variant:
        import json

        variants = json.loads(Path(args.model).read_text(encoding="utf-8"))["variants"]
        if args.variant not in variants:
            sys.stderr.write(f"cpl: no variant {args.variant!r} in {args.model}\n")
            return 1
        skip |= set(variants[args.variant])
    out_rows = []
    corrected = 0
    with open(args.pos, newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            if row["Ref"] in skip:
                continue
            x, y, rot, hit = correct(rotations, row["Package"], numbers.get(row["Ref"], ""),
                                     float(row["PosX"]), float(row["PosY"]), float(row["Rot"]))
            corrected += hit
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


POLARISED = ("D_", "LED_", "CP_", "SOT-", "SOIC-", "TSSOP-", "TO-", "SMDIP-", "TI_SO-", "ESP32", "USB_C", "JST_",
             "FPC_", "R_Array", "BatteryHolder", "RotaryEncoder")


def cmd_polarity(args) -> int:
    """Where pin 1 of every polarised or multi-pin part is, from ``build/placement.json``.

    The assembly house's library does not always use KiCad's zero rotation, and
    the correction table is not checked against its placement preview. This
    table is what to compare the preview with: for each such part, its
    rotation and the position of pad 1 (the cathode of a diode, the positive
    end of a polarised capacitor) relative to the part's centre.
    """
    import json

    data = json.loads(Path(args.placement).read_text(encoding="utf-8"))
    rows = []
    for p in data["parts"]:
        name = p["footprint"].split(":")[-1]
        numbered = [q for q in p["pads"] if q["number"]]
        if not name.startswith(POLARISED) or not numbered or p["dnp"]:
            continue
        first = next((q for q in numbered if q["number"] in ("1", "A1", "A")), numbered[0])
        box = p["body"] or p["courtyard"]
        cx, cy = ((box[0] + box[2]) / 2, (box[1] + box[3]) / 2) if box else p["at"]
        dx, dy = first["at"][0] - cx, first["at"][1] - cy
        where = ("upper" if dy > 0.05 else "lower" if dy < -0.05 else "") + \
                (" left" if dx < -0.05 else " right" if dx > 0.05 else "")
        rows.append({"Designator": p["ref"], "Footprint": name, "Side": p["side"], "Rotation": f"{p['rot']:g}",
                     "Centre X": f"{cx:.3f}", "Centre Y": f"{cy:.3f}", "Pad": first["number"],
                     "Pad X": f"{first['at'][0]:.3f}", "Pad Y": f"{first['at'][1]:.3f}",
                     "Pad is at": where.strip() or "centre"})
    with open(args.out, "w", newline="", encoding="utf-8") as fh:
        w = csv.DictWriter(fh, fieldnames=list(rows[0]) if rows else ["Designator"])
        w.writeheader()
        w.writerows(rows)
    print(f"polarity: {len(rows)} polarised or multi-pin parts -> {args.out}")
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
    c.add_argument("--bom", help="the JLCPCB BOM, for correction rows that name one LCSC number")
    c.add_argument("--exclude", help="comma-separated references to leave out")
    c.add_argument("--variant", help="leave out the parts this build variant does not fit (needs --model)")
    c.add_argument("--model", help="build/model.json, which lists the parts each variant leaves out")
    c.set_defaults(fn=cmd_cpl)
    q = sub.add_parser("polarity")
    q.add_argument("placement")
    q.add_argument("out")
    q.set_defaults(fn=cmd_polarity)
    b = sub.add_parser("bom")
    b.add_argument("bom")
    b.add_argument("out")
    b.set_defaults(fn=cmd_bom)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())
