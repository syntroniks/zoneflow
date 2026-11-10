# SPDX-License-Identifier: Apache-2.0
"""Write a ``Design`` as KiCad schematic files (one per sheet definition)."""

from __future__ import annotations

from pathlib import Path

from . import symlib
from .design import (
    DOWN,
    LEFT,
    RIGHT,
    UP,
    Design,
    DesignError,
    Flat,
    Net,
    Part,
    PinRef,
    Sheet,
    offset_ref,
    stable_uuid,
    transform,
)
from .sexpr import Sym, dumps, fmt

SCH_VERSION = "20260306"  # written by KiCad 10.0.8

S = Sym
YES, NO = Sym("yes"), Sym("no")

_LABEL_ANGLE = {RIGHT: 0, UP: 90, LEFT: 180, DOWN: 270}
# Rotation that points a power symbol along a stub, by where its body sits at rotation 0.
_POWER_ROT = {
    "down": {DOWN: 0, RIGHT: 90, UP: 180, LEFT: 270},
    "up": {UP: 0, LEFT: 90, DOWN: 180, RIGHT: 270},
}


def _effects(justify: tuple[str, ...] = (), hide: bool = False, size: float = 1.27) -> list:
    e = [S("effects"), [S("font"), [S("size"), fmt(size), fmt(size)]]]
    if justify:
        e.append([S("justify"), *[S(j) for j in justify]])
    if hide:
        e.append([S("hide"), YES])
    return e


def _prop(name: str, value: str, x: float, y: float, angle: int = 0, justify=(), hide=False) -> list:
    p = [S("property"), name, value, [S("at"), fmt(x), fmt(y), S(str(angle))]]
    if hide:
        p.append([S("hide"), YES])
    p.append(_effects(justify))
    return p


def _add(a, d, k=1.0):
    return (round(a[0] + d[0] * k, 4), round(a[1] + d[1] * k, 4))


