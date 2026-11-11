# SPDX-License-Identifier: Apache-2.0
"""Layout rules that KiCad's DRC cannot express, checked on the finished board.

DRC measures copper against copper on one layer. Some rules of a safety
analysis are about what lies next to what in any sense: a net that must stay
away from another on every layer, two circuits that must be a set distance
apart, a net that must have no test pad. ``board.toml`` states them and this
module measures them:

``[[keepaway]]``
    ``net``, ``from`` (patterns), ``distance``, and ``scope``: ``"plan"``
    measures in plan view, whatever the layers, so that not even a crossing on
    another layer passes; ``"layer"`` measures only copper that shares a
    layer. ``kinds`` limits what counts on the other nets: ``"track"``,
    ``"via"``, ``"pad"``. ``except`` lists nets left out of a ``"*"`` pattern.
``[[apart]]``
    ``a`` and ``b``, two lists of references, and ``distance``: the smallest
    gap between a pad of one group and a pad of the other.
``[[perpendicular]]``
    ``parts``, two references that must be at right angles to each other.
``[[no_pad]]``
    ``nets`` and ``refs`` (a pattern): no part matching ``refs`` may have a
    pad on one of the nets.

Pads are taken as their bounding rectangles, tracks as lines with a width,
vias as circles on every layer. Copper pours are not looked at. It compares
numbers in a file. Nothing is measured on a board.

Runs under KiCad's Python: ``python -m hwflow.layoutrules <project> <design>``.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import math
import sys
import tomllib
from pathlib import Path


def seg_seg(a, b, c, d) -> float:
    """Distance between segments ab and cd."""
    def pt_seg(p, q, r):
        vx, vy = r[0] - q[0], r[1] - q[1]
        n2 = vx * vx + vy * vy
        t = 0.0 if n2 == 0 else max(0.0, min(1.0, ((p[0] - q[0]) * vx + (p[1] - q[1]) * vy) / n2))
        return math.hypot(p[0] - q[0] - t * vx, p[1] - q[1] - t * vy)

    def side(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])

    if side(a, b, c) * side(a, b, d) < 0 and side(c, d, a) * side(c, d, b) < 0:
        return 0.0
    return min(pt_seg(a, c, d), pt_seg(b, c, d), pt_seg(c, a, b), pt_seg(d, a, b))


def _inside(p, rect) -> bool:
    return rect[0] <= p[0] <= rect[2] and rect[1] <= p[1] <= rect[3]


def _edges(rect):
    x0, y0, x1, y1 = rect
    c = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    return list(zip(c, c[1:] + c[:1]))


def gap(s, t) -> float:
    """Clear distance between two shapes. A shape is ("seg", a, b, width), ("circle", c, r) or ("rect", box)."""
    def as_segs(sh):
        if sh[0] == "seg":
            return [(sh[1], sh[2])], sh[3] / 2
        if sh[0] == "circle":
            return [(sh[1], sh[1])], sh[2]
        return _edges(sh[1]), 0.0

    sa, ra = as_segs(s)
    sb, rb = as_segs(t)
    if s[0] == "rect" and any(_inside(p, s[1]) for seg in sb for p in seg):
        return 0.0
    if t[0] == "rect" and any(_inside(p, t[1]) for seg in sa for p in seg):
        return 0.0
    d = min(seg_seg(a, b, c, e) for a, b in sa for c, e in sb)
    return max(0.0, d - ra - rb)


def match(name: str, patterns) -> bool:
    return any(fnmatch.fnmatchcase(name, p) for p in patterns)


def check(copper: dict, parts: dict, cfg: dict) -> tuple[list[str], list[dict]]:
    """``copper``: net -> list of (kind, layers, shape). ``parts``: ref -> dict(rot, pads=[(net, rect)]).

    Returns the problems and one result row per rule and net.
    """
    problems, rows = [], []
    for rule in cfg.get("keepaway", []):
        net, dist, scope = rule["net"], float(rule["distance"]), rule.get("scope", "plan")
        kinds = set(rule.get("kinds", ["track", "via", "pad"]))
        own_kinds = set(rule.get("own_kinds", ["track", "via", "pad"]))
        if net not in copper:
            problems.append(f"keepaway: net {net} has no copper")
            continue
        own = [c for c in copper[net] if c[0] in own_kinds]
        seen = False
        for other in sorted(copper):
            if other == net or not match(other, rule["from"]) or match(other, rule.get("except", [])):
                continue
            seen = True
            best = None
            for kind, layers, shape in copper[other]:
                if kind not in kinds:
                    continue
                for _, own_layers, own_shape in own:
                    if scope == "layer" and not (layers & own_layers):
                        continue
                    g = gap(own_shape, shape)
                    if best is None or g < best[0]:
                        best = (g, shape)
            if best is None:
                continue
            at = best[1][1] if best[1][0] != "rect" else (best[1][1][0], best[1][1][1])
            row = {"rule": "keepaway", "net": net, "other": other, "scope": scope, "need": dist,
                   "found": round(best[0], 3), "at": [round(at[0], 2), round(at[1], 2)]}
            rows.append(row)
            if best[0] < dist - 1e-6:
                problems.append(f"keepaway: {other} comes within {best[0]:.3f} mm of {net} "
                                f"({'in plan view' if scope == 'plan' else 'on one layer'}; {dist:g} mm needed) "
                                f"near ({at[0]:.2f}, {at[1]:.2f})")
        if not seen and not any("*" in p for p in rule["from"]):
            problems.append(f"keepaway: none of {rule['from']} is a net of this board")
    for rule in cfg.get("apart", []):
        dist = float(rule["distance"])
        miss = [r for r in rule["a"] + rule["b"] if r not in parts]
        if miss:
            problems.append(f"apart: unknown parts {miss}")
            continue
        g = min(gap(("rect", pa), ("rect", pb)) for ra in rule["a"] for _, pa in parts[ra]["pads"]
                for rb in rule["b"] for _, pb in parts[rb]["pads"])
        rows.append({"rule": "apart", "a": rule["a"], "b": rule["b"], "need": dist, "found": round(g, 2)})
        if g < dist:
            problems.append(f"apart: {rule['a']} and {rule['b']} are {g:.2f} mm apart; {dist:g} mm needed")
    for rule in cfg.get("perpendicular", []):
        a, b = rule["parts"]
        if a not in parts or b not in parts:
            problems.append(f"perpendicular: unknown part in {rule['parts']}")
            continue
        turn = abs(parts[a]["rot"] - parts[b]["rot"]) % 180
        rows.append({"rule": "perpendicular", "parts": [a, b], "found": turn})
        if abs(turn - 90) > 0.01:
            problems.append(f"perpendicular: {a} and {b} are at {turn:g} degrees to each other, not 90")
    for rule in cfg.get("no_pad", []):
        hits = sorted(f"{ref} on {net}" for ref, p in parts.items() if fnmatch.fnmatchcase(ref, rule["refs"])
                      for net, _ in p["pads"] if net in rule["nets"])
        rows.append({"rule": "no_pad", "nets": rule["nets"], "refs": rule["refs"], "found": hits})
        missing = [n for n in rule["nets"] if not any(net == n for p in parts.values() for net, _ in p["pads"])]
        if missing:
            problems.append(f"no_pad: {missing} are not nets of this board")
        for h in hits:
            problems.append(f"no_pad: {h}")
    return problems, rows


def read_board(pcb_path: Path, origin, height, y_up: bool):
    """Copper and parts of a board file, in board coordinates."""
    import pcbnew

    board = pcbnew.LoadBoard(str(pcb_path))
    ox, oy = origin

    def xy(v):
        x, y = pcbnew.ToMM(v.x) - ox, pcbnew.ToMM(v.y) - oy
        return (x, height - y) if y_up else (x, y)

    copper_ids = [i for i in board.GetEnabledLayers().CuStack()]
    every = frozenset(copper_ids)
    copper: dict[str, list] = {}
    for t in board.GetTracks():
        net = t.GetNetname()
        if t.Type() == pcbnew.PCB_VIA_T:
            item = ("via", every, ("circle", xy(t.GetPosition()), pcbnew.ToMM(t.GetWidth(pcbnew.F_Cu)) / 2))
        else:
            item = ("track", frozenset([t.GetLayer()]),
                    ("seg", xy(t.GetStart()), xy(t.GetEnd()), pcbnew.ToMM(t.GetWidth())))
        copper.setdefault(net, []).append(item)
    parts: dict[str, dict] = {}
    for fp in board.GetFootprints():
        pads = []
        for p in fp.Pads():
            b = p.GetBoundingBox()
            x0, y0 = xy(pcbnew.VECTOR2I(b.GetLeft(), b.GetTop()))
            x1, y1 = xy(pcbnew.VECTOR2I(b.GetRight(), b.GetBottom()))
            rect = (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1))
            layers = frozenset(i for i in copper_ids if p.IsOnLayer(i))
            if not layers:
                continue
            net = p.GetNetname()
            pads.append((net, rect))
            copper.setdefault(net, []).append(("pad", layers, ("rect", rect)))
        parts[fp.GetReference()] = {"rot": fp.GetOrientationDegrees(), "pads": pads}
    return copper, parts


def report(rows: list[dict]) -> str:
    out = ["# Layout rules checked on the board file", "",
           "Written by `hwflow.layoutrules`. Distances in millimetres, from the board file. "
           "Nothing was measured on a board.", ""]
    ka = [r for r in rows if r["rule"] == "keepaway"]
    if ka:
        out += ["| Net | Kept away from | Measured | Needed | Found | Nearest point |", "|---|---|---|---|---|---|"]
        for r in ka:
            scope = "plan view, all layers" if r["scope"] == "plan" else "same layer"
            out.append(f"| `{r['net']}` | `{r['other']}` | {scope} | "
                       f"{r['need']:g} | {r['found']:.2f} | ({r['at'][0]:g}, {r['at'][1]:g}) |")
        out.append("")
    for r in rows:
        if r["rule"] == "apart":
            out.append(f"- {', '.join(r['a'])} and {', '.join(r['b'])}: {r['found']:g} mm apart, "
                       f"{r['need']:g} mm needed.")
        elif r["rule"] == "perpendicular":
            out.append(f"- {r['parts'][0]} and {r['parts'][1]}: {r['found']:g} degrees to each other.")
        elif r["rule"] == "no_pad":
            out.append(f"- No `{r['refs']}` pad on {', '.join('`' + n + '`' for n in r['nets'])}: "
                       f"{'none found' if not r['found'] else ', '.join(r['found'])}.")
    return "\n".join(out) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hwflow.layoutrules")
    ap.add_argument("project_dir")
    ap.add_argument("design_dir")
    ap.add_argument("--report", help="write the results as a Markdown file")
    args = ap.parse_args(argv)
    project = Path(args.project_dir)
    cfg = tomllib.loads((Path(args.design_dir) / "board.toml").read_text(encoding="utf-8"))
    name = json.loads((project / "build" / "model.json").read_text(encoding="utf-8"))["name"]
    b = cfg["board"]
    copper, parts = read_board(project / f"{name}.kicad_pcb", b["origin"], b["size"][1], bool(b.get("y_up", False)))
    problems, rows = check(copper, parts, cfg)
    if args.report:
        Path(args.report).parent.mkdir(parents=True, exist_ok=True)
        Path(args.report).write_text(report(rows), encoding="utf-8")
    if problems:
        sys.stderr.write("layout rules: FAILED\n  " + "\n  ".join(problems) + "\n")
        return 1
    n = {k: sum(1 for r in rows if r["rule"] == k) for k in ("keepaway", "apart", "perpendicular", "no_pad")}
    print(f"layout rules: {n['keepaway']} keep-away distances, {n['apart']} spacings, {n['perpendicular']} angles and "
          f"{n['no_pad']} pad bans hold")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
