# SPDX-License-Identifier: Apache-2.0
"""Check a built board against a mechanical contract (``hardware/mechanical.yaml``).

Input is ``build/placement.json``, which ``hwflow.pcbbuild dump`` writes from
the saved board, the contract, and a table of component heights
(``design/heights.toml``). Nothing is measured: the check compares the numbers
in the board file with the numbers in the contract.

What is checked:

- the outline, the number of copper layers and the thickness;
- every hole of the contract: an unplated hole of that diameter at that place;
- every part the contract places: its reference point (body centre, pin 1 or
  the pads, as the contract gives it) and its side. A part the contract calls
  free (a ``placement`` text and no ``fixed: true``) is checked against the
  position the contract records, too, so that the enclosure model and the
  board cannot drift apart;
- the keep-out circle around every hole, foot and post;
- the height zones: for every other part, the lowest limit under its body
  against its height. A later zone overrides an earlier one where they overlap;
- the bottom side: only the parts the contract puts there;
- the edge keep-out;
- the soldering access rule around the top-side pads of bottom-side parts.

Only the standard library is used. The caller parses the YAML.
"""

from __future__ import annotations

import json
import math
import tomllib
from pathlib import Path

TOL = 0.05          # mm, on a position
ACCESS_RADIUS = 2.5  # mm around a top-side pad of a bottom-side part
ACCESS_HEIGHT = 1.5  # mm, taller parts must stay outside that radius


def _inter(a, b):
    x0, y0, x1, y1 = max(a[0], b[0]), max(a[1], b[1]), min(a[2], b[2]), min(a[3], b[3])
    return (x0, y0, x1, y1) if x1 - x0 > 1e-6 and y1 - y0 > 1e-6 else None


def _subtract(a, b):
    """The parts of rectangle a outside rectangle b, as up to four rectangles."""
    i = _inter(a, b)
    if not i:
        return [a]
    out = []
    if i[1] > a[1]:
        out.append((a[0], a[1], a[2], i[1]))
    if i[3] < a[3]:
        out.append((a[0], i[3], a[2], a[3]))
    if i[0] > a[0]:
        out.append((a[0], i[1], i[0], i[3]))
    if i[2] < a[2]:
        out.append((i[2], i[1], a[2], i[3]))
    return [r for r in out if r[2] - r[0] > 1e-6 and r[3] - r[1] > 1e-6]


def zone_limit(body, side: str, zones: list[dict]) -> tuple[float, str] | None:
    """Lowest height limit under a body rectangle, and the zone that sets it.

    A zone listed later overrides an earlier one where they overlap, so the
    zones are applied from the last to the first and each takes what is left.
    """
    left = [tuple(body)]
    best = None
    for z in reversed([z for z in zones if z["side"] == side]):
        rest = []
        for r in left:
            if _inter(r, z["rect"]):
                if best is None or z["max_height"] < best[0]:
                    best = (float(z["max_height"]), z["id"])
            rest += _subtract(r, tuple(z["rect"]))
        left = rest
        if not left:
            break
    return best


def _circle_hits(body, cx: float, cy: float, r: float) -> bool:
    nx, ny = min(max(cx, body[0]), body[2]), min(max(cy, body[1]), body[3])
    return math.hypot(nx - cx, ny - cy) < r - 1e-6


def load_heights(path: Path) -> dict:
    with open(path, "rb") as fh:
        return tomllib.load(fh)


def height_of(part: dict, heights: dict) -> float | None:
    by_ref, by_fp = heights.get("ref", {}), heights.get("footprint", {})
    if part["ref"] in by_ref:
        return float(by_ref[part["ref"]]["height"])
    name = part["footprint"].split(":")[-1]
    if name in by_fp:
        return float(by_fp[name]["height"])
    return None


