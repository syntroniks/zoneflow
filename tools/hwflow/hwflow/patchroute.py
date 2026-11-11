# SPDX-License-Identifier: Apache-2.0
"""Close the connections the autorouter left open, one at a time, with a plain maze router.

Freerouting ends with a handful of connections open on a dense board, and it
does not give the same result twice, so routing again is no way to close them.
This module takes the routed board as it is, reads from a DRC report which
pairs of items are still unconnected, and for each pair searches a path on a
grid: Lee's algorithm with A*, eight directions, a via to change layer. Every
other net's copper is an obstacle grown by its clearance and half the new
track's width, so a path that is found keeps the rules by construction; DRC
checks it again afterwards.

The result is written as ``[[patch]]`` tracks, in ``design/patches.toml``,
with the hash of the routing session they were made for. ``pcbbuild finish``
lays them after it has imported that session, and refuses them for any other.

A patch of one net can cut the pour of another in two. So the work is done in
passes: the first on the board without patches; then, with ``--add``, on the
board with the patches laid, for what they opened. ``--check`` only says
whether the report still has something open.

It is a small tool for the last few connections. It does not rip up, it does
not optimise, and it knows nothing of signal quality: what it routes is listed
in the routing report for a person to look at.

Runs under KiCad's Python:
``python -m hwflow.patchroute <project> <design> --drc <drc.json> [--add | --check]``.
"""

from __future__ import annotations

import argparse
import fnmatch
import heapq
import json
import math
import sys
import tomllib
from pathlib import Path

GRID = 0.05          # mm
MARGIN = 0.03        # mm added to every clearance: the grid's own error
WINDOW = 9.0         # mm round the two ends of a connection
VIA = (0.6, 0.3)     # diameter, drill
VIA_COST = 60        # cells: a via is worth 3 mm of track
HOLE_GAP = 0.3       # mm between the walls of two holes
SQRT2 = math.sqrt(2.0)


class Grid:
    """Blocked cells of one window of the board, per routing layer, and where a via may go."""

    def __init__(self, x0, y0, x1, y1, layers):
        self.x0, self.y0 = x0, y0
        self.w = int(round((x1 - x0) / GRID)) + 1
        self.h = int(round((y1 - y0) / GRID)) + 1
        self.layers = layers
        self.blocked = {name: bytearray(self.w * self.h) for name in layers}
        self.no_via = bytearray(self.w * self.h)
        self.own_vias: set = set()       # cells where a via of the net already is: change layer there for free

    def cell(self, x, y):
        return int(round((x - self.x0) / GRID)), int(round((y - self.y0) / GRID))

    def point(self, ix, iy):
        return round(self.x0 + ix * GRID, 3), round(self.y0 + iy * GRID, 3)

    def _disc(self, buf, cx, cy, r, value=1):
        n = int(math.ceil(r / GRID))
        ix0, iy0 = self.cell(cx, cy)
        r2 = (r / GRID) ** 2
        fx, fy = (cx - self.x0) / GRID, (cy - self.y0) / GRID
        for iy in range(max(0, iy0 - n), min(self.h, iy0 + n + 1)):
            row = iy * self.w
            dy2 = (iy - fy) ** 2
            for ix in range(max(0, ix0 - n), min(self.w, ix0 + n + 1)):
                if (ix - fx) ** 2 + dy2 <= r2:
                    buf[row + ix] = value

    def _rect(self, buf, rect, r, value=1):
        ix0, iy0 = self.cell(rect[0] - r, rect[1] - r)
        ix1, iy1 = self.cell(rect[2] + r, rect[3] + r)
        for iy in range(max(0, iy0), min(self.h, iy1 + 1)):
            row = iy * self.w
            for ix in range(max(0, ix0), min(self.w, ix1 + 1)):
                buf[row + ix] = value

    def stamp(self, buf, shape, r, value=1):
        """Mark every cell within r of a shape: ("seg", a, b, width), ("circle", c, radius) or ("rect", box)."""
        if shape[0] == "rect":
            self._rect(buf, shape[1], r, value)
        elif shape[0] == "circle":
            self._disc(buf, shape[1][0], shape[1][1], r + shape[2], value)
        else:
            (ax, ay), (bx, by), width = shape[1], shape[2], shape[3]
            length = math.hypot(bx - ax, by - ay)
            steps = max(1, int(length / GRID))
            for i in range(steps + 1):
                t = i / steps
                self._disc(buf, ax + (bx - ax) * t, ay + (by - ay) * t, r + width / 2, value)


