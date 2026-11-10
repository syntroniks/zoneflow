# SPDX-License-Identifier: Apache-2.0
"""Draw a schematic whose connectivity comes from a design netlist.

``hardware/netlist.yaml`` already says which part every reference is and which
net every pin is on. A circuit description that uses this module does not
repeat that. It says only where each symbol goes and which connections are
drawn as wires:

    def keepalive(s):                       # s is a CapSheet
        r29 = s.place("R29", at=(50.8, 76.2), rot=90)
        c22 = s.place("C22", at=(63.5, 76.2), rot=90)
        s.wire(r29[2], c22[1])              # refused unless both pins are on one net
        ...
        # every pin not wired gets a short stub with its net's label or power symbol

The symbol, footprint, displayed value and pin numbers come from the part
table (``design/parts.toml``, read by ``hwflow.parity``). The net kind is
worked out here: a rail listed in ``power`` gets a power symbol, a net that
reaches another sheet becomes a hierarchical port, anything else a local
label. The label text is always the design net's own name, so KiCad's net
names match the design netlist.

A sheet that is used several times (one output channel, nine instances) is
drawn from its first instance. The others are checked to be the same circuit,
part for part and net for net, before anything is written.
"""

from __future__ import annotations

import math
from typing import Callable

from . import parity, symlib
from .design import DOWN, LEFT, RIGHT, UP, Design, DesignError, Net, Part, PinRef, Sheet, offset_ref, transform

__all__ = ["Capture", "CapSheet", "UP", "DOWN", "LEFT", "RIGHT"]


