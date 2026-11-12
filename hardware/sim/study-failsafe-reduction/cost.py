#!/usr/bin/env python3
"""Parts, landed cost and board area of the study's variants against revision A.

    uv run --project tools/cost --with 'pyyaml>=6,<7' python hardware/sim/study-failsafe-reduction/cost.py

STUDY, not the product. Uses the landed-cost model of tools/cost (zfcost)
with its price snapshot of 2026-11-11, and new-parts.csv in this directory
for the part numbers revision A does not have (JLCPCB parts search,
2026-11-12). Reads results/footprints.csv, which area.py writes from the
board file. Writes results/variants.csv and results/variants.md.

It is an estimate, as docs/hardware/cost.md is: the model has never been
compared with a real order.
"""

from __future__ import annotations

import csv
import sys
from dataclasses import replace
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import yaml  # noqa: E402
from variants import VARIANTS, Variant  # noqa: E402
from zfcost import model as M  # noqa: E402

NETLIST = HERE.parents[1] / "netlist.yaml"
QTYS = (5, 25, 100)
BUILD = "8-zone"


def load() -> tuple[dict, dict, M.Inputs]:
    comps = {c["ref"]: c for c in yaml.safe_load(NETLIST.read_text())["components"]}
    with (HERE / "results" / "footprints.csv").open(newline="") as fh:
        fps = {r["ref"]: r for r in csv.DictReader(fh)}
    inp = M.Inputs.load()
    parts = dict(inp.parts)
    parts.update(M.load_parts(HERE / "new-parts.csv"))
    return comps, fps, inp.copy(parts=parts)


def apply(v: Variant, comps: dict, fps: dict, inp: M.Inputs) -> M.Inputs:
    """The order of revision A with the variant's parts taken out and put in."""
    delta: dict[str, int] = {}
    joints = 0
    for ref in v.removed:
        lcsc = comps[ref].get("lcsc") or ""
        if lcsc and lcsc != "none":
            delta[lcsc] = delta.get(lcsc, 0) - 1
        if fps[ref]["mount"] == "smt" and lcsc and lcsc != "none":
            joints -= int(fps[ref]["pads"])
    for n in v.added:
        delta[n.lcsc] = delta.get(n.lcsc, 0) + 1
        joints += n.pads
    lines, seen = [], set()
    for ln in inp.lines:
        d = delta.get(ln.lcsc, 0)
        seen.add(ln.lcsc)
        q = {b: ln.qty[b] + d for b in ln.qty}
        if min(q.values()) < 0:
            raise ValueError(f"{v.key}: more {ln.lcsc} removed than the BOM has")
        lines.append(replace(ln, qty=q))
    for lcsc, d in delta.items():
        if lcsc not in seen:
            lines.append(M.Line(refs="study", mpn=inp.parts[lcsc].mpn, lcsc=lcsc, qty={b: d for b in M.BUILDS}, kind="smt"))
    new = inp.copy(lines=lines)
    for b in M.BUILDS:
        new.joints[b] += joints
    return new


def area(v: Variant, fps: dict) -> tuple[float, float]:
    freed = sum(float(fps[r]["courtyard_mm2"]) for r in v.removed)
    used = sum(n.area_mm2 if n.like is None else float(fps[n.like]["courtyard_mm2"]) for n in v.added)
    return freed, used


def main() -> int:
    comps, fps, inp = load()
    base = {n: M.batch_cost(n, BUILD, inp) for n in QTYS}
    rows = []
    for v in VARIANTS:
        new = apply(v, comps, fps, inp)
        fitted_removed = [r for r in v.removed if (comps[r].get("lcsc") or "none") != "none"]
        freed, used = area(v, fps)
        row = [v.key, v.title, len(v.removed), len(v.added), len(v.added) - len(v.removed),
               M.fee_types(new, BUILD) - M.fee_types(inp, BUILD), new.joints[BUILD] - inp.joints[BUILD]]
        for n in QTYS:
            c = M.batch_cost(n, BUILD, new)
            row.append(f"{c.per_unit() - base[n].per_unit():+.2f}")
        for n in QTYS:
            c = M.batch_cost(n, BUILD, new)
            parts = (c.smt_parts + c.hand_parts - base[n].smt_parts - base[n].hand_parts) / n
            row.append(f"{parts:+.2f}")
        row += [f"{freed:.0f}", f"{used:.0f}", f"{freed - used:.0f}", " ".join(v.removed), ", ".join(f"{n.name} ({n.part}, {n.lcsc})" for n in v.added)]
        rows.append(row)
        assert len(fitted_removed) <= len(v.removed)
    header = ["variant", "what", "parts removed", "parts added", "net parts", "extended part types that pay the fee", "SMT joints",
              *[f"landed cost per controller at {n}, USD" for n in QTYS], *[f"of which parts at {n}, USD" for n in QTYS],
              "courtyard area freed, mm2", "courtyard area of the new parts, mm2", "net area, mm2", "removed", "added"]
    out = HERE / "results"
    with (out / "variants.csv").open("w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(header)
        w.writerows(rows)
    board = inp.sourced["board"]
    md = ["# Variants against revision A: parts, cost, area", "", "Study, not the product. Written by `cost.py`; do not edit.", "",
          f"8-zone build. Revision A by the same model: {', '.join(f'{base[n].per_unit():.2f} USD per controller at {n}' for n in QTYS)}",
          f"(board, parts, assembly, freight, duty, enclosure and the builder's time, as `docs/hardware/cost.md`). The board is {board['width_mm']} x {board['height_mm']} mm,",
          f"{board['width_mm'] * board['height_mm']} mm2, with {inp.joints[BUILD]} SMT joints and {M.fee_types(inp, BUILD)} extended part types that pay the feeder fee.",
          "Prices: the snapshot of `tools/cost` of 2026-11-11, and `new-parts.csv` of 2026-11-12 for the one-shot. A negative number is a saving.", "",
          "| " + " | ".join(header[:-2]) + " |", "|" + "---|" * (len(header) - 2)]
    for r in rows:
        md.append("| " + " | ".join(str(x) for x in r[:-2]) + " |")
    md += ["", "| variant | removed | added |", "|---|---|---|"]
    for r in rows:
        md.append(f"| {r[0]} | {r[-2]} | {r[-1] or 'nothing'} |")
    (out / "variants.md").write_text("\n".join(md) + "\n")
    print((out / "variants.md").read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
