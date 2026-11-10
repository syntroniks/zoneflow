# SPDX-License-Identifier: Apache-2.0
"""Write ``<out>/reports/build-summary.json``: what the build produced and what the checks found.

Runs under the system interpreter because it reads the board with ``pcbnew``.
"""

from __future__ import annotations

import json
import os
import sys
from collections import Counter, defaultdict
from pathlib import Path

import pcbnew


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    project, out = Path(argv[0]), Path(argv[1])
    model = json.loads((project / "build" / "model.json").read_text())
    name = model["name"]
    board = pcbnew.LoadBoard(os.path.abspath(project / f"{name}.kicad_pcb"))
    settings = board.GetDesignSettings().m_NetSettings

    widths: dict[str, list[float]] = defaultdict(list)
    lengths: Counter = Counter()
    vias = 0
    segments = 0
    for t in board.GetTracks():
        if t.Type() == pcbnew.PCB_VIA_T:
            vias += 1
            continue
        segments += 1
        cls = str(settings.GetEffectiveNetClass(t.GetNetname()).GetName())
        widths[cls].append(round(pcbnew.ToMM(t.GetWidth()), 3))
        lengths[cls] += pcbnew.ToMM(t.GetLength())
    classes = {}
    for cls, w in sorted(widths.items()):
        nc = settings.GetNetClassByName(cls)
        classes[cls] = {
            "class_track_width_mm": round(pcbnew.ToMM(nc.GetTrackWidth()), 3),
            "class_clearance_mm": round(pcbnew.ToMM(nc.GetClearance()), 3),
            "routed_width_min_mm": min(w),
            "routed_width_max_mm": max(w),
            "segments": len(w),
            "length_mm": round(lengths[cls], 1),
        }

    def counts(path: Path, keys):
        if not path.exists():
            return None
        data = json.loads(path.read_text())
        if "sheets" in data:
            items = [v for s in data["sheets"] for v in s["violations"]]
            return {"violations": len(items)}
        return {k: len(data.get(k, [])) for k in keys}

    route_stats = {}
    for f in (project / "build" / "route-stats.json", project / "route" / f"{name}.freerouting.json"):
        if f.exists():
            route_stats.update(json.loads(f.read_text()))
    fps = list(board.GetFootprints())
    summary = {
        "project": name,
        "kicad_version": str(pcbnew.Version()),
        "schematic": {"parts": len(model["parts"]), "nets": model["nets"]},
        "board": {
            "footprints": len(fps),
            "smd": sum(1 for f in fps if f.GetAttributes() & pcbnew.FP_SMD),
            "through_hole": sum(1 for f in fps if f.GetAttributes() & pcbnew.FP_THROUGH_HOLE),
            "copper_layers": board.GetCopperLayerCount(),
            "track_segments": segments,
            "vias": vias,
            "zones": len(list(board.Zones())),
        },
        "net_classes": classes,
        "routing": route_stats,
        "erc": counts(out / "reports" / "erc.json", ()),
        "drc": counts(out / "reports" / "drc.json", ("violations", "unconnected_items", "schematic_parity")),
    }
    dest = out / "reports" / "build-summary.json"
    dest.write_text(json.dumps(summary, indent=1, sort_keys=True) + "\n")
    print(f"summary written to {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
