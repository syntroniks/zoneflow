# SPDX-License-Identifier: Apache-2.0
"""Is the walkthrough still about the board in the repository? Needs Python only.

Fails when the content names a part or a net that the board does not have, when
a picture is missing or left over, or when the pictures were drawn from another
board, another render or other picture descriptions than the committed ones.
"""

from __future__ import annotations

import json
from pathlib import Path

from . import spec as specmod
from .boardtext import BoardText


def problems(spec, board=specmod.BOARD, out_dir=specmod.OUT, top_render=specmod.TOP_RENDER):
    """A list of sentences, empty when all is well."""
    found = []
    bt = BoardText(board)
    for ref, where in sorted(specmod.named_refs(spec).items()):
        if not bt.has_ref(ref):
            found.append(f"{where} names {ref}, and the board has no part {ref}")
    for net, where in sorted(specmod.named_nets(spec).items()):
        if not bt.has_net(net):
            found.append(f"{where} names net {net}, and the board has no such net")
    out_dir = Path(out_dir)
    manifest_path = out_dir / specmod.MANIFEST
    if not manifest_path.is_file():
        return found + [f"{manifest_path} is missing: run make -C hardware/zoneflow-a review"]
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    want = {specmod.figure_name(owner, fig) for owner, fig in specmod.figures(spec)}
    want |= set(specmod.SHARED)
    have = {p.name for p in out_dir.glob("*.svg")}
    for name in sorted(want - have):
        found.append(f"picture {name} is missing from {out_dir}")
    for name in sorted(have - want):
        found.append(f"picture {name} in {out_dir} belongs to no step or finding")
    for name in sorted(want - set(manifest.get("figures", {})) - set(specmod.SHARED)):
        found.append(f"picture {name} is not in the manifest")
    if manifest.get("inputs") != specmod.inputs_hash(spec, board, top_render):
        found.append("the pictures were not drawn from the committed board, render, picture descriptions and drawing "
                     "code: run make -C hardware/zoneflow-a review and commit the result")
    return found
