# SPDX-License-Identifier: Apache-2.0
"""Read symbols from KiCad ``.kicad_sym`` libraries.

Only the wanted symbol is parsed (the stock libraries are several megabytes).
A derived symbol (``extends``) is flattened the way KiCad flattens it when it
embeds the symbol in a schematic, so the embedded copy matches the library.
"""

from __future__ import annotations

import copy
import os
import re
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path

from . import sexpr
from .sexpr import Sym, find, find_all

DEFAULT_SYMBOL_DIR = "/usr/share/kicad/symbols"


def symbol_dir() -> Path:
    return Path(os.environ.get("KICAD10_SYMBOL_DIR", DEFAULT_SYMBOL_DIR))


@dataclass(frozen=True)
class LibPin:
    number: str
    name: str
    x: float  # connection point, symbol coordinates (y up)
    y: float
    angle: int  # direction from the connection point towards the body
    length: float
    etype: str
    unit: int
    hidden: bool


@dataclass
class LibSymbol:
    lib_id: str
    node: list  # flattened (symbol "Lib:Name" ...) ready to embed
    pins: list[LibPin]
    bbox: tuple[float, float, float, float]  # body graphics only, y up: x0,y0,x1,y1
    is_power: bool
    props: dict[str, str]
    prop_nodes: dict[str, list]

    def pins_named(self, key: str) -> list[LibPin]:
        by_number = [p for p in self.pins if p.number == key]
        if by_number:
            return by_number
        return [p for p in self.pins if p.name == key]


@lru_cache(maxsize=None)
def _lib_text(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def _extract_block(text: str, name: str) -> str | None:
    m = re.search(r'^\t\(symbol "' + re.escape(name) + r'"\s*$', text, re.M)
    if not m:
        return None
    i = m.start() + 1
    depth = 0
    in_str = False
    j = i
    n = len(text)
    while j < n:
        c = text[j]
        if in_str:
            if c == "\\":
                j += 1
            elif c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return text[i : j + 1]
        j += 1
    raise ValueError(f"unterminated symbol {name}")


def _raw_symbol(lib_file: Path, name: str) -> list:
    block = _extract_block(_lib_text(str(lib_file)), name)
    if block is None:
        raise KeyError(f"symbol {name!r} not found in {lib_file}")
    return sexpr.loads(block)


def _flatten(lib_file: Path, name: str) -> list:
    node = _raw_symbol(lib_file, name)
    ext = find(node, "extends")
    if ext is None:
        return node
    parent_name = ext[1]
    parent = copy.deepcopy(_flatten(lib_file, parent_name))
    out: list = [Sym("symbol"), name]
    child_props = {p[1]: p for p in find_all(node, "property")}
    seen = set()
    for item in parent[2:]:
        if isinstance(item, list) and item[0] == "property":
            if item[1] in child_props:
                out.append(child_props[item[1]])
                seen.add(item[1])
            else:
                out.append(item)
        elif isinstance(item, list) and item[0] == "symbol":
            unit = copy.deepcopy(item)
            assert unit[1].startswith(parent_name + "_")
            unit[1] = name + unit[1][len(parent_name) :]
            out.append(unit)
        else:
            out.append(item)
    # Properties the child adds that the parent does not have go after the
    # parent's last property, which is where KiCad puts them.
    extra = [p for k, p in child_props.items() if k not in seen]
    if extra:
        last = max(i for i, it in enumerate(out) if isinstance(it, list) and it[0] == "property")
        out[last + 1 : last + 1] = extra
    return out


def _f(a) -> float:
    return float(a)


def _graphic_points(item: list) -> list[tuple[float, float]]:
    kind = item[0]
    pts: list[tuple[float, float]] = []
    if kind == "rectangle":
        for k in ("start", "end"):
            p = find(item, k)
            pts.append((_f(p[1]), _f(p[2])))
    elif kind in ("polyline", "bezier"):
        for xy in find_all(find(item, "pts"), "xy"):
            pts.append((_f(xy[1]), _f(xy[2])))
    elif kind == "circle":
        c = find(item, "center")
        r = _f(find(item, "radius")[1])
        cx, cy = _f(c[1]), _f(c[2])
        pts += [(cx - r, cy - r), (cx + r, cy + r)]
    elif kind == "arc":
        for k in ("start", "mid", "end"):
            p = find(item, k)
            if p is not None:
                pts.append((_f(p[1]), _f(p[2])))
    return pts


@lru_cache(maxsize=None)
def load(lib_id: str, extra_dirs: tuple[str, ...] = ()) -> LibSymbol:
    """Load ``Library:Symbol``. ``extra_dirs`` are searched before the stock libraries."""
    lib, name = lib_id.split(":", 1)
    lib_file = None
    for d in [*extra_dirs, str(symbol_dir())]:
        cand = Path(d) / f"{lib}.kicad_sym"
        if cand.exists():
            lib_file = cand
            break
    if lib_file is None:
        raise FileNotFoundError(f"symbol library {lib!r} not found")
    node = _flatten(lib_file, name)
    node = copy.deepcopy(node)
    node[1] = lib_id

    pins: list[LibPin] = []
    pts: list[tuple[float, float]] = []
    for unit in find_all(node, "symbol"):
        m = re.match(r".*_(\d+)_(\d+)$", unit[1])
        unit_no, style = int(m.group(1)), int(m.group(2))
        if style > 1:
            continue  # alternate (De Morgan) body style is not used
        for item in unit[2:]:
            if not isinstance(item, list):
                continue
            if item[0] == "pin":
                at = find(item, "at")
                hide = find(item, "hide")
                pins.append(
                    LibPin(
                        number=find(item, "number")[1],
                        name=find(item, "name")[1],
                        x=_f(at[1]),
                        y=_f(at[2]),
                        angle=int(float(at[3])) if len(at) > 3 else 0,
                        length=_f(find(item, "length")[1]),
                        etype=str(item[1]),
                        unit=unit_no,
                        hidden=hide is not None and hide[1] == "yes",
                    )
                )
            else:
                pts += _graphic_points(item)
    if not pts:
        pts = [(p.x, p.y) for p in pins] or [(0.0, 0.0)]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    prop_nodes = {p[1]: p for p in find_all(node, "property")}
    return LibSymbol(
        lib_id=lib_id,
        node=node,
        pins=pins,
        bbox=(min(xs), min(ys), max(xs), max(ys)),
        is_power=find(node, "power") is not None,
        props={k: v[2] for k, v in prop_nodes.items()},
        prop_nodes=prop_nodes,
    )
