# SPDX-License-Identifier: Apache-2.0
"""Changes to a Specctra DSN file before the autorouter reads it.

KiCad writes the file. These functions edit its text: they take nets away from
the autorouter, tell it which layers it may route on, and add keep-outs for
copper it cannot see. Only the standard library is used.
"""

from __future__ import annotations

import fnmatch
import re
from pathlib import Path

FIXED = "__fixed"       # suffix of the net classes that the autorouter is told to ignore


def _class_blocks(text: str):
    """Yield (start, end, name, nets, body) of every ``(class ...)`` block of a Specctra file."""
    pos = 0
    while True:
        start = text.find("(class ", pos)
        if start < 0:
            return
        depth, i = 0, start
        while True:
            ch = text[i]
            if ch == '"':
                i = text.index('"', i + 1)
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        end = i + 1
        inner = text[start + len("(class "):i]
        body_at = len(inner)
        j = 0
        while j < len(inner):               # the first parenthesis outside a quoted name starts the body
            if inner[j] == '"':
                j = inner.index('"', j + 1)
            elif inner[j] == "(":
                body_at = j
                break
            j += 1
        tokens = re.findall(r'"[^"]*"|\S+', inner[:body_at])
        yield start, end, tokens[0], tokens[1:], inner[body_at:].rstrip()
        pos = end


def fix_nets(dsn: Path, patterns: list[str]) -> tuple[int, list[str]]:
    """Mark nets as not to be routed: move them to net classes of their own, for the autorouter to ignore.

    ``patterns`` are shell-style patterns of KiCad net names. A net that is
    connected through a plane, a pour or hand-routed copper must be left alone
    by the autorouter. Each such net moves from its class ``X`` to a class
    ``X__fixed`` with the same rules; the caller gives the names of those
    classes to Freerouting as ``router.autorouter.ignore_net_classes``.

    The pins of the net stay in the file, so its pads keep the clearance of
    their class. Taking the pins out, as an earlier version of this function
    did, left the pads with the default clearance, and Freerouting also joined
    the pieces of a net that connects through a plane.

    Returns the number of nets moved and the names of the new classes.
    """
    if not patterns:
        return 0, []
    text = dsn.read_text(encoding="utf-8")
    out, last, count, names = [], 0, 0, []
    for start, end, name, nets, body in _class_blocks(text):
        fixed = [n for n in nets if any(fnmatch.fnmatchcase(n.strip('"'), pat) for pat in patterns)]
        if not fixed:
            continue
        kept = [n for n in nets if n not in fixed]
        new_name = (name.strip('"') + FIXED)
        token = f'"{new_name}"' if name.startswith('"') else new_name
        indent = "    "
        block = ""
        if kept:
            block += f"(class {name} " + " ".join(kept) + f"\n{indent}  {body}\n{indent})\n{indent}"
        block += f"(class {token} " + " ".join(fixed) + f"\n{indent}  {body}\n{indent})"
        out.append(text[last:start] + block)
        last = end
        count += len(fixed)
        names.append(new_name)
    if not out:
        return 0, []
    dsn.write_text("".join(out) + text[last:], encoding="utf-8")
    return count, names


def widen_clearances(dsn: Path, margin_mm: float) -> int:
    """Add a margin to every clearance the autorouter is given.

    Freerouting places a diagonal track to the micrometre it is told and
    rounds; KiCad's DRC then finds 0.198 mm where 0.2 mm is the rule. With
    ``[route] clearance_margin = 0.01`` the router works to 0.21 mm and DRC
    still checks 0.2 mm.
    """
    if margin_mm <= 0:
        return 0
    text = dsn.read_text(encoding="utf-8")
    add = margin_mm * 1000.0
    text, n = re.subn(r"\(clearance (\d+(?:\.\d+)?)\)", lambda m: f"(clearance {float(m.group(1)) + add:g})", text)
    dsn.write_text(text, encoding="utf-8")
    return n


