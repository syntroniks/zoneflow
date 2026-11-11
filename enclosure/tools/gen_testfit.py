# /// script
# requires-python = ">=3.11"
# dependencies = ["pyyaml>=6"]
# ///
"""Turn the laid-out board into the stand-ins of the printable test-fit board.

    uv run tools/gen_testfit.py board_layout.json ../hardware/mechanical.yaml \
        ../hardware/zoneflow-a/design/heights.toml testfit_params.scad output/test-fit/stand-ins.md

Inputs:

- `board_layout.json`, written from the board file by `tools/board_layout.py`:
  the outline, the holes, and for every footprint its position, side, pads
  and the outline it draws on the fabrication layer.
- `hardware/mechanical.yaml`: for the parts the enclosure touches, the body
  as boxes and cylinders, each from a datasheet the contract names.
- `design/heights.toml`: the height of every other footprint.

A board file has no heights and a contract has no layout, so each stand-in
takes its position from the board and its body from one of the other two:

- A part the contract models stands where the board file puts it, with the
  contract's body. The script first compares the two positions the way the
  hardware flow does (pin 1, the pads or the body centre) and stops if they
  differ by more than 0.05 mm: then the enclosure is stale against the board.
- Any other part at least MIN_HEIGHT tall is a box: the footprint's
  fabrication outline, as high as heights.toml says. Lower parts are left
  out. They are too small to print, and no printed part comes that close to
  the board outside the places the contract already models.

It writes `testfit_params.scad` for `testfit.scad`, and a table of every
stand-in with where its numbers came from.
"""

from __future__ import annotations

import json
import math
import sys
import tomllib
from pathlib import Path

import yaml

TOL = 0.05         # mm, board position against contract position
MIN_HEIGHT = 1.5   # mm, lowest part that gets a stand-in. The light pipes end at z = 1.5.
FAB_LINE = 0.1     # mm, width of the fabrication-layer line; its half is in every bounding box
TAIL = 2.5         # mm, assumed length of a soldered lead beyond the board
TAIL_MIN_DRILL = 0.5   # smaller plated holes are vias inside a footprint, not leads
ARC_STEPS = 8      # segments per outline arc

HEADERS = ("J1", "J2", "J3", "J4")


