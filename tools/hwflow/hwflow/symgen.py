# SPDX-License-Identifier: Apache-2.0
"""Write a project-local KiCad symbol library from a description in Python.

Three kinds of symbol are enough for a board's own library:

- ``box``: an IC or module drawn as a rectangle, pins listed per side in the
  order they should appear, grouped by function and not by pin number.
- ``derive``: a copy of a stock KiCad symbol under a new name, with pin
  numbers, unit order or pin types changed. Used where the stock drawing is
  right and the pinning or naming is not.
- ``power``: a power symbol for a rail the stock ``power`` library does not have.

All pin ends land on the 2.54 mm grid when the symbol is placed on it.
"""

from __future__ import annotations

import copy
import math
from pathlib import Path

from . import symlib
from .sexpr import Sym, dumps, find, find_all, fmt

S = Sym
YES, NO = Sym("yes"), Sym("no")
P = 2.54
LIB_VERSION = "20251024"  # written by KiCad 10.0.8


def _font(size: float = 1.27, justify: str | None = None) -> list:
    e = [S("effects"), [S("font"), [S("size"), fmt(size), fmt(size)]]]
    if justify:
        e.append([S("justify"), S(justify)])
    return e


def prop(name: str, value: str, x: float = 0, y: float = 0, hide: bool = False, justify: str | None = None) -> list:
    node = [S("property"), name, value, [S("at"), fmt(x), fmt(y), S("0")], [S("show_name"), NO],
            [S("do_not_autoplace"), NO]]
    if hide:
        node.append([S("hide"), YES])
    node.append(_font(justify=justify))
    return node


def pin(etype: str, number: str, name: str, x: float, y: float, angle: int, length: float = P,
        shape: str = "line", hide: bool = False) -> list:
    """A pin. (x, y) is its connection point; ``angle`` points from there towards the body."""
    node = [S("pin"), S(etype), S(shape), [S("at"), fmt(x), fmt(y), S(str(angle))], [S("length"), fmt(length)]]
    if hide:
        node.append([S("hide"), YES])
    node += [[S("name"), name, _font()], [S("number"), number, _font()]]
    return node


def rect(x0: float, y0: float, x1: float, y1: float, fill: str = "background", width: float = 0.254) -> list:
    return [S("rectangle"), [S("start"), fmt(x0), fmt(y0)], [S("end"), fmt(x1), fmt(y1)],
            [S("stroke"), [S("width"), fmt(width)], [S("type"), S("default")]], [S("fill"), [S("type"), S(fill)]]]


def polyline(points, width: float = 0, fill: str = "none") -> list:
    return [S("polyline"), [S("pts"), *[[S("xy"), fmt(x), fmt(y)] for x, y in points]],
            [S("stroke"), [S("width"), fmt(width)], [S("type"), S("default")]], [S("fill"), [S("type"), S(fill)]]]


def text(s: str, x: float, y: float, size: float = 1.27) -> list:
    return [S("text"), s, [S("at"), fmt(x), fmt(y), S("0")], _font(size)]


def symbol(name: str, ref: str, value: str, units: dict[tuple[int, int], list], footprint: str = "",
           datasheet: str = "", description: str = "", ref_at=(0, 0), value_at=(0, 0), power: bool = False,
           hide_pin_numbers: bool = False, hide_pin_names: bool = False, pin_name_offset: float | None = None,
           justify: str | None = "left", in_bom: bool = True, on_board: bool = True) -> list:
    """``units`` maps (unit, body style) to that sub-symbol's graphics and pins; unit 0 is drawn in every unit."""
    node: list = [S("symbol"), name]
    if power:
        node.append([S("power"), S("global")])
    if hide_pin_numbers:
        node.append([S("pin_numbers"), [S("hide"), YES]])
    if hide_pin_names or pin_name_offset is not None:
        pn = [S("pin_names")]
        if pin_name_offset is not None:
            pn.append([S("offset"), fmt(pin_name_offset)])
        if hide_pin_names:
            pn.append([S("hide"), YES])
        node.append(pn)
    node += [[S("exclude_from_sim"), NO], [S("in_bom"), YES if in_bom else NO],
             [S("on_board"), YES if on_board else NO], [S("in_pos_files"), YES],
             [S("duplicate_pin_numbers_are_jumpers"), NO]]
    node.append(prop("Reference", ref, ref_at[0], ref_at[1], hide=power, justify=None if power else justify))
    node.append(prop("Value", value, value_at[0], value_at[1], justify=None if power else justify))
    node.append(prop("Footprint", footprint, hide=True))
    node.append(prop("Datasheet", datasheet, hide=True))
    node.append(prop("Description", description, hide=True))
    for (unit, style), items in sorted(units.items()):
        node.append([S("symbol"), f"{name}_{unit}_{style}", *items])
    node.append([S("embedded_fonts"), NO])
    return node


def _name_len(side) -> float:
    """Longest pin name on a side, in millimetres of 1.27 mm text (overbar markup not counted)."""
    longest = 0
    for p in side:
        if p is not None:
            longest = max(longest, len(p[1].replace("~{", "").replace("}", "")))
    return longest * 1.05