def add_keepouts(dsn: Path, keepouts: list[dict], to_dsn) -> int:
    """Add rectangles where the autorouter may put neither track nor via.

    ``[[route.keepout]]`` gives them in board coordinates, per layer: the
    planes and islands that are added as zones after the file is written, and
    that the autorouter therefore cannot see.
    """
    lines = []
    for k in keepouts:
        (x0, y0), (x1, y1) = to_dsn(k["rect"][0], k["rect"][1]), to_dsn(k["rect"][2], k["rect"][3])
        for layer in k["layers"]:
            lines.append(f'    (keepout "" (rect {layer} {min(x0, x1):.0f} {min(y0, y1):.0f} {max(x0, x1):.0f} '
                         f'{max(y0, y1):.0f}))')
    if not lines:
        return 0
    text = dsn.read_text(encoding="utf-8")
    marker = "\n    (via "
    if marker not in text:
        raise ValueError("the Specctra file has no via list to put the keep-outs before")
    head, tail = text.split(marker, 1)
    dsn.write_text(head + "\n" + "\n".join(lines) + marker + tail, encoding="utf-8")
    return len(lines)


def add_planes(dsn: Path, planes: list[dict], rect: tuple[float, float, float, float]) -> int:
    """Declare plane layers: the only way found to keep Freerouting 2.5.0 off a copper layer.

    ``[[route.plane]]`` names a ``layer`` and the ``net`` of its plane. The
    layer's type becomes ``power`` and a plane polygon of that net, ``rect``
    in the file's coordinates, is added to the structure. With the type alone
    Freerouting says "declared as a power layer but no plane is defined for
    it" and routes on the layer all the same. The ``routable`` flag of its
    settings file is read and not obeyed: with it set to false, 2.5.0 put
    more track on this layer than on any other.
    """
    if not planes:
        return 0
    text = dsn.read_text(encoding="utf-8")
    lines = []
    x0, y0, x1, y1 = rect
    for p in planes:
        old = f"(layer {p['layer']}\n      (type signal)"
        if text.count(old) != 1:
            raise ValueError(f"the Specctra file has no signal layer {p['layer']}")
        text = text.replace(old, f"(layer {p['layer']}\n      (type power)")
        net = p["net"] if re.fullmatch(r"[^\s()\"]+", p["net"]) else f'"{p["net"]}"'
        lines.append(f"    (plane {net} (polygon {p['layer']} 0  {x0:.0f} {y0:.0f}  {x1:.0f} {y0:.0f}  "
                     f"{x1:.0f} {y1:.0f}  {x0:.0f} {y1:.0f}  {x0:.0f} {y0:.0f}))")
    marker = "\n    (via "
    if marker not in text:
        raise ValueError("the Specctra file has no via list to put the planes before")
    head, tail = text.split(marker, 1)
    dsn.write_text(head + "\n" + "\n".join(lines) + marker + tail, encoding="utf-8")
    return len(lines)


def router_settings(board_layers: list[str], route: dict, ignore: list[str] | None = None) -> dict:
    """Freerouting's ``router`` settings for a board: which layers it may route on, and at what cost.

    Freerouting 2.5.0 does not take these from the Specctra file. They go into
    its settings file, which ``hwflow.route`` writes. ``[route] layers`` lists
    the copper layers the autorouter may route on; its vias still pass through
    the others. The fan-out stage is off unless ``[route] fanout = true``: on a
    dense board it spends the space the router needs.
    ``ignore`` are the net classes of ``fix_nets``.
    """
    settings: dict = {"fanout": {"enabled": bool(route.get("fanout", False))}}
    if ignore:
        settings["autorouter"] = {"ignore_net_classes": sorted(ignore)}
    if "layers" in route:
        costs = route.get("layer_cost", {})
        layers = []
        for i, layer in enumerate(board_layers):
            entry = {"routable": layer in route["layers"], "preferred_direction_horizontal": i % 2 == 0}
            if layer in costs:
                entry["preferred_direction_trace_cost"] = float(costs[layer])
                entry["undesired_direction_trace_cost"] = float(costs[layer]) * 2
            layers.append(entry)
        settings["layers"] = layers
    return settings
