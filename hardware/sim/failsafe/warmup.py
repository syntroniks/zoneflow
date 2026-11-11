"""Release times in the stop test against how long the keep-alive ran.

The start-up self-test and the polling cycle of design section 19.6 toggle
the keep-alive for a short time only, with no valve and no relay, and then
run the stop test. The firmware team asked whether the release limits of
step 9 hold after such a short toggle (firmware architecture, section 20,
question 1). This runs the keep-alive at the firmware's 5 ms edges for 0.1
to 2 s, stops it low, and times both read-backs, at typical values and at
the corners of the grid that release latest, release earliest and charge
slowest.
"""

from __future__ import annotations

import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import corners as C
import run as R
from model import NETLIST, Params, Stimulus, load_netlist

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"

DURATIONS = (0.1, 0.15, 0.2, 0.3, 0.5, 1.0, 2.0)
T0 = 0.05


def _corner(temp: float, v33: float, small: bool, vtn: float, vt_tc: float, sch: float, extra: dict | None = None) -> Params:
    return C.make(
        "", temp=temp, v33=v33, vbuck=5.16 if small else 5.40, bleed=0.985 if small else 1.015, r=1.0,
        c_res=C.RANGES["c_res"][0] if small else C.RANGES["c_res"][1], c_pump=C.RANGES["c_pump"][0] if small else C.RANGES["c_pump"][1],
        sch_is=sch, vtn=vtn, vt_tc=vt_tc, vos=15e-3 if small else -15e-3, lm_ib=25e-9 if small else 500e-9,
        gpio_ro=70.0 if small else 16.0, k1_r=1.0, hc_vtp=0.55, hc_vtm=0.36, thi=5e-3, tlo=5e-3, stop="low", scale_extra=extra).params


CASES = {
    "typical parts, 27 C": Params(),
    "latest release: 70 C, 3.6 V, large capacitors, Q2 at 0.65 V": _corner(70.0, 3.6, False, 0.65, -4.5e-3, 1.5e-8),
    "earliest release: 70 C, 3.0 V, small capacitors, Q2 at 1.45 V, leaky diodes": _corner(70.0, 3.0, True, 1.45, -1.5e-3, 6.6e-7),
    "slowest to charge: -20 C, 3.0 V, large reservoirs, small pump capacitors, Q2 at 1.45 V": _corner(
        -20.0, 3.0, True, 1.45, -4.5e-3, 1.5e-8, {"C23": C.RANGES["c_res"][1], "C50": C.RANGES["c_res"][1]}),
}


def one(comps: dict, case: str, dur: float) -> dict:
    p = CASES[case]
    t_stop = T0 + dur
    stim = Stimulus(f"{case}, {dur:g} s", [(0.0, "low"), (T0, ("toggle", 5e-3, 5e-3)), (t_stop, "low")], [], maxstep=R.STEP)
    tend = t_stop + 3.5
    tr = R.simulate(comps, p, [], 0.0, stim, tend)
    row = {"case": case, "toggled_s": dur}
    if tr is None:
        row["sim"] = "failed"
        return row

    def first(name: str, want: int, t0: float, t1: float) -> float | None:
        t = t0
        while t <= t1:
            if R.gpio(tr.at(t, name), p.v33) == want:
                return t - t0
            t += 1e-3
        return None

    row.update(sim="ok", suspect="" if tr.suspect is None else f"{tr.suspect:.4f}",
               a_on=R.gpio(tr.at(t_stop - 1e-3, "arb"), p.v33) == 1, b_on=R.gpio(tr.at(t_stop - 1e-3, "brb"), p.v33) == 0,
               t_a=first("arb", 0, t_stop, tend), t_b=first("brb", 1, t_stop, tend))
    return row


def cmd_warmup(args) -> int:
    comps = load_netlist(Path(args.netlist))
    jobs = [(case, d) for case in CASES for d in DURATIONS]
    print(f"{len(jobs)} runs", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(lambda j: one(comps, *j), jobs))
    RESULTS.mkdir(exist_ok=True)
    C.write_csv(RESULTS / f"{args.tag}.csv", rows)
    by = dict(zip(jobs, rows))

    def cell(case: str, d: float) -> str:
        r = by[(case, d)]
        if r.get("sim") != "ok":
            return "did not simulate"
        if not (r["a_on"] and r["b_on"]):
            return "not both enabled yet: " + ", ".join(n for n, on in (("A", r["a_on"]), ("B", r["b_on"])) if not on) + " off"
        return f"{r['t_a']:.2f} / {r['t_b']:.2f}"

    lines = ["<!-- Written by hardware/sim/failsafe/failsafe.py warmup. Do not edit. -->", "",
             "tA / tB in seconds: the time from the stop until the gate-drive read-back is low and until the relay read-back is high, "
             "after the keep-alive has toggled at 5 ms edges for the time in the first column. No valve and no relay.", "",
             "| Toggled for | " + " | ".join(CASES) + " |", "|---|" + "---|" * len(CASES)]
    for d in DURATIONS:
        lines.append(f"| {d:g} s | " + " | ".join(cell(case, d) for case in CASES) + " |")
    lines += ["", f"Solver check: {sum(1 for r in rows if r.get('suspect'))} of {len(rows)} runs show the over-current latch setting with no cause.", ""]
    (RESULTS / f"{args.tag}.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def register(sub) -> None:
    w = sub.add_parser("warmup", help="release times against how long the keep-alive ran before the stop")
    w.add_argument("--netlist", default=str(NETLIST))
    w.add_argument("--tag", default="warmup")
    w.add_argument("--jobs", type=int, default=os.cpu_count())
    w.set_defaults(fn=cmd_warmup)
