# SPDX-License-Identifier: Apache-2.0
"""Draw parts of a KiCad board as annotated SVG pictures.

Runs under KiCad's Python, which has the ``pcbnew`` module. Every shape in a
picture comes from the board file: pads, tracks, vias, zone fills, drawings
and text are turned into outlines by KiCad's own code, so that a picture
shows what KiCad would plot. What this module adds is the crop, the colours,
the highlighting and the labels.

Coordinates are those of the mechanical contract: origin at the bottom-left
corner of the board seen from the component side, x right, y up, millimetres.
A bottom view is mirrored left to right, as the board looks when turned over.
"""

from __future__ import annotations

import html
import math

import pcbnew

MAX_ERROR = 5000  # nm: how far a polygon may lie inside the arc it stands for
TEXT_ERROR = 20000  # nm: the same for the round ends of text strokes, of which there are very many

COPPER = ("F.Cu", "In1.Cu", "In2.Cu", "B.Cu")

# Close to KiCad's default colours, so that the picture and the KiCad window look alike.
LAYER_COLOUR = {
    "F.Cu": "#d0393b",
    "In1.Cu": "#69b86b",
    "In2.Cu": "#d08a2e",
    "B.Cu": "#4f86d6",
    "F.Silkscreen": "#f0ecc0",
    "B.Silkscreen": "#e9c2b4",
    "F.Fab": "#b9bcc4",
    "B.Fab": "#8f97c8",
    "F.Courtyard": "#ff4fe0",
    "B.Courtyard": "#35d4d4",
    "F.Paste": "#c9a8a0",
    "F.Mask": "#a04fa0",
    "Edge.Cuts": "#e6d96a",
}
MARK_COLOUR = {"a": "#ffe100", "b": "#3df2ff", "c": "#ff7bf3", "d": "#9dff5c"}
BACKGROUND = "#06080c"
BOARD_FILL = "#121a24"
HOLE_FILL = "#06080c"
FONT = "DejaVu Sans, Verdana, Segoe UI, Helvetica, Arial, sans-serif"


class RenderError(Exception):
    """The board does not have what a picture asks for."""


def short_net(name: str) -> str:
    if not name:
        return ""
    if name.startswith("unconnected-"):
        return "nc"
    return name.rsplit("/", 1)[-1]


