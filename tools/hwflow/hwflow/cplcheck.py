# SPDX-License-Identifier: Apache-2.0
"""Check a JLCPCB placement file against the footprints of JLCPCB's own part library.

JLCPCB places the footprint that its library holds for an LCSC number: the footprint's origin on the
coordinate of the placement file, turned by the rotation of the file. That library does not always
lie as KiCad's footprint lies, and a correction table by package family cannot know: two parts in
one package can differ. This check takes, for every placed part, the library footprint of its LCSC
number, puts it on the board as the placement file says, and compares where its pads land with the
pads of the board: all pads as a set, pad 1 on pad 1 for a part with numbered pins, the cathode on
pad 1 for a diode or an LED.

The library footprints are those of the JLCEDA/EasyEDA Official Library (https://lceda.cn/,
https://easyeda.com), read through the same interface that EasyEDA's editor uses. They are fetched
on request (``--fetch``) into a cache directory and are not part of this repository.

What it proves: the placement file agrees with the library footprint, which is what JLCPCB's
placement preview draws. What it does not prove: that the library footprint is right for the part.

    python -m hwflow.cplcheck build/placement.json output/x-cpl-jlcpcb.csv output/x-bom-jlcpcb.csv \\
        --cache ../.cache/jlc-library [--fetch] [--report output/reports/placement-check.md]
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
import subprocess
import time
from pathlib import Path

UNIT = 0.254        # one library unit is 10 mil
SOURCE = "https://lceda.cn/api/products/{lcsc}/components?version=6.4.19.5"
CATHODE = re.compile(r"^(K|C|CATHODE|NEG|-)\d?$", re.I)
ANODE = re.compile(r"^(A|ANODE|POS|\+)\d?$", re.I)
FIT = 0.6           # a library pad this far from every board pad does not fit, in mm


def parse_library(data: dict) -> dict | None:
    """Pads (number, x, y, w, h; millimetres, y up, origin the footprint origin) and pin names."""
    result = data.get("result")
    if not data.get("success") or not result or "packageDetail" not in result:
        return None
    ds = result["packageDetail"]["dataStr"]
    ds = json.loads(ds) if isinstance(ds, str) else ds
    ox, oy = float(ds["head"]["x"]), float(ds["head"]["y"])
    pads = []
    for shape in ds["shape"]:
        if shape.startswith("PAD~"):
            f = shape.split("~")
            pads.append({"number": f[8], "x": (float(f[2]) - ox) * UNIT, "y": -(float(f[3]) - oy) * UNIT,
                         "w": float(f[4]) * UNIT, "h": float(f[5]) * UNIT})
    sym = result.get("dataStr") or {}
    sym = json.loads(sym) if isinstance(sym, str) else sym
    pins = {}
    for shape in sym.get("shape", []):
        if shape.startswith("P~"):
            parts = shape.split("^^")
            pins[shape.split("~")[3]] = parts[3].split("~")[4] if len(parts) > 3 else ""
    return {"package": result["packageDetail"].get("title", ""), "pads": pads, "pins": pins}


def library(lcsc: str, cache: Path, fetch: bool) -> dict | None:
    f = cache / f"{lcsc}.json"
    if not f.exists():
        if not fetch:
            return None
        cache.mkdir(parents=True, exist_ok=True)
        subprocess.run(["curl", "-sS", "--max-time", "40", "-A", "Mozilla/5.0", "--compressed", "-o", str(f),
                        SOURCE.format(lcsc=lcsc)], check=False)
        time.sleep(1.0)
    try:
        return parse_library(json.loads(f.read_text(encoding="utf-8")))
    except (OSError, ValueError, KeyError):
        return None


def turn(x: float, y: float, deg: float) -> tuple[float, float]:
    a = math.radians(deg)
    return x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)


def worst_offset(lib_pads, board_pads, x, y, deg) -> float:
    """The largest distance from a library pad, placed at x, y and turned by deg, to the nearest board pad."""
    worst = 0.0
    for p in lib_pads:
        dx, dy = turn(p["x"], p["y"], deg)
        worst = max(worst, min(math.hypot(x + dx - b["at"][0], y + dy - b["at"][1]) for b in board_pads))
    return worst


def lands_on(lib_pads, number, target, x, y, deg) -> bool:
    for p in lib_pads:
        if p["number"] == number:
            dx, dy = turn(p["x"], p["y"], deg)
            return math.hypot(x + dx - target[0], y + dy - target[1]) < FIT
    return False


def leads(lib_pads: list[dict]) -> list[dict]:
    """The library pads without a heat tab or ground pad.

    A pad of more than three times the usual area sits differently in two land patterns for one
    package (a DPAK tab, an exposed pad drawn whole or in pieces): the leads decide where the part is.
    """
    areas = sorted(p["w"] * p["h"] for p in lib_pads)
    usual = areas[len(areas) // 2]
    small = [p for p in lib_pads if p["w"] * p["h"] <= 3 * usual]
    return small if len(small) >= 2 else lib_pads


def check_part(part: dict, lib: dict, x: float, y: float, deg: float) -> dict:
    """Does the library footprint, placed at x, y and turned by deg, land on this part's pads?

    Returns ``ok``, ``kind`` (what was compared: "set", "pin 1" or "cathode") and ``offset``, the largest
    distance of a library pad from the nearest board pad at the rotation of the file.
    """
    board = [p for p in part["pads"] if p.get("plated", True) or not p.get("through")]
    lp = lib["pads"]
    offset = worst_offset(leads(lp), board, x, y, deg)
    ok = offset < FIT
    kind = "set"
    by_number: dict[str, list] = {}
    for p in board:
        if p["number"]:
            by_number.setdefault(p["number"], []).append(p["at"])
    two = len(lp) == 2 and len({p["number"] for p in lp}) == 2
    name = part["footprint"].split(":")[-1]
    if two and name.startswith(("D_", "LED_")) and not lib["package"].endswith("-BI"):
        # "-BI" in the library's footprint name is a bidirectional part: either way round is right
        names = lib["pins"]
        k = [n for n, v in names.items() if CATHODE.match(v.strip())]
        a = [n for n, v in names.items() if ANODE.match(v.strip())]
        if k or a:
            kind = "cathode"
            cathode = k[0] if k else next(n for n in names if n not in a)
            ok = ok and lands_on(lp, cathode, by_number["1"][0], x, y, deg)    # KiCad: pad 1 is the cathode
    elif not two and len(by_number.get("1", [])) == 1 and any(p["number"] == "1" for p in lp):
        # A footprint with two pads numbered 1 (a switch with two lands per contact) has no pin 1 to compare.
        kind = "pin 1"
        ok = ok and lands_on(lp, "1", by_number["1"][0], x, y, deg)
    return {"ok": ok, "kind": kind, "offset": offset}


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hwflow.cplcheck")
    ap.add_argument("placement", help="build/placement.json of the board")
    ap.add_argument("cpl", help="the JLCPCB placement file")
    ap.add_argument("bom", help="the JLCPCB BOM, which gives the LCSC number of every designator")
    ap.add_argument("--cache", required=True, help="directory for the library footprints")
    ap.add_argument("--fetch", action="store_true", help="download the library footprints that the cache lacks")
    ap.add_argument("--report")
    args = ap.parse_args(argv)

    parts = {p["ref"]: p for p in json.loads(Path(args.placement).read_text(encoding="utf-8"))["parts"]}
    numbers: dict[str, str] = {}
    with open(args.bom, newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            for ref in row["Designator"].split(","):
                numbers[ref.strip()] = row.get("LCSC Part #", "").strip()
    groups: dict[tuple, list] = {}
    missing = []
    with open(args.cpl, newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            ref = row["Designator"]
            lcsc = numbers.get(ref, "")
            lib = library(lcsc, Path(args.cache), args.fetch) if lcsc else None
            if lib is None or not lib["pads"]:
                missing.append(f"{ref} ({lcsc or 'no LCSC number'})")
                continue
            part = parts[ref]
            deg = float(row["Rotation"]) % 360
            r = check_part(part, lib, float(row["Mid X"].rstrip("m")), float(row["Mid Y"].rstrip("m")), deg)
            key = (part["footprint"].split(":")[-1], lcsc, lib["package"], r["kind"], (deg - part["rot"]) % 360,
                   r["ok"])
            groups.setdefault(key, []).append((ref, r["offset"]))
    bad = sum(len(v) for k, v in groups.items() if not k[5])
    total = sum(len(v) for v in groups.values())
    lines = ["# Placement file against JLCPCB's library footprints", "",
             f"`{Path(args.cpl).name}`: {total} placements compared, {bad} do not fit"
             + (f", {len(missing)} have no library footprint in the cache: {', '.join(missing)}" if missing else "")
             + ".",
             "",
             "Library footprints: JLCEDA/EasyEDA Official Library (<https://lceda.cn/>, <https://easyeda.com>), "
             "one per LCSC number.", "",
             "| Footprint | LCSC | Library footprint | Parts | Compared | Rotation added to KiCad's "
             "| Largest pad offset, mm | Fits |",
             "|---|---|---|---|---|---|---|---|"]
    for key in sorted(groups, key=lambda k: (k[5], k[0], k[1])):
        v = sorted(groups[key])
        lines.append(f"| `{key[0]}` | {key[1]} | `{key[2]}` | {len(v)}: {v[0][0]}{' ...' if len(v) > 1 else ''} "
                     f"| {key[3]} | {key[4]:g} | {max(o for _, o in v):.2f} | {'yes' if key[5] else '**no**'} |")
    text = "\n".join(lines) + "\n"
    if args.report:
        Path(args.report).write_text(text, encoding="utf-8")
    print(f"cplcheck: {total} placements compared with the library, {bad} do not fit, {len(missing)} without a "
          f"library footprint" + (f" -> {args.report}" if args.report else ""))
    for key, v in sorted(groups.items()):
        if not key[5]:
            print(f"  does not fit: {key[0]} {key[1]} ({key[2]}), {key[3]}: {', '.join(r for r, _ in sorted(v))}")
    if missing:
        print("  no library footprint: " + ", ".join(missing) + ("" if args.fetch else " (run with --fetch)"))
    return 1 if bad or missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
