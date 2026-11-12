# SPDX-License-Identifier: Apache-2.0
"""What can be read from a KiCad board file without KiCad: its parts and its nets.

The documentation build has no KiCad. It still has to fail when the walkthrough
names a part that is no longer on the board, so this reads the file as text.
"""

from __future__ import annotations

import hashlib
import re
from pathlib import Path

_REFERENCE = re.compile(r'\(property "Reference" "([^"]+)"')
_NET = re.compile(r'\(net (?:\d+ )?"([^"]+)"\)')
_ORIGIN = re.compile(r"\(aux_axis_origin (-?[\d.]+) (-?[\d.]+)\)")


def sha256(path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class BoardText:
    def __init__(self, path):
        text = Path(path).read_text(encoding="utf-8")
        self.refs = set(_REFERENCE.findall(text))
        self.nets = set(_NET.findall(text))
        self.short_nets = {n.rsplit("/", 1)[-1] for n in self.nets}
        m = _ORIGIN.search(text)
        self.origin = (float(m.group(1)), float(m.group(2))) if m else None

    def has_ref(self, ref: str) -> bool:
        return ref in self.refs

    def has_net(self, net: str) -> bool:
        return net in self.nets or net in self.short_nets
