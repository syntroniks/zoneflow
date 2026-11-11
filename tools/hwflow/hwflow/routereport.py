# SPDX-License-Identifier: Apache-2.0
"""A table of what is routed how, for the person who reviews an autorouted board.

Reads the finished board and writes ``reports/routing.md``:

- how much copper was laid by hand (locked tracks), how much by the patch
  router (the ``[[patch]]`` tracks of board.toml) and how much by the
  autorouter, per layer;
- for every net with autorouted or patched copper: its class, length, vias,
  layers, and its narrowest track against the width of its class;
- every connection the patch router closed;
- the things a router does badly, each as a list to look at: a track
  narrower than its class asks, a via inside a surface-mount pad, a net much
  longer than the distance between its pins, tracks on a layer that is meant
  to be a plane.

It reports; it does not judge. DRC and ``hwflow.layoutrules`` are the checks.

Runs under KiCad's Python: ``python -m hwflow.routereport <project> <design> <out.md>``.
"""

from __future__ import annotations

import argparse
import json
import tomllib
from collections import defaultdict
from pathlib import Path

WANDER = 2.5        # a net this many times longer than its pins are apart is listed
WANDER_MIN = 15.0   # mm: shorter nets are not worth a look


def patch_keys(cfg: dict) -> tuple[set, set]:
    """The segments and vias of the ``[[patch]]`` tracks, as keys in page coordinates, to know them on the board."""
    b = cfg.get("board", {})
    ox, oy = b.get("origin", [0.0, 0.0])
    height, y_up = b.get("size", [0.0, 0.0])[1], bool(b.get("y_up", False))

    def key(p):
        x, y = p
        return round(ox + x, 2), round(oy + (height - y if y_up else y), 2)

    segs, vias = set(), set()
    for t in cfg.get("patch", []):
        pts = [key(p["via"]) if isinstance(p, dict) else key(p) for p in t["points"]]
        for a, c in zip(pts, pts[1:]):
            segs.add((t["layer"], frozenset([a, c])))
        vias |= {key(p["via"]) for p in t["points"] if isinstance(p, dict)}
    return segs, vias


