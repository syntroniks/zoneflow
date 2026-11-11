"""How strong a bridge onto the keep-alive net has to be to matter.

A bridge from `KA` to a net that toggles with no control loop behind it is
the one board fault that can feed both detectors. This sweeps the
resistance of such a bridge, for a push-pull source (a PSRAM line, the
backlight PWM, the boot log) and for the zero-cross signal, with the pin
driven low and with the pin high-impedance, at the corners where the
detectors are easiest to hold: a relay transistor with a low threshold, a
warm board, diodes with typical leakage and with low leakage.

It also runs the self-test with each bridge, to see which step reports it.
"""

from __future__ import annotations

import csv
import os
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
from pathlib import Path

import run as R
from faults import EXT_SOURCES
from model import NETLIST, Params, Prim, load_netlist

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"

OHMS = (0.01, 100.0, 470.0, 1e3, 2.2e3, 3.3e3, 4.7e3, 10e3)
CORNERS = {
    "typical parts, 27 C": {},
    "Q2 at 0.65 V, 27 C": {"vtn": {"Q2": 0.65}},
    "Q2 at 0.65 V, -4.5 mV/K, 70 C": {"vtn": {"Q2": 0.65}, "vt_tc": -4.5e-3, "temp": 70.0},
    "Q2 at 0.65 V, -4.5 mV/K, 70 C, low-leakage diodes, 3V3 at 3.4 V": {"vtn": {"Q2": 0.65}, "vt_tc": -4.5e-3, "temp": 70.0, "sch_is": 1.5e-8, "v33": 3.4},
}
SOURCES = {
    "push-pull, 3.3 V": ("x_dqs", {"x_dqs": EXT_SOURCES["x_dqs"]}, False),
    "zero-cross signal AC_ZC": ("AC_ZC", {}, True),
}


def healthy(comps: dict, source: str, corner: str) -> dict:
    """The self-test with no bridge at this corner. Steps that fail here are
    not the bridge's doing, and the release times are this board's own."""
    _, ext, ac = SOURCES[source]
    p = replace(Params(), ac=ac, ext=dict(ext), **CORNERS[corner])
    pull = any("IO39" in k for k, net in comps["U4"]["pins"].items() if net == "KA")
    tr = R.simulate(comps, p, [], 0.0, R.selftest_stimulus(pull), R.T_SELFTEST_END)
    st = R.read_selftest(tr, p)
    return {"fails": {c.step for c in st.checks if c.ok is False}, "learned": (st.t_a, st.t_b) if st.t_a and st.t_b else None}


def one(comps: dict, base: dict, source: str, ohms: float, corner: str, stop: str) -> dict:
    node, ext, ac = SOURCES[source]
    p = replace(Params(), ac=ac, ext=dict(ext), **CORNERS[corner])
    prims = [Prim("bridge", ("KA", node, ohms))]
    pull = any("IO39" in k for k, net in comps["U4"]["pins"].items() if net == "KA")
    row = {"source": source, "ohms": ohms, "corner": corner, "stop": stop}
    try:
        if stop == "selftest":
            tr = R.simulate(comps, p, prims, 0.0, R.selftest_stimulus(pull), R.T_SELFTEST_END)
            ref = base[(source, corner)]
            st = R.read_selftest(tr, p, learned=ref["learned"]) if tr else None
            for key, steps in (("found_old", R.OLD_STEPS), ("found_new", R.NEW_STEPS)):
                first = st.first(tuple(s for s in steps if s not in ref["fails"])) if st else None
                row[key] = first.step.replace(" (old limits)", "") if first else ""
            row["suspect"] = bool(tr and tr.suspect is not None)
        else:
            tr = R.simulate(comps, p, prims, R.C_FAULT_IN_RUN, R.crash_stimulus(stop, ka_pullup=pull), R.C_END)
            c = R.read_crash(tr, vt_n=p.vt_n("Q2")) if tr else None
            row.update(on=bool(c and c.on_at_end), a=bool(c and c.led_at_end), b=bool(c and (c.relay_at_end or c.q2_at_end)), suspect=bool(tr and tr.suspect is not None))
    except R.Oscillates:
        row["found_old"] = row["found_new"] = "run not finished"
    return row