class _SheetWriter:
    def __init__(self, design: Design, sheet: Sheet, flat: Flat, pwr_counter: list[int]):
        self.d = design
        self.s = sheet
        self.flat = flat
        self.contexts = flat.contexts.get(sheet.name, [])
        self.items: list = []
        self.lib_ids: list[str] = []
        self.segments: list[tuple[tuple[float, float], tuple[float, float], Net]] = []
        self.pin_points: dict[tuple[float, float], PinRef] = {}
        self.pwr_counter = pwr_counter
        self.n = 0

    def uid(self, kind: str, key: str = "") -> str:
        self.n += 1
        return stable_uuid(self.d.name, self.s.name, kind, key or str(self.n))

    def use(self, lib_id: str) -> symlib.LibSymbol:
        if lib_id not in self.lib_ids:
            self.lib_ids.append(lib_id)
        return symlib.load(lib_id, self.d.symbol_dirs)

    # ---- symbols ----------------------------------------------------------
    def _field_places(self, part: Part):
        """Where Reference and Value are shown: (x, y, angle, justify) as seen on the page."""
        x0, y0, x1, y1 = part.body_box()
        sides = {p.outward for p in part.all_pins()}
        horizontal_only = sides <= {LEFT, RIGHT}
        vertical_only = sides <= {UP, DOWN}
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        if vertical_only:
            return [(x1 + 1.27, cy - 1.27, 0, ("left",)), (x1 + 1.27, cy + 1.27, 0, ("left",))]
        if horizontal_only and len(part.sym.pins) <= 3:
            return [(cx, y0 - 1.524, 0, ()), (cx, y1 + 1.524, 0, ())]
        if part.rot == 0 and part.mirror is None:
            out = []
            for name in ("Reference", "Value"):
                node = part.sym.prop_nodes[name]
                at = next(c for c in node if isinstance(c, list) and c[0] == "at")
                eff = next(c for c in node if isinstance(c, list) and c[0] == "effects")
                just = next((c for c in eff if isinstance(c, list) and c[0] == "justify"), None)
                dx, dy = transform(float(at[1]), float(at[2]), 0, None)
                out.append((part.at[0] + dx, part.at[1] + dy, int(float(at[3])),
                            tuple(str(j) for j in just[1:]) if just else ()))
            return out
        return [(x0, y0 - 1.524, 0, ("left",)), (x0, y1 + 1.524, 0, ("left",))]

    def symbol(self, part: Part, refs: list[tuple[tuple[str, ...], str]], value_visible=True,
               ref_visible=True) -> None:
        sym = self.use(part.lib_id)
        places = self._field_places(part)
        if part.ref_at:
            places[0] = (*part.ref_at, 0, ("left",))
        if part.value_at:
            places[1] = (*part.value_at, 0, ("left",))
        stored = [_stored_field(p[2], p[3], part.rot, part.mirror) for p in places]
        fa = [st[0] for st in stored]
        places = [(p[0], p[1], p[2], st[1]) for p, st in zip(places, stored)]
        node = [S("symbol"), [S("lib_id"), part.lib_id],
                [S("at"), fmt(part.at[0]), fmt(part.at[1]), S(str(part.rot))]]
        if part.mirror:
            node.append([S("mirror"), S(part.mirror)])
        node += [[S("unit"), S("1")], [S("body_style"), S("1")], [S("exclude_from_sim"), NO],
                 [S("in_bom"), YES if part.in_bom else NO], [S("on_board"), YES if part.on_board else NO],
                 [S("in_pos_files"), YES], [S("dnp"), YES if part.dnp else NO],
                 [S("uuid"), stable_uuid(self.d.name, self.s.name, "sym", part.ref)]]
        first_ref = refs[0][1] if refs else part.ref
        node.append(_prop("Reference", first_ref, places[0][0], places[0][1], fa[0], places[0][3],
                          hide=not ref_visible or part.virtual))
        node.append(_prop("Value", part.value, places[1][0], places[1][1], fa[1], places[1][3],
                          hide=not value_visible))
        node.append(_prop("Footprint", part.footprint, part.at[0], part.at[1], hide=True))
        node.append(_prop("Datasheet", sym.props.get("Datasheet", ""), part.at[0], part.at[1], hide=True))
        node.append(_prop("Description", sym.props.get("Description", ""), part.at[0], part.at[1], hide=True))
        for k, v in part.fields.items():
            node.append(_prop(k, v, part.at[0], part.at[1], hide=True))
        for pin in sym.pins:
            node.append([S("pin"), pin.number,
                         [S("uuid"), stable_uuid(self.d.name, self.s.name, "pin", part.ref, pin.number)]])
        paths = [[S("path"), "/" + "/".join((self.d.root_uuid,) + path),
                  [S("reference"), ref], [S("unit"), S("1")]] for path, ref in refs]
        node.append([S("instances"), [S("project"), self.d.name, *paths]])
        self.items.append(node)

    def power_symbol(self, net: Net, at, direction) -> None:
        sym = self.use(net.symbol)
        if not sym.is_power or len(sym.pins) != 1:
            raise DesignError(f"{net.symbol} is not a single-pin power symbol")
        x0, y0, x1, y1 = sym.bbox
        natural = "down" if (y0 + y1) / 2 < 0 else "up"
        rot = _POWER_ROT[natural][direction]
        self.n += 1
        part = Part(self.s, f"#PWR{self.n}", net.symbol, at, rot, None, net.name, "", {}, False,
                    None, None, True, True)
        refs = []
        for path, _offset, _names in self.contexts:
            self.pwr_counter[0] += 1
            refs.append((path, f"#PWR{self.pwr_counter[0]:04d}"))
        start = len(self.items)
        self.symbol(part, refs, ref_visible=False)
        # Value text sits beyond the symbol body, along the stub direction.
        vx, vy = _add(at, direction, 3.81 if direction in (UP, DOWN) else 3.556)
        want = {RIGHT: ("left",), LEFT: ("right",)}.get(direction, ())
        angle, just = _stored_field(0, want, rot, None)
        node = self.items[start]
        idx = next(i for i, c in enumerate(node)
                   if isinstance(c, list) and c[0] == "property" and c[1] == "Value")
        node[idx] = _prop("Value", net.name, vx, vy, angle, just)

    # ---- nets -------------------------------------------------------------
    def seg(self, a, b, net: Net) -> None:
        if a != b:
            self.segments.append((a, b, net))

    def label(self, net: Net, at, direction) -> None:
        angle = _LABEL_ANGLE[direction]
        just = ("left",) if angle in (0, 90) else ("right",)
        if net.kind == "local":
            self.items.append([S("label"), net.name, [S("at"), fmt(at[0]), fmt(at[1]), S(str(angle))],
                               _effects(just + ("bottom",)), [S("uuid"), self.uid("label")]])
        elif net.kind == "hier":
            self.items.append([S("hierarchical_label"), net.name, [S("shape"), S(net.direction)],
                               [S("at"), fmt(at[0]), fmt(at[1]), S(str(angle))],
                               _effects(just), [S("uuid"), self.uid("hlabel")]])
        elif net.kind == "global":
            self.items.append([S("global_label"), net.name, [S("shape"), S(net.direction)],
                               [S("at"), fmt(at[0]), fmt(at[1]), S(str(angle))],
                               _effects(just), [S("uuid"), self.uid("glabel")],
                               [S("property"), "Intersheetrefs", "${INTERSHEET_REFS}",
                                [S("at"), S("0"), S("0"), S("0")], [S("hide"), YES], _effects()]])
        elif net.kind == "power":
            self.power_symbol(net, at, direction)
        else:
            raise DesignError(f"cannot label anonymous net {net.name}")

    def attach(self, at, direction, net: Net, stub: float) -> None:
        end = _add(at, direction, stub)
        self.seg(at, end, net)
        self.label(net, end, direction)

    # ---- sheet symbols ----------------------------------------------------
    def sheet_symbol(self, inst, page_of) -> None:
        x, y = inst.at
        w, h = inst.size
        node = [S("sheet"), [S("at"), fmt(x), fmt(y)], [S("size"), fmt(w), fmt(h)],
                [S("exclude_from_sim"), NO], [S("in_bom"), YES], [S("on_board"), YES], [S("dnp"), NO],
                [S("stroke"), [S("width"), S("0.1524")], [S("type"), S("solid")]],
                [S("fill"), [S("color"), S("0"), S("0"), S("0"), S("0.0000")]],
                [S("uuid"), inst.uuid],
                _prop("Sheetname", inst.name, x, y - 0.7116, 0, ("left", "bottom")),
                _prop("Sheetfile", f"{inst.child.name}.kicad_sch", x, y + h + 0.5846, 0, ("left", "top"))]
        for port, pos, direction in inst.pin_layout():
            angle = 180 if direction == LEFT else 0
            node.append([S("pin"), port.name, S(port.direction),
                         [S("at"), fmt(pos[0]), fmt(pos[1]), S(str(angle))],
                         [S("uuid"), stable_uuid(self.d.name, self.s.name, "sheetpin", inst.name, port.name)],
                         _effects(("left",) if direction == LEFT else ("right",))])
            self.attach(pos, direction, inst.connect[port.name], inst.stub)
        paths = []
        for path, _offset, _names in self.contexts:
            full = (self.d.root_uuid,) + path
            paths.append([S("path"), "/" + "/".join(full), [S("page"), str(page_of[path + (inst.uuid,)])]])
        node.append([S("instances"), [S("project"), self.d.name, *paths]])
        self.items.append(node)

    # ---- wires ------------------------------------------------------------
    def emit_wires(self) -> None:
        points: set = set()
        for a, b, _ in self.segments:
            points.add(a)
            points.add(b)
        pin_net = {}
        for a in self.s.attachments:
            pin_net[a.pin.pos] = a.net
        segs = list(self.segments)
        # Split a wire where another wire end or a pin lands on its interior.
        changed = True
        while changed:
            changed = False
            for i, (a, b, net) in enumerate(segs):
                for p in sorted(points | set(self.pin_points)):
                    if p != a and p != b and _on_segment(p, a, b):
                        segs[i] = (a, p, net)
                        segs.append((p, b, net))
                        changed = True
                        break
                if changed:
                    break
        count: dict = {}
        for a, b, _net in segs:
            for p in (a, b):
                count[p] = count.get(p, 0) + 1
        for p in self.pin_points:
            if p in count:
                count[p] += 1
        seen = set()
        for a, b, _net in segs:
            key = (min(a, b), max(a, b))
            if key in seen:
                continue
            seen.add(key)
            self.items.append([S("wire"), [S("pts"), [S("xy"), fmt(a[0]), fmt(a[1])],
                                           [S("xy"), fmt(b[0]), fmt(b[1])]],
                               [S("stroke"), [S("width"), S("0")], [S("type"), S("default")]],
                               [S("uuid"), self.uid("wire", f"{key}")]])
        for p, n in sorted(count.items()):
            if n >= 3:
                self.items.append([S("junction"), [S("at"), fmt(p[0]), fmt(p[1])], [S("diameter"), S("0")],
                                   [S("color"), S("0"), S("0"), S("0"), S("0")],
                                   [S("uuid"), self.uid("junction", f"{p}")]])

    # ---- whole sheet ------------------------------------------------------
    def build(self, page_of) -> str:
        s = self.s
        for part in s.parts.values():
            refs = [(path, offset_ref(part.ref, offset)) for path, offset, _ in self.contexts]
            self.symbol(part, refs)
            for p in part.all_pins():
                other = self.pin_points.get(p.pos)
                if other is not None and other.part is not part:
                    raise DesignError(f"sheet {s.name}: pins {other} and {p} are at the same point {p.pos}")
                self.pin_points[p.pos] = p
        drawn: dict = {}
        for a in s.attachments:
            prev = drawn.get(a.pin.pos)
            if prev is None:
                drawn[a.pin.pos] = a.net
                self.attach(a.pin.pos, a.pin.outward, a.net, a.stub)
            elif prev is not a.net:
                raise DesignError(f"sheet {s.name}: pin {a.pin} is joined to both {prev.name} and {a.net.name}")
        for w in s.wires:
            for a, b in zip(w.points, w.points[1:]):
                self.seg(a, b, w.net)
        for net, at, direction in s.labels:
            self.label(net, at, direction)
        nc_done = set()
        for p in s.no_connects:
            if p.pos in nc_done:
                continue
            nc_done.add(p.pos)
            self.items.append([S("no_connect"), [S("at"), fmt(p.pos[0]), fmt(p.pos[1])],
                               [S("uuid"), self.uid("nc", repr(p))]])
        for inst in s.instances:
            self.sheet_symbol(inst, page_of)
        self.emit_wires()
        for text, at, size in s.texts:
            self.items.append([S("text"), text, [S("exclude_from_sim"), NO],
                               [S("at"), fmt(at[0]), fmt(at[1]), S("0")],
                               _effects(("left", "bottom"), size=size), [S("uuid"), self.uid("text", text)]])
        for x0, y0, x1, y1, title in s.frames:
            self.items.append([S("rectangle"), [S("start"), fmt(x0), fmt(y0)], [S("end"), fmt(x1), fmt(y1)],
                               [S("stroke"), [S("width"), S("0")], [S("type"), S("dash")]],
                               [S("fill"), [S("type"), S("none")]], [S("uuid"), self.uid("frame", title)]])
            self.items.append([S("text"), title, [S("exclude_from_sim"), NO],
                               [S("at"), fmt(x0 + 1.27), fmt(y0 + 3.2), S("0")],
                               _effects(("left", "bottom"), size=1.778), [S("uuid"), self.uid("frametitle", title)]])

        title = [S("title_block"), [S("title"), s.title]]
        if self.d.date:
            title.append([S("date"), self.d.date])
        title.append([S("rev"), self.d.rev])
        if self.d.company:
            title.append([S("company"), self.d.company])
        title.append([S("comment"), S("1"), self.d.title])
        libs = [S("lib_symbols")] + [symlib.load(i, self.d.symbol_dirs).node for i in sorted(self.lib_ids)]
        root = [S("kicad_sch"), [S("version"), S(SCH_VERSION)], [S("generator"), "hwflow"],
                [S("generator_version"), "10.0"],
                [S("uuid"), stable_uuid(self.d.name, s.name, "file")],
                [S("paper"), s.paper], title, libs, *self.items]
        if s is self.d.root:
            root.append([S("sheet_instances"), [S("path"), "/", [S("page"), "1"]]])
        root.append([S("embedded_fonts"), NO])
        return dumps(root) + "\n"