def collect(board, plane_layers: list[str], patches: tuple[set, set] = (frozenset(), frozenset())) -> dict:
    import pcbnew

    nets: dict[str, dict] = defaultdict(lambda: {"auto_len": 0.0, "hand_len": 0.0, "auto_vias": 0, "hand_vias": 0,
                                                 "patch_len": 0.0, "patch_vias": 0, "patch_min_w": None,
                                                 "layers": set(), "min_w": None, "plane_len": 0.0,
                                                 "narrow_len": 0.0})
    per_layer: dict[str, dict] = defaultdict(lambda: {"hand": 0.0, "patch": 0.0, "auto": 0.0})
    patch_segs, patch_vias = patches
    class_width: dict[str, float] = {}

    def at(v):
        return round(pcbnew.ToMM(v.x), 2), round(pcbnew.ToMM(v.y), 2)

    def asked(net):
        if net not in class_width:
            class_width[net] = pcbnew.ToMM(settings.GetEffectiveNetClass(net).GetTrackWidth())
        return class_width[net]

    settings = board.GetDesignSettings().m_NetSettings
    smd = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_SMD:
                smd.append((p.GetBoundingBox(), fp.GetReference(), p.GetNumber(), p.GetNetname()))
    vias_in_pads = []
    for t in board.GetTracks():
        n = nets[t.GetNetname()]
        hand = t.IsLocked()
        if t.Type() == pcbnew.PCB_VIA_T:
            if at(t.GetPosition()) in patch_vias:
                n["patch_vias"] += 1
                continue
            n["hand_vias" if hand else "auto_vias"] += 1
            if not hand:
                pos = t.GetPosition()
                for box, ref, num, net in smd:
                    if box.Contains(pos):
                        vias_in_pads.append((t.GetNetname(), ref, num, net))
                        break
            continue
        length = pcbnew.ToMM(t.GetLength())
        layer = t.GetLayerName()
        w = pcbnew.ToMM(t.GetWidth())
        is_patch = (str(layer), frozenset([at(t.GetStart()), at(t.GetEnd())])) in patch_segs
        if (is_patch or not hand) and w < asked(t.GetNetname()) - 1e-6:
            n["narrow_len"] += length
        if is_patch:
            per_layer[layer]["patch"] += length
            n["patch_len"] += length
            n["layers"].add(layer)
            n["patch_min_w"] = w if n["patch_min_w"] is None else min(n["patch_min_w"], w)
            if layer in plane_layers:
                n["plane_len"] += length
            continue
        per_layer[layer]["hand" if hand else "auto"] += length
        if hand:
            n["hand_len"] += length
            continue
        n["auto_len"] += length
        n["layers"].add(layer)
        n["min_w"] = w if n["min_w"] is None else min(n["min_w"], w)
        if layer in plane_layers:
            n["plane_len"] += length
    span: dict[str, float] = {}
    pins: dict[str, list] = defaultdict(list)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetNetname():
                pins[p.GetNetname()].append((pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)))
    for net, pts in pins.items():
        xs, ys = [q[0] for q in pts], [q[1] for q in pts]
        span[net] = (max(xs) - min(xs)) + (max(ys) - min(ys))      # half the perimeter of the box round the pins
    rows = []
    for net, n in sorted(nets.items()):
        if n["auto_len"] == 0 and n["auto_vias"] == 0 and n["patch_len"] == 0:
            continue
        nc = settings.GetEffectiveNetClass(net)
        widths = [x for x in (n["min_w"], n["patch_min_w"]) if x is not None]
        rows.append({"net": net, "class": str(nc.GetName()), "class_width": pcbnew.ToMM(nc.GetTrackWidth()),
                     "length": round(n["auto_len"] + n["hand_len"] + n["patch_len"], 1),
                     "auto_length": round(n["auto_len"], 1), "patch_length": round(n["patch_len"], 1),
                     "vias": n["auto_vias"], "patch_vias": n["patch_vias"], "layers": sorted(n["layers"]),
                     "min_width": min(widths) if widths else None, "patch_min_width": n["patch_min_w"],
                     "narrow_length": round(n["narrow_len"], 1),
                     "span": round(span.get(net, 0.0), 1), "plane_length": round(n["plane_len"], 1)})
    layers = {k: {a: round(b, 1) for a, b in v.items()} for k, v in sorted(per_layer.items())}
    return {"rows": rows, "per_layer": layers,
            "vias_in_pads": vias_in_pads,
            "hand_nets": sum(1 for n in nets.values()
                             if n["hand_len"] > 0 and n["auto_len"] == 0 and n["patch_len"] == 0),
            "hand_vias": sum(n["hand_vias"] for n in nets.values()),
            "patch_vias": sum(n["patch_vias"] for n in nets.values()),
            "auto_vias": sum(n["auto_vias"] for n in nets.values())}


