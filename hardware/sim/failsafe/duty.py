"""How lopsided the keep-alive may be before detector B lets go of the relay.

The contract of design section 19.4 asked for "an edge at least every
20 ms". A waveform that is high for 1 ms and low for 20 ms meets that and
does not hold the relay at the worst corner: R89 and the pin resistance
charge C51 with a time constant of about 3 ms, so a 1 ms phase moves a
quarter of the charge a long one does. This sweeps the two phases at that
corner, to find the shortest phase that still holds, and shows what a
smaller R89 would do.

The corner: -20 C, the smallest capacitors, a Q2 with the highest threshold
and the strongest drift, diodes with the highest forward voltage, the pin
at its highest resistance.
"""

from __future__ import annotations

import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import corners as C
from model import NETLIST, load_netlist

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"

# high phase, low phase, in ms
WAVES = [(1, 1), (2.5, 2.5), (5, 5), (10, 10), (20, 20),
         (1, 20), (2, 20), (2.5, 20), (3, 20), (5, 20),
         (20, 1), (20, 2), (20, 2.5), (20, 3), (20, 5),
         (1, 9), (2.5, 7.5), (2.5, 17.5), (17.5, 2.5)]
CASES = {
    "3V3 at 3.0 V": {"v33": 3.0},
    "3V3 at 3.2 V, the lowest U3 delivers": {"v33": 3.2},
    "3V3 at 3.0 V, R89 220 ohm": {"v33": 3.0, "scale_extra": {"R89": 0.22}},
}


def corner(case: str, thi: float, tlo: float) -> C.Corner:
    kw = CASES[case]
    return C.make(
        f"{case}, {thi:g} ms high, {tlo:g} ms low", temp=-20.0, v33=kw["v33"], vbuck=5.16, bleed=0.985, r=1.0,
        c_res=C.RANGES["c_res"][0], c_pump=C.RANGES["c_pump"][0], sch_is=1.5e-8, vtn=1.45, vt_tc=-4.5e-3, vos=15e-3, lm_ib=25e-9,
        gpio_ro=70.0, k1_r=1.1, hc_vtp=0.55, hc_vtm=0.36, thi=thi * 1e-3, tlo=tlo * 1e-3, stop="low", scale_extra=kw.get("scale_extra"))


def cmd_duty(args) -> int:
    comps = load_netlist(Path(args.netlist))
    jobs = [(case, thi, tlo) for case in CASES for thi, tlo in WAVES]
    print(f"{len(jobs)} runs", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(lambda j: C.run_corner(comps, corner(*j)) | {"case": j[0]}, jobs))
    RESULTS.mkdir(exist_ok=True)
    C.write_csv(RESULTS / f"{args.tag}.csv", rows)
    by = dict(zip(jobs, rows))

    def cell(case: str, thi: float, tlo: float) -> str:
        r = by[(case, thi, tlo)]
        if r.get("sim") != "ok":
            return "did not simulate"
        held = r["hold_q2_drop"] <= 0.1
        return f"{r['hold_g2_margin']:+.2f} V, {'held' if held else '**not held**'}"

    lines = ["<!-- Written by hardware/sim/failsafe/failsafe.py duty. Do not edit. -->", "",
             "Gate of Q2 above its threshold while toggling, at the worst corner for detector B, and whether the relay is fully driven "
             "(Q2 drops 0.1 V or less).", "",
             "| Keep-alive, high / low | " + " | ".join(CASES) + " |", "|---|" + "---|" * len(CASES)]
    for thi, tlo in WAVES:
        lines.append(f"| {thi:g} / {tlo:g} ms | " + " | ".join(cell(case, thi, tlo) for case in CASES) + " |")
    lines += ["", f"Solver check: {sum(1 for r in rows if r.get('suspect'))} of {len(rows)} runs show the over-current latch setting with no cause.", ""]
    (RESULTS / f"{args.tag}.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def register(sub) -> None:
    d = sub.add_parser("duty", help="sweep the two phases of the keep-alive at the worst corner for detector B")
    d.add_argument("--netlist", default=str(NETLIST))
    d.add_argument("--tag", default="duty")
    d.add_argument("--jobs", type=int, default=os.cpu_count())
    d.set_defaults(fn=cmd_duty)
