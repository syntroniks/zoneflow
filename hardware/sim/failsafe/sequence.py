"""The self-test sequence on a board with no fault, at every tolerance corner.

The corner runs of corners.py look at holding and release. They do not run
the sequence of design section 19.7, so they cannot show a step that fails a
healthy board. This does: steps 1 to 9 with one short run of zone 1, at the
96 corners of the grid with the firmware's 5 ms edges, and every pass
condition as run.py applies it to a fault.

It found one. Step 2 holds the keep-alive pin high and then expects the
relay read-back to read high. The single rising edge of that test lifts the
gate of Q2 to about 1 V, and a Q2 with a low threshold stays on until the
bleed resistors have pulled the gate down again. With the 1.0 s of review A2
two corners failed. The second half of this file measures how long the
read-back stays low after one edge, which is what the hold has to cover.
"""

from __future__ import annotations

import itertools
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import corners as C
import run as R
from model import NETLIST, Params, Stimulus, load_netlist

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"

T_EDGE = 0.35
T_WATCH = 4.0


def grid() -> dict[str, Params]:
    out = {}
    for temp, v33, small, vtn, vt_tc, sch in itertools.product((-20.0, 27.0, 70.0), (3.0, 3.6), (True, False), (0.65, 1.45), (-4.5e-3, -1.5e-3), (1.5e-8, 6.6e-7)):
        name = f"{temp:g} C, 3V3 {v33} V, {'small' if small else 'large'} capacitors, Vth {vtn} V at {vt_tc * 1e3:g} mV/K, Schottky IS {sch:g}"
        out[name] = C.make(name, temp=temp, v33=v33, vbuck=5.16 if small else 5.40, bleed=0.985 if small else 1.015, r=1.0,
                           c_res=C.RANGES["c_res"][0] if small else C.RANGES["c_res"][1], c_pump=C.RANGES["c_pump"][0] if small else C.RANGES["c_pump"][1],
                           sch_is=sch, vtn=vtn, vt_tc=vt_tc, vos=15e-3 if small else -15e-3, lm_ib=25e-9 if small else 500e-9,
                           gpio_ro=70.0 if small else 16.0, k1_r=1.1 if small else 0.9, hc_vtp=0.55, hc_vtm=0.36, thi=5e-3, tlo=5e-3, stop="low").params
    return out


def run_sequence(comps: dict, name: str, p: Params) -> dict:
    row = {"corner": name, "run": "sequence"}
    try:
        tr = R.simulate(comps, p, [], 0.0, R.selftest_stimulus(False), R.T_SELFTEST_END)
    except R.Oscillates:
        tr = None
    if tr is None:
        row["sim"] = "failed"
        return row
    st = R.read_selftest(tr, p)
    bad = [f"{c.step}: {c.text}" + ("" if c.ok is False else " (level not defined)") for c in st.checks if c.ok is not True and "old limits" not in c.step]
    row.update(sim="ok", suspect="" if tr.suspect is None else f"{tr.suspect:.4f}", fails="; ".join(bad), t_a=st.t_a, t_b=st.t_b)
    return row


def run_edge(comps: dict, name: str, p: Params) -> dict:
    """One rising edge from rest, then the pin stays high: how long is the
    relay read-back not high, and the gate-drive read-back not low?"""
    row = {"corner": name, "run": "one edge"}
    stim = Stimulus(f"one edge, {name}", [(0.0, "low"), (T_EDGE, "high")], [], maxstep=R.STEP)
    try:
        tr = R.simulate(comps, p, [], 0.0, stim, T_EDGE + T_WATCH)
    except R.Oscillates:
        tr = None
    if tr is None:
        row["sim"] = "failed"
        return row
    b_low = a_high = None
    t = T_EDGE
    while t <= T_EDGE + T_WATCH - 0.05:
        if R.gpio(tr.at(t, "brb"), p.v33) != 1:
            b_low = t - T_EDGE
        if R.gpio(tr.at(t, "arb"), p.v33) != 0:
            a_high = t - T_EDGE
        t += 1e-3
    row.update(sim="ok", suspect="" if tr.suspect is None else f"{tr.suspect:.4f}", fails="", t_b_low=b_low, t_a_high=a_high,
               g2_max=max(tr.col["g2"]), vth_q2=p.vt_n("Q2"), temp=p.temp)
    return row