def markdown(data: dict, plane_layers: list[str]) -> str:
    rows = data["rows"]
    out = ["# Routing of the board, as built", "",
           "Written by `hwflow.routereport` from the board file. Lengths in millimetres. \"By hand\" is copper",
           "written in `design/layout.py` or `design/board.toml`; \"patch router\" is the `[[patch]]` tracks that",
           "`hwflow.patchroute` wrote into `design/patches.toml`; the rest is the autorouter's.", "",
           "| Layer | By hand | Patch router | Autorouter |", "|---|---|---|---|"]
    for layer, v in data["per_layer"].items():
        out.append(f"| {layer} | {v['hand']:.0f} | {v['patch']:.0f} | {v['auto']:.0f} |")
    patched = [r for r in rows if r["patch_length"] > 0]
    out += ["", f"{data['hand_nets']} nets are routed by hand alone, {len(rows)} have autorouted or patched copper. "
            f"{data['hand_vias']} vias were placed by hand or by the plane fan-out, "
            f"{data['auto_vias']} by the autorouter and {data['patch_vias']} by the patch router.", "",
            "## Copper laid by the patch router", "",
            "A maze router closed these after the autorouter. It keeps the clearances and knows nothing else:",
            "look at each one.", ""]
    out += [f"- `{r['net']}` ({r['class']}): {r['patch_length']:g} mm, {r['patch_vias']} vias, narrowest "
            f"{r['patch_min_width']:g} mm (class width {r['class_width']:g} mm)" for r in patched] or ["None."]
    out.append("")

    narrow = [r for r in rows if r["min_width"] is not None and r["min_width"] < r["class_width"] - 1e-6]
    out += ["## Tracks narrower than their net class asks", ""]
    out += [f"- `{r['net']}` ({r['class']}): {r['narrow_length']:g} mm of track, narrowest {r['min_width']:g} mm, "
            f"class width {r['class_width']:g} mm" for r in narrow] or ["None."]
    if narrow:
        out += ["", f"Total: {sum(r['narrow_length'] for r in narrow):.1f} mm in {len(narrow)} nets."]
    out += ["", "## Autorouted vias inside a surface-mount pad", ""]
    out += [f"- `{net}`: a via in pad {num} of {ref}" for net, ref, num, _ in data["vias_in_pads"]] or ["None."]
    wander = [r for r in rows if r["span"] > 0 and r["length"] > WANDER_MIN and r["length"] > WANDER * r["span"]]
    out += ["", f"## Nets more than {WANDER:g} times as long as their pins are apart", "",
            "The distance is half the perimeter of the box round the pins of the net.", ""]
    out += [f"- `{r['net']}`: {r['length']:g} mm of track for {r['span']:g} mm" for r in wander] or ["None."]
    if plane_layers:
        on_plane = [r for r in rows if r["plane_length"] > 0]
        out += ["", f"## Autorouted and patched tracks on {', '.join(plane_layers)}", "",
                "Each one is a slot in the pour of that layer.", ""]
        out += [f"- `{r['net']}`: {r['plane_length']:g} mm" for r in on_plane] or ["None."]
        out += ["", f"Total: {sum(r['plane_length'] for r in on_plane):.0f} mm in {len(on_plane)} nets."]
    out += ["", "## Every net with autorouted copper", "",
            "| Net | Class | Length | Pins apart | Vias | Layers | Narrowest |", "|---|---|---|---|---|---|---|"]
    for r in rows:
        w = "" if r["min_width"] is None else f"{r['min_width']:g}"
        out.append(f"| `{r['net']}` | {r['class']} | {r['length']:g} | {r['span']:g} | {r['vias']} | "
                   f"{', '.join(r['layers'])} | {w} |")
    return "\n".join(out) + "\n"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hwflow.routereport")
    ap.add_argument("project_dir")
    ap.add_argument("design_dir")
    ap.add_argument("out")
    args = ap.parse_args(argv)
    import pcbnew

    project = Path(args.project_dir)
    cfg = tomllib.loads((Path(args.design_dir) / "board.toml").read_text(encoding="utf-8"))
    name = json.loads((project / "build" / "model.json").read_text(encoding="utf-8"))["name"]
    plane_layers = [str(x) for x in cfg.get("route", {}).get("plane_layers", [])]
    board = pcbnew.LoadBoard(str(project / f"{name}.kicad_pcb"))
    data = collect(board, plane_layers, patch_keys(cfg))
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(markdown(data, plane_layers), encoding="utf-8")
    (project / "build" / "routing.json").write_text(json.dumps(data, indent=1, default=list) + "\n", encoding="utf-8")
    print(f"routing report: {len(data['rows'])} nets with autorouted copper, {data['auto_vias']} autorouted vias "
          f"-> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
