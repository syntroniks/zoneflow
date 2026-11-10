# SPDX-License-Identifier: Apache-2.0
"""Circuit model: sheets, parts, nets and the hierarchy that joins them.

A design is written as Python functions that fill ``Sheet`` objects. The same
model is used twice: once to draw the KiCad schematic, and once to compute
the netlist the drawing is supposed to have (``Design.flatten``), so the two
can be compared after KiCad has read the files back.
"""

from __future__ import annotations

import math
import re
import uuid
from dataclasses import dataclass, field
from typing import Callable, Iterable

from . import symlib

GRID = 1.27
_NS = uuid.UUID("6f1d3c0e-5a0b-4a52-9a3c-2f8f9a7e0001")

# Unit vectors in sheet coordinates (x right, y down).
RIGHT, LEFT, UP, DOWN = (1, 0), (-1, 0), (0, -1), (0, 1)


class DesignError(Exception):
    pass


def stable_uuid(*parts: str) -> str:
    return str(uuid.uuid5(_NS, "/".join(parts)))


def on_grid(v: float, what: str) -> float:
    n = round(v / GRID)
    if abs(n * GRID - v) > 1e-6:
        raise DesignError(f"{what}: {v} is not on the {GRID} mm grid")
    return round(n * GRID, 4)


def transform(px: float, py: float, rot: int, mirror: str | None) -> tuple[float, float]:
    """Map a library point (y up) to an offset in sheet coordinates (y down).

    Matches KiCad 10: the symbol is rotated counter-clockwise, then mirrored in
    the sheet frame. ``mirror`` is the file keyword: ``"x"`` flips top/bottom,
    ``"y"`` flips left/right. Checked against KiCad's own netlist in
    ``tests/test_kicad_roundtrip.py``.
    """
    x, y = px, -py
    r = rot % 360
    if r == 90:
        x, y = y, -x
    elif r == 180:
        x, y = -x, -y
    elif r == 270:
        x, y = -y, x
    elif r != 0:
        raise DesignError(f"rotation must be 0, 90, 180 or 270, not {rot}")
    if mirror == "x":
        y = -y
    elif mirror == "y":
        x = -x
    elif mirror is not None:
        raise DesignError(f"mirror must be None, 'x' or 'y', not {mirror!r}")
    return (round(x, 4), round(y, 4))


class Net:
    """A net inside one sheet definition.

    kind: ``local`` (net label), ``hier`` (hierarchical label, a sheet port),
    ``global`` (global label), ``power`` (power symbol) or ``anon`` (wires only).
    """

    def __init__(self, sheet, kind, name, direction="passive", netclass=None, symbol=None):
        self.sheet = sheet
        self.kind = kind
        self.name = name
        self.direction = direction
        self.netclass = netclass
        self.symbol = symbol  # lib_id of the power symbol, for kind == "power"
        self.side = None  # preferred side of the sheet symbol, for kind == "hier"

    def __repr__(self):
        return f"<Net {self.kind} {self.name}>"


@dataclass(frozen=True)
class PinRef:
    part: "Part"
    pin: symlib.LibPin

    @property
    def pos(self) -> tuple[float, float]:
        dx, dy = transform(self.pin.x, self.pin.y, self.part.rot, self.part.mirror)
        return (round(self.part.at[0] + dx, 4), round(self.part.at[1] + dy, 4))

    @property
    def outward(self) -> tuple[int, int]:
        """Direction a wire leaves the pin, in sheet coordinates."""
        a = math.radians(self.pin.angle)
        dx, dy = transform(-math.cos(a), -math.sin(a), self.part.rot, self.part.mirror)
        return (int(round(dx)), int(round(dy)))

    def __repr__(self):
        return f"{self.part.ref}.{self.pin.number}"