def fmt(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return f"{round(v, 4):g}"
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(v, (list, tuple)):
        return "[" + ", ".join(fmt(x) for x in v) + "]"
    raise TypeError(f"cannot write {v!r} to OpenSCAD")


def rows(name: str, data: list, comment: str = "") -> str:
    head = f"// {comment}\n" if comment else ""
    if not data:
        return f"{head}{name} = [];\n"
    return f"{head}{name} = [\n" + "".join(f"    {fmt(r)},\n" for r in data) + "];\n"


def outline_polygon(segments: list[dict]) -> list[list[float]]:
    """Chain the lines and arcs of the outline into one closed polygon."""
    def pts(seg: dict, reverse: bool) -> list[tuple[float, float]]:
        if seg["type"] == "line":
            p = [tuple(seg["start"]), tuple(seg["end"])]
        else:
            cx, cy = seg["centre"]
            a0 = math.atan2(seg["start"][1] - cy, seg["start"][0] - cx)
            a1 = math.atan2(seg["end"][1] - cy, seg["end"][0] - cx)
            am = math.atan2(seg["mid"][1] - cy, seg["mid"][0] - cx)
            # Go from a0 to a1 through am: pick the direction that passes the mid point.
            ccw = (a1 - a0) % (2 * math.pi)
            if (am - a0) % (2 * math.pi) > ccw:
                ccw -= 2 * math.pi
            r = seg["radius"]
            p = [(cx + r * math.cos(a0 + ccw * i / ARC_STEPS), cy + r * math.sin(a0 + ccw * i / ARC_STEPS))
                 for i in range(ARC_STEPS + 1)]
            p[0], p[-1] = tuple(seg["start"]), tuple(seg["end"])
        return p[::-1] if reverse else p

    def close(a, b) -> bool:
        return math.hypot(a[0] - b[0], a[1] - b[1]) < 1e-3

    left = list(segments)
    loop = pts(left.pop(0), False)
    while left:
        for i, seg in enumerate(left):
            if close(seg["start"], loop[-1]):
                loop += pts(left.pop(i), False)[1:]
                break
            if close(seg["end"], loop[-1]):
                loop += pts(left.pop(i), True)[1:]
                break
        else:
            raise SystemExit("board outline is not one closed loop")
    if not close(loop[0], loop[-1]):
        raise SystemExit("board outline does not close")
    loop.pop()
    # Counter-clockwise, starting at the lowest-left point, so the file is stable.
    area = sum(a[0] * b[1] - b[0] * a[1] for a, b in zip(loop, loop[1:] + loop[:1]))
    if area < 0:
        loop.reverse()
    k = min(range(len(loop)), key=lambda i: (round(loop[i][1], 4), round(loop[i][0], 4)))
    loop = loop[k:] + loop[:k]
    return [[round(x, 4), round(y, 4)] for x, y in loop]


def true_body(fp: dict) -> list[float] | None:
    """The fabrication outline without the half line width its bounding box includes."""
    b = fp["body"]
    if b is None:
        return None
    h = FAB_LINE / 2
    return [b[0] + h, b[1] + h, b[2] - h, b[3] - h]


def board_reference(c: dict, fp: dict) -> tuple[list[float], list[float], str]:
    """Where the board puts the point the contract positions, the contract's value, and its name."""
    if "pin1" in c:
        got = next((q["at"] for q in fp["pads"] if q["number"] == "1"), None)
        if got is None:
            raise SystemExit(f"{c['ref']}: no pad 1 on the board")
        return got, c["pin1"], "pin 1"
    if "pads" in c:
        got = sorted(tuple(q["at"]) for q in fp["pads"] if q["number"])
        want = sorted(map(tuple, c["pads"]))
        if len(got) != len(want):
            raise SystemExit(f"{c['ref']}: {len(got)} pads on the board, {len(want)} in the contract")
        mean = lambda pts: [sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)]  # noqa: E731
        return mean(got), mean(want), "pads"
    b = true_body(fp)
    if c.get("reference") == "anchor" or b is None:
        return fp["at"], [c["x"], c["y"]], "anchor"
    return [(b[0] + b[2]) / 2, (b[1] + b[3]) / 2], [c["x"], c["y"]], "body centre"