class Capture:
    def __init__(self, design: Design, components: list[dict], parts: dict[str, parity.PartSpec],
                 bom: dict[str, dict] | None = None, power: dict[str, str] | None = None,
                 netclass: Callable[[str], str | None] | None = None):
        self.d = design
        self.components = components
        self.comps = {c["ref"]: c for c in components}
        self.parts = parts
        self.bom = bom or {}
        self.power = dict(power or {})  # design net -> power symbol
        self.netclass = netclass or (lambda net: None)
        self.exp = parity.expected(components, parts)
        self.where: dict[tuple[str, int], tuple[str, str | None]] = {}  # (ref, unit) -> (sheet, instance)
        self.templates: dict[str, dict[str, int]] = {}  # sheet -> {instance name: reference offset}
        self.drivers: dict[str, str] = {}  # design net -> sheet that drives it (port direction)
        self.sides: dict[tuple[str, str], str] = {}  # (sheet, design net) -> "left" | "right"
        self.capsheets: dict[str, CapSheet] = {}
        self.extra_refs: set[str] = set()  # parts that are not in the design netlist (mounting holes)
        self._scopes: dict[str, set[tuple[str, str | None]]] | None = None
        self._placed: set[tuple[str, int]] = set()

    # ---- parts ------------------------------------------------------------
    def spec(self, ref: str) -> parity.PartSpec:
        if ref not in self.exp.comps:
            raise DesignError(f"{ref} is not in the design netlist")
        return self.exp.comps[ref]["spec"]

    def symbol(self, ref: str) -> symlib.LibSymbol:
        return symlib.load(self.spec(ref).symbol, self.d.symbol_dirs)

    def units(self, ref: str) -> tuple[int, ...]:
        return self.symbol(ref).units

    # ---- which sheet holds which part ------------------------------------
    def assign(self, sheet: str, refs) -> None:
        """Put parts on a sheet. ``"U6"`` means every unit, ``("U6", 4)`` one unit."""
        for item in refs:
            ref, units = (item, None) if isinstance(item, str) else (item[0], [item[1]])
            for unit in units or self.units(ref):
                if (ref, unit) in self.where:
                    raise DesignError(f"{ref} unit {unit} is assigned to {self.where[(ref, unit)][0]} and {sheet}")
                self.where[(ref, unit)] = (sheet, None)
        self._scopes = None

    def template(self, sheet: str, instances: dict[str, int], first_refs) -> None:
        """A sheet drawn once and used for several instances.

        ``instances`` maps the instance name to its reference offset, first
        instance first. ``first_refs`` are the references of the first
        instance. Instance ``Z3`` with offset 300 holds the same parts with
        200 added to each number, on nets with ``Z3`` where the first has ``Z1``.
        """
        names = list(instances)
        base = instances[names[0]]
        for name, off in instances.items():
            for ref in first_refs:
                r = offset_ref(ref, off - base)
                for unit in self.units(r):
                    if (r, unit) in self.where:
                        raise DesignError(f"{r} is assigned twice")
                    self.where[(r, unit)] = (sheet, name)
        self.templates[sheet] = dict(instances)
        self._first_refs = getattr(self, "_first_refs", {})
        self._first_refs[sheet] = list(first_refs)
        self._scopes = None

    def _pad_unit(self, ref: str, pad: str) -> int:
        sym = self.symbol(ref)
        for p in sym.pins:
            if p.number == pad:
                return p.unit if p.unit else sym.units[0]
        raise DesignError(f"{ref} ({self.spec(ref).symbol}) has no pin {pad}; the part table maps a design pin to it")

    def scopes(self, net: str) -> set[tuple[str, str | None]]:
        """The sheet instances a design net has pins in."""
        if self._scopes is None:
            missing = sorted(set(self.exp.comps) - {r for r, _ in self.where})
            if missing:
                raise DesignError("parts of the design netlist that are on no sheet: " + ", ".join(missing))
            table: dict[str, set] = {}
            for (ref, pad), n in self.exp.pin_net.items():
                if n != parity.NC:
                    table.setdefault(n, set()).add(self.where[(ref, self._pad_unit(ref, pad))])
            self._scopes = table
            self._check_templates()
        return self._scopes.get(net, set())

    def local_name(self, sheet: str, instance: str | None, net: str) -> str:
        """Label text for a design net inside a sheet instance."""
        if instance is None or net in self.power:
            return net
        token = f"_{instance}"
        return net.replace(token, "", 1) if token in net else net

    def _check_templates(self) -> None:
        """Every instance of a template sheet must be the first instance's circuit."""
        for sheet, instances in self.templates.items():
            names = list(instances)
            first, base = names[0], instances[names[0]]
            refs = self._first_refs[sheet]
            for name in names:
                off = instances[name] - base
                for ref in refs:
                    a, b = self.comps[ref], self.comps[offset_ref(ref, off)]
                    if parity.part_key(a) != parity.part_key(b) or a["value"] != b["value"]:
                        raise DesignError(f"sheet {sheet}: {b['ref']} is not the same part as {ref}")
                    if list(a["pins"]) != list(b["pins"]):
                        raise DesignError(f"sheet {sheet}: {b['ref']} and {ref} list different pins")
                    for key in a["pins"]:
                        la = self.local_name(sheet, first, a["pins"][key])
                        lb = self.local_name(sheet, name, b["pins"][key])
                        if la != lb:
                            raise DesignError(
                                f"sheet {sheet}: {b['ref']} pin {key} is on {b['pins'][key]}, which does not "
                                f"correspond to {a['pins'][key]} on {ref}")
                # A net that keeps its name in every instance must really be shared.
                for ref in refs:
                    for net in self.comps[offset_ref(ref, off)]["pins"].values():
                        if net == parity.NC or net in self.power:
                            continue
                        if f"_{name}" not in net and self._scopes[net] == {(sheet, name)}:
                            raise DesignError(f"sheet {sheet}: net {net} is only in instance {name} "
                                              f"and does not carry its name")

    # ---- sheets -----------------------------------------------------------
    def sheet(self, name: str, title: str, paper: str = "A4",
              build: Callable[["CapSheet"], None] | None = None) -> "CapSheet":
        s = self.d.sheet(name, title, paper)
        cs = CapSheet(self, s)
        self.capsheets[name] = cs
        if build:
            build(cs)
            cs.finish()
        return cs

    def root(self) -> "CapSheet":
        if self.d.root.name not in self.capsheets:
            self.capsheets[self.d.root.name] = CapSheet(self, self.d.root)
        return self.capsheets[self.d.root.name]

    def instantiate(self, sheet: str, name: str, at, width: float = 38.1, stub: float = 5.08):
        """Place a sheet symbol on the root sheet and join its pins to the design nets by label."""
        cs = self.capsheets[sheet]
        root = self.d.root
        if sheet in self.templates:
            if name not in self.templates[sheet]:
                raise DesignError(f"{name} is not an instance of {sheet}")
            offset = self.templates[sheet][name]  # the sheet's own references are F1, R1...: F1 + 300 is F301
            first = next(iter(self.templates[sheet]))
        else:
            offset, first = 0, None
        connect = {}
        for port in cs.s.ports():
            design_net = cs.port_net[port.name]
            if first is not None and f"_{first}" in design_net:
                design_net = design_net.replace(f"_{first}", f"_{name}", 1)
            connect[port.name] = root.net(design_net, netclass=self.netclass(design_net))
        return root.instantiate(cs.s, name, at, connect, ref_offset=offset, width=width, stub=stub)

    def finish(self) -> None:
        """Fail unless every unit of every part of the design netlist has been drawn."""
        want = set(self.where)
        missing = sorted(want - self._placed)
        if missing:
            names = sorted({r if len(self.units(r)) == 1 else f"{r} unit {u}" for r, u in missing})
            raise DesignError("in the design netlist and not placed on its sheet: " + ", ".join(names))


