# SPDX-License-Identifier: Apache-2.0
"""Make the silkscreen of a generated board pass DRC.

A dense board cannot keep every footprint's stock silkscreen: outlines cross
the pads of the neighbours, reference designators land on other parts, and the
outline of an edge connector hangs over the board edge. KiCad's DRC reports
each of these. This module uses that report as the judge: it runs DRC on a
copy of the board, and for every silkscreen finding it

- deletes the footprint graphic that is at fault,
- moves a reference designator to the next of a few fixed positions around
  its part, and hides it when none is free,
- deletes a ``[[text]]`` of board.toml that is marked ``optional``, and fails
  the build for one that is not: a terminal label must not be dropped silently.

It repeats until DRC has no silkscreen finding left. Nothing is decided by a
geometry model of this module's own, so the result is clean by KiCad's rules
for the text font and the solder mask openings, whatever they are.

Runs under KiCad's Python. ``kicad-cli`` must be on the path, or named by the
environment variable ``KICAD_CLI``.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
from pathlib import Path

import pcbnew

SILK_CHECKS = {"silk_overlap", "silk_over_copper", "silk_edge_clearance"}
MAX_ROUNDS = 30


class SilkError(Exception):
    pass


def _uuid(item) -> str:
    return item.m_Uuid.AsString()


def _extent(fp) -> tuple[int, int, int, int]:
    """Left, top, right, bottom of a footprint's courtyard, or of its pads without one."""
    boxes = [g.GetBoundingBox() for g in fp.GraphicalItems() if g.GetLayer() in (pcbnew.F_CrtYd, pcbnew.B_CrtYd)]
    if not boxes:
        boxes = [p.GetBoundingBox() for p in fp.Pads()]
    return (min(b.GetLeft() for b in boxes), min(b.GetTop() for b in boxes),
            max(b.GetRight() for b in boxes), max(b.GetBottom() for b in boxes))


def candidates(fp, size_mm: float = 1.0) -> list[tuple[int, int, float]]:
    """Positions to try for a reference designator, in order.

    Above, below, right and left of the part; the same with the text turned;
    the middle of the part, which is free on a switch, a relay or a cell
    holder; then above and below, shifted along the edge. Page coordinates
    (y down) and the text angle in degrees. The first entry is where the text
    already is.
    """
    ref = fp.Reference()
    left, top, right, bottom = _extent(fp)
    cx, cy = (left + right) // 2, (top + bottom) // 2
    gap = pcbnew.FromMM(size_mm / 2 + 0.35)
    # half the length of the text, for the positions where it runs away from the part
    half = pcbnew.FromMM(len(fp.GetReference()) * size_mm * 0.45 + 0.2)
    shift = pcbnew.FromMM(1.5)
    here = (ref.GetPosition().x, ref.GetPosition().y, ref.GetTextAngleDegrees())
    return [
        here,
        (cx, top - gap, 0.0), (cx, bottom + gap, 0.0), (right + gap, cy, 90.0), (left - gap, cy, 90.0),
        (cx, top - half, 90.0), (cx, bottom + half, 90.0), (right + half, cy, 0.0), (left - half, cy, 0.0),
        (cx, cy, 0.0), (cx, cy, 90.0),
        (cx - shift, top - gap, 0.0), (cx + shift, top - gap, 0.0),
        (cx - shift, bottom + gap, 0.0), (cx + shift, bottom + gap, 0.0),
    ]