class Part:
    def __init__(self, sheet, ref, lib_id, at, rot, mirror, value, footprint, fields, dnp,
                 ref_at, value_at, on_board, in_bom):
        self.sheet = sheet
        self.ref = ref
        self.lib_id = lib_id
        self.sym = symlib.load(lib_id, sheet.design.symbol_dirs)
        self.at = (on_grid(at[0], f"{ref} x"), on_grid(at[1], f"{ref} y"))
        self.rot = rot % 360
        self.mirror = mirror
        self.value = value if value is not None else self.sym.props.get("Value", "")
        self.footprint = footprint if footprint is not None else self.sym.props.get("Footprint", "")
        self.fields = dict(fields or {})
        self.dnp = dnp
        self.ref_at = ref_at
        self.value_at = value_at
        self.on_board = on_board
        self.in_bom = in_bom
        transform(0, 0, self.rot, self.mirror)  # validates rot

    @property
    def virtual(self) -> bool:
        return self.ref.startswith("#")

    def pins(self, key) -> list[PinRef]:
        found = self.sym.pins_named(str(key))
        if not found:
            names = sorted({p.name for p in self.sym.pins} | {p.number for p in self.sym.pins})
            raise DesignError(f"{self.ref} ({self.lib_id}) has no pin {key!r}; it has {names}")
        return [PinRef(self, p) for p in found]

    def __getitem__(self, key) -> PinRef:
        found = self.pins(key)
        if len({p.pos for p in found}) > 1:
            nums = [p.pin.number for p in found]
            raise DesignError(
                f"{self.ref} has {len(found)} pins named {key!r} at different places (numbers {nums}); "
                f"use a pin number or part.pins({key!r})"
            )
        return found[0]

    def all_pins(self) -> list[PinRef]:
        return [PinRef(self, p) for p in self.sym.pins]

    def body_box(self) -> tuple[float, float, float, float]:
        """Body outline in sheet coordinates: x0, y0, x1, y1."""
        x0, y0, x1, y1 = self.sym.bbox
        pts = [transform(x, y, self.rot, self.mirror) for x in (x0, x1) for y in (y0, y1)]
        xs = [self.at[0] + p[0] for p in pts]
        ys = [self.at[1] + p[1] for p in pts]
        return (min(xs), min(ys), max(xs), max(ys))


@dataclass
class Attachment:
    pin: PinRef
    net: Net
    stub: float


@dataclass
class Wire:
    points: list[tuple[float, float]]
    net: Net
    ends: list = field(default_factory=list)  # the pins this wire starts or ends on


@dataclass
class SheetInstance:
    parent: "Sheet"
    child: "Sheet"
    name: str
    at: tuple[float, float]
    size: tuple[float, float]
    connect: dict[str, Net]
    ref_offset: int
    stub: float

    @property
    def uuid(self) -> str:
        return stable_uuid(self.parent.design.name, self.parent.name, "sheet", self.name)

    def pin_layout(self) -> list[tuple[Net, tuple[float, float], tuple[int, int]]]:
        """Port, position and outward direction of every sheet pin."""
        left = [p for p in self.child.ports() if p.side == "left"]
        right = [p for p in self.child.ports() if p.side == "right"]
        out = []
        for i, p in enumerate(left):
            out.append((p, (self.at[0], round(self.at[1] + 2.54 * (i + 1), 4)), LEFT))
        for i, p in enumerate(right):
            out.append((p, (round(self.at[0] + self.size[0], 4), round(self.at[1] + 2.54 * (i + 1), 4)), RIGHT))
        return out


