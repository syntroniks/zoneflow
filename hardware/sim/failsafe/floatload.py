"""What the net behind a bridge has to do for the float test to see the bridge.

Step 2b of design section 19.7 floats the keep-alive pin for 0.5 s and asks
firmware to keep the display, the backlight and the PSRAM busy meanwhile.
The firmware engineer asked what "busy" means (pull request #37, question
7). bridges.py assumes a net that toggles at 500 Hz with equal high and low
times. This varies that assumption: the share of time the net is high, and
its rate, at the same four corners and the same bridge resistances.

Each case is run twice. As a crash: zone 1 runs, the bridge appears, the pin
goes high-impedance for good, and the run says whether the valve stays on.
And as the self-test with the bridge there from power-up, which says whether
a step reports it. A case matters if the valve stays on. The float test has
done its job if every such case is reported with the net at 50 %, because
that is how firmware runs the nets it controls during the test.
"""

from __future__ import annotations

import csv
import os
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
from pathlib import Path

import bridges as B
import run as R
from model import NETLIST, Params, Prim, load_netlist

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"

OHMS = (0.01, 470.0, 1e3, 2.2e3, 3.3e3, 4.7e3)
# name: (share of the period the net is high, period in seconds)
NETS = {
    "50 % high, 500 Hz": (0.50, 2e-3),
    "50 % high, 2 kHz": (0.50, 5e-4),
    "25 % high, 500 Hz": (0.25, 2e-3),
    "10 % high, 500 Hz": (0.10, 2e-3),
    "90 % high, 500 Hz": (0.90, 2e-3),
    "steady high": (1.0, 0.0),
}


def source(name: str) -> str:
    high, per = NETS[name]
    if per == 0.0:
        return "VXN {node}_s 0 3.3\nRXN {node}_s {node} 30"
    edge = min(5e-6, per / 40)
    return f"VXN {{node}}_s 0 PULSE(0 3.3 1m {edge:g} {edge:g} {high * per - edge:g} {per:g})\nRXN {{node}}_s {{node}} 30"


def healthy(comps: dict, corner: str) -> dict:
    p = replace(Params(), **B.CORNERS[corner])
    tr = R.simulate(comps, p, [], 0.0, R.selftest_stimulus(False), R.T_SELFTEST_END)
    st = R.read_selftest(tr, p)
    return {"fails": {c.step for c in st.checks if c.ok is False}, "learned": (st.t_a, st.t_b) if st.t_a and st.t_b else None}


def one(comps: dict, base: dict, net: str, ohms: float, corner: str, kind: str) -> dict:
    p = replace(Params(), ext={"x_net": source(net)}, **B.CORNERS[corner])
    prims = [Prim("bridge", ("KA", "x_net", ohms))]
    row = {"net": net, "ohms": ohms, "corner": corner, "run": kind}
    # a 2 kHz source needs a finer step than the 0.5 ms grid of the other runs
    fine = NETS[net][1] and NETS[net][1] < 1e-3
    try:
        if kind == "selftest":
            stim = R.selftest_stimulus(False)
            if fine:
                stim.maxstep = 5e-5
            tr = R.simulate(comps, p, prims, 0.0, stim, R.T_SELFTEST_END)
            ref = base[corner]
            st = R.read_selftest(tr, p, learned=ref["learned"]) if tr else None
            first = st.first(tuple(s for s in R.NEW_STEPS if s not in ref["fails"])) if st else None
            row.update(sim="ok" if tr else "failed", found=first.step if first else "")
        else:
            stim = R.crash_stimulus("hiz")
            if fine:
                stim.maxstep = 5e-5
            tr = R.simulate(comps, p, prims, R.C_FAULT_IN_RUN, stim, R.C_END)
            c = R.read_crash(tr, vt_n=p.vt_n("Q2")) if tr else None
            row.update(sim="ok" if tr else "failed", on=bool(c and c.on_at_end), a=bool(c and c.led_at_end), b=bool(c and (c.relay_at_end or c.q2_at_end)))
    except R.Oscillates:
        row["sim"] = "failed"
    return row