def main(argv: list[str]) -> int:
    if len(argv) != 6:
        print(__doc__, file=sys.stderr)
        return 2
    layout = json.loads(Path(argv[1]).read_text(encoding="utf-8"))
    contract = yaml.safe_load(Path(argv[2]).read_text(encoding="utf-8"))
    with open(argv[3], "rb") as fh:
        heights = tomllib.load(fh)
    dest, table = Path(argv[4]), Path(argv[5])

    fps = {f["ref"]: f for f in layout["footprints"]}
    by_fp = heights.get("footprint", {})
    by_ref = heights.get("ref", {})
    bad: list[str] = []

    # ---- The board itself ------------------------------------------------
    poly = outline_polygon(layout["outline"])
    xs, ys = [p[0] for p in poly], [p[1] for p in poly]
    cxs = [p[0] for p in contract["board"]["outline"]]
    cys = [p[1] for p in contract["board"]["outline"]]
    if any(abs(a - b) > TOL for a, b in zip([min(xs), min(ys), max(xs), max(ys)],
                                             [min(cxs), min(cys), max(cxs), max(cys)])):
        bad.append(f"outline {min(xs)}, {min(ys)} to {max(xs)}, {max(ys)} is not the contract's")
    if abs(layout["thickness"] - contract["board"]["thickness"]) > 1e-6:
        bad.append(f"thickness {layout['thickness']} is not the contract's {contract['board']['thickness']}")
    radius = max([s["radius"] for s in layout["outline"] if s["type"] == "arc"], default=0)
    if radius > contract["board"]["corner_radius_max"] + 1e-6:
        bad.append(f"corner radius {radius} is over the contract's {contract['board']['corner_radius_max']}")

    holes = []
    for f in layout["footprints"]:
        for q in f["pads"]:
            if q["through"] and not q["plated"] and f["ref"].startswith("H"):
                holes.append([f["ref"], q["at"][0], q["at"][1], q["drill"]])
    for h in contract["holes"]:
        if h["d"] > 0 and not any(math.hypot(x - h["x"], y - h["y"]) <= TOL and abs(d - h["d"]) < 0.01
                                  for _, x, y, d in holes):
            bad.append(f"hole {h['id']}: no unplated {h['d']} mm hole at ({h['x']}, {h['y']}) on the board")

    # ---- Stand-ins --------------------------------------------------------
    prims: list[list] = []        # [ref, group, kind, cx, cy, a, b, z0, z1]
    listing: list[list[str]] = []
    headers, tails = [], []
    contract_refs = set()
    for c in contract["parts"]:
        ref = c["ref"]
        if ref not in fps:
            continue                      # the panel, its cable, the plugs: not on the board
        contract_refs.add(ref)
        fp = fps[ref]
        if fp["side"] != c["side"]:
            bad.append(f"{ref} is on the {fp['side']} side, the contract says {c['side']}")
        got, want, what = board_reference(c, fp)
        dx, dy = got[0] - want[0], got[1] - want[1]
        if math.hypot(dx, dy) > TOL:
            bad.append(f"{ref}: {what} is at ({got[0]:.3f}, {got[1]:.3f}) on the board, "
                       f"the contract says ({want[0]}, {want[1]})")
        group = "top" if c["side"] == "top" else ref
        sizes = []
        for m in c.get("model", []):
            if "box" in m:
                ox, oy, sx, sy, z0, z1 = m["box"]
                prims.append([ref, group, "box", c["x"] + ox + dx, c["y"] + oy + dy, sx, sy, z0, z1])
                sizes.append(f"{sx:g} x {sy:g} x {z1 - z0:g}")
            else:
                ox, oy, d, z0, z1 = m["cyl"]
                prims.append([ref, group, "cyl", c["x"] + ox + dx, c["y"] + oy + dy, d, 0, z0, z1])
                sizes.append(f"dia {d:g} x {z1 - z0:g}")
        top = max(m.get("box", m.get("cyl"))[-1] for m in c["model"])
        listing.append([ref, c["name"], c["side"], f"{got[0]:.3f}, {got[1]:.3f} ({what})", "; ".join(sizes),
                        f"{top:g}", "Body: `hardware/mechanical.yaml`, part entry. Position: board file."])
        if ref in HEADERS:
            _, _, _, cx, cy, sx, sy, z0, z1 = next(p for p in prims if p[0] == ref)
            headers.append([ref, cx - sx / 2, cx + sx / 2, cy - sy / 2, cy + sy / 2, z1, c["poles"]])

    # The cell. The contract's box is the holder; the footprint also draws the 20 mm cell.
    bt = fps.get("BT1")
    if bt and bt["body"] and "BT1" in contract_refs:
        b = true_body(bt)
        d = min(b[2] - b[0], b[3] - b[1])
        top = max(p[8] for p in prims if p[0] == "BT1")
        if abs(d - 20.0) < 0.05:
            prims.append(["BT1", "top", "cyl", (b[0] + b[2]) / 2, (b[1] + b[3]) / 2, d, 0, 0, top])
            listing.append(["BT1", "CR2032 cell in the holder", "top",
                            f"{(b[0] + b[2]) / 2:.3f}, {(b[1] + b[3]) / 2:.3f} (body centre)", f"dia {d:g} x {top:g}",
                            f"{top:g}", "Diameter: the 20 mm circle on the footprint's fabrication layer "
                            "(a CR2032 is 20 mm by its IEC size code). Drawn to the holder's height."])

    skipped = 0
    for f in layout["footprints"]:
        if f["ref"] in contract_refs or f["dnp"]:
            continue
        name = f["footprint"].split(":")[-1]
        entry = by_ref.get(f["ref"]) or by_fp.get(name)
        if entry is None:
            bad.append(f"{f['ref']}: no height for footprint {name} in heights.toml")
            continue
        h = float(entry["height"])
        b = true_body(f)
        if h <= 0 or b is None:
            continue                      # pads only: test pads, unfitted header, holes
        if h < MIN_HEIGHT:
            skipped += 1
            continue
        if f["side"] != "top":
            bad.append(f"{f['ref']} is on the bottom side and is not in the contract")
            continue
        if f["rot"] % 90 != 0:
            bad.append(f"{f['ref']} is turned {f['rot']} degrees; only multiples of 90 are handled")
        prims.append([f["ref"], "top", "box", (b[0] + b[2]) / 2, (b[1] + b[3]) / 2, b[2] - b[0], b[3] - b[1], 0, h])
        listing.append([f["ref"], name, "top", f"{f['at'][0]:.3f}, {f['at'][1]:.3f} (anchor)",
                        f"{b[2] - b[0]:.2f} x {b[3] - b[1]:.2f} x {h:g}", f"{h:g}",
                        f"Outline: fabrication layer of the footprint. Height: `heights.toml`, src \"{entry['src']}\"."])

    # Soldered lead ends, for the check only. A top-side part's leads come out below.
    for f in layout["footprints"]:
        name = f["footprint"].split(":")[-1]
        fitted = f["ref"] in contract_refs or float((by_ref.get(f["ref"]) or by_fp.get(name) or {}).get("height", 0)) > 0
        if f["dnp"] or not fitted:
            continue
        for q in f["pads"]:
            if q["through"] and q["plated"] and q["drill"] >= TAIL_MIN_DRILL:
                tails.append([f["ref"], q["at"][0], q["at"][1], q["drill"], "bottom" if f["side"] == "top" else "top"])

    if bad:
        raise SystemExit("the board and the mechanical contract disagree; the enclosure is stale:\n  "
                         + "\n  ".join(bad))

    enc = next(c for c in contract["parts"] if c["ref"] == "SW3")
    j5 = next(p for p in prims if p[0] == "J5")
    u4 = next(p for p in prims if p[0] == "U4")
    plugs = []
    for c in contract["parts"]:
        if c.get("group") == "mate":
            ox, oy, sx, sy, z0, z1 = c["model"][0]["box"]
            h = next(h for h in headers if h[0] == c["ref"].split("-")[0])
            x = (h[1] + h[2]) / 2          # in line with the header as the board places it
            plugs.append([c["ref"], x - sx / 2, x + sx / 2, c["y"] + oy - sy / 2, c["y"] + oy + sy / 2, z0, z1])
    ds1 = next(c for c in contract["parts"] if c["ref"] == "DS1")

    order = {"top": 0}
    prims.sort(key=lambda p: (order.get(p[1], 1), p[1], p[0]))
    out = [
        "// GENERATED by enclosure/tools/gen_testfit.py from enclosure/board_layout.json",
        "// (the board file), hardware/mechanical.yaml and design/heights.toml.",
        "// Do not edit. Run `make` in enclosure/.",
        "",
        f"tf_board_rev = {fmt(layout['revision'])};",
        f"tf_board_date = {fmt(layout['date'])};",
        f"tf_contract_rev = {fmt(contract['revision'])};",
        f"tf_thickness = {fmt(layout['thickness'])};",
        f"tf_min_height = {fmt(MIN_HEIGHT)};",
        f"tf_tail_len = {fmt(TAIL)};",
        rows("tf_outline", poly, "Board outline, counter-clockwise, corner arcs as polygons.").rstrip(),
        rows("tf_holes", holes, "Unplated mounting holes: ref, x, y, diameter.").rstrip(),
        rows("tf_prims", prims,
             "Stand-ins: ref, group, kind, centre x, y, then size x, y (box) or diameter, 0 (cyl), z0, z1.\n"
             "// z is the height above the surface the part is on. group \"top\" is on the main piece;\n"
             "// any other group is a bottom-side part, printed on its own.").rstrip(),
        rows("tf_headers", headers, "Terminal headers: ref, x0, x1, y of the mating face, y of the back, height, poles.").rstrip(),
        rows("tf_plugs", plugs, "Mated plugs: ref, x0, x1, y0, y1, z0, z1.").rstrip(),
        rows("tf_tails", tails, "Soldered lead ends: ref, x, y, hole diameter, side they stick out of.").rstrip(),
        f"tf_usb = {fmt([j5[3] - j5[5] / 2, j5[3] + j5[5] / 2, j5[4] - j5[6] / 2, j5[4] + j5[6] / 2, j5[8]])};"
        "   // x0, x1, y of the mouth, y of the back, height",
        f"tf_module = {fmt([u4[3] - u4[5] / 2, u4[3] + u4[5] / 2, u4[4] - u4[6] / 2, u4[4] + u4[6] / 2, u4[8]])};"
        "   // x0, x1, y0, y1, height",
        f"tf_shaft_flat = {fmt([enc['shaft_flat_across'], enc['shaft_flat_length']])};   // across the flat, its length from the top",
        f"tf_panel_tail = {fmt([ds1['fpc_tail_length'], ds1['fpc_tail_width'], ds1['fpc_tail_centre_y']])};"
        "   // FPC tail beyond the outline: length, width, centre y",
        f"tf_panel_root_y = {fmt(ds1['fpc_root_y'])};   // wide part of the FPC at the panel edge",
        f"tf_panel_exit = {fmt(ds1['fpc_exit_below_front'])};   // the tail leaves this far below the glass",
        "",
    ]
    dest.write_text("\n".join(out), encoding="utf-8")

    n_contract = len(contract_refs)
    n_other = len({p[0] for p in prims}) - n_contract
    md = [
        "# Test-fit board: the stand-ins and where their numbers come from",
        "",
        "Generated by `enclosure/tools/gen_testfit.py`. Do not edit.",
        "",
        f"Board file: `{layout['source']}`, revision {layout['revision']}, dated {layout['date']}.",
        f"Mechanical contract: `hardware/mechanical.yaml`, revision {contract['revision']}.",
        "",
        f"- **{n_contract} parts the contract models.** Each stands where the board file puts it. The",
        f"  board and the contract agree on every one within {TOL} mm, or this file would not exist.",
        "  Their bodies are the contract's boxes and cylinders, which name their datasheets.",
        f"- **{n_other} other parts at least {MIN_HEIGHT:g} mm tall.** Each is a box: the outline the footprint",
        "  draws on the fabrication layer, as high as `hardware/zoneflow-a/design/heights.toml` says.",
        f"- **{skipped} parts lower than {MIN_HEIGHT:g} mm are left out**, and so are the test pads and the",
        "  unfitted debug header J8. No printed part comes within 1.5 mm of the board except at",
        "  places the contract models: the fuse well, the board edges and the light pipes.",
        f"- **{len(tails)} soldered lead ends** are in the interference check and are not printed. Each is a",
        f"  cylinder as wide as its hole and {TAIL:g} mm long, on the side the lead comes out of. {TAIL:g} mm is an",
        "  assumption: no drawing gives the trimmed length.",
        "",
        "Positions are in board coordinates, millimetres: origin at the bottom-left corner seen",
        "from the component side. Heights are above the side the part is on.",
        "",
        "| Ref | Part | Side | Position on the board | Stand-in, mm | Top, mm | Source |",
        "|---|---|---|---|---|---|---|",
    ]
    md += ["| " + " | ".join(r) + " |" for r in listing]
    md.append("")
    table.parent.mkdir(parents=True, exist_ok=True)
    table.write_text("\n".join(md), encoding="utf-8")
    print(f"{dest}: {n_contract} contract parts, {n_other} other parts, {skipped} left out, {len(tails)} lead ends")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