class Sheet:
    """One schematic file. It can be instantiated more than once."""

    def __init__(self, design: "Design", name: str, title: str, paper: str = "A4"):
        self.design = design
        self.name = name
        self.title = title
        self.paper = paper
        self.parts: dict[str, Part] = {}
        self.nets: dict[tuple[str, str], Net] = {}
        self.attachments: list[Attachment] = []
        self.wires: list[Wire] = []
        self.labels: list[tuple[Net, tuple[float, float], tuple[int, int]]] = []
        self.no_connects: list[PinRef] = []
        self.instances: list[SheetInstance] = []
        self.texts: list[tuple[str, tuple[float, float], float]] = []
        self.frames: list[tuple[float, float, float, float, str]] = []
        self._anon = 0

    # ---- nets -------------------------------------------------------------
    def _net(self, kind, name, **kw) -> Net:
        key = (kind, name)
        if key not in self.nets:
            for (k, n) in self.nets:
                if n == name and k != kind and "anon" not in (k, kind):
                    raise DesignError(f"sheet {self.name}: net name {name!r} is used as both {k} and {kind}")
            self.nets[key] = Net(self, kind, name, **kw)
        return self.nets[key]

    def net(self, name: str, netclass: str | None = None) -> Net:
        """A net named by local labels in this sheet."""
        return self._net("local", name, netclass=netclass)

    def port(self, name: str, direction: str = "passive", side: str | None = None,
             netclass: str | None = None) -> Net:
        """A net that leaves this sheet through a hierarchical label."""
        n = self._net("hier", name, direction=direction, netclass=netclass)
        n.side = side or ("right" if direction == "output" else "left")
        return n

    def global_net(self, name: str, netclass: str | None = None) -> Net:
        return self._net("global", name, netclass=netclass)

    def power(self, name: str, symbol: str | None = None, netclass: str | None = None) -> Net:
        """A global net drawn with a power symbol (``power:<name>`` unless given)."""
        return self._net("power", name, symbol=symbol or f"power:{name}", netclass=netclass)

    def ports(self) -> list[Net]:
        return [n for n in self.nets.values() if n.kind == "hier"]

    # ---- parts ------------------------------------------------------------
    def part(self, ref: str, lib_id: str, at, rot: int = 0, mirror: str | None = None,
             value: str | None = None, footprint: str | None = None, lcsc: str | None = None,
             fields: dict | None = None, dnp: bool = False, ref_at=None, value_at=None,
             on_board: bool = True, in_bom: bool = True) -> Part:
        if ref in self.parts:
            raise DesignError(f"sheet {self.name}: duplicate reference {ref}")
        f = dict(fields or {})
        if lcsc is not None:
            f["LCSC"] = lcsc
        p = Part(self, ref, lib_id, at, rot, mirror, value, footprint, f, dnp, ref_at, value_at,
                 on_board, in_bom)
        self.parts[ref] = p
        return p

    # ---- connections ------------------------------------------------------
    def connect(self, net: Net, *pins, stub: float = 2.54) -> Net:
        """Join pins to ``net`` with a short wire and a label or power symbol."""
        if net.sheet is not self:
            raise DesignError(f"net {net.name} belongs to sheet {net.sheet.name}, not {self.name}")
        for p in _flat(pins):
            self.attachments.append(Attachment(p, net, on_grid(stub, "stub")))
        return net

    def wire(self, a, b, *, route: str = "auto", net: Net | None = None) -> Net:
        """Draw a wire from ``a`` to ``b`` (pins or (x, y) points).

        ``route``: ``"hv"`` leaves ``a`` horizontally, ``"vh"`` vertically; ``"auto"``
        requires the two ends to share an x or y coordinate. Without ``net`` the
        wire is an unnamed net that joins only the pins at its two ends. With
        ``net`` every pin the wire ends on joins that net; a wire that ends on
        another wire of the same net is joined to it with a junction.
        """
        pa, pb = _point(a), _point(b)
        if route == "auto":
            if abs(pa[0] - pb[0]) > 1e-6 and abs(pa[1] - pb[1]) > 1e-6:
                raise DesignError(f"wire {a} -> {b} is not straight; give route='hv' or 'vh'")
            pts = [pa, pb]
        elif route == "hv":
            pts = [pa, (pb[0], pa[1]), pb]
        elif route == "vh":
            pts = [pa, (pa[0], pb[1]), pb]
        else:
            raise DesignError(f"unknown route {route!r}")
        pts = [p for i, p in enumerate(pts) if i == 0 or p != pts[i - 1]]
        if net is None:
            self._anon += 1
            net = self._net("anon", f"_w{self._anon}")
        self.wires.append(Wire(pts, net, [x for x in (a, b) if isinstance(x, PinRef)]))
        return net

    def label(self, net: Net, at, direction=RIGHT) -> None:
        """Put the label (or power symbol) of ``net`` at a point on a wire of that net.

        ``direction`` is where the text or power symbol extends from the point.
        """
        if net.kind == "anon":
            raise DesignError("an unnamed wire cannot carry a label; pass net= to wire()")
        self.labels.append((net, _point(at), direction))

    def nc(self, *pins) -> None:
        """Mark pins as deliberately unconnected."""
        self.no_connects += list(_flat(pins))

    def nc_rest(self, part: Part) -> None:
        """Mark every pin of ``part`` not used so far as unconnected."""
        used = {(id(a.pin.part), a.pin.pos) for a in self.attachments}
        used |= {(id(p.part), p.pos) for w in self.wires for p in w.ends}
        used |= {(id(p.part), p.pos) for p in self.no_connects}
        for p in part.all_pins():
            if (id(part), p.pos) not in used:
                self.no_connects.append(p)

    # ---- hierarchy and decoration -----------------------------------------
    def instantiate(self, child: "Sheet", name: str, at, connect: dict[str, Net],
                    ref_offset: int = 0, width: float = 25.4, stub: float = 2.54) -> SheetInstance:
        ports = {p.name for p in child.ports()}
        if set(connect) != ports:
            raise DesignError(
                f"sheet instance {name}: connect has {sorted(connect)}, sheet {child.name} has ports {sorted(ports)}"
            )
        nleft = sum(1 for p in child.ports() if p.side == "left")
        nright = len(ports) - nleft
        height = 2.54 * (max(nleft, nright) + 1)
        inst = SheetInstance(self, child, name, (on_grid(at[0], "sheet x"), on_grid(at[1], "sheet y")),
                             (on_grid(width, "sheet width"), height), dict(connect), ref_offset,
                             on_grid(stub, "stub"))
        self.instances.append(inst)
        return inst

    def text(self, s: str, at, size: float = 1.27) -> None:
        self.texts.append((s, (at[0], at[1]), size))

    def frame(self, x0, y0, x1, y1, title: str) -> None:
        """A dashed box with a title, to group a functional block."""
        self.frames.append((x0, y0, x1, y1, title))