def cmd_bridges(args) -> int:
    comps = load_netlist(Path(args.netlist))
    jobs = [(s, o, c, stop) for s in SOURCES for o in OHMS for c in CORNERS for stop in ("low", "hiz", "selftest")]
    refs = [(s, c) for s in SOURCES for c in CORNERS]
    print(f"{len(jobs) + len(refs)} runs", flush=True)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        base = dict(zip(refs, pool.map(lambda j: healthy(comps, *j), refs)))
        rows = list(pool.map(lambda j: one(comps, base, *j), jobs))
    RESULTS.mkdir(exist_ok=True)
    with (RESULTS / f"{args.tag}.csv").open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=["source", "ohms", "corner", "stop", "on", "a", "b", "found_old", "found_new", "suspect"], lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    lines = ["<!-- Written by hardware/sim/failsafe/failsafe.py bridges. Do not edit. -->", "",
             "| Net bridged to `KA` | Bridge | Pin driven low after the stop | Pin high-impedance after the stop | Reported by the sequence as merged | Reported by the sequence with step 2b |",
             "|---|---|---|---|---|---|"]

    def cell(sel: list[dict]) -> str:
        if any(r["on"] for r in sel):
            n = sum(1 for r in sel if r["on"])
            return f"**valve stays on** in {n} of {len(sel)} corners"
        held = [x for x, key in (("A", "a"), ("B", "b")) if any(r[key] for r in sel)]
        return ("detector " + " and ".join(held) + " held, valve off") if held else "both let go"

    def found(sel: list[dict], key: str) -> str:
        tests = [r[key] for r in sel if r["stop"] == "selftest"]
        if not any(tests):
            return "not reported"
        steps = ", ".join(sorted({t for t in tests if t}))
        return f"every corner: {steps}" if all(tests) else f"{sum(1 for t in tests if t)} of {len(tests)} corners: {steps}"

    for s in SOURCES:
        for o in OHMS:
            sel = [r for r in rows if r["source"] == s and r["ohms"] == o]
            label = "0 ohm" if o < 1 else (f"{o:g} ohm" if o < 1000 else f"{o / 1000:g} kohm")
            lines.append(f"| {s} | {label} | {cell([r for r in sel if r['stop'] == 'low'])} | {cell([r for r in sel if r['stop'] == 'hiz'])} | "
                         f"{found(sel, 'found_old')} | {found(sel, 'found_new')} |")
    # a valve that stays on in a corner where the sequence reports nothing
    silent = {"found_old": [], "found_new": []}
    for s in SOURCES:
        for o in OHMS:
            for c in CORNERS:
                sel = {r["stop"]: r for r in rows if r["source"] == s and r["ohms"] == o and r["corner"] == c}
                if sel["low"].get("on") or sel["hiz"].get("on"):
                    for key in silent:
                        if not sel["selftest"].get(key):
                            silent[key].append(f"{s}, {o:g} ohm, {c}")
    lines += ["", "Corners: " + "; ".join(CORNERS) + ".", "",
              "A step counts as reporting the bridge only if the same step passes at the same corner with no bridge. "
              "The band of step 9 is taken around the release times of the same corner with no bridge.", "",
              f"Cases in which a valve stays on and the sequence as merged reports nothing: {len(silent['found_old'])} of the "
              f"{sum(1 for s in SOURCES for o in OHMS for c in CORNERS)} combinations of net, bridge and corner.",
              f"Cases in which a valve stays on and the sequence with step 2b reports nothing: {len(silent['found_new'])}"
              + (": " + "; ".join(silent["found_new"]) if silent["found_new"] else "") + ".", "",
              f"Solver check: {sum(1 for r in rows if r.get('suspect'))} of {len(rows)} runs show the over-current latch setting with no cause.", ""]
    (RESULTS / f"{args.tag}.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def register(sub) -> None:
    b = sub.add_parser("bridges", help="sweep the resistance of a bridge onto the keep-alive net")
    b.add_argument("--netlist", default=str(NETLIST))
    b.add_argument("--tag", default="bridges")
    b.add_argument("--jobs", type=int, default=os.cpu_count())
    b.set_defaults(fn=cmd_bridges)