def search(grid: Grid, starts, goals):
    """A* from any start node to any goal node. A node is (layer index, ix, iy). Returns the nodes of the path."""
    w, h, names = grid.w, grid.h, grid.layers
    blocked = [grid.blocked[n] for n in names]
    goal_set = set(goals)
    gx = sum(g[1] for g in goals) / len(goals)
    gy = sum(g[2] for g in goals) / len(goals)

    def est(node):
        dx, dy = abs(node[1] - gx), abs(node[2] - gy)
        return max(dx, dy) + (SQRT2 - 1) * min(dx, dy)

    best = {s: 0.0 for s in starts}
    came: dict = {}
    heap = [(est(s), 0.0, s) for s in starts]
    heapq.heapify(heap)
    moves = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
             (1, 1, SQRT2), (1, -1, SQRT2), (-1, 1, SQRT2), (-1, -1, SQRT2)]
    while heap:
        _, cost, node = heapq.heappop(heap)
        if node in goal_set:
            path = [node]
            while node in came:
                node = came[node]
                path.append(node)
            return path[::-1]
        if cost > best.get(node, 1e18):
            continue
        layer, ix, iy = node
        b = blocked[layer]
        for dx, dy, step in moves:
            nx, ny = ix + dx, iy + dy
            if not (0 <= nx < w and 0 <= ny < h) or b[ny * w + nx]:
                continue
            if dx and dy and (b[iy * w + nx] or b[ny * w + ix]):
                continue                      # no cutting a corner between two blocked cells
            nxt = (layer, nx, ny)
            c = cost + step
            if c < best.get(nxt, 1e18):
                best[nxt] = c
                came[nxt] = node
                heapq.heappush(heap, (c + est(nxt), c, nxt))
        have = (ix, iy) in grid.own_vias
        if have or not grid.no_via[iy * w + ix]:
            for other in range(len(names)):
                if other != layer and (have or not blocked[other][iy * w + ix]):
                    nxt = (other, ix, iy)
                    c = cost + (1 if have else VIA_COST)
                    if c < best.get(nxt, 1e18):
                        best[nxt] = c
                        came[nxt] = node
                        heapq.heappush(heap, (c + est(nxt), c, nxt))
    return None


def reach_of(grid: Grid, starts) -> tuple[int, tuple]:
    """How far a flood from the start nodes gets: the number of cells and their box. For a failed search."""
    w, h = grid.w, grid.h
    blocked = [grid.blocked[n] for n in grid.layers]
    seen = set(starts)
    todo = list(starts)
    while todo:
        layer, ix, iy = todo.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = ix + dx, iy + dy
            if 0 <= nx < w and 0 <= ny < h and not blocked[layer][ny * w + nx] and (layer, nx, ny) not in seen:
                seen.add((layer, nx, ny))
                todo.append((layer, nx, ny))
        if (ix, iy) in grid.own_vias or not grid.no_via[iy * w + ix]:
            for other in range(len(blocked)):
                if other != layer and not blocked[other][iy * w + ix] and (other, ix, iy) not in seen:
                    seen.add((other, ix, iy))
                    todo.append((other, ix, iy))
        if len(seen) > 200000:
            break
    xs, ys = [n[1] for n in seen], [n[2] for n in seen]
    a, b = grid.point(min(xs), min(ys)), grid.point(max(xs), max(ys))
    return len(seen), (a[0], a[1], b[0], b[1], sorted({grid.layers[n[0]] for n in seen}))


