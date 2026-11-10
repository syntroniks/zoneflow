# SPDX-License-Identifier: Apache-2.0
"""Compare a KiCad netlist with a design netlist (``hardware/netlist.yaml``), pin for pin.

The design netlist names every component, its part and the net on every pin,
but not KiCad's symbol, footprint or pad numbers. The part table
(``design/parts.toml``) supplies those, one entry per part number. This module
joins the two into the netlist the schematic must have, and compares that
with what ``kicad-cli sch export netlist`` extracted from the generated files.

Only the standard library is used. The caller parses the YAML.
"""

from __future__ import annotations

import csv
import re
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

NC = "NC"


class ParityError(Exception):
    pass


@dataclass
class PartSpec:
    key: str  # LCSC number, or "value|package" for a part without one
    symbol: str
    footprint: str
    value: str  # what the schematic shows as Value
    package: str
    pins: dict[str, list[str]]  # design pin key -> symbol pin numbers
    unconnected: list[str] = field(default_factory=list)  # symbol pins the design netlist does not list
    on_board: bool = True
    in_bom: bool = True
    fields: dict[str, str] = field(default_factory=dict)

    def pin_numbers(self, key: str) -> list[str]:
        if key in self.pins:
            return self.pins[key]
        # "3 EN/UVLO" or "3": the leading token is the pin number.
        head = key.split()[0]
        if re.fullmatch(r"[0-9]+", head):
            return [head]
        raise ParityError(f"part {self.key}: no symbol pin is given for design pin {key!r}")


def part_key(comp: dict) -> str:
    return comp["lcsc"] or f"{comp['value']}|{comp['package']}"


def load_parts(path: Path) -> dict[str, PartSpec]:
    with open(path, "rb") as fh:
        raw = tomllib.load(fh)
    out = {}
    for key, t in raw.get("part", {}).items():
        pins = {k: ([v] if isinstance(v, str) else list(v)) for k, v in t.get("pins", {}).items()}
        out[key] = PartSpec(
            key=key, symbol=t["symbol"], footprint=t.get("footprint", ""), value=t["value"],
            package=t["package"], pins=pins, unconnected=list(t.get("unconnected", [])),
            on_board=t.get("on_board", True), in_bom=t.get("in_bom", True),
            fields=dict(t.get("fields", {})),
        )
    return out


def load_bom(path: Path) -> dict[str, dict]:
    """Rows of ``bom-preliminary.csv`` by LCSC number (maker and part number for the symbol fields)."""
    with open(path, newline="", encoding="utf-8") as fh:
        return {r["lcsc"]: r for r in csv.DictReader(fh) if r["lcsc"]}


@dataclass
class Expected:
    """What the schematic must contain, derived from the design netlist and the part table."""

    comps: dict[str, dict] = field(default_factory=dict)  # ref -> {spec, value, lcsc, variant, comp}
    nets: dict[str, set[tuple[str, str]]] = field(default_factory=dict)  # design net -> {(ref, pad)}
    no_connect: set[tuple[str, str]] = field(default_factory=set)
    pin_net: dict[tuple[str, str], str] = field(default_factory=dict)  # (ref, pad) -> design net or NC


def expected(components: list[dict], parts: dict[str, PartSpec]) -> Expected:
    exp = Expected()
    for c in components:
        key = part_key(c)
        if key not in parts:
            raise ParityError(f"{c['ref']}: part {key!r} is not in the part table")
        spec = parts[key]
        if spec.package != c["package"]:
            raise ParityError(f"{c['ref']}: the netlist says package {c['package']!r}, the part table "
                              f"{spec.package!r} for {key}")
        exp.comps[c["ref"]] = {"spec": spec, "comp": c}
        if not spec.on_board:
            continue
        for pin_key, net in c["pins"].items():
            for number in spec.pin_numbers(pin_key):
                node = (c["ref"], number)
                if node in exp.pin_net and exp.pin_net[node] != net:
                    raise ParityError(f"{c['ref']} pin {number} is given two nets: {exp.pin_net[node]} and {net}")
                exp.pin_net[node] = net
                if net == NC:
                    exp.no_connect.add(node)
                else:
                    exp.nets.setdefault(net, set()).add(node)
        for number in spec.unconnected:
            node = (c["ref"], number)
            if node in exp.pin_net:
                raise ParityError(f"{c['ref']} pin {number} is both listed in the netlist and marked unconnected")
            exp.pin_net[node] = NC
            exp.no_connect.add(node)
    return exp


def short_name(kicad_net: str) -> str:
    """``/Fail-safe/KA_N2`` -> ``KA_N2``. A net name itself never contains a slash."""
    return kicad_net.rsplit("/", 1)[-1]


def stands_for(kicad_net: str, design_net: str) -> bool:
    """Whether a KiCad net name is an acceptable name for a design net.

    The label must be the design name. Inside a sheet that is used more than
    once the label cannot carry the instance, so ``/Z3/T2`` stands for
    ``T2_Z3`` and ``/MV/ZD_N`` for ``ZD_MV_N``: the sheet instance name is
    the token the design name carries.
    """
    short = short_name(kicad_net)
    if short == design_net:
        return True
    path = kicad_net.split("/")[1:-1]
    return bool(path) and f"_{path[-1]}" in design_net and design_net.replace(f"_{path[-1]}", "", 1) == short