def cmd_sequence(args) -> int:
    comps = load_netlist(Path(args.netlist))
    corners = grid()
    jobs = [(run_sequence, n, p) for n, p in corners.items()] + [(run_edge, n, p) for n, p in corners.items()]
    print(f"{len(jobs)} runs", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        rows = list(pool.map(lambda j: j[0](comps, j[1], j[2]), jobs))
    RESULTS.mkdir(exist_ok=True)
    C.write_csv(RESULTS / f"{args.tag}.csv", rows)
    seq = [r for r in rows if r["run"] == "sequence"]
    edge = [r for r in rows if r["run"] == "one edge" and r.get("sim") == "ok"]
    ok = [r for r in seq if r.get("sim") == "ok"]
    failing = [r for r in ok if r["fails"]]
    lines = ["<!-- Written by hardware/sim/failsafe/failsafe.py sequence. Do not edit. -->", "",
             f"The self-test sequence of design section 19.7 on a board with no fault, with step 2 holding the pin high for {R.T_HIGH_HOLD:g} s: "
             f"{len(seq)} corners, {len(seq) - len(ok)} did not simulate, {len(failing)} fail a step.", ""]
    if failing:
        lines += ["| Corner | Step that fails |", "|---|---|"] + [f"| {r['corner']} | {r['fails']} |" for r in failing] + [""]
    lines += [f"Release times in step 9 over the corners that pass: tA {min(r['t_a'] for r in ok if r['t_a']):.2f} to {max(r['t_a'] for r in ok if r['t_a']):.2f} s, "
              f"tB {min(r['t_b'] for r in ok if r['t_b']):.2f} to {max(r['t_b'] for r in ok if r['t_b']):.2f} s.", "",
              "## One rising edge from rest, then the pin stays high", "",
              "How long the relay read-back does not read high after the edge. Step 2 must hold the pin for longer than this before it reads. "
              "The gate-drive read-back stays low in every corner unless the last column says otherwise.", "",
              "| Temperature | Relay read-back not high for, longest (s) | Corner | Gate of Q2, highest (V) | Corners in which the gate-drive read-back left low |", "|---|---|---|---|---|"]
    for temp in (-20.0, 27.0, 70.0):
        sel = [r for r in edge if r["temp"] == temp]
        worst = max(sel, key=lambda r: r["t_b_low"] or 0.0)
        lines.append(f"| {temp:g} C | {(worst['t_b_low'] or 0.0):.2f} | {worst['corner']} | {max(r['g2_max'] for r in sel):.2f} | {sum(1 for r in sel if r['t_a_high'] is not None)} of {len(sel)} |")
    over = sorted({round(r["t_b_low"], 2) for r in edge if (r["t_b_low"] or 0) > 1.0})
    lines += ["", f"Corners in which it is longer than the 1.0 s that review A2 gave step 2: {sum(1 for r in edge if (r['t_b_low'] or 0) > 1.0)} of {len(edge)}"
              + (f" ({', '.join(f'{x:.2f}' for x in over)} s)." if over else "."),
              f"Solver check: {sum(1 for r in rows if r.get('suspect'))} of {len(rows)} runs show the over-current latch setting with no cause.", ""]
    (RESULTS / f"{args.tag}.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def register(sub) -> None:
    c = sub.add_parser("sequence", help="the self-test sequence on a healthy board at every corner")
    c.add_argument("--netlist", default=str(NETLIST))
    c.add_argument("--tag", default="sequence")
    c.add_argument("--jobs", type=int, default=os.cpu_count())
    c.set_defaults(fn=cmd_sequence)