def _free(buf, w: int, a, c) -> bool:
    """Whether the straight run of cells from a to c, along an axis or a diagonal, is free."""
    dx, dy = (c[0] > a[0]) - (c[0] < a[0]), (c[1] > a[1]) - (c[1] < a[1])
    x, y = a
    for _ in range(max(abs(c[0] - a[0]), abs(c[1] - a[1]))):
        nx, ny = x + dx, y + dy
        if buf[ny * w + nx] or (dx and dy and (buf[y * w + nx] or buf[ny * w + x])):
            return False
        x, y = nx, ny
    return True


def _bends(a, c) -> list[list]:
    """The ways from a to c with at most one bend: no middle point, or the diagonal first, or the straight first."""
    dx, dy = c[0] - a[0], c[1] - a[1]
    d = min(abs(dx), abs(dy))
    if d == 0 or abs(dx) == abs(dy):
        return [[]]
    sx, sy = (dx > 0) - (dx < 0), (dy > 0) - (dy < 0)
    return [[(a[0] + sx * d, a[1] + sy * d)], [(c[0] - sx * d, c[1] - sy * d)]]


def straighten(buf, w: int, cells: list) -> list:
    """The corner points of a path of cells, with the steps of the search pulled straight.

    A* returns one of many paths of equal length, and it is a staircase. From
    each corner this takes the farthest cell of the path that one bend
    reaches over free cells, so the track runs straight and turns by 45
    degrees. It only ever uses cells the search could have used, so it keeps
    the same clearances.
    """
    def joins(i, j):
        for mid in _bends(cells[i], cells[j]):
            pts = [cells[i], *mid, cells[j]]
            if all(_free(buf, w, p, q) for p, q in zip(pts, pts[1:])):
                return mid
        return None

    out, i, last = [cells[0]], 0, len(cells) - 1
    while i < last:
        j, mid = i + 1, []                        # the next cell always joins
        step, lo, hi = 2, i + 1, i + 1
        while j < last:                           # look twice as far each time, until it does not join
            k = min(i + step, last)
            got = joins(i, k)
            if got is None:
                hi = k
                break
            lo, j, mid = k, k, got
            step *= 2
        while hi - lo > 1:                        # then halve the stretch between what joined and what did not
            k = (lo + hi) // 2
            got = joins(i, k)
            if got is None:
                hi = k
            else:
                lo, j, mid = k, k, got
        out += [*mid, cells[j]]
        i = j
    corners = [out[0]]
    for a, b, c in zip(out, out[1:], out[2:]):
        if b == a:
            continue
        cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
        if cross != 0 or (b[0] - a[0]) * (c[0] - b[0]) + (b[1] - a[1]) * (c[1] - b[1]) < 0:
            corners.append(b)
    if len(out) > 1:
        corners.append(out[-1])
    return corners


def to_tracks(grid: Grid, path, net: str, width: float) -> list[dict]:
    """The path as ``[[patch]]`` entries: one per run on a layer, a via where the layer changes."""
    runs, cur = [], [path[0]]
    for node in path[1:]:
        if node[0] != cur[-1][0]:
            runs.append(cur)
            cur = [node]
        else:
            cur.append(node)
    runs.append(cur)
    out = []
    for i, run in enumerate(runs):
        pts = straighten(grid.blocked[grid.layers[run[0][0]]], grid.w, [(n[1], n[2]) for n in run])
        points = [list(grid.point(p[0], p[1])) for p in pts]
        entry = {"layer": grid.layers[run[0][0]], "net": net, "width": width, "stay": True, "points": points}
        if i < len(runs) - 1 and (run[-1][1], run[-1][2]) not in grid.own_vias:
            if len(points) == 1:
                points.append(list(points[0]))
            x, y = points[-1]
            entry["points"] = points[:-1] + [{"via": [x, y]}]
            entry["via"] = list(VIA)
        if len(entry["points"]) >= 2:
            out.append(entry)
    return out