def _num(v: float) -> str:
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def _bbox(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    return (min(xs), min(ys), max(xs), max(ys))


def _overlap(a, b):
    return a[0] <= b[2] and b[0] <= a[2] and a[1] <= b[3] and b[1] <= a[3]


class Board:
    """A loaded board, with its shapes per layer in contract coordinates."""

    def __init__(self, path):
        self.path = str(path)
        self.board = pcbnew.LoadBoard(self.path)
        aux = self.board.GetDesignSettings().GetAuxOrigin()
        self._ox, self._oy = aux.x, aux.y
        xs, ys = [], []
        for d in self.board.GetDrawings():
            if d.GetLayerName() == "Edge.Cuts" and d.GetClass() == "PCB_SHAPE":
                for p in (d.GetStart(), d.GetEnd()):
                    xs.append(p.x)
                    ys.append(p.y)
        if not xs:
            raise RenderError("the board has no outline on Edge.Cuts")
        if abs(min(xs) - self._ox) > 1000 or abs(max(ys) - self._oy) > 1000:
            raise RenderError(
                "the drill/place origin is not the bottom-left corner of the outline: "
                "the coordinates of the pictures would not be those of the mechanical contract"
            )
        self.width = pcbnew.ToMM(max(xs) - min(xs))
        self.height = pcbnew.ToMM(max(ys) - min(ys))
        self._layer_id = {self.board.GetLayerName(i): i for i in self.board.GetEnabledLayers().Seq()}
        self._items = {}
        self._holes = None
        self.footprints = {fp.GetReference(): fp for fp in self.board.GetFootprints()}
        self.nets = {}
        for name in self.board.GetNetsByName().keys():
            name = str(name)
            if name:
                self.nets.setdefault(short_net(name), set()).add(name)

    # Coordinates -------------------------------------------------------------------------------

    def x(self, kx):
        return (kx - self._ox) / 1e6

    def y(self, ky):
        return (self._oy - ky) / 1e6

    def kicad_xy(self, x, y):
        """Contract coordinates to the page coordinates KiCad shows by default, in millimetres."""
        return (round(self._ox / 1e6 + x, 3), round(self._oy / 1e6 - y, 3))

    def _ring(self, chain):
        return [(self.x(p.x), self.y(p.y)) for p in (chain.CPoint(i) for i in range(chain.PointCount()))]

    def _polys(self, ps):
        out = []
        for i in range(ps.OutlineCount()):
            rings = [self._ring(ps.Outline(i))]
            for j in range(ps.HoleCount(i)):
                rings.append(self._ring(ps.Hole(i, j)))
            if len(rings[0]) >= 3:
                out.append(rings)
        return out

    # Lookups -----------------------------------------------------------------------------------

    def footprint(self, ref):
        fp = self.footprints.get(ref)
        if fp is None:
            raise RenderError(f"no part {ref} on the board")
        return fp

    def net_names(self, short):
        """The full names of the nets a short name stands for. Exactly one must match."""
        if short in self.nets:
            names = self.nets[short]
        else:
            names = {n for group in self.nets.values() for n in group if n == short}
        if not names:
            raise RenderError(f"no net {short} on the board")
        if len(names) > 1:
            raise RenderError(f"net name {short} is ambiguous: {sorted(names)}")
        return names

    def pad(self, spec):
        """'U1.9' -> the pads of U1 numbered 9 (a part may have several)."""
        ref, _, number = spec.partition(".")
        pads = [p for p in self.footprint(ref).Pads() if p.GetNumber() == number]
        if not pads:
            raise RenderError(f"part {ref} has no pad {number}")
        return pads

    def pad_xy(self, spec):
        pads = self.pad(spec)
        return (sum(self.x(p.GetPosition().x) for p in pads) / len(pads),
                sum(self.y(p.GetPosition().y) for p in pads) / len(pads))

    def ref_xy(self, ref):
        p = self.footprint(ref).GetPosition()
        return (self.x(p.x), self.y(p.y))

    def ref_box(self, ref):
        """The box round a part's pads and drawn outline, without its texts."""
        bb = self.footprint(ref).GetBoundingBox(False)
        return (self.x(bb.GetLeft()), self.y(bb.GetBottom()), self.x(bb.GetRight()), self.y(bb.GetTop()))

    def side(self, ref):
        return "bottom" if self.footprint(ref).GetLayerName().startswith("B.") else "top"

    # Shapes ------------------------------------------------------------------------------------

    def _poly_item(self, kind, ps, net="", ref="", pad=""):
        polys = self._polys(ps)
        if not polys:
            return None
        pts = [p for rings in polys for p in rings[0]]
        return {"kind": kind, "polys": polys, "bbox": _bbox(pts), "net": net, "ref": ref, "pad": pad}

    def _graphic(self, item, lid, ref=""):
        ps = pcbnew.SHAPE_POLY_SET()
        cls = item.GetClass()
        if cls in ("PCB_TEXT", "PCB_FIELD"):
            if not item.IsVisible():
                return None
            item.TransformTextToPolySet(ps, 0, TEXT_ERROR, pcbnew.ERROR_INSIDE)
            kind = "text"
        elif cls == "PCB_SHAPE":
            item.TransformShapeToPolygon(ps, lid, 0, MAX_ERROR, pcbnew.ERROR_INSIDE)
            kind = "shape"
        else:
            return None
        net = item.GetNetname() if hasattr(item, "GetNetname") and item.IsOnCopperLayer() else ""
        it = self._poly_item(kind, ps, net=net, ref=ref)
        if it and cls == "PCB_FIELD":
            it["field"] = item.GetName()
        return it

    def items(self, layer):
        """Everything on a layer but the zone fills, each as outlines with its net and part."""
        if layer in self._items:
            return self._items[layer]
        if layer not in self._layer_id:
            raise RenderError(f"the board has no layer {layer}")
        lid = self._layer_id[layer]
        out = []
        copper = layer in COPPER
        if copper:
            for t in self.board.GetTracks():
                cls = t.GetClass()
                if cls == "PCB_VIA":
                    if not t.IsOnLayer(lid):
                        continue
                    p = t.GetPosition()
                    cx, cy, r = self.x(p.x), self.y(p.y), t.GetWidth(lid) / 2e6
                    out.append({"kind": "via", "circle": (cx, cy, r), "bbox": (cx - r, cy - r, cx + r, cy + r),
                                "net": t.GetNetname(), "ref": "", "pad": ""})
                elif t.GetLayer() != lid:
                    continue
                elif cls == "PCB_TRACK":
                    a, b, w = t.GetStart(), t.GetEnd(), t.GetWidth() / 1e6
                    x1, y1, x2, y2 = self.x(a.x), self.y(a.y), self.x(b.x), self.y(b.y)
                    out.append({"kind": "seg", "seg": (x1, y1, x2, y2, w), "net": t.GetNetname(), "ref": "", "pad": "",
                                "bbox": (min(x1, x2) - w / 2, min(y1, y2) - w / 2, max(x1, x2) + w / 2, max(y1, y2) + w / 2)})
                else:  # an arc
                    ps = pcbnew.SHAPE_POLY_SET()
                    t.TransformShapeToPolygon(ps, lid, 0, MAX_ERROR, pcbnew.ERROR_INSIDE)
                    it = self._poly_item("shape", ps, net=t.GetNetname())
                    if it:
                        out.append(it)
        for fp in self.board.GetFootprints():
            ref = fp.GetReference()
            if copper:
                for pad in fp.Pads():
                    if not pad.IsOnLayer(lid):
                        continue
                    ps = pcbnew.SHAPE_POLY_SET()
                    pad.TransformShapeToPolygon(ps, lid, 0, MAX_ERROR, pcbnew.ERROR_INSIDE)
                    it = self._poly_item("pad", ps, net=pad.GetNetname(), ref=ref, pad=pad.GetNumber())
                    if it:
                        c = pad.GetPosition()
                        it["centre"] = (self.x(c.x), self.y(c.y))
                        out.append(it)
            for g in list(fp.GraphicalItems()) + list(fp.GetFields()):
                if g.GetLayer() == lid:
                    it = self._graphic(g, lid, ref)
                    if it:
                        out.append(it)
        for d in self.board.GetDrawings():
            if d.GetLayer() == lid:
                it = self._graphic(d, lid)
                if it:
                    out.append(it)
        self._items[layer] = out
        return out

    def holes(self):
        """Every drilled hole, as an outline."""
        if self._holes is None:
            out = []
            for t in self.board.GetTracks():
                if t.GetClass() == "PCB_VIA":
                    p = t.GetPosition()
                    cx, cy, r = self.x(p.x), self.y(p.y), t.GetDrillValue() / 2e6
                    out.append({"circle": (cx, cy, r), "bbox": (cx - r, cy - r, cx + r, cy + r)})
            for fp in self.board.GetFootprints():
                for pad in fp.Pads():
                    if pad.HasHole():
                        ps = pcbnew.SHAPE_POLY_SET()
                        pad.TransformHoleToPolygon(ps, 0, MAX_ERROR, pcbnew.ERROR_INSIDE)
                        for rings in self._polys(ps):
                            out.append({"polys": [rings], "bbox": _bbox(rings[0])})
            self._holes = out
        return self._holes

    def zone_fills(self, layer, box):
        """Zone fills on a layer, cut to a box: a list of (net, polygons)."""
        lid = self._layer_id[layer]
        x0, y0, x1, y1 = box
        clip = pcbnew.SHAPE_POLY_SET()
        clip.NewOutline()
        for cx, cy in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
            clip.Append(int(round(self._ox + cx * 1e6)), int(round(self._oy - cy * 1e6)))
        out = []
        for z in self.board.Zones():
            if z.GetIsRuleArea() or not z.IsOnLayer(lid):
                continue
            ps = pcbnew.SHAPE_POLY_SET(z.GetFilledPolysList(lid))
            if ps.OutlineCount() == 0:
                continue
            ps.BooleanIntersection(clip)
            polys = self._polys(ps)
            if polys:
                out.append((z.GetNetname(), polys))
        return out


# ---------------------------------------------------------------------------------------------------
# One picture


class _View:
    """Board coordinates to picture coordinates for one crop."""

    def __init__(self, box, width, mirror):
        self.x0, self.y0, self.x1, self.y1 = box
        self.s = width / (self.x1 - self.x0)
        self.w = width
        self.h = round((self.y1 - self.y0) * self.s)
        self.mirror = mirror
        # A tenth of a pixel is 10 um or less in a whole-board picture, and keeps its file small.
        self.digits = 2 if self.s >= 20 else 1

    def px(self, x):
        return (self.x1 - x) * self.s if self.mirror else (x - self.x0) * self.s

    def py(self, y):
        return (self.y1 - y) * self.s

    def pt(self, p):
        if self.digits == 1:
            return f"{self.px(p[0]):.1f},{self.py(p[1]):.1f}"
        return f"{_num(self.px(p[0]))},{_num(self.py(p[1]))}"

    def path(self, polys):
        d = []
        for rings in polys:
            for ring in rings:
                d.append("M" + "L".join(self.pt(p) for p in ring) + "Z")
        return "".join(d)


def _text(x, y, s, size, fill="#fff", anchor="middle", weight="normal", halo=True, opacity=1.0):
    extra = ' stroke="#000" stroke-width="3" stroke-opacity="0.75" paint-order="stroke" stroke-linejoin="round"' if halo else ""
    op = f' opacity="{opacity}"' if opacity != 1.0 else ""
    return (f'<text x="{_num(x)}" y="{_num(y)}" font-size="{_num(size)}" fill="{fill}" text-anchor="{anchor}" '
            f'font-weight="{weight}"{extra}{op}>{html.escape(s)}</text>')


def _layer_svg(view, board, layer, box, marks, zones, values=False, text=True):
    """One layer: zone fills, then the rest, then whatever is marked, in its mark colour.

    The value of a part ("100n", "SMBJ6.0A") is text on the fabrication layer. It is left out unless
    asked for: it covers the pads the picture is there to show.
    """
    colour = LAYER_COLOUR.get(layer, "#aaaaaa")
    copper = layer in COPPER

    def mark_of(net):
        for m in marks:
            if net and net in m["net_names"]:
                return m["colour"]
        return None

    # colour -> key -> shapes. The key is the net on a copper layer and the part on any other, and it
    # goes into the picture as data-n or data-r: the page reads it when a shape is selected, so what it
    # says about a shape is what the board file says.
    groups = {}

    def group(c, key):
        return groups.setdefault(c, {}).setdefault(key, {"zone": [], "pad": [], "poly": [], "segs": {}, "circ": []})

    if copper and zones:
        for net, polys in board.zone_fills(layer, box):
            group(mark_of(net) or colour, net)["zone"].append(view.path(polys))
    for it in board.items(layer):
        if not _overlap(it["bbox"], box) or (it.get("field") == "Value" and not values):
            continue
        if it["kind"] == "text" and not text:
            continue
        g = group((mark_of(it["net"]) if copper else None) or colour, it["net"] if copper else it["ref"])
        if it["kind"] == "seg":
            x1, y1, x2, y2, w = it["seg"]
            g["segs"].setdefault(w, []).append(f"M{view.pt((x1, y1))}L{view.pt((x2, y2))}")
        elif it["kind"] == "via":
            cx, cy, r = it["circle"]
            g["circ"].append(f'<circle cx="{_num(view.px(cx))}" cy="{_num(view.py(cy))}" r="{_num(r * view.s)}"/>')
        elif it["kind"] == "pad":
            g["pad"].append(f'<path data-p="{html.escape(it["ref"] + "." + it["pad"], quote=True)}" d="{view.path(it["polys"])}"/>')
        else:
            g["poly"].append(view.path(it["polys"]))

    attr = "data-n" if copper else "data-r"
    out = [f'<g id="layer-{layer.replace(".", "-")}" data-layer="{layer}">']
    # The layer colour first, the mark colours over it.
    for c in sorted(groups, key=lambda c: c != colour):
        marked = c != colour
        keys = sorted(groups[c])
        for key in keys:
            g = groups[c][key]
            if g["zone"]:
                out.append(f'<path {attr}="{html.escape(key, quote=True)}" data-zone="1" fill="{c}" fill-rule="evenodd" '
                           f'opacity="{0.6 if marked else 0.38}" d="{"".join(g["zone"])}"/>')
        body = []
        for key in keys:
            g = groups[c][key]
            shapes = []
            if g["poly"]:
                shapes.append(f'<path fill-rule="evenodd" d="{"".join(g["poly"])}"/>')
            for w, segs in sorted(g["segs"].items()):
                shapes.append(f'<path fill="none" stroke="{c}" stroke-width="{_num(w * view.s)}" stroke-linecap="round" d="{"".join(segs)}"/>')
            shapes.extend(g["circ"])
            # Pads last: a track of the same net that ends in a pad does not hide the pad from a selection.
            shapes.extend(g["pad"])
            if shapes:
                body.append(f'<g {attr}="{html.escape(key, quote=True)}">' + "".join(shapes) + "</g>")
        if body:
            opacity = 1.0 if marked or not copper else 0.88
            out.append(f'<g fill="{c}" opacity="{opacity}">' + "".join(body) + "</g>")
    out.append("</g>")
    return "".join(out)


def _grid(view):
    step = next((g for g in (0.5, 1, 2, 5, 10, 20, 50) if g * view.s >= 70), 50)
    out = ['<g id="grid" pointer-events="none" stroke="#ffffff" stroke-opacity="0.10" stroke-width="1" font-size="10" fill="#ffffff" '
           'fill-opacity="0.55">']
    k = math.ceil(view.x0 / step - 1e-9)
    while k * step <= view.x1 + 1e-9:
        px = view.px(k * step)
        out.append(f'<path d="M{_num(px)},0V{view.h}"/>')
        if 14 < px < view.w - 14:
            out.append(f'<text x="{_num(px + 3)}" y="{view.h - 4}" stroke="none">{_num(k * step)}</text>')
        k += 1
    k = math.ceil(view.y0 / step - 1e-9)
    while k * step <= view.y1 + 1e-9:
        py = view.py(k * step)
        out.append(f'<path d="M0,{_num(py)}H{view.w}"/>')
        if 14 < py < view.h - 18:
            out.append(f'<text x="3" y="{_num(py - 3)}" stroke="none">{_num(k * step)}</text>')
        k += 1
    out.append("</g>")
    return "".join(out)


def _label_box(x, y, lines, size=15, colour="#ffe100", anchor="middle"):
    """A dark box with text, centred on (x, y) or starting or ending there. Returns (svg, box)."""
    w = max(len(line) for line in lines) * size * 0.60 + 14
    h = len(lines) * (size + 4) + 8
    left = x - w / 2 if anchor == "middle" else (x if anchor == "start" else x - w)
    top = y - h / 2
    out = [f'<rect x="{_num(left)}" y="{_num(top)}" width="{_num(w)}" height="{_num(h)}" rx="4" fill="#0b0e13" '
           f'fill-opacity="0.92" stroke="{colour}" stroke-width="1.5"/>']
    for i, line in enumerate(lines):
        out.append(_text(left + w / 2, top + 4 + (i + 1) * (size + 4) - 5, line, size, fill="#ffffff", halo=False))
    return "".join(out), (left, top, left + w, top + h)


def _callout(view, target, dx, dy, text, colour, ring):
    tx, ty = view.px(target[0]), view.py(target[1])
    sign = -1 if view.mirror else 1
    lx, ly = tx + sign * dx * view.s, ty - dy * view.s
    lines = text.split("|")
    box_svg, (bx0, by0, bx1, by1) = _label_box(lx, ly, lines, colour=colour)
    # Keep the label inside the picture.
    shift_x = max(4 - bx0, 0) + min(view.w - 4 - bx1, 0)
    shift_y = max(4 - by0, 0) + min(view.h - 4 - by1, 0)
    if shift_x or shift_y:
        lx, ly = lx + shift_x, ly + shift_y
        box_svg, (bx0, by0, bx1, by1) = _label_box(lx, ly, lines, colour=colour)
    # The leader ends on the edge of the box nearest the target.
    ex, ey = min(max(tx, bx0), bx1), min(max(ty, by0), by1)
    out = []
    r = ring * view.s if ring else 0
    d = math.hypot(tx - ex, ty - ey)
    if d > r + 2:
        k = (d - r) / d
        out.append(f'<path d="M{_num(ex)},{_num(ey)}L{_num(ex + (tx - ex) * k)},{_num(ey + (ty - ey) * k)}" '
                   f'stroke="#000" stroke-width="5" stroke-opacity="0.6" fill="none"/>')
        out.append(f'<path d="M{_num(ex)},{_num(ey)}L{_num(ex + (tx - ex) * k)},{_num(ey + (ty - ey) * k)}" '
                   f'stroke="{colour}" stroke-width="2" fill="none"/>')
    if ring:
        out.append(f'<circle cx="{_num(tx)}" cy="{_num(ty)}" r="{_num(r)}" fill="none" stroke="#000" stroke-width="5" stroke-opacity="0.6"/>')
        out.append(f'<circle cx="{_num(tx)}" cy="{_num(ty)}" r="{_num(r)}" fill="none" stroke="{colour}" stroke-width="2.5"/>')
    else:
        out.append(f'<circle cx="{_num(tx)}" cy="{_num(ty)}" r="3.5" fill="{colour}" stroke="#000" stroke-width="1"/>')
    out.append(box_svg)
    return "".join(out)


def _minimap(view, board, corner):
    mw = 132.0
    k = mw / board.width
    mh = board.height * k
    pad = 10
    left = pad if "l" in corner else view.w - mw - pad
    top = pad + 16 if "t" in corner else view.h - mh - pad - 4
    x0, x1 = (board.width - view.x1, board.width - view.x0) if view.mirror else (view.x0, view.x1)
    cx0, cx1 = max(x0, 0), min(x1, board.width)
    cy0, cy1 = max(view.y0, 0), min(view.y1, board.height)
    out = [f'<g id="minimap"><rect x="{_num(left - 5)}" y="{_num(top - 19)}" width="{_num(mw + 10)}" height="{_num(mh + 24)}" rx="4" '
           f'fill="#0b0e13" fill-opacity="0.88" stroke="#ffffff" stroke-opacity="0.35"/>',
           _text(left + mw / 2, top - 6, "bottom side, mirrored" if view.mirror else "where on the board", 10.5, halo=False,
                 fill="#d6dae2"),
           f'<rect x="{_num(left)}" y="{_num(top)}" width="{_num(mw)}" height="{_num(mh)}" fill="#1c2733" stroke="#e6d96a" stroke-width="1"/>']
    if cx1 > cx0 and cy1 > cy0:
        w, h = max((cx1 - cx0) * k, 5), max((cy1 - cy0) * k, 5)
        mx = left + (cx0 + cx1) / 2 * k - w / 2
        my = top + (board.height - (cy0 + cy1) / 2) * k - h / 2
        out.append(f'<rect x="{_num(mx)}" y="{_num(my)}" width="{_num(w)}" height="{_num(h)}" fill="#ffe100" fill-opacity="0.55" '
                   f'stroke="#ffe100" stroke-width="1.5"/>')
    out.append("</g>")
    return "".join(out)


def _legend(view, fig, layers, marks, band):
    y = view.h + band / 2 + 4.5
    side = "Seen from below (mirrored)" if view.mirror else "Seen from above"
    out = [f'<rect x="0" y="{view.h}" width="{view.w}" height="{band}" fill="#0b0e13"/>',
           _text(10, y, side, 12.5, anchor="start", halo=False, fill="#d6dae2")]
    x = 10 + len(side) * 12.5 * 0.56 + 18
    chips = [(LAYER_COLOUR.get(layer, "#aaa"), layer) for layer in layers if layer != "Edge.Cuts"]
    chips += [(m["colour"], m["label"]) for m in marks if m.get("label")]
    for colour, label in chips:
        out.append(f'<rect x="{_num(x)}" y="{_num(y - 10.5)}" width="13" height="13" rx="2" fill="{colour}"/>')
        out.append(_text(x + 18, y, label, 12.5, anchor="start", halo=False, fill="#d6dae2"))
        x += 18 + len(label) * 12.5 * 0.58 + 16
    span = f"x {_num(view.x0)} to {_num(view.x1)}, y {_num(view.y0)} to {_num(view.y1)} mm"
    if x + len(span) * 12.5 * 0.56 + 10 < view.w:
        out.append(_text(view.w - 10, y, span, 12.5, anchor="end", halo=False, fill="#9aa3b2"))
    return "".join(out)


def figure_box(board, fig):
    """The crop of a figure in board coordinates."""
    if "box" in fig:
        x0, y0, x1, y1 = (float(v) for v in fig["box"])
    elif "around" in fig:
        boxes = [board.ref_box(r) for r in fig["around"]]
        m = float(fig.get("margin", 2.0))
        x0, y0 = min(b[0] for b in boxes) - m, min(b[1] for b in boxes) - m
        x1, y1 = max(b[2] for b in boxes) + m, max(b[3] for b in boxes) + m
    else:
        raise RenderError(f"figure {fig['id']}: neither box nor around")
    if x1 <= x0 or y1 <= y0:
        raise RenderError(f"figure {fig['id']}: empty box")
    # Not taller than square and not flatter than 5:2, so that a picture fits a screen without scrolling.
    w, h = x1 - x0, y1 - y0
    if w / h < 1.0:
        grow = (h - w) / 2
        x0, x1 = x0 - grow, x1 + grow
    elif w / h > 2.5:
        grow = (w / 2.5 - h) / 2
        y0, y1 = y0 - grow, y1 + grow
    return (round(x0, 3), round(y0, 3), round(x1, 3), round(y1, 3))


def references(fig):
    """The parts and nets a figure names: (refs, nets)."""
    refs, nets = set(fig.get("around", [])), set()
    for m in fig.get("marks", []):
        refs.update(m.get("refs", []))
        nets.update(m.get("nets", []))
    refs.update(fig.get("pad_labels", []))
    for c in fig.get("callouts", []):
        if "ref" in c:
            refs.add(c["ref"])
        if "pad" in c:
            refs.add(c["pad"].partition(".")[0])
    return refs, nets


def render_figure(board, fig):
    """One picture as SVG text, and what the page needs to know about it."""
    view_side = fig.get("view", "top")
    if view_side not in ("top", "bottom"):
        raise RenderError(f"figure {fig['id']}: view must be top or bottom")
    mirror = view_side == "bottom"
    layers = fig.get("layers") or (["B.Cu", "B.Silkscreen", "Edge.Cuts"] if mirror else ["F.Cu", "F.Silkscreen", "Edge.Cuts"])
    box = figure_box(board, fig)
    view = _View(box, int(fig.get("width", 1200)), mirror)
    band = 30
    marks = []
    for i, m in enumerate(fig.get("marks", [])):
        names = set()
        for n in m.get("nets", []):
            names |= board.net_names(n)
        key = m.get("colour", "abcd"[i % 4])
        if key not in MARK_COLOUR:
            raise RenderError(f"figure {fig['id']}: no mark colour {key}")
        marks.append({"net_names": names, "refs": list(m.get("refs", [])), "colour": MARK_COLOUR[key],
                      "label": m.get("label", "")})

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {view.w} {view.h + band}" width="{view.w}" height="{view.h + band}" '
           f'font-family="{FONT}">',
           f'<title>{html.escape(fig.get("alt", fig["id"]))}</title>',
           f'<rect width="{view.w}" height="{view.h + band}" fill="{BACKGROUND}"/>',
           f'<clipPath id="crop"><rect width="{view.w}" height="{view.h}"/></clipPath>',
           '<g clip-path="url(#crop)">']
    # The board itself, lighter than what is beyond its edge.
    bx0, bx1 = sorted((view.px(0), view.px(board.width)))
    out.append(f'<rect x="{_num(bx0)}" y="{_num(view.py(board.height))}" width="{_num(bx1 - bx0)}" '
               f'height="{_num(board.height * view.s)}" fill="{BOARD_FILL}"/>')
    if fig.get("grid", True):
        out.append(_grid(view))
    for layer in layers:
        out.append(_layer_svg(view, board, layer, box, marks, fig.get("zones", True), fig.get("values", False),
                              fig.get("text", True)))
    if any(layer in COPPER for layer in layers) and fig.get("holes", True):
        hole = []
        for h in board.holes():
            if not _overlap(h["bbox"], box):
                continue
            if "circle" in h:
                cx, cy, r = h["circle"]
                hole.append(f'<circle cx="{_num(view.px(cx))}" cy="{_num(view.py(cy))}" r="{_num(r * view.s)}"/>')
            else:
                hole.append(f'<path d="{view.path(h["polys"])}"/>')
        out.append(f'<g id="holes" pointer-events="none" fill="{HOLE_FILL}" stroke="#aeb4bf" stroke-width="0.8">' + "".join(hole) + "</g>")

    # Everything from here on is annotation. A click goes through it to the copper under it.
    out.append('<g id="pad-labels" pointer-events="none">')
    # Pad numbers, and the net on each pad, for the parts that ask for them.
    mode = fig.get("pad_label_mode", "net")
    seen = set()
    for ref in fig.get("pad_labels", []):
        board.footprint(ref)
        for layer in layers:
            if layer not in COPPER:
                continue
            for it in board.items(layer):
                if it["kind"] != "pad" or it["ref"] != ref or not it["pad"] or not _overlap(it["bbox"], box):
                    continue
                cx, cy = it["centre"]
                key = (ref, it["pad"], round(cx, 2), round(cy, 2))
                if key in seen:
                    continue
                seen.add(key)
                bw, bh = (it["bbox"][2] - it["bbox"][0]) * view.s, (it["bbox"][3] - it["bbox"][1]) * view.s
                net = short_net(it["net"])
                want_net = mode == "net" and net and bh >= 30 and bw >= len(net) * 6.2
                size = max(9.0, min(22.0, bh * (0.36 if want_net else 0.62), bw * 0.8 / max(len(it["pad"]) * 0.62, 0.62)))
                if min(bw, bh) < 8:
                    continue
                px, py = view.px(cx), view.py(cy)
                if want_net:
                    nsize = max(8.5, min(size * 0.8, bw / (len(net) * 0.62)))
                    out.append(_text(px, py - 1, it["pad"], size, weight="bold"))
                    out.append(_text(px, py + nsize + 1, net, nsize))
                else:
                    out.append(_text(px, py + size * 0.36, it["pad"], size, weight="bold"))
    out.append("</g></g>")

    out.append('<g id="notes" pointer-events="none">')
    # Boxes round marked parts.
    for m in marks:
        for ref in m["refs"]:
            x0, y0, x1, y1 = board.ref_box(ref)
            px0, px1 = sorted((view.px(x0), view.px(x1)))
            out.append(f'<rect x="{_num(px0 - 4)}" y="{_num(view.py(y1) - 4)}" width="{_num(px1 - px0 + 8)}" '
                       f'height="{_num((y1 - y0) * view.s + 8)}" rx="5" fill="none" stroke="#000" stroke-opacity="0.6" stroke-width="5"/>')
            out.append(f'<rect x="{_num(px0 - 4)}" y="{_num(view.py(y1) - 4)}" width="{_num(px1 - px0 + 8)}" '
                       f'height="{_num((y1 - y0) * view.s + 8)}" rx="5" fill="none" stroke="{m["colour"]}" stroke-width="2.5" '
                       'stroke-dasharray="9 5"/>')
    for c in fig.get("callouts", []):
        if "pad" in c:
            target = board.pad_xy(c["pad"])
        elif "ref" in c:
            target = board.ref_xy(c["ref"])
        elif "at" in c:
            target = (float(c["at"][0]), float(c["at"][1]))
        else:
            raise RenderError(f"figure {fig['id']}: a callout needs pad, ref or at")
        colour = MARK_COLOUR.get(c.get("colour", "a"))
        if colour is None:
            raise RenderError(f"figure {fig['id']}: no callout colour {c.get('colour')}")
        out.append(_callout(view, target, float(c.get("dx", 0)), float(c.get("dy", 3)), c["text"], colour,
                            float(c.get("ring", 0))))
    corner = fig.get("minimap", "br")
    covers = (box[2] - box[0]) * (box[3] - box[1]) / (board.width * board.height)
    if corner != "none" and covers < 0.5:
        out.append(_minimap(view, board, corner))
    out.append(_legend(view, fig, layers, marks, band))
    out.append("</g></svg>\n")
    refs, nets = references(fig)
    meta = {"width": view.w, "height": view.h + band, "box": list(box), "view": view_side, "layers": layers,
            "kicad_centre": list(board.kicad_xy((box[0] + box[2]) / 2, (box[1] + box[3]) / 2)),
            "refs": sorted(refs), "nets": sorted(nets)}
    return "".join(out), meta