def clean(board, pcb_path: Path, work_dir: Path, optional: set[str] = frozenset(), log=None) -> dict:
    """Run DRC on a copy of ``board`` until no silkscreen finding is left. Changes ``board`` in place."""
    cli = os.environ.get("KICAD_CLI", "kicad-cli")
    work_dir.mkdir(parents=True, exist_ok=True)
    copy = work_dir / pcb_path.name
    for ext in (".kicad_pro", ".kicad_dru"):
        src = pcb_path.with_suffix(ext)
        if src.exists():
            shutil.copyfile(src, work_dir / src.name)
    report = work_dir / "silk-drc.json"

    step: dict[str, int] = {}            # reference -> index into its candidate list
    cand: dict[str, list] = {}
    stats = {"rounds": 0, "graphics_removed": 0, "refs_moved": [], "refs_hidden": [], "texts_removed": []}

    for round_no in range(1, MAX_ROUNDS + 1):
        stats["rounds"] = round_no
        pcbnew.SaveBoard(str(copy), board)
        subprocess.run([cli, "pcb", "drc", "--severity-all", "--format", "json", "-o", str(report), str(copy)],
                       check=False, capture_output=True)
        if not report.exists():
            raise SilkError("kicad-cli wrote no DRC report for the silkscreen check")
        found = [v for v in json.loads(report.read_text(encoding="utf-8")).get("violations", [])
                 if v["type"] in SILK_CHECKS]
        report.unlink()
        if not found:
            break

        graphics, refs, texts = {}, {}, {}
        for fp in board.GetFootprints():
            refs[_uuid(fp.Reference())] = fp
            for g in fp.GraphicalItems():
                if g.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                    graphics[_uuid(g)] = (fp, g)
        for d in board.GetDrawings():
            if d.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                texts[_uuid(d)] = d

        kill: dict[str, tuple] = {}       # uuid -> (footprint, graphic)
        move: dict[str, object] = {}      # reference -> footprint
        drop: dict[str, object] = {}      # uuid -> board text
        errors = []
        for v in found:
            ids = [i["uuid"] for i in v["items"]]
            g_hit = [u for u in ids if u in graphics]
            r_hit = [u for u in ids if u in refs]
            t_hit = [u for u in ids if u in texts]
            if r_hit:
                # A designator gives way to everything else. Two designators: the later one moves.
                fps = sorted((refs[u] for u in r_hit), key=lambda f: f.GetReference())
                fp = fps[-1]
                move[fp.GetReference()] = fp
            elif t_hit and g_hit:
                for u in g_hit:           # a label of board.toml wins over a footprint outline
                    kill[u] = graphics[u]
            elif t_hit:
                opt = sorted(u for u in t_hit if u in optional)
                if opt:
                    drop[opt[-1]] = texts[opt[-1]]
                else:
                    pos = v["items"][0].get("pos", {})
                    errors.append(f"{v['type']}: " + "; ".join(i["description"] for i in v["items"])
                                  + f" at page ({pos.get('x')}, {pos.get('y')})")
            elif len(g_hit) == 2:
                # Two outlines: the one of the later reference goes.
                u = max(g_hit, key=lambda x: (graphics[x][0].GetReference(), x))
                kill[u] = graphics[u]
            elif g_hit:
                kill[g_hit[0]] = graphics[g_hit[0]]
            else:
                errors.append(f"{v['type']}: " + "; ".join(i["description"] for i in v["items"]))
        if errors:
            raise SilkError("silkscreen: a fixed text of board.toml collides and is not marked optional:\n  "
                            + "\n  ".join(sorted(set(errors))))

        for u in sorted(kill):
            fp, g = kill[u]
            fp.Delete(g)
            stats["graphics_removed"] += 1
        for u in sorted(drop):
            stats["texts_removed"].append(drop[u].GetText())
            board.Delete(drop[u])
        for name in sorted(move):
            fp = move[name]
            if name not in cand:
                cand[name] = candidates(fp)
                step[name] = 0
            step[name] += 1
            field = fp.Reference()
            if step[name] >= len(cand[name]):
                field.SetVisible(False)
                if name not in stats["refs_hidden"]:
                    stats["refs_hidden"].append(name)
                continue
            x, y, angle = cand[name][step[name]]
            field.SetPosition(pcbnew.VECTOR2I(int(x), int(y)))
            field.SetTextAngleDegrees(angle)
            if name not in stats["refs_moved"]:
                stats["refs_moved"].append(name)
        if log:
            log(f"silkscreen round {round_no}: {len(found)} findings, {len(kill)} outlines removed, "
                f"{len(move)} designators moved, {len(drop)} optional texts removed")
    else:
        raise SilkError(f"silkscreen: DRC still reports findings after {MAX_ROUNDS} rounds")

    stats["refs_moved"] = sorted(set(stats["refs_moved"]) - set(stats["refs_hidden"]))
    stats["refs_hidden"] = sorted(stats["refs_hidden"])
    return stats
