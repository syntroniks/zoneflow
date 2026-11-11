"""Read the laid-out board and write where everything on it is, as JSON.

    python3 tools/board_layout.py ../hardware/zoneflow-a/zoneflow-a.kicad_pcb board_layout.json

This is the only step of the enclosure build that needs KiCad: it runs under
the Python that has the `pcbnew` module and uses nothing else outside the
standard library. Its output, `enclosure/board_layout.json`, is committed, so
that the rest of the build (`tools/gen_testfit.py`, OpenSCAD, the check) runs
without KiCad.

Positions are in the frame of hardware/mechanical.yaml: origin at the
bottom-left corner of the outline seen from the component side, x to the
right, y up, millimetres. Rotation is counter-clockwise seen from the top.

Nothing is measured and nothing is taken from a datasheet here. `body` is the
bounding box of what the footprint draws on its fabrication layer,
`courtyard` of its courtyard. Heights are not in a board file; the generator
takes them from the contract and from design/heights.toml.

Component values are left out on purpose. They do not change the shape of
anything, and with them every change of a resistor would make this file
stale.
"""

from __future__ import annotations

import json
import math
import re
import sys
from pathlib import Path

import pcbnew


def mm(v: int) -> float:
    return round(pcbnew.ToMM(v), 4)


def ref_key(ref: str):
    m = re.match(r"([A-Za-z]+)(\d*)", ref)
    return (m.group(1), int(m.group(2) or 0)) if m else (ref, 0)


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    src, dest = Path(argv[1]), Path(argv[2])
    board = pcbnew.LoadBoard(str(src))

    edge = [d for d in board.GetDrawings() if d.GetLayer() == pcbnew.Edge_Cuts]
    if not edge:
        raise SystemExit(f"{src}: no outline on Edge.Cuts")
    # The outline is drawn with a line of some width, which a bounding box
    # includes. The frame is set by the end points, not by the bounding box.
    pts = [p for e in edge for p in (e.GetStart(), e.GetEnd())]
    ox = min(p.x for p in pts)
    oy = max(p.y for p in pts)          # KiCad's y runs down the page

    def xy(p) -> list[float]:
        return [mm(p.x - ox), mm(oy - p.y)]

    def box(items) -> list[float] | None:
        ext = None
        for g in items:
            b = g.GetBoundingBox()
            e = (b.GetLeft(), b.GetTop(), b.GetRight(), b.GetBottom())
            ext = e if ext is None else (min(ext[0], e[0]), min(ext[1], e[1]), max(ext[2], e[2]), max(ext[3], e[3]))
        if ext is None:
            return None
        return [mm(ext[0] - ox), mm(oy - ext[3]), mm(ext[2] - ox), mm(oy - ext[1])]

    outline = []
    for e in edge:
        shape = e.ShowShape()
        if shape == "Line":
            outline.append({"type": "line", "start": xy(e.GetStart()), "end": xy(e.GetEnd())})
        elif shape == "Arc":
            c = e.GetCenter()
            outline.append({"type": "arc", "start": xy(e.GetStart()), "end": xy(e.GetEnd()),
                            "mid": xy(e.GetArcMid()), "centre": xy(c),
                            "radius": mm(int(round(math.hypot(e.GetStart().x - c.x, e.GetStart().y - c.y))))})
        else:
            raise SystemExit(f"{src}: outline has a {shape}; only lines and arcs are handled")

    footprints = []
    for fp in sorted(board.GetFootprints(), key=lambda f: ref_key(f.GetReference())):
        back = fp.IsFlipped()
        shapes = [g for g in fp.GraphicalItems() if g.GetClass() == "PCB_SHAPE"]
        fab = [g for g in shapes if g.GetLayer() == (pcbnew.B_Fab if back else pcbnew.F_Fab)]
        yard = [g for g in shapes if g.GetLayer() == (pcbnew.B_CrtYd if back else pcbnew.F_CrtYd)]
        pads = []
        for p in fp.Pads():
            b = p.GetBoundingBox()
            pads.append({
                "number": p.GetNumber(),
                "at": xy(p.GetPosition()),
                "size": [mm(b.GetWidth()), mm(b.GetHeight())],
                "drill": mm(p.GetDrillSize().x),
                "plated": p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH,
                "through": p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH),
            })
        footprints.append({
            "ref": fp.GetReference(),
            "footprint": str(fp.GetFPID().GetUniStringLibId()),
            "side": "bottom" if back else "top",
            "at": xy(fp.GetPosition()),
            "rot": fp.GetOrientationDegrees(),
            "dnp": bool(fp.IsDNP()),
            "body": box(fab),
            "courtyard": box(yard),
            "model": [Path(m.m_Filename).name for m in fp.Models()],
            "pads": pads,
        })

    tb = board.GetTitleBlock()
    out = {
        "generated_by": "enclosure/tools/board_layout.py. Do not edit.",
        "source": "hardware/zoneflow-a/zoneflow-a.kicad_pcb",
        "title": tb.GetTitle(),
        "revision": tb.GetRevision(),
        "date": tb.GetDate(),
        "frame": "origin bottom-left of the outline seen from the top, x right, y up, mm",
        "thickness": pcbnew.ToMM(board.GetDesignSettings().GetBoardThickness()),
        "copper_layers": board.GetCopperLayerCount(),
        "outline": outline,
        "footprints": footprints,
    }
    dest.write_text(json.dumps(out, indent=1) + "\n", encoding="utf-8")
    print(f"{dest}: {len(footprints)} footprints, {len(outline)} outline segments")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