def _flat(items) -> Iterable[PinRef]:
    for it in items:
        if isinstance(it, PinRef):
            yield it
        elif isinstance(it, (list, tuple)):
            yield from _flat(it)
        else:
            raise DesignError(f"expected a pin, got {it!r}")


def _point(x) -> tuple[float, float]:
    if isinstance(x, PinRef):
        return x.pos
    return (on_grid(x[0], "wire x"), on_grid(x[1], "wire y"))


_REF_RE = re.compile(r"^(#?[A-Za-z_]+)(\d+)$")


def offset_ref(ref: str, offset: int) -> str:
    m = _REF_RE.match(ref)
    if not m:
        raise DesignError(f"reference {ref!r} must be letters followed by a number")
    return f"{m.group(1)}{int(m.group(2)) + offset}"


@dataclass
class FlatPart:
    ref: str
    part: Part
    path: tuple[str, ...]  # sheet-instance UUIDs below the root
    sheet_path: str  # human-readable, e.g. "/CH1/"

    @property
    def sym_uuid(self) -> str:
        return stable_uuid(self.part.sheet.design.name, self.part.sheet.name, "sym", self.part.ref)


@dataclass
class FlatNet:
    nodes: set[tuple[str, str]]  # (reference, pin number)
    names: set[str]
    netclass: str | None = None


@dataclass
class Flat:
    parts: list[FlatPart] = field(default_factory=list)
    nets: list[FlatNet] = field(default_factory=list)
    no_connects: set[tuple[str, str]] = field(default_factory=set)
    # (sheet name, instance path) -> list of sheet-instance contexts, for the writer
    contexts: dict[str, list[tuple[tuple[str, ...], int, str]]] = field(default_factory=dict)