class CapSheet:
    """One sheet being drawn. ``s`` is the underlying ``hwflow.design.Sheet``."""

    def __init__(self, cap: Capture, sheet: Sheet):
        self.cap = cap
        self.s = sheet
        self.name = sheet.name
        t = cap.templates.get(sheet.name)
        self.instance = next(iter(t)) if t else None  # the instance this sheet is drawn from
        self.pin_net: dict[tuple[int, str], str] = {}  # (id(part), pin number) -> design net
        self.drawn: set[tuple[int, tuple[float, float]]] = set()  # (id(part), pin position)
        self.port_net: dict[str, str] = {}  # port name -> design net (of the first instance)
        self.parts: dict[tuple[str, int], Part] = {}
        self.pin_at: dict[tuple[float, float], PinRef] = {}  # where each placed pin ends
        self._flags = 0
        self.default_stub = 2.54

    # ---- nets -------------------------------------------------------------
    def net(self, design_net: str) -> Net:
        cap = self.cap
        if design_net in cap.power:
            return self.s.power(design_net, symbol=cap.power[design_net], netclass=cap.netclass(design_net))
        scopes = cap.scopes(design_net)
        here = (self.name, self.instance)
        if not scopes:
            raise DesignError(f"sheet {self.name}: {design_net} is not a net of the design netlist")
        if here not in scopes and self.s is not cap.d.root:
            raise DesignError(f"sheet {self.name}: net {design_net} has no pin on this sheet")
        local = cap.local_name(self.name, self.instance, design_net)
        if scopes == {here} or self.s is cap.d.root:
            return self.s.net(local, netclass=cap.netclass(design_net))
        driver = cap.drivers.get(design_net)
        direction = "passive" if driver is None else ("output" if driver == self.name else "input")
        side = cap.sides.get((self.name, design_net))
        self.port_net[local] = design_net
        return self.s.port(local, direction, side=side, netclass=cap.netclass(design_net))

    def ports(self, *design_nets: str) -> None:
        """Declare ports in the order the sheet symbol should list them."""
        for n in design_nets:
            if self.net(n).kind != "hier":
                raise DesignError(f"sheet {self.name}: {n} does not leave the sheet, so it is not a port")

    def net_of(self, pin: PinRef) -> str:
        return self.pin_net[(id(pin.part), pin.pin.number)]

    # ---- parts ------------------------------------------------------------
    def place(self, ref: str, at, rot: int = 0, mirror: str | None = None, unit: int = 1,
              ref_at=None, value_at=None) -> Part:
        cap = self.cap
        c = cap.comps.get(ref)
        if c is None:
            raise DesignError(f"{ref} is not in the design netlist")
        where = cap.where.get((ref, unit))
        if where is None or where != (self.name, self.instance):
            raise DesignError(f"{ref} unit {unit} belongs on {where}, not on sheet {self.name}")
        spec = cap.spec(ref)
        local = ref
        if self.instance is not None:  # drawn from the first instance: F101 is F1 here
            local = offset_ref(ref, -cap.templates[self.name][self.instance])
        fields = {"Spec": c["value"]}
        row = cap.bom.get(c["lcsc"] or "")
        if row:
            fields["MPN"] = row["mpn"]
            fields["Manufacturer"] = row["manufacturer"]
        fields.update(spec.fields)
        part = self.s.part(local, spec.symbol, at=at, rot=rot, mirror=mirror, value=spec.value,
                           footprint=spec.footprint, lcsc=c["lcsc"] or "", fields=fields,
                           dnp=c["variant"] == "dnf", ref_at=ref_at, value_at=value_at,
                           on_board=spec.on_board, in_bom=spec.in_bom and c["variant"] != "dnf", unit=unit)
        nc_at = set()
        for p in part.all_pins():
            net = cap.exp.pin_net.get((ref, p.pin.number))
            if net is None:
                if not spec.on_board:
                    continue
                raise DesignError(f"{ref} ({spec.symbol}) pin {p.pin.number} ({p.pin.name}) has no net: the design "
                                  f"netlist does not list it and the part table does not mark it unconnected")
            self.pin_net[(id(part), p.pin.number)] = net
            if net == parity.NC and p.pos not in nc_at:
                nc_at.add(p.pos)
                self.s.nc(p)
                self.drawn.add((id(part), p.pos))
        # Pins a symbol stacks on one point must be on one net.
        by_pos: dict = {}
        for p in part.all_pins():
            n = self.pin_net.get((id(part), p.pin.number))
            if by_pos.setdefault(p.pos, n) != n:
                raise DesignError(f"{ref}: pins stacked at one point are on {by_pos[p.pos]} and {n}")
        self.parts[(ref, unit)] = part
        for p in part.all_pins():
            other = self.pin_at.get(p.pos)
            if other is not None and other.part is not part:
                if self.pin_net.get((id(other.part), other.pin.number)) != self.pin_net.get((id(part), p.pin.number)):
                    raise DesignError(f"sheet {self.name}: {ref} pin {p.pin.number} lands on {other}, a different net")
            else:
                self.pin_at[p.pos] = p
        if self.instance is None:
            cap._placed.add((ref, unit))
        else:
            for off in cap.templates[self.name].values():
                cap._placed.add((offset_ref(local, off), unit))
        return part

    def place_pin(self, ref: str, pin, at, rot: int = 0, mirror: str | None = None, unit: int = 1,
                  ref_at=None, value_at=None) -> Part:
        """Place a part so that its pin ``pin`` (number or name) lands on the point or pin ``at``."""
        sym = self.cap.symbol(ref)
        found = sym.pins_named(str(pin), unit)
        if len({(p.x, p.y) for p in found}) != 1:
            raise DesignError(f"{ref} has no single pin {pin!r} in unit {unit}")
        dx, dy = transform(found[0].x, found[0].y, rot, mirror)
        x, y = at.pos if isinstance(at, PinRef) else at
        return self.place(ref, (round(x - dx, 4), round(y - dy, 4)), rot, mirror, unit, ref_at, value_at)

    def attach(self, ref: str, pin, at, towards, mirror: str | None = None, ref_at=None, value_at=None) -> Part:
        """Place a two-terminal part with ``pin`` on ``at``, its body extending in direction ``towards``.

        ``at`` is a point or another part's pin; a pin is not wired to automatically,
        but a part attached straight onto a pin of the same net is connected by touching it.
        """
        sym = self.cap.symbol(ref)
        found = sym.pins_named(str(pin), 1)
        if not found:
            raise DesignError(f"{ref} has no pin {pin!r}")
        want = (-towards[0], -towards[1])  # the anchored pin points back, away from the body
        for rot in (0, 90, 180, 270):
            a = math.radians(found[0].angle)
            ox, oy = transform(-math.cos(a), -math.sin(a), rot, mirror)
            if (int(round(ox)), int(round(oy))) == want:
                part = self.place_pin(ref, pin, at, rot, mirror, 1, ref_at, value_at)
                if isinstance(at, PinRef):
                    if self.net_of(at) != self.net_of(part[pin]):
                        raise DesignError(f"sheet {self.name}: {ref} pin {pin} ({self.net_of(part[pin])}) is put on "
                                          f"{at} ({self.net_of(at)})")
                return part
        raise DesignError(f"{ref}: pin {pin} cannot face that way")

    def extra(self, ref: str, lib_id: str, at, value: str, footprint: str, rot: int = 0, **kw) -> Part:
        """A part the board needs that is not in the design netlist, such as a mounting hole."""
        if ref in self.cap.comps:
            raise DesignError(f"{ref} is in the design netlist; use place()")
        self.cap.extra_refs.add(ref)
        return self.s.part(ref, lib_id, at=at, rot=rot, value=value, footprint=footprint, in_bom=False, **kw)

    # ---- connections ------------------------------------------------------
    def _mark(self, pin: PinRef) -> None:
        self.drawn.add((id(pin.part), pin.pos))

    def wire(self, a, b, route: str = "auto", net: str | None = None, label=None) -> None:
        """A wire between two pins or points. All pins on it must be on the same design net.

        ``label`` puts the net's label on the wire: a point, or a distance in
        millimetres from ``a`` along the first leg.
        """
        # A wire that ends on a pin joins that pin, whether the end was given as the pin or as its position.
        a, b = (self.pin_at.get(_pt(x), x) if not isinstance(x, PinRef) else x for x in (a, b))
        nets = {self.net_of(x) for x in (a, b) if isinstance(x, PinRef)}
        if net is not None:
            nets.add(net)
        if not nets:
            found = {self._net_at(x) for x in (a, b)} - {None}
            if len(found) != 1:
                raise DesignError(f"sheet {self.name}: wire {a} -> {b} touches no pin and no wire; give net=")
            nets = found
        if len(nets) > 1:
            raise DesignError(f"sheet {self.name}: wire {a} -> {b} would join {sorted(nets)}")
        name = next(iter(nets))
        if name == parity.NC:
            raise DesignError(f"sheet {self.name}: {a} or {b} must stay unconnected")
        self.s.wire(a, b, route=route, net=self.net(name))
        for x in (a, b):
            if isinstance(x, PinRef):
                self._mark(x)
        if label is not None:
            if isinstance(label, (int, float)):
                pts = self.s.wires[-1].points
                (x0, y0), (x1, y1) = pts[0], pts[1]
                n = max(abs(x1 - x0), abs(y1 - y0)) or 1.0
                label = (round(x0 + (x1 - x0) / n * label, 4), round(y0 + (y1 - y0) / n * label, 4))
            self.s.label(self.net(name), label, RIGHT)

    def path(self, *points, net: str | None = None) -> None:
        """A wire through several pins or points, each leg straight."""
        for a, b in zip(points, points[1:]):
            self.wire(a, b, net=net or self._net_of_any(points))

    def _net_of_any(self, points) -> str | None:
        for x in points:
            if isinstance(x, PinRef):
                return self.net_of(x)
        return None

    def _net_at(self, point) -> str | None:
        """Design net of the drawn wire that passes through ``point``."""
        if isinstance(point, PinRef):
            return self.net_of(point)
        x, y = _pt(point)
        for w in self.s.wires:
            for (x0, y0), (x1, y1) in zip(w.points, w.points[1:]):
                if (abs(x0 - x1) < 1e-6 and abs(x - x0) < 1e-6 and min(y0, y1) - 1e-6 <= y <= max(y0, y1) + 1e-6) or \
                   (abs(y0 - y1) < 1e-6 and abs(y - y0) < 1e-6 and min(x0, x1) - 1e-6 <= x <= max(x0, x1) + 1e-6):
                    return self._design_name(w.net)
        return None

    def _design_name(self, net: Net) -> str:
        if net.kind == "hier":
            return self.port_net[net.name]
        if net.kind == "power" or self.instance is None:
            return net.name
        # A local label of a template sheet: put the instance token back.
        for (_, _), design in self.pin_net.items():
            if self.cap.local_name(self.name, self.instance, design) == net.name:
                return design
        return net.name

    def stub(self, *pins, length: float | None = None) -> None:
        """A short wire from each pin to its net's label, port or power symbol."""
        for p in _flat(pins):
            net = self.net_of(p)
            if net == parity.NC:
                raise DesignError(f"sheet {self.name}: {p} must stay unconnected")
            self.s.connect(self.net(net), p, stub=self.default_stub if length is None else length)
            self._mark(p)

    def label(self, at, direction=RIGHT, net: str | None = None) -> None:
        """The label, port or power symbol of the net that the wire at ``at`` belongs to."""
        name = net or self._net_at(at)
        if name is None:
            raise DesignError(f"sheet {self.name}: no wire passes through {at}; give net=")
        self.s.label(self.net(name), at, direction)

    def flag(self, at, rot: int = 0) -> None:
        """A PWR_FLAG with its pin on the wire at ``at``: this net is fed through a passive part."""
        self._flags += 1
        self.s.part(f"#FLG{self._flags}", "power:PWR_FLAG", at=at, rot=rot)

    def finish(self) -> None:
        """Give every pin that was not wired a stub and its label, then check the sheet is complete."""
        for part in list(self.parts.values()):
            for p in part.all_pins():
                if (id(part), p.pos) in self.drawn or (id(part), p.pin.number) not in self.pin_net:
                    continue
                # A pin that sits on a wire, or on another part's pin, is connected by touching it.
                touching = self._net_at(p.pos)
                other = self.pin_at.get(p.pos)
                if touching is None and other is not None and other.part is not part:
                    touching = self.net_of(other)
                if touching is not None:
                    if touching != self.net_of(p):
                        raise DesignError(f"sheet {self.name}: {p} ({self.net_of(p)}) touches a wire of {touching}")
                    self.s.wire(p, p, net=self.net(touching))
                    self._mark(p)
                    continue
                self.stub(p)
        here = (self.name, self.instance)
        want = {k for k, v in self.cap.where.items() if v == here}
        missing = sorted(want - set(self.parts))
        if missing:
            raise DesignError(f"sheet {self.name}: not placed: " + ", ".join(f"{r}" for r, _ in missing))

    # ---- decoration, passed through --------------------------------------
    def frame(self, x0, y0, x1, y1, title: str) -> None:
        self.s.frame(x0, y0, x1, y1, title)

    def note(self, text: str, at, size: float = 1.27, bold: bool = False) -> None:
        self.s.note(text, at, size, bold)

    def text(self, text: str, at, size: float = 1.27, **kw) -> None:
        self.s.text(text, at, size, **kw)

    def line(self, points, style: str = "dash") -> None:
        self.s.line(points, style)


def _pt(point) -> tuple[float, float]:
    return (round(point[0], 4), round(point[1], 4))


def _flat(items):
    for it in items:
        if isinstance(it, PinRef):
            yield it
        elif isinstance(it, (list, tuple)):
            yield from _flat(it)
        else:
            raise DesignError(f"expected a pin, got {it!r}")
