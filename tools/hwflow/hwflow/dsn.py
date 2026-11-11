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


def exclude_nets(dsn: Path, patterns: list[str]) -> int:
    """Take the pins of some nets out of the Specctra file, so that the autorouter leaves them alone.

    ``patterns`` are shell-style patterns of KiCad net names. A net that is
    connected through a plane, a pour or tracks on a layer the autorouter does
    not see looks unrouted to it. With no pins the net has nothing to route;
    its pads stay in the file as obstacles and its pre-routed tracks and vias
    stay as fixed copper.
    """
    if not patterns:
        return 0
    text = dsn.read_text(encoding="utf-8")
    count = 0

    def strip(m):
        nonlocal count
        name = m.group(1).strip('"')
        if any(fnmatch.fnmatchcase(name, pat) for pat in patterns):
            count += 1
            return f"(net {m.group(1)}\n    )"
        return m.group(0)

    text = re.sub(r'\(net ("[^"]+"|\S+)\s*\(pins[^)]*\)\s*\)', strip, text)
    dsn.write_text(text, encoding="utf-8")
    return count


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


def router_settings(board_layers: list[str], route: dict) -> dict:
    """Freerouting's ``router`` settings for a board: which layers it may route on, and at what cost.

    Freerouting 2.5.0 does not take these from the Specctra file. They go into
    its settings file, which ``hwflow.route`` writes. ``[route] layers`` lists
    the copper layers the autorouter may route on; its vias still pass through
    the others. The fan-out stage is off unless ``[route] fanout = true``: on a
    dense board it spends the space the router needs.
    """
    settings: dict = {"fanout": {"enabled": bool(route.get("fanout", False))}}
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
