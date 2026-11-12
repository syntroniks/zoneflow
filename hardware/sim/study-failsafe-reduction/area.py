#!/usr/bin/env python3
"""Pads and courtyard area of the revision A parts that the study's variants name.

    python3 hardware/sim/study-failsafe-reduction/area.py

STUDY, not the product. Reads hardware/zoneflow-a/zoneflow-a.kicad_pcb with
KiCad's pcbnew module (KiCad 10) and writes results/footprints.csv. The area
is the bounding box of each footprint's courtyard: what a part occupies, not
what its tracks occupy.
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

import pcbnew

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from variants import ALL_REFS  # noqa: E402

BOARD = HERE.parents[1] / "zoneflow-a" / "zoneflow-a.kicad_pcb"


def main() -> int:
    board = pcbnew.LoadBoard(str(BOARD))
    by_ref = {fp.GetReference(): fp for fp in board.GetFootprints()}
    rows = []
    for ref in ALL_REFS:
        fp = by_ref.get(ref)
        if fp is None:
            rows.append([ref, "not on the board", 0, "0.00", ""])
            continue
        side = "F.CrtYd" if fp.GetLayer() == pcbnew.F_Cu else "B.CrtYd"
        layer = board.GetLayerID(side)
        box = None
        for item in fp.GraphicalItems():
            if item.GetLayer() == layer:
                b = item.GetBoundingBox()
                if box is None:
                    box = b
                else:
                    box.Merge(b)
        area = pcbnew.ToMM(box.GetWidth()) * pcbnew.ToMM(box.GetHeight()) if box else 0.0
        pads = len({p.GetNumber() for p in fp.Pads() if p.GetNumber()})
        tht = any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for p in fp.Pads())
        rows.append([ref, str(fp.GetFPID().GetLibItemName()), pads, f"{area:.2f}", "tht" if tht else "smt"])
    out = HERE / "results" / "footprints.csv"
    out.parent.mkdir(exist_ok=True)
    with out.open("w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["ref", "footprint", "pads", "courtyard_mm2", "mount"])
        w.writerows(rows)
    print(f"wrote {out.relative_to(HERE)} ({len(rows)} rows)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