def box(name: str, ref: str, value: str, left=(), right=(), top=(), bottom=(), footprint: str = "",
        datasheet: str = "", description: str = "", min_width: float = 0.0, title: str | None = None) -> list:
    """A rectangular symbol. Each side is a list of ``(number, pin name, electrical type)`` or ``None`` for a gap.

    Left and right pins are listed top to bottom, top and bottom pins left to
    right. ``number`` may be a list: the pins are then drawn side by side (top
    and bottom) or one under the other (left and right), all visible.
    """
    def expand(side):
        out = []
        for p in side:
            if p is None:
                out.append(None)
            elif isinstance(p[0], (list, tuple)):
                out += [(n, p[1], p[2]) for n in p[0]]
            else:
                out.append(p)
        return out

    left, right, top, bottom = (expand(s) for s in (left, right, top, bottom))
    rows = max(len(left), len(right), 1)
    cols = max(len(top), len(bottom))
    need = _name_len(left) + _name_len(right) + 5.0
    need = max(need, (cols + 1) * P, min_width)
    half_w = math.ceil(need / 2 / P) * P
    y_top = (rows // 2) * P
    y_bot = y_top - (rows - 1) * P
    # Room for the names of top and bottom pins, which are drawn rotated inside the body.
    y0 = y_top + P + (math.ceil(_name_len(top) / P) * P if top else 0)
    y1 = y_bot - P - (math.ceil(_name_len(bottom) / P) * P if bottom else 0)
    graphics = [rect(-half_w, y0, half_w, y1)]
    if title:
        graphics.append(text(title, 0, (y0 + y1) / 2 if not (left or right) else y0 - 1.9, 1.27))
    pins = []
    for i, p in enumerate(left):
        if p:
            pins.append(pin(p[2], p[0], p[1], -half_w - P, y_top - i * P, 0))
    for i, p in enumerate(right):
        if p:
            pins.append(pin(p[2], p[0], p[1], half_w + P, y_top - i * P, 180))
    for side, edge, sign, angle in ((top, y0, 1, 270), (bottom, y1, -1, 90)):
        x_first = -((len(side) - 1) // 2) * P
        # A pin number is printed along its pin. Three characters need a longer pin than 2.54 mm.
        length = 2 * P if any(p and len(p[0]) > 2 for p in side) else P
        for i, p in enumerate(side):
            if p:
                pins.append(pin(p[2], p[0], p[1], x_first + i * P, edge + sign * length, angle, length=length))
    # Reference and value sit above the top-left corner, clear of any pins on the bottom edge.
    return symbol(name, ref, value, {(0, 1): graphics, (1, 1): pins}, footprint, datasheet, description,
                  ref_at=(-half_w, y0 + 3.81), value_at=(-half_w, y0 + 1.27))


def derive(lib_id: str, name: str, value: str | None = None, footprint: str | None = None,
           datasheet: str | None = None, description: str | None = None, unit_map: dict[int, int] | None = None,
           renumber: dict[str, str] | None = None, pin_types: dict[str, str] | None = None,
           pin_names: dict[str, str] | None = None, pin_shapes: dict[str, str] | None = None) -> list:
    """A stock symbol under a new name. ``unit_map`` renumbers units (old -> new);
    ``renumber``, ``pin_types``, ``pin_names`` and ``pin_shapes`` are keyed by the stock pin number."""
    node = copy.deepcopy(symlib.load(lib_id).node)
    old = lib_id.split(":", 1)[1]
    node[1] = name
    for p in find_all(node, "property"):
        new = {"Value": value or name, "Footprint": footprint, "Datasheet": datasheet,
               "Description": description}.get(p[1])
        if new is not None:
            p[2] = new
        if p[1] in ("ki_fp_filters", "ki_keywords"):
            p[2] = ""
    subs = find_all(node, "symbol")
    for sub in subs:
        tail = sub[1][len(old):]  # "_<unit>_<style>"
        _, unit, style = tail.split("_")
        unit = str((unit_map or {}).get(int(unit), int(unit)))
        sub[1] = f"{name}_{unit}_{style}"
        for item in sub[2:]:
            if isinstance(item, list) and item[0] == "pin":
                num = find(item, "number")
                stock = num[1]
                if pin_types and stock in pin_types:
                    item[1] = S(pin_types[stock])
                if pin_shapes and stock in pin_shapes:
                    item[2] = S(pin_shapes[stock])
                if pin_names and stock in pin_names:
                    find(item, "name")[1] = pin_names[stock]
                if renumber and stock in renumber:
                    num[1] = renumber[stock]
    # KiCad lists the units in order.
    order = sorted(subs, key=lambda sub: [int(v) for v in sub[1][len(name):].split("_")[1:]])
    first = next(i for i, it in enumerate(node) if isinstance(it, list) and it[0] == "symbol")
    rest = [it for it in node[first:] if not (isinstance(it, list) and it[0] == "symbol")]
    node[first:] = order + rest
    return node


def power(name: str, description: str = "") -> list:
    """An arrow-style power symbol whose global net is ``name``."""
    graphics = [polyline([(-0.762, 1.27), (0, 2.54)]), polyline([(0, 2.54), (0.762, 1.27)]),
                polyline([(0, 0), (0, 2.54)])]
    pins = [pin("power_in", "1", "", 0, 0, 90, length=0)]
    return symbol(name, "#PWR", name, {(0, 1): graphics, (1, 1): pins},
                  description=description or f"Power symbol, global net {name}", ref_at=(0, -3.81),
                  value_at=(0, 3.556), power=True, hide_pin_numbers=True, hide_pin_names=True, pin_name_offset=0)


def write_lib(path: Path, symbols: list[list]) -> bool:
    """Write the library. Returns False when the file already has this content."""
    root = [S("kicad_symbol_lib"), [S("version"), S(LIB_VERSION)], [S("generator"), "hwflow"],
            [S("generator_version"), "10.0"], *sorted(symbols, key=lambda n: n[1])]
    text_out = dumps(root) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text(encoding="utf-8") == text_out:
        return False
    path.write_text(text_out, encoding="utf-8")
    return True