def cmd_floatload(args) -> int:
    comps = load_netlist(Path(args.netlist))
    R.TIMEOUT = max(R.TIMEOUT, 300)   # the 2 kHz runs take three times as long as the others
    jobs = [(n, o, c, k) for n in NETS for o in OHMS for c in B.CORNERS for k in ("crash", "selftest")]
    print(f"{len(jobs) + len(B.CORNERS)} runs", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        base = dict(zip(B.CORNERS, pool.map(lambda c: healthy(comps, c), B.CORNERS)))
        rows = list(pool.map(lambda j: one(comps, base, *j), jobs))
    RESULTS.mkdir(exist_ok=True)
    with (RESULTS / f"{args.tag}.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["net", "ohms", "corner", "run", "sim", "on", "a", "b", "found"], lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    lines = ["<!-- Written by hardware/sim/failsafe/failsafe.py floatload. Do not edit. -->", "",
             f"{len(rows)} runs, {sum(1 for r in rows if r.get('sim') != 'ok')} did not simulate. Corners: " + "; ".join(B.CORNERS) + ".", "",
             "A push-pull net of 3.3 V behind 30 ohm is bridged to `KA`. Second column: what the net does. "
             "Third: in how many of the four corners the valve stays on when the pin goes high-impedance with the valve running. "
             "Fourth: in how many the self-test reports the bridge, and by which step.", "",
             "| Bridge | Net | Valve stays on, pin high-impedance | Detector held, valve off | Reported by the self-test |", "|---|---|---|---|---|"]
    silent = []
    for o in OHMS:
        label = "0 ohm" if o < 1 else (f"{o:g} ohm" if o < 1000 else f"{o / 1000:g} kohm")
        for n in NETS:
            cr = [r for r in rows if r["net"] == n and r["ohms"] == o and r["run"] == "crash" and r.get("sim") == "ok"]
            stt = [r for r in rows if r["net"] == n and r["ohms"] == o and r["run"] == "selftest" and r.get("sim") == "ok"]
            steps = sorted({r["found"] for r in stt if r["found"]})
            held = sum(1 for r in cr if not r["on"] and (r["a"] or r["b"]))
            lines.append(f"| {label} | {n} | {sum(1 for r in cr if r['on'])} of {len(cr)} | {held} of {len(cr)} | "
                         f"{sum(1 for r in stt if r['found'])} of {len(stt)}{': ' + ', '.join(steps) if steps else ''} |")
            for c in B.CORNERS:
                on = any(r["on"] for r in cr if r["corner"] == c)
                rep = any(r["found"] for r in rows if r["net"] == n and r["ohms"] == o and r["corner"] == c and r["run"] == "selftest")
                if on and not rep:
                    silent.append(f"{label}, {n}, {c}")
    lines += ["", f"Cases in which a valve stays on and the self-test, run with the net doing the same thing, reports nothing: {len(silent)}"
              + (": " + "; ".join(silent) if silent else "") + ".", ""]
    # the question that matters: does a test at 50 % find every bridge that holds at any duty?
    worst = []
    for o in OHMS:
        for c in B.CORNERS:
            holds = [n for n in NETS if any(r["on"] for r in rows if r["net"] == n and r["ohms"] == o and r["corner"] == c and r["run"] == "crash")]
            found50 = any(r["found"] for r in rows if r["net"] == "50 % high, 500 Hz" and r["ohms"] == o and r["corner"] == c and r["run"] == "selftest")
            if holds and not found50:
                worst.append(f"{o:g} ohm, {c}: held by " + ", ".join(holds))
    lines += [f"Bridges that hold the valve on with the net at some duty and that a self-test with the net at 50 % does not report: {len(worst)}"
              + (": " + "; ".join(worst) if worst else "") + ".", ""]
    (RESULTS / f"{args.tag}.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def register(sub) -> None:
    c = sub.add_parser("floatload", help="what the bridged net must do for the float test to see the bridge")
    c.add_argument("--netlist", default=str(NETLIST))
    c.add_argument("--tag", default="floatload")
    c.add_argument("--jobs", type=int, default=os.cpu_count())
    c.set_defaults(fn=cmd_floatload)