def read_board(pcb_path: Path, origin, height, y_up: bool):
    """Everything that is copper or forbids copper, in board coordinates."""
    import pcbnew

    board = pcbnew.LoadBoard(str(pcb_path))
    ox, oy = origin

    def xy(v):
        x, y = pcbnew.ToMM(v.x) - ox, pcbnew.ToMM(v.y) - oy
        return (x, height - y) if y_up else (x, y)

    settings = board.GetDesignSettings().m_NetSettings
    copper_ids = list(board.GetEnabledLayers().CuStack())
    names = {i: str(board.GetLayerName(i)) for i in copper_ids}
    every = frozenset(names.values())
    items = []                                # (net, layers, shape, uuid, radius of its hole or 0)
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            radius = pcbnew.ToMM(t.GetWidth(pcbnew.F_Cu)) / 2
            items.append((t.GetNetname(), every, ("circle", xy(t.GetPosition()), radius), t.m_Uuid.AsString(),
                          pcbnew.ToMM(t.GetDrill()) / 2))
        else:
            items.append((t.GetNetname(), frozenset([names[t.GetLayer()]]),
                          ("seg", xy(t.GetStart()), xy(t.GetEnd()), pcbnew.ToMM(t.GetWidth())), t.m_Uuid.AsString(),
                          0.0))
    for fp in board.GetFootprints():
        for p in fp.Pads():
            b = p.GetBoundingBox()
            x0, y0 = xy(pcbnew.VECTOR2I(b.GetLeft(), b.GetTop()))
            x1, y1 = xy(pcbnew.VECTOR2I(b.GetRight(), b.GetBottom()))
            layers = frozenset(n for i, n in names.items() if p.IsOnLayer(i))
            if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH or not layers:
                layers = every                 # a hole: nothing passes on any layer
            hole = max(pcbnew.ToMM(p.GetDrillSize().x), pcbnew.ToMM(p.GetDrillSize().y)) / 2
            items.append((p.GetNetname(), layers, ("rect", (min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1))),
                          p.m_Uuid.AsString(), hole))
    areas = []                                # (layers, outline, tracks forbidden, vias forbidden, net)
    for z in board.Zones():
        o = z.Outline()
        if o.OutlineCount() == 0:
            continue
        line = o.Outline(0)
        pts = [xy(line.CPoint(i)) for i in range(line.PointCount())]
        layers = frozenset(n for i, n in names.items() if z.IsOnLayer(i))
        if z.GetIsRuleArea():
            areas.append((layers, pts, z.GetDoNotAllowTracks(), z.GetDoNotAllowVias(), ""))
        else:
            areas.append((layers, pts, True, True, z.GetNetname()))

    def clearance(net):
        return pcbnew.ToMM(settings.GetEffectiveNetClass(net).GetClearance())

    def width(net):
        return pcbnew.ToMM(settings.GetEffectiveNetClass(net).GetTrackWidth())

    return items, areas, clearance, width


