#!/usr/bin/env python3
"""V1 of the fail-safe study with one thing put back at a time: parts, cost, area.

    uv run --project tools/cost --with 'pyyaml>=6,<7' python hardware/sim/study-failsafe-reduction/review_options.py

STUDY, not the product. Belongs to docs/design/failsafe-reduction-review.md.
It uses cost.py and variants.py of the study unchanged, and adds the rows the
owner needs to choose "V1 plus none, one or two of these". Every row is a
difference from revision A as it is on main, for the 8-zone build, by the
landed-cost model of tools/cost. Writes results/review-options.csv and .md.

The model has never been compared with a real order. The differences are
small against that uncertainty: read the part counts as exact and the cents
as an order of size.
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import cost as C  # noqa: E402
import variants as V  # noqa: E402
from zfcost import model as M  # noqa: E402

V1 = next(v for v in V.VARIANTS if v.key == "V1")

# Parts of revision A that the window comparator needs in front of it. The
# comparators U6B and U6C stay on the board in every variant, because U6D is
# the zero-cross comparator, and the threshold chain R48 to R51 stays for the
# amplifier's bias.
TRIP = ["R52", "C33"]
# The high-gain current channel: second amplifier half, its ADC filter.
HI_CHANNEL = "R42 R43 C28 R44 R45 C29 R47 C31".split()
# Footprints for the area of parts that results/footprints.csv does not list.
LIKE = {r: ("C20" if r.startswith("C") else "R25") for r in HI_CHANNEL}


def variant(key: str, title: str, keep: list[str] = (), more: list[str] = (), add: list = ()) -> V.Variant:
    removed = [r for r in V1.removed if r not in keep] + list(more)
    return V.Variant(key, title, tuple(removed), tuple(V1.added) + tuple(add))


OPTIONS = [
    V1,
    variant("V1+T", "V1 with the over-current comparator on the clear input of the one-shot: R52 and C33 stay", keep=TRIP),
    variant("V1+R", "V1 with K1 kept on an expander bit: K1, D5, Q1, R11, R12, D23, JP1 stay", keep=V.RELAY),
    variant("V1+S", "V1 with the LED supply switch kept: Q3, R25, R26, C21 stay, Q_INV added", keep=V.LED_SWITCH, add=V.INVERTER),
    variant("V1+T+R", "V1 with the comparator on the clear input and K1", keep=TRIP + V.RELAY),
    variant("V1+T+R+S", "V1 with all three: V2 of the study with the fast trip put back", keep=TRIP + V.RELAY + V.LED_SWITCH, add=V.INVERTER),
    variant("V1-H", "V1 with the high-gain current channel removed as well: one amplifier, one ADC pin", more=HI_CHANNEL),
    variant("V1+T-H", "V1 with the comparator on the clear input, and one current channel", keep=TRIP, more=HI_CHANNEL),
]


def area(v: V.Variant, fps: dict) -> tuple[float, float]:
    freed = sum(float(fps[LIKE.get(r, r)]["courtyard_mm2"]) for r in v.removed)
    used = sum(n.area_mm2 if n.like is None else float(fps[n.like]["courtyard_mm2"]) for n in v.added)
    return freed, used


def main() -> int:
    comps, fps, inp = C.load()
    for r in HI_CHANNEL:
        fps.setdefault(r, {"pads": "2", "mount": "smt", "courtyard_mm2": fps[LIKE[r]]["courtyard_mm2"]})
    base = {n: M.batch_cost(n, C.BUILD, inp) for n in C.QTYS}
    soldered = sum(ln.qty[C.BUILD] for ln in inp.lines if ln.kind in ("smt", "tht"))
    v1_cost = None
    rows = []
    for v in OPTIONS:
        new = C.apply(v, comps, fps, inp)
        fitted = [r for r in v.removed if (comps[r].get("lcsc") or "none") != "none"]
        freed, used = area(v, fps)
        costs = [M.batch_cost(n, C.BUILD, new).per_unit() - base[n].per_unit() for n in C.QTYS]
        if v.key == "V1":
            v1_cost = costs
        net = len(v.added) - len(fitted)
        rows.append([v.key, v.title, len(fitted), len(v.added), net, soldered + net, new.joints[C.BUILD] - inp.joints[C.BUILD],
                     *[f"{c:+.2f}" for c in costs], *[f"{c - b:+.2f}" for c, b in zip(costs, v1_cost)], f"{used - freed:+.0f}",
                     " ".join(v.removed), ", ".join(n.name for n in v.added)])
    header = ["option", "what", "soldered parts removed", "parts added", "net parts", "soldered parts on the board", "SMT joints",
              *[f"against revision A at {n}, USD" for n in C.QTYS], *[f"against V1 at {n}, USD" for n in C.QTYS], "courtyard area, mm2", "removed", "added"]
    out = HERE / "results"
    with (out / "review-options.csv").open("w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(header)
        w.writerows(rows)
    md = ["# V1 with one thing put back at a time: parts, cost, area", "", "Study, not the product. Written by `review_options.py`; do not edit.", "",
          f"8-zone build. Revision A on main has {soldered} soldered parts by the bill of materials ({inp.joints[C.BUILD]} SMT joints) and costs",
          f"{', '.join(f'{base[n].per_unit():.2f} USD per controller at {n}' for n in C.QTYS)} by the model of `tools/cost`.",
          "JP1 is a pad pattern and not a soldered part, so V1 removes 39 soldered parts and 40 designators. A negative number is a saving.", "",
          "| " + " | ".join(header[:-2]) + " |", "|" + "---|" * (len(header) - 2)]
    for r in rows:
        md.append("| " + " | ".join(str(x) for x in r[:-2]) + " |")
    md += ["", "| option | removed | added |", "|---|---|---|"]
    for r in rows:
        md.append(f"| {r[0]} | {r[-2]} | {r[-1]} |")
    (out / "review-options.md").write_text("\n".join(md) + "\n")
    print((out / "review-options.md").read_text())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