def check(placement: dict, contract: dict, heights: dict) -> tuple[list[str], str]:
    problems: list[str] = []
    parts = {p["ref"]: p for p in placement["parts"]}
    board = contract["board"]
    xs = [p[0] for p in board["outline"]]
    ys = [p[1] for p in board["outline"]]
    want = [min(xs), min(ys), max(xs), max(ys)]
    # The outline is drawn with a 0.1 mm line, which the bounding box includes.
    if any(abs(a - b) > 0.06 for a, b in zip(placement["outline"], want)):
        problems.append(f"outline is {placement['outline']}, the contract says {want}")
    if placement["layers"] != board["layers"]:
        problems.append(f"{placement['layers']} copper layers, the contract says {board['layers']}")
    if abs(placement["thickness"] - board["thickness"]) > 1e-6:
        problems.append(f"thickness {placement['thickness']}, the contract says {board['thickness']}")

    # Holes: an unplated hole of the right size at the right place.
    npth = [(q["at"], q["drill"]) for p in placement["parts"] for q in p["pads"] if q["through"] and not q["plated"]]
    for h in contract["holes"]:
        if h["d"] <= 0:
            continue
        if not any(math.hypot(at[0] - h["x"], at[1] - h["y"]) <= TOL and abs(d - h["d"]) < 0.01 for at, d in npth):
            problems.append(f"hole {h['id']}: no unplated {h['d']} mm hole at ({h['x']}, {h['y']})")

    # Parts the contract places.
    placed = 0
    contract_refs = set()
    for c in contract["parts"]:
        ref = c["ref"]
        if ref not in parts:
            continue  # the panel, the plugs: not on the board
        contract_refs.add(ref)
        p = parts[ref]
        placed += 1
        if p["side"] != c["side"]:
            problems.append(f"{ref} is on the {p['side']} side, the contract says {c['side']}")
        if "pin1" in c:
            got = next((q["at"] for q in p["pads"] if q["number"] == "1"), None)
            if got is None or math.hypot(got[0] - c["pin1"][0], got[1] - c["pin1"][1]) > TOL:
                problems.append(f"{ref} pin 1 is at {got}, the contract says {c['pin1']}")
        elif "pads" in c:
            got = sorted(tuple(q["at"]) for q in p["pads"] if q["number"])
            if len(got) != len(c["pads"]) or any(
                    math.hypot(g[0] - w[0], g[1] - w[1]) > TOL for g, w in zip(got, sorted(map(tuple, c["pads"])))):
                problems.append(f"{ref} pads are at {got}, the contract says {c['pads']}")
        else:
            # The contract gives the body centre, or the axis of a round part. Both are the centre
            # of the body outline of the footprint, except where the anchor is the axis (reference
            # "anchor" in the contract entry).
            body = p["body"]
            centre = p["at"] if c.get("reference") == "anchor" or body is None else \
                [(body[0] + body[2]) / 2, (body[1] + body[3]) / 2]
            if math.hypot(centre[0] - c["x"], centre[1] - c["y"]) > TOL:
                problems.append(f"{ref} is at ({centre[0]:.3f}, {centre[1]:.3f}), the contract says "
                                f"({c['x']}, {c['y']})")
        # The long side of the body must lie the way the contract's model has it.
        box = (c.get("model") or [{}])[0].get("box")
        if box and p["body"] and abs(box[2] - box[3]) > 1.0:
            w, h = p["body"][2] - p["body"][0], p["body"][3] - p["body"][1]
            if abs(w - h) > 1.0 and (w > h) != (box[2] > box[3]):
                problems.append(f"{ref}: the body is {w:.1f} x {h:.1f}, the contract's model {box[2]} x {box[3]}")

    # Keep-outs around holes, feet and posts.
    for h in contract["holes"]:
        for p in placement["parts"]:
            r = h["top_keepout" if p["side"] == "top" else "bottom_keepout"] / 2
            region = p["body"] or p["courtyard"]
            if r <= 0 or region is None or not p["pads"] or p["ref"] in contract_refs:
                continue
            if any(q["through"] and not q["plated"] and math.hypot(q["at"][0] - h["x"], q["at"][1] - h["y"]) <= TOL
                   for q in p["pads"]):
                continue  # the hole itself
            if _circle_hits(region, h["x"], h["y"], r):
                problems.append(f"{p['ref']} is inside the {2 * r:g} mm keep-out of {h['kind']} {h['id']} "
                                f"({p['side']} side)")

    # Bottom side: only what the contract puts there.
    allowed_bottom = {c["ref"] for c in contract["parts"] if c["side"] == "bottom"}
    for p in placement["parts"]:
        if p["side"] == "bottom" and p["ref"] not in allowed_bottom:
            problems.append(f"{p['ref']} is on the bottom side; the contract allows only {sorted(allowed_bottom)}")

    # Height zones and edge keep-out, for every part the contract does not place itself.
    edge = float(board.get("edge_keepout", 0))
    checked = 0
    for p in placement["parts"]:
        if p["ref"] in contract_refs:
            continue
        region = p["body"] or p["courtyard"]
        if region is None:
            continue
        h = height_of(p, heights)
        if h is None:
            problems.append(f"{p['ref']}: no height for footprint {p['footprint']} in the heights table")
            continue
        if h <= 0:
            continue  # pads only: test pads, unfitted header, solder jumper, mounting hole
        checked += 1
        limit = zone_limit(region, p["side"], contract["zones"])
        if limit is None:
            problems.append(f"{p['ref']} is in no height zone")
        elif h > limit[0] + 1e-9:
            problems.append(f"{p['ref']} is {h:g} mm high in zone {limit[1]}, which allows {limit[0]:g}")
        if edge and (region[0] < want[0] + edge - 1e-6 or region[1] < want[1] + edge - 1e-6
                     or region[2] > want[2] - edge + 1e-6 or region[3] > want[3] - edge + 1e-6):
            problems.append(f"{p['ref']}: body {['%.2f' % v for v in region]} is within {edge:g} mm of the board edge")

    # Soldering access around the top-side pads of bottom-side parts.
    for b in (p for p in placement["parts"] if p["side"] == "bottom"):
        for q in b["pads"]:
            if not q["through"]:
                continue
            r = max(q["size"]) / 2 + ACCESS_RADIUS
            for p in placement["parts"]:
                region = p["body"] or p["courtyard"]
                if p["side"] != "top" or region is None:
                    continue
                h = height_of(p, heights)
                if h is not None and h > ACCESS_HEIGHT and _circle_hits(region, q["at"][0], q["at"][1], r):
                    problems.append(f"{p['ref']} ({h:g} mm high) is within {ACCESS_RADIUS} mm of pad {q['number']} "
                                    f"of {b['ref']}, which is soldered from the top")

    holes = len([h for h in contract["holes"] if h["d"] > 0])
    summary = (f"mechanical contract revision {contract.get('revision')}: outline, {holes} holes, {placed} placed "
               f"parts, height zones for {checked} parts, keep-outs, bottom side, edge and soldering access agree")
    return problems, summary


def run(project_dir: Path, contract: dict, heights_path: Path) -> tuple[list[str], str]:
    placement = json.loads((Path(project_dir) / "build" / "placement.json").read_text(encoding="utf-8"))
    return check(placement, contract, load_heights(heights_path))