def inside(poly, x, y) -> bool:
    n, hit = len(poly), False
    for i in range(n):
        (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            hit = not hit
    return hit


def build_grid(items, areas, clearance, net, width, window, layers, extra, pours, size):
    """The obstacle grid for one net in one window. ``extra`` adds clearance to nets by pattern."""
    x0, y0, x1, y1 = window
    grid = Grid(max(0.5, x0), max(0.5, y0), min(size[0] - 0.5, x1), min(size[1] - 0.5, y1), layers)
    own = clearance(net)
    via_r = VIA[0] / 2

    def reach(other):
        c = max(own, clearance(other))
        for one, others, excepted, more in extra:
            for a, b in ((net, other), (other, net)):
                if a == one and any(fnmatch.fnmatchcase(b, p) for p in others) \
                        and not any(fnmatch.fnmatchcase(b, p) for p in excepted):
                    c = max(c, more)
        return c + MARGIN

    def near(shape, r):
        if shape[0] == "rect":
            bx0, by0, bx1, by1 = shape[1]
        elif shape[0] == "circle":
            bx0, by0, bx1, by1 = shape[1][0], shape[1][1], shape[1][0], shape[1][1]
            r += shape[2]
        else:
            bx0, bx1 = sorted((shape[1][0], shape[2][0]))
            by0, by1 = sorted((shape[1][1], shape[2][1]))
            r += shape[3] / 2
        return bx1 + r >= grid.x0 and bx0 - r <= x1 and by1 + r >= grid.y0 and by0 - r <= y1

    mine = []
    for other, item_layers, shape, _, hole in items:
        if hole > 0:
            # hole to hole, whatever the nets: a new via keeps its distance from every hole
            centre = shape[1] if shape[0] == "circle" else ((shape[1][0] + shape[1][2]) / 2,
                                                            (shape[1][1] + shape[1][3]) / 2)
            if near(("circle", centre, 0.0), hole + 1.0):
                grid._disc(grid.no_via, centre[0], centre[1], hole + VIA[1] / 2 + HOLE_GAP + MARGIN)
        if other == net:
            mine.append((item_layers, shape))
            if shape[0] == "circle" and hole > 0 and near(shape, 0):
                cell = grid.cell(*shape[1])
                if 0 <= cell[0] < grid.w and 0 <= cell[1] < grid.h:
                    grid.own_vias.add(cell)
            continue
        c = reach(other)
        if not near(shape, c + via_r):
            continue
        for name in layers:
            if name in item_layers:
                grid.stamp(grid.blocked[name], shape, c + width / 2)
        grid.stamp(grid.no_via, shape, c + via_r)
    for area_layers, poly, no_track, no_via, area_net in areas:
        if area_net == net or area_net in pours:
            continue
        xs, ys = [p[0] for p in poly], [p[1] for p in poly]
        if max(xs) < grid.x0 or min(xs) > x1 or max(ys) < grid.y0 or min(ys) > y1:
            continue
        grow = (reach(area_net) + width / 2) if area_net else 0.0
        ax0, ay0 = grid.cell(min(xs) - grow, min(ys) - grow)
        ax1, ay1 = grid.cell(max(xs) + grow, max(ys) + grow)
        for iy in range(max(0, ay0), min(grid.h, ay1 + 1)):
            y = grid.y0 + iy * GRID
            for ix in range(max(0, ax0), min(grid.w, ax1 + 1)):
                x = grid.x0 + ix * GRID
                if inside(poly, x, y) or (grow and any(inside(poly, x + dx, y + dy) for dx, dy in
                                                       ((grow, 0), (-grow, 0), (0, grow), (0, -grow)))):
                    k = iy * grid.w + ix
                    if no_track:
                        for name in layers:
                            if name in area_layers:
                                grid.blocked[name][k] = 1
                    if no_via or area_net:
                        grid.no_via[k] = 1
    # No via in a surface-mount pad of the net itself: it goes beside the pad.
    for item_layers, shape in mine:
        if shape[0] == "rect" and len(item_layers) == 1 and near(shape, via_r):
            grid.stamp(grid.no_via, shape, via_r * 0.8)
    # Inside its own copper the new track may run even where a neighbour's clearance reaches in.
    for item_layers, shape in mine:
        half = (min(shape[1][2] - shape[1][0], shape[1][3] - shape[1][1]) / 2 if shape[0] == "rect"
                else shape[2] if shape[0] == "circle" else shape[3] / 2)
        r = half - width / 2
        if r < 0 or not near(shape, 0):
            continue
        core = (("rect", (shape[1][0] + width / 2, shape[1][1] + width / 2, shape[1][2] - width / 2,
                          shape[1][3] - width / 2)) if shape[0] == "rect" else
                ("circle", shape[1], 0.0) if shape[0] == "circle" else ("seg", shape[1], shape[2], 0.0))
        for name in layers:
            if name in item_layers:
                grid.stamp(grid.blocked[name], core, r if shape[0] != "rect" else 0.0, 0)
    return grid


def nodes_of(grid: Grid, layers_of_item, pos):
    ix, iy = grid.cell(*pos)
    if not (0 <= ix < grid.w and 0 <= iy < grid.h):
        return []
    return [(i, ix, iy) for i, name in enumerate(grid.layers) if name in layers_of_item]


def toml(entries: list[dict], session: str, notes: list[str]) -> str:
    out = ["# Tracks that close what the autorouter left open. WRITTEN BY hwflow.patchroute: do not edit.",
           "# They belong to one stored routing, named by its hash. pcbbuild finish lays them after it",
           "# has imported that routing, and refuses them for any other: run `make patches` again then.",
           "", "[patches]", f'session = "{session}"', ""]
    out += [f"# {n}" for n in notes] + [""]
    for e in entries:
        pts = ", ".join("{via = [%g, %g]}" % tuple(p["via"]) if isinstance(p, dict) else "[%g, %g]" % tuple(p)
                        for p in e["points"])
        out += ["[[patch]]", f'layer = "{e["layer"]}"', f'net = "{e["net"]}"', f"width = {e['width']:g}"]
        if "via" in e:
            out.append(f"via = [{e['via'][0]:g}, {e['via'][1]:g}]")
        out += ["stay = true", f"points = [{pts}]", ""]
    return "\n".join(out)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hwflow.patchroute")
    ap.add_argument("project_dir")
    ap.add_argument("design_dir")
    ap.add_argument("--drc", required=True, help="DRC report (JSON) of the routed board, with its zones filled")
    ap.add_argument("--out", help="default: <design>/patches.toml")
    ap.add_argument("--first", help="nets to route first, in this order, comma-separated")
    ap.add_argument("--add", action="store_true",
                    help="the board has the patches of the file laid: keep them and add what is still open")
    ap.add_argument("--check", action="store_true", help="route nothing: exit 0 if the report has nothing open, else 1")
    args = ap.parse_args(argv)
    project, design = Path(args.project_dir), Path(args.design_dir)
    report = json.loads(Path(args.drc).read_text(encoding="utf-8"))
    if args.check:
        n = len(report.get("unconnected_items", []))
        print(f"patch router: {n} connections open" if n else "patch router: nothing is open")
        return 1 if n else 0
    cfg = tomllib.loads((design / "board.toml").read_text(encoding="utf-8"))
    name = json.loads((project / "build" / "model.json").read_text(encoding="utf-8"))["name"]
    b = cfg["board"]
    route = cfg.get("route", {})
    layers = [layer for layer in route.get("layers", ["F.Cu", "B.Cu"])
              if layer not in {p["layer"] for p in route.get("plane", [])}]
    items, areas, clearance, class_width = read_board(project / f"{name}.kicad_pcb", b["origin"], b["size"][1],
                                                      bool(b.get("y_up", False)))
    by_uuid = {u: (net, item_layers, shape) for net, item_layers, shape, u, _ in items}
    # nets that ask for more room than their class: (net, from whom, except whom, distance)
    extra = [(g["net"], ["*"], [], float(g["distance"])) for g in cfg.get("guard", [])]
    extra += [(k["net"], k["from"], k.get("except", []), float(k["distance"])) for k in cfg.get("keepaway", [])]
    pours = {p["net"] for p in cfg.get("plane", [])}
    ox, oy = b["origin"]

    out = Path(args.out) if args.out else design / "patches.toml"
    entries, notes, failed = [], [], []
    if args.add and out.exists():
        text = out.read_text(encoding="utf-8")
        entries = tomllib.loads(text).get("patch", [])
        notes = [line[2:] for line in text.splitlines() if line.startswith("# ") and " mm wide" in line]
    kept = len(notes)
    first = args.first.split(",") if args.first else []

    def rank(u):
        d = u["items"][0]["description"]
        return next((i for i, pat in enumerate(first) if f"[{pat}]" in d), len(first))

    for u in sorted(report.get("unconnected_items", []), key=rank):
        a, c = u["items"][0], u["items"][1]
        if a["uuid"] not in by_uuid or c["uuid"] not in by_uuid:
            failed.append(f"{a['description']} to {c['description']}: not a track, via or pad")
            continue
        net, la, _ = by_uuid[a["uuid"]]
        _, lc, _ = by_uuid[c["uuid"]]

        def pos(i):
            x, y = i["pos"]["x"] - ox, i["pos"]["y"] - oy
            return (x, b["size"][1] - y) if b.get("y_up", False) else (x, y)

        pa, pc = pos(a), pos(c)
        done, why = None, ""
        # the class width in a small window first, then a wider window, then a narrower track
        tries = [(w_, m_) for m_ in (WINDOW, 2.5 * WINDOW) for w_ in dict.fromkeys((class_width(net), 0.25, 0.2))
                 if w_ <= class_width(net)]
        for width, around in sorted(tries, key=lambda t: (-t[0], t[1])):
            window = (min(pa[0], pc[0]) - around, min(pa[1], pc[1]) - around, max(pa[0], pc[0]) + around,
                      max(pa[1], pc[1]) + around)
            grid = build_grid(items, areas, clearance, net, width, window, layers, extra, pours, b["size"])
            starts, goals = nodes_of(grid, la, pa), nodes_of(grid, lc, pc)
            for n in starts + goals:
                grid.blocked[grid.layers[n[0]]][n[2] * grid.w + n[1]] = 0
            path = search(grid, starts, goals) if starts and goals else None
            if path:
                done = to_tracks(grid, path, net, width)
                break
            if starts and goals:
                why = (f"; at {width:g} mm the first end reaches {reach_of(grid, starts)}, "
                       f"the second {reach_of(grid, goals)}")
        if not done:
            failed.append(f"{net}: no path from ({pa[0]:.2f}, {pa[1]:.2f}) to ({pc[0]:.2f}, {pc[1]:.2f}){why}")
            continue
        length = sum(math.hypot(q[0] - p[0], q[1] - p[1]) for e in done
                     for p, q in zip([x["via"] if isinstance(x, dict) else x for x in e["points"]],
                                     [x["via"] if isinstance(x, dict) else x for x in e["points"]][1:]))
        vias = sum(1 for e in done if "via" in e)
        notes.append(f"{net}: ({pa[0]:.2f}, {pa[1]:.2f}) to ({pc[0]:.2f}, {pc[1]:.2f}), {length:.1f} mm, "
                     f"{vias} vias, {done[0]['width']:g} mm wide")
        entries += done
        # what was just laid is an obstacle for what follows
        for e in done:
            pts = [x["via"] if isinstance(x, dict) else x for x in e["points"]]
            for p, q in zip(pts, pts[1:]):
                items.append((net, frozenset([e["layer"]]), ("seg", tuple(p), tuple(q), e["width"]), "", 0.0))
            if "via" in e:
                items.append((net, frozenset(layers) | {"In1.Cu"}, ("circle", tuple(pts[-1]), VIA[0] / 2), "",
                              VIA[1] / 2))
    stamp = project / "route" / f"{name}.dsn.sha256"
    session = stamp.read_text().strip() if stamp.exists() else ""
    out.write_text(toml(entries, session, notes), encoding="utf-8")
    print(f"patch router: {len(notes) - kept} connections closed" + (f", {kept} kept" if args.add else "")
          + f"; {len(entries)} track runs -> {out}")
    for f in failed:
        sys.stderr.write(f"patch router: FAILED {f}\n")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