class Design:
    def __init__(self, name: str, title: str, rev: str = "0", date: str = "", company: str = "",
                 symbol_dirs: tuple[str, ...] = ()):
        self.name = name
        self.title = title
        self.rev = rev
        self.date = date
        self.company = company
        self.symbol_dirs = tuple(symbol_dirs)
        self.sheets: dict[str, Sheet] = {}
        self.netclass_priority: list[str] = []
        self.root = self.sheet(name, title)

    def sheet(self, name: str, title: str, paper: str = "A4",
              build: Callable[[Sheet], None] | None = None) -> Sheet:
        if name in self.sheets:
            raise DesignError(f"duplicate sheet {name}")
        s = Sheet(self, name, title, paper)
        self.sheets[name] = s
        if build:
            build(s)
        return s

    @property
    def root_uuid(self) -> str:
        return stable_uuid(self.name, self.root.name, "file")

    def flatten(self) -> Flat:
        """Resolve the hierarchy into parts and nets with final references."""
        flat = Flat()
        parent: dict = {}

        def find(k):
            parent.setdefault(k, k)
            while parent[k] != k:
                parent[k] = parent[parent[k]]
                k = parent[k]
            return k

        def union(a, b):
            ra, rb = find(a), find(b)
            if ra != rb:
                parent[rb] = ra

        net_meta: dict = {}

        def walk(sheet: Sheet, path: tuple[str, ...], names: str, offset: int, bind: dict[str, object]):
            flat.contexts.setdefault(sheet.name, []).append((path, offset, names))

            def key(net: Net):
                if net.kind in ("power", "global"):
                    k = ("G", net.name)
                elif net.kind == "hier":
                    if sheet is self.root:
                        raise DesignError("the root sheet cannot have ports")
                    k = bind[net.name]
                else:
                    k = ("L", path, net.kind, net.name)
                meta = net_meta.setdefault(find(k), {"names": set(), "classes": set()})
                if net.kind != "anon":
                    meta["names"].add(net.name)
                if net.netclass:
                    meta["classes"].add(net.netclass)
                return k

            for part in sheet.parts.values():
                ref = offset_ref(part.ref, offset)
                flat.parts.append(FlatPart(ref, part, path, names))
                # Pins a symbol stacks on one point are one node in KiCad.
                stacked: dict = {}
                for pr in part.all_pins():
                    stacked.setdefault(pr.pos, []).append(pr)
                for group in stacked.values():
                    for other in group[1:]:
                        union(("P", ref, group[0].pin.number), ("P", ref, other.pin.number))

            def node(p: PinRef):
                return ("P", offset_ref(p.part.ref, offset), p.pin.number)

            for a in sheet.attachments:
                union(key(a.net), node(a.pin))
            for w in sheet.wires:
                k = key(w.net)
                for p in w.ends:
                    union(k, node(p))
            for p in sheet.no_connects:
                if not p.part.virtual:
                    for q in p.part.all_pins():
                        if q.pos == p.pos:
                            flat.no_connects.add((offset_ref(q.part.ref, offset), q.pin.number))
            for inst in sheet.instances:
                child_bind = {port: key(net) for port, net in inst.connect.items()}
                walk(inst.child, path + (inst.uuid,), f"{names}{inst.name}/", offset + inst.ref_offset,
                     child_bind)

        walk(self.root, (), "/", 0, {})

        refs = [p.ref for p in flat.parts]
        dup = sorted({r for r in refs if refs.count(r) > 1})
        if dup:
            raise DesignError(f"duplicate references after applying sheet offsets: {dup}")

        groups: dict = {}
        metas: dict = {}
        for k in list(parent):
            root = find(k)
            if k[0] == "P":
                if not k[1].startswith("#"):
                    groups.setdefault(root, set()).add((k[1], k[2]))
            m = net_meta.get(k)
            if m:
                agg = metas.setdefault(root, {"names": set(), "classes": set()})
                agg["names"] |= m["names"]
                agg["classes"] |= m["classes"]
        for root, nodes in groups.items():
            meta = metas.get(root, {"names": set(), "classes": set()})
            cls = None
            if meta["classes"]:
                order = self.netclass_priority
                unknown = meta["classes"] - set(order)
                if unknown:
                    raise DesignError(f"net class {sorted(unknown)} is not listed in netclass_priority")
                cls = min(meta["classes"], key=order.index)
            flat.nets.append(FlatNet(nodes, meta["names"], cls))

        # Every pin of every real part must be connected or marked unconnected.
        connected = {n for net in flat.nets for n in net.nodes}
        missing = []
        for fp in flat.parts:
            if fp.part.virtual:
                continue
            for pin in fp.part.sym.pins:
                node = (fp.ref, pin.number)
                if node in connected and node in flat.no_connects:
                    raise DesignError(f"{fp.ref} pin {pin.number} is both connected and marked nc")
                if node not in connected and node not in flat.no_connects:
                    missing.append(f"{fp.ref}.{pin.number}({pin.name})")
        if missing:
            raise DesignError("pins neither connected nor marked nc: " + ", ".join(missing))
        return flat