def compare(exp: Expected, nl, extra_refs: set[str] = frozenset()) -> list[str]:
    """Differences between the expected netlist and KiCad's. Empty means equal.

    ``nl`` is a ``hwflow.netlist.Netlist``. ``extra_refs`` are parts the board
    needs and the design netlist does not list (mounting holes).
    """
    problems: list[str] = []
    want = {r for r, e in exp.comps.items() if e["spec"].on_board}
    got = set(nl.comps) - set(extra_refs)
    for r in sorted(want - got):
        problems.append(f"{r} is in the design netlist and not in the schematic")
    for r in sorted(got - want):
        problems.append(f"{r} is in the schematic and not in the design netlist")
    for r in sorted(set(extra_refs) - set(nl.comps)):
        problems.append(f"{r} is expected as a mechanical part and is not in the schematic")

    for ref in sorted(want & got):
        e, k = exp.comps[ref], nl.comps[ref]
        spec, c = e["spec"], e["comp"]
        checks = [
            ("value", k.value, spec.value),
            ("footprint", k.footprint, spec.footprint),
            ("LCSC", k.fields.get("LCSC", ""), c["lcsc"] or ""),
            ("Spec", k.fields.get("Spec", ""), c["value"]),
        ]
        for what, have, need in checks:
            if have != need:
                problems.append(f"{ref} {what}: the schematic has {have!r}, the design netlist needs {need!r}")
        if k.dnp != (c["variant"] == "dnf"):
            problems.append(f"{ref}: DNP is {k.dnp} in the schematic, the design netlist says variant {c['variant']}")

    # Net by net, as KiCad sees them: every pad on it belongs to one design net,
    # the net carries that design net's name, and no design net is in two pieces.
    seen_nodes: set[tuple[str, str]] = set()
    pieces: dict[str, list[str]] = {}
    for name, nodes in sorted(nl.nets.items()):
        real = sorted(n for n in nodes if n[0] not in extra_refs)
        seen_nodes.update(real)
        unknown = [n for n in real if n not in exp.pin_net and n[0] in want]
        for n in unknown:
            problems.append(f"{n[0]} pin {n[1]} (net {name}) is in the schematic and not in the design netlist")
        nets = {exp.pin_net[n] for n in real if n in exp.pin_net}
        if nets == {NC} or not nets:
            if len(real) > 1:
                problems.append(f"KiCad net {name} joins pins that must be unconnected: {real}")
            continue
        if NC in nets:
            nc = [n for n in real if exp.pin_net.get(n) == NC]
            problems.append(f"KiCad net {name}: {nc} must be unconnected")
            nets.discard(NC)
        if len(nets) > 1:
            problems.append(f"KiCad net {name} joins design nets {sorted(nets)}")
            continue
        design = next(iter(nets))
        pieces.setdefault(design, []).append(name)
        if not stands_for(name, design):
            problems.append(f"KiCad net {name} carries design net {design}: label it {design}")
    for design, names in sorted(pieces.items()):
        if len(names) > 1:
            problems.append(f"design net {design} is in {len(names)} pieces in the schematic: {names}")
    for node, net in sorted(exp.pin_net.items()):
        if node[0] in nl.comps and node not in seen_nodes and net != NC:
            problems.append(f"{node[0]} pin {node[1]} is not in the schematic netlist; it should be on {net}")
    for design in sorted(set(exp.nets) - set(pieces)):
        if any(n[0] in nl.comps for n in exp.nets[design]):
            problems.append(f"design net {design} is not in the schematic")
    return problems


def compare_bom(exp: Expected, rows: list[dict], fitted: set[str], what: str) -> list[str]:
    """Check one of KiCad's BOM exports: which parts are fitted, and their part numbers.

    ``rows`` is an ungrouped ``kicad-cli sch export bom --exclude-dnp`` with
    the columns Reference, Value, Footprint, LCSC and Spec. ``fitted`` is the
    set of design-netlist variants this build fits (``{"all", "8"}`` for the
    8-zone build). This is also the only check on parts that have no
    footprint, such as the display panel, which are not in a netlist.
    """
    problems: list[str] = []
    want = {r: e for r, e in exp.comps.items() if e["comp"]["variant"] in fitted and e["spec"].in_bom}
    got = {r["Reference"]: r for r in rows}
    for r in sorted(set(want) - set(got)):
        problems.append(f"{what}: {r} should be fitted and is not in KiCad's BOM")
    for r in sorted(set(got) - set(want)):
        problems.append(f"{what}: {r} is in KiCad's BOM and should not be fitted")
    for ref in sorted(set(want) & set(got)):
        spec, c, row = want[ref]["spec"], want[ref]["comp"], got[ref]
        for col, need in (("Value", spec.value), ("Footprint", spec.footprint), ("LCSC", c["lcsc"] or ""),
                          ("Spec", c["value"])):
            if row[col] != need:
                problems.append(f"{what}: {ref} {col} is {row[col]!r} in KiCad's BOM, expected {need!r}")
    return problems


def normalise_netlist(text: str) -> str:
    """KiCad's netlist with the build machine's path and the export time taken out.

    The result is what is committed, so that it does not change from one
    machine, one build of KiCad or one minute to the next.
    """
    text = re.sub(r'\(source "[^"]*[/\\]([^"/\\]+)"\)', r'(source "\1")', text)
    text = re.sub(r'\(date "[^"]*"\)', '(date "")', text, count=1)
    # A multi-unit part lists the id of each of its units. Two builds of one
    # KiCad release give them in a different order; the set is what matters.
    text = re.sub(r'\(tstamps((?:\s+"[0-9a-f-]{36}"){2,})\s*\)',
                  lambda m: "(tstamps " + " ".join(sorted(m.group(1).split())) + ")", text)
    return text