def _stored_field(angle: int, justify: tuple[str, ...], rot: int, mirror: str | None):
    """Field angle and justification to store so the text shows as asked.

    KiCad draws a field through its symbol's transform: the stored angle is
    relative to the symbol, and a left-justified field of a rotated or mirrored
    symbol can extend to the left. This returns the stored values for a field
    that must appear at ``angle`` (0 or 90) with ``justify`` on the page.
    """
    stored_angle = (angle - (90 if rot in (90, 270) else 0)) % 180
    horiz = [j for j in justify if j in ("left", "right")]
    if not horiz:
        return stored_angle, justify

    def direction(a, j):
        d = (1, 0) if a == 0 else (0, -1)
        return d if j == "left" else (-d[0], -d[1])

    want = direction(angle, horiz[0])
    for j in ("left", "right"):
        d = direction(stored_angle, j)
        shown = transform(d[0], -d[1], rot, mirror)
        if (int(shown[0]), int(shown[1])) == want:
            return stored_angle, tuple(j if x in ("left", "right") else x for x in justify)
    raise DesignError("no field justification gives the wanted text direction")


def _on_segment(p, a, b) -> bool:
    if abs(a[0] - b[0]) < 1e-6:
        return abs(p[0] - a[0]) < 1e-6 and min(a[1], b[1]) - 1e-6 <= p[1] <= max(a[1], b[1]) + 1e-6
    if abs(a[1] - b[1]) < 1e-6:
        return abs(p[1] - a[1]) < 1e-6 and min(a[0], b[0]) - 1e-6 <= p[0] <= max(a[0], b[0]) + 1e-6
    return False


def page_numbers(design: Design) -> dict[tuple[str, ...], int]:
    """Page number of every sheet instance, depth first in drawing order."""
    pages: dict[tuple[str, ...], int] = {(): 1}

    def walk(sheet: Sheet, path):
        for inst in sheet.instances:
            p = path + (inst.uuid,)
            pages[p] = len(pages) + 1
            walk(inst.child, p)

    walk(design.root, ())
    return pages


def write(design: Design, out_dir: Path) -> Flat:
    """Write every sheet of ``design`` into ``out_dir`` and return the flattened netlist."""
    flat = design.flatten()
    pages = page_numbers(design)
    out_dir.mkdir(parents=True, exist_ok=True)
    counter = [0]
    for sheet in design.sheets.values():
        if sheet.name not in flat.contexts:
            raise DesignError(f"sheet {sheet.name} is defined but never instantiated")
        text = _SheetWriter(design, sheet, flat, counter).build(pages)
        (out_dir / f"{sheet.name}.kicad_sch").write_text(text, encoding="utf-8")
    return flat