# ---------------------------------------------------------------------------------------------------
# The assembly drawing at true size, for printing


def _mm(v):
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def render_assembly(board, side, note):
    """The fabrication layer of one side with the lands and holes, at 1:1, with two 100 mm scale bars."""
    mirror = side == "bottom"
    fab, cu = ("B.Fab", "B.Cu") if mirror else ("F.Fab", "F.Cu")
    left, right, top, bottom = 20.0, 8.0, 24.0, 26.0
    w, h = left + board.width + right, top + board.height + bottom

    def px(x):
        return left + (board.width - x if mirror else x)

    def py(y):
        return top + board.height - y

    def path(polys):
        return "".join("M" + "L".join(f"{_mm(px(p[0]))},{_mm(py(p[1]))}" for p in ring) + "Z" for rings in polys for ring in rings)

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {_mm(w)} {_mm(h)}" width="{_mm(w)}mm" height="{_mm(h)}mm" '
           f'font-family="{FONT}">',
           f"<title>ZoneFlow rev A, {side} side, assembly drawing at true size</title>",
           f'<rect width="{_mm(w)}" height="{_mm(h)}" fill="#ffffff"/>']
    pads = [it for it in board.items(cu) if it["kind"] == "pad"]
    out.append('<g fill="#d4d7dc">' + "".join(f'<path d="{path(it["polys"])}"/>' for it in pads) + "</g>")
    holes = []
    for hole in board.holes():
        if "circle" in hole:
            continue  # vias are not something a part is laid on
        holes.append(f'<path d="{path(hole["polys"])}"/>')
    out.append('<g fill="#ffffff" stroke="#000000" stroke-width="0.08">' + "".join(holes) + "</g>")
    outlines = "".join(f'<path d="{path(it["polys"])}"/>' for it in board.items(fab) if it.get("field") != "Value")
    out.append(f'<g fill="#000000" fill-rule="evenodd">{outlines}</g>')
    out.append(f'<rect x="{_mm(left)}" y="{_mm(top)}" width="{_mm(board.width)}" height="{_mm(board.height)}" fill="none" '
               'stroke="#000000" stroke-width="0.15"/>')

    def bar(x0, y0, horizontal):
        seg = ['<g stroke="#000000" stroke-width="0.12" fill="#000000" font-size="2.2">']
        for k in range(0, 101):
            long = 2.4 if k % 10 == 0 else (1.6 if k % 5 == 0 else 0.9)
            if horizontal:
                seg.append(f'<path d="M{_mm(x0 + k)},{_mm(y0)}v{_mm(long)}"/>')
                if k % 10 == 0:
                    seg.append(f'<text x="{_mm(x0 + k)}" y="{_mm(y0 + 5.2)}" text-anchor="middle" stroke="none">{k}</text>')
            else:
                seg.append(f'<path d="M{_mm(x0)},{_mm(y0 - k)}h{_mm(-long)}"/>')
                if k % 10 == 0:
                    seg.append(f'<text x="{_mm(x0 - 3.2)}" y="{_mm(y0 - k + 0.8)}" text-anchor="end" stroke="none">{k}</text>')
        seg.append(f'<path d="M{_mm(x0)},{_mm(y0)}{"h100" if horizontal else "v-100"}"/>')
        seg.append("</g>")
        return "".join(seg)

    out.append(bar(left, top + board.height + 5.0, True))
    out.append(bar(left - 4.0, top + board.height, False))
    out.append(f'<g font-size="2.6" fill="#000000"><text x="{_mm(left)}" y="6">ZoneFlow rev A, {side} side'
               f'{", seen from below (mirrored)" if mirror else ", seen from above"}. True size.</text>'
               f'<text x="{_mm(left)}" y="18" font-size="2.2">Print at 100 % or "actual size", not "fit to page".</text>'
               f'<text x="{_mm(left)}" y="10.4" font-size="2.2">Check the print: each scale bar is 100.0 mm long, and the board edge is '
               f'{_mm(board.width)} x {_mm(board.height)} mm.</text>'
               f'<text x="{_mm(left)}" y="14" font-size="2.2">{html.escape(note)}</text>'
               f'<text x="{_mm(left)}" y="{_mm(h - 9.5)}" font-size="2.2">Grey: lands. Black: part outlines and designators '
               f'from the {fab} layer.</text>'
               f'<text x="{_mm(left)}" y="{_mm(h - 6)}" font-size="2.2">Circles: drilled holes for parts. Scale bars in '
               'millimetres.</text></g>')
    out.append("</svg>\n")
    return "".join(out), {"width_mm": w, "height_mm": h, "side": side}


