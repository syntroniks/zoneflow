# SPDX-License-Identifier: Apache-2.0
"""Give the autorouter's tracks the width of their net class wherever there is room.

Freerouting 2.5.0 narrows a track where it cannot get through at the width of
its net class: to three quarters of that width, to 60 per cent, to half. It
does so for whole connections and not only at a pin, and it tells nobody. On
the revision A board 350 mm of track in 61 nets came back narrower than its
class asks, supply rails among them.

This module undoes that where it can. It sets every autorouted track that is
narrower than its class to the class width, runs KiCad's DRC on a copy of the
board, and puts back to its old width every such track that DRC names in a
clearance finding. It repeats until no finding names one. What stays narrow
is narrow because the board has no room there, and the routing report lists
it.

The centre lines do not move, so the stored routing session stays what the
board is made from. DRC is the judge, as in ``hwflow.silk``: nothing here
models a clearance.

Runs under KiCad's Python. ``kicad-cli`` must be on the path, or named by the
environment variable ``KICAD_CLI``.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from pathlib import Path

# The findings a wider track can cause. Dangling tracks and unconnected items are not among them:
# at this point of the build the zones are not filled and half the board is "unconnected".
CHECKS = {"clearance", "hole_clearance", "copper_edge_clearance", "shorting_items", "tracks_crossing",
          "solder_mask_bridge", "creepage"}
MAX_ROUNDS = 12


class WidenError(Exception):
    pass


def revert(found: list[dict], wide: set[str]) -> set[str]:
    """The tracks to put back: every widened track that a clearance finding names."""
    return {i["uuid"] for v in found if v["type"] in CHECKS for i in v.get("items", []) if i.get("uuid") in wide}


def widen(board, pcb_path: Path, work_dir: Path, log=None) -> dict:
    """Widen the unlocked tracks of ``board`` to their class width where DRC allows. Changes ``board`` in place."""
    import pcbnew

    cli = os.environ.get("KICAD_CLI", "kicad-cli")
    settings = board.GetDesignSettings().m_NetSettings
    tracks, old = {}, {}
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T or t.IsLocked():
            continue
        want = settings.GetEffectiveNetClass(t.GetNetname()).GetTrackWidth()
        if t.GetWidth() < want:
            u = t.m_Uuid.AsString()
            tracks[u], old[u] = t, t.GetWidth()
            t.SetWidth(want)
    stats = {"narrow_segments": len(tracks), "narrow_mm": round(sum(pcbnew.ToMM(t.GetLength())
                                                                   for t in tracks.values()), 1),
             "rounds": 0, "kept_narrow_segments": 0, "kept_narrow_mm": 0.0}
    if not tracks:
        return stats
    work_dir.mkdir(parents=True, exist_ok=True)
    copy = work_dir / pcb_path.name
    for ext in (".kicad_pro", ".kicad_dru"):
        src = pcb_path.with_suffix(ext)
        if src.exists():
            shutil.copyfile(src, work_dir / src.name)
    report = work_dir / "widen-drc.json"
    wide = set(tracks)
    for round_no in range(1, MAX_ROUNDS + 1):
        stats["rounds"] = round_no
        pcbnew.SaveBoard(str(copy), board)
        subprocess.run([cli, "pcb", "drc", "--severity-error", "--severity-warning", "--format", "json",
                        "-o", str(report), str(copy)], check=False, capture_output=True)
        if not report.exists():
            raise WidenError("kicad-cli wrote no DRC report for the track width pass")
        found = json.loads(report.read_text(encoding="utf-8")).get("violations", [])
        report.unlink()
        back = revert(found, wide)
        if log:
            log(f"track width round {round_no}: {len(wide)} tracks at class width, {len(back)} put back")
        if not back:
            break
        for u in sorted(back):
            tracks[u].SetWidth(old[u])
        wide -= back
    else:
        raise WidenError(f"track widths: DRC still names widened tracks after {MAX_ROUNDS} rounds")
    kept = set(tracks) - wide
    stats["kept_narrow_segments"] = len(kept)
    stats["kept_narrow_mm"] = round(sum(pcbnew.ToMM(tracks[u].GetLength()) for u in kept), 1)
    return stats
