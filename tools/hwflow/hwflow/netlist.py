# SPDX-License-Identifier: Apache-2.0
"""Read a KiCad netlist (``kicad-cli sch export netlist``) and compare it with the model."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

from . import sexpr
from .sexpr import find, find_all


@dataclass
class NetlistComp:
    ref: str
    value: str
    footprint: str
    fields: dict[str, str]
    sheet_names: str
    sheet_tstamps: str
    tstamp: str
    lib: str
    part: str
    dnp: bool = False


@dataclass
class Netlist:
    comps: dict[str, NetlistComp] = field(default_factory=dict)
    nets: dict[str, set[tuple[str, str]]] = field(default_factory=dict)
    pin_types: dict[tuple[str, str], str] = field(default_factory=dict)


def read(path: Path) -> Netlist:
    root = sexpr.loads(Path(path).read_text(encoding="utf-8"))
    nl = Netlist()
    for c in find_all(find(root, "components"), "comp"):
        ref = find(c, "ref")[1]
        fields = {}
        fnode = find(c, "fields")
        if fnode:
            for f in find_all(fnode, "field"):
                name = find(f, "name")[1]
                fields[name] = f[2] if len(f) > 2 and not isinstance(f[2], list) else ""
        props = {p[1][1]: (p[2][1] if len(p) > 2 else "") for p in find_all(c, "property")}
        sp = find(c, "sheetpath")
        ls = find(c, "libsource")
        fp = find(c, "footprint")
        nl.comps[ref] = NetlistComp(
            ref=ref,
            value=find(c, "value")[1],
            footprint=fp[1] if fp else "",
            fields=fields,
            sheet_names=find(sp, "names")[1],
            sheet_tstamps=find(sp, "tstamps")[1],
            tstamp=find(c, "tstamps")[1],
            lib=find(ls, "lib")[1],
            part=find(ls, "part")[1],
            dnp="dnp" in props,
        )
    for n in find_all(find(root, "nets"), "net"):
        name = find(n, "name")[1]
        nodes = set()
        for node in find_all(n, "node"):
            key = (find(node, "ref")[1], find(node, "pin")[1])
            nodes.add(key)
            t = find(node, "pintype")
            if t:
                nl.pin_types[key] = t[1]
        nl.nets[name] = nodes
    return nl


def compare(flat, nl: Netlist) -> list[str]:
    """Differences between the model's nets and the nets KiCad extracted. Empty means equal."""
    problems: list[str] = []
    want_refs = {p.ref for p in flat.parts if not p.part.virtual and p.part.on_board}
    got_refs = set(nl.comps)
    for r in sorted(want_refs - got_refs):
        problems.append(f"part {r} is in the design but not in KiCad's netlist")
    for r in sorted(got_refs - want_refs):
        problems.append(f"part {r} is in KiCad's netlist but not in the design")

    want = {frozenset(n.nodes) for n in flat.nets if len(n.nodes) >= 2}
    got = {frozenset(nodes) for nodes in nl.nets.values() if len(nodes) >= 2}
    got_names = {frozenset(nodes): name for name, nodes in nl.nets.items()}
    for net in sorted(want - got, key=sorted):
        hits = sorted({got_names[g] for g in got if g & net})
        problems.append(f"design net {sorted(net)} is not one net in KiCad (it touches {hits})")
    for net in sorted(got - want, key=sorted):
        problems.append(f"KiCad net {got_names[net]} {sorted(net)} does not match any design net")
    single_want = {next(iter(n.nodes)) for n in flat.nets if len(n.nodes) == 1}
    for nodes in nl.nets.values():
        if len(nodes) == 1:
            node = next(iter(nodes))
            if node not in flat.no_connects and node not in single_want:
                problems.append(f"{node[0]} pin {node[1]} is unconnected in KiCad but connected in the design")
    return problems


def net_classes(flat, nl: Netlist) -> dict[str, str]:
    """KiCad net name -> net class, for nets the design gave a class."""
    by_nodes = {frozenset(nodes): name for name, nodes in nl.nets.items()}
    out = {}
    for n in flat.nets:
        if n.netclass and frozenset(n.nodes) in by_nodes:
            out[by_nodes[frozenset(n.nodes)]] = n.netclass
    return out