# ---------------------------------------------------------------------------------------------------
# Numbered boxes over the 3D render


def render_overlay(calib, blocks, board_w, board_h):
    """Numbered outlines of the blocks, in the pixel frame of the rendered picture."""
    w, h = calib["width"], calib["height"]
    kx = (calib["right"] - calib["left"]) / board_w
    ky = (calib["bottom"] - calib["top"]) / board_h

    def px(x):
        return calib["left"] + x * kx

    def py(y):
        return calib["bottom"] - y * ky

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" font-family="{FONT}">',
           "<title>Numbered blocks of the board</title>"]
    for b in blocks:
        x0, y0, x1, y1 = b["box"]
        out.append(f'<rect x="{_num(px(x0))}" y="{_num(py(y1))}" width="{_num((x1 - x0) * kx)}" height="{_num((y1 - y0) * ky)}" rx="6" '
                   'fill="#ffffff" fill-opacity="0.07" stroke="#000000" stroke-opacity="0.55" stroke-width="5"/>')
        out.append(f'<rect x="{_num(px(x0))}" y="{_num(py(y1))}" width="{_num((x1 - x0) * kx)}" height="{_num((y1 - y0) * ky)}" rx="6" '
                   'fill="none" stroke="#ffe100" stroke-width="2.5"/>')
    for i, b in enumerate(blocks, 1):
        x0, y0, x1, y1 = b["box"]
        bx, by = b.get("badge", [x0, y1])
        cx, cy = px(bx), py(by)
        out.append(f'<circle cx="{_num(cx)}" cy="{_num(cy)}" r="17" fill="#ffe100" stroke="#000000" stroke-width="2"/>')
        out.append(f'<text x="{_num(cx)}" y="{_num(cy + 6.5)}" font-size="19" font-weight="bold" text-anchor="middle" fill="#000000">{i}</text>')
    out.append("</svg>\n")
    return "".join(out)
