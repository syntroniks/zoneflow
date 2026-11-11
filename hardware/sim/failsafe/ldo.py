"""A shorted 3.3 V regulator: are the valves off whatever the processor does?

Finding M5 of review A2. U3 fails with its input joined to its output and
`3V3` goes to `V5`, 4.86 to 5.10 V on 24 VAC. The expander, U7, the
comparators and both detectors are inside their ratings there. The
processor is not (3.6 V absolute maximum), and nothing can be said about
what it does. So the question is put to the hardware alone, for a processor
that is dead and for one that still moves its pins:

- **dead**: the valve runs, U3 shorts, and half a second later the
  keep-alive pin stops low, stops high or goes high-impedance for good.
- **erratic**: the valve runs, U3 shorts, and the pin keeps toggling with
  nothing behind it. Nothing is written to the expander after the fault.
- **pin joined to its supply**: the processor is dead with the keep-alive
  pin shorted to `3V3` inside the chip, and it loads the rail in bursts, so
  that the rail itself swings and the pin swings with it.
- **no edges**: U3 is shorted from power-up and the pin is steady, makes
  one edge or toggles slowly. Nothing may enable drive.
- **the step itself**: the keep-alive stopped long ago with the pin high,
  then U3 shorts. The pin follows the rail up by 1.6 V, which is an edge.
- **threshold**: `3V3` is set to levels from 3.6 to 4.3 V with the valve
  running, with the detector U13 and its divider at their tolerance limits.

Every case is run twice: with the over-voltage detector U13 of design
section 9.5, and without it, which is the board as review A2 left it. "Without"
keeps U13 in the deck and puts its threshold out of reach.
"""

from __future__ import annotations

import csv
import itertools
import os
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
from pathlib import Path

import corners as C
import run as R
from model import NETLIST, Params, Prim, Stimulus, load_netlist

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"

T_FAULT, T_STOP, T_END = R.C_FAULT_IN_RUN, R.C_STOP, 6.4
SHORT = Prim("bridge", ("3V3", "V5", 0.05))
PIN_TO_RAIL = Prim("bridge", ("KA", "3V3", 30.0))
DETECTOR = {"with U13": {}, "without U13": {"ov_vit": 100.0}}
WRITES = [(R.C_CONFIG, "oe", n, 1) for n in ("z1", "mv", "rly")] + [(R.C_CONFIG, "lv", "rly", 0), (R.C_CONFIG + 0.02, "lv", "rly", 1), (R.C_VALVE, "lv", "z1", 0)]


def corner(temp: float, small: bool, vtn: float, vt_tc: float, sch: float) -> Params:
    """As the grid of corners.py. `3V3` does not matter here: after the fault
    it is `V5`, 4.86 V with the low `V_BUCK` and 5.10 V with the high one."""
    return C.make("", temp=temp, v33=3.3, vbuck=5.16 if small else 5.40, bleed=0.985 if small else 1.015, r=1.0,
                  c_res=C.RANGES["c_res"][0] if small else C.RANGES["c_res"][1], c_pump=C.RANGES["c_pump"][0] if small else C.RANGES["c_pump"][1],
                  sch_is=sch, vtn=vtn, vt_tc=vt_tc, vos=15e-3 if small else -15e-3, lm_ib=25e-9 if small else 500e-9,
                  gpio_ro=70.0 if small else 16.0, k1_r=1.1 if small else 0.9, hc_vtp=0.55, hc_vtm=0.36, thi=5e-3, tlo=5e-3, stop="low").params


def grid() -> dict[str, Params]:
    out = {}
    for temp, small, vtn, vt_tc, sch in itertools.product((-20.0, 27.0, 70.0), (True, False), (0.65, 1.45), (-4.5e-3, -1.5e-3), (1.5e-8, 6.6e-7)):
        out[f"{temp:g} C, {'small' if small else 'large'} capacitors, Vth {vtn} V at {vt_tc * 1e3:g} mV/K, IS {sch:g}"] = corner(temp, small, vtn, vt_tc, sch)
    return out


# The corners that hold longest and that hold least, and typical parts.
FEW = {
    "typical parts, 27 C": Params(),
    "holds longest: 70 C, large capacitors, Q2 at 0.65 V, -4.5 mV/K": corner(70.0, False, 0.65, -4.5e-3, 1.5e-8),
    "holds least: -20 C, small capacitors, Q2 at 1.45 V, -4.5 mV/K": corner(-20.0, True, 1.45, -4.5e-3, 1.5e-8),
    "70 C, small capacitors, Q2 at 0.65 V, leaky diodes": corner(70.0, True, 0.65, -4.5e-3, 6.6e-7),
}

ERRATIC = {"5 ms / 5 ms": (5e-3, 5e-3), "20 ms / 20 ms": (20e-3, 20e-3), "1 ms / 20 ms": (1e-3, 20e-3), "100 ms / 100 ms": (0.1, 0.1), "0.5 ms / 0.5 ms": (0.5e-3, 0.5e-3)}
# The rail is pulled to 2.0 V for a fifth of each period, as a chip that
# latches up and lets go again might do. 2.0 V keeps the expander's bits.
HICCUP_HZ = (2.0, 10.0, 50.0, 200.0)
LEVELS = (3.6, 3.7, 3.8, 3.9, 3.95, 4.0, 4.05, 4.1, 4.15, 4.2, 4.3)
# U13 and its divider at the limits of the data sheet and of 1 % resistors.
TRIP = {
    # ov_ib is the current into the pin: 25 nA out of it lifts the pin by 2.5 mV.
    "trips lowest": {"ov_vit": 0.397, "ov_ib": -25e-9, "scale": {"R13": 0.99, "R100": 1.01}},
    "typical": {},
    "trips highest": {"ov_vit": 0.403, "ov_ib": 25e-9, "scale": {"R13": 1.01, "R100": 0.99}},
}


def with_detector(p: Params, det: str, extra: dict | None = None) -> Params:
    kw = dict(DETECTOR[det])
    extra = dict(extra or {})
    scale = {**p.scale, **extra.pop("scale", {})}
    kw.update(extra)
    return replace(p, scale=scale, **kw)


def read(tr: R.Trace | None, p: Params, t_from: float) -> dict:
    """Valve current after t_from: when it ends, and whether it is there at the end."""
    if tr is None:
        return {"sim": "failed"}
    closed = R.relay_closed(tr.col["icoil"])
    led = [i > R.I_LED for i in tr.col["iz1"]]
    valve = [a and b for a, b in zip(closed, led)]
    i0, n = tr.idx(t_from), len(valve)
    tail = n - int(0.05 / R.STEP)

    def last(series: list[bool]) -> float | None:
        for i in range(n - 1, i0 - 1, -1):
            if series[i]:
                return tr.t[i] - t_from
        return None

    ran = sum(valve[tr.idx(T_FAULT - 0.2): tr.idx(T_FAULT)]) > 0.9 * (0.2 / R.STEP)
    return {"sim": "ok", "suspect": "" if tr.suspect is None else f"{tr.suspect:.4f}", "ran": ran, "on_at_end": any(valve[tail:]),
            "t_valve": last(valve), "t_led": last(led), "t_relay": last(closed),
            "rail": tr.col["v33"][tr.idx(T_STOP - 0.01)], "drv_en_max": max(tr.col["drv_en"][i0:]), "alive_after": max(tr.col["alive"][i0:]) > 0.5,
            "g2_end": tr.col["g2"][n - 1]}


def sim(comps: dict, p: Params, prims: list[Prim], tf: float, stim: Stimulus, tend: float) -> R.Trace | None:
    try:
        return R.simulate(comps, p, prims, tf, stim, tend)
    except R.Oscillates:
        return None


def job_dead(comps: dict, cname: str, p: Params, stop: str, det: str) -> dict:
    p = with_detector(p, det)
    stim = Stimulus(f"dead, {cname}, stops {stop}", [(0.0, "low"), (R.C_START, R.toggle()), (T_STOP, stop)], WRITES, maxstep=R.STEP)
    row = {"case": "dead", "corner": cname, "what": stop, "detector": det}
    row.update(read(sim(comps, p, [SHORT], T_FAULT, stim, T_END), p, T_STOP if det == "without U13" else T_FAULT))
    return row


def job_erratic(comps: dict, cname: str, p: Params, wname: str, det: str) -> dict:
    p = with_detector(p, det)
    thi, tlo = ERRATIC[wname]
    stim = Stimulus(f"erratic, {cname}, {wname}", [(0.0, "low"), (R.C_START, R.toggle()), (T_FAULT, ("toggle", thi, tlo))], WRITES, maxstep=R.STEP)
    row = {"case": "erratic", "corner": cname, "what": wname, "detector": det}
    row.update(read(sim(comps, p, [SHORT], T_FAULT, stim, T_END), p, T_FAULT))
    return row


def job_hiccup(comps: dict, cname: str, p: Params, hz: float, det: str) -> dict:
    per = 1.0 / hz
    spec = (f"VXHC {{node}}_c 0 PULSE(0 1 {T_FAULT + 0.1:g} 20u 20u {per / 5:g} {per:g})\n"
            "SXH n3v3 {node} {node}_c 0 FSWC\nVXH {node} 0 2.0")
    p = replace(with_detector(p, det), ext={"x_hic": spec})
    # The processor is gone at the fault: its pin is no longer driven by it.
    stim = Stimulus(f"hiccup, {cname}, {hz:g} Hz", [(0.0, "low"), (R.C_START, R.toggle()), (T_FAULT, "hiz")], WRITES, maxstep=R.STEP)
    row = {"case": "pin joined to its supply", "corner": cname, "what": f"{hz:g} Hz", "detector": det}
    row.update(read(sim(comps, p, [SHORT, PIN_TO_RAIL], T_FAULT, stim, T_END), p, T_FAULT))
    return row


def job_noedge(comps: dict, cname: str, p: Params, kind: str, det: str) -> dict:
    p = with_detector(p, det)
    stim = Stimulus(f"{kind}; {cname}", C.NO_EDGE[kind], [], maxstep=R.STEP)
    tr = sim(comps, p, [SHORT], 0.0, stim, 6.0)
    row = {"case": "no edges", "corner": cname, "what": kind, "detector": det}
    if tr is None:
        row["sim"] = "failed"
        return row
    i0 = tr.idx(0.010)
    rail = tr.col["v33"][-1]
    row.update(sim="ok", suspect="" if tr.suspect is None else f"{tr.suspect:.4f}", rail=rail, drv_en_max=max(tr.col["drv_en"][i0:]),
               a_enabled=max(tr.col["drv_en"][i0:]) > 0.3 * rail, n2_max=max(tr.col["n2"][i0:]), alive_after=max(tr.col["alive"][i0:]) > 0.5,
               g2_end=max(tr.col["g2"][i0:]))
    return row


def job_step(comps: dict, cname: str, p: Params, level: str, det: str) -> dict:
    p = with_detector(p, det)
    tf = 4.0
    stim = Stimulus(f"step, {cname}, pin {level}", [(0.0, "low"), (R.C_START, R.toggle()), (0.6, level)], WRITES, maxstep=R.STEP)
    prims = [SHORT] + ([PIN_TO_RAIL] if level == "high" else [])
    tr = sim(comps, p, prims, tf, stim, tf + 3.0)
    row = {"case": "the step itself", "corner": cname, "what": f"pin {level}", "detector": det}
    if tr is None:
        row["sim"] = "failed"
        return row
    i0 = tr.idx(tf - 0.01)
    rail = tr.col["v33"][-1]
    closed = R.relay_closed(tr.col["icoil"])
    valve = [a and i > R.I_LED for a, i in zip(closed, tr.col["iz1"])]
    row.update(sim="ok", suspect="" if tr.suspect is None else f"{tr.suspect:.4f}", rail=rail, drv_en_max=max(tr.col["drv_en"][i0:]),
               a_enabled=max(tr.col["drv_en"][i0:]) > 0.3 * rail, n2_max=max(tr.col["n2"][i0:]), alive_after=max(tr.col["alive"][i0:]) > 0.5,
               on_at_end=any(valve[i0:]), g2_end=max(tr.col["g2"][i0:]))
    return row


def job_level(comps: dict, tname: str, level: float) -> dict:
    p = with_detector(Params(), "with U13", TRIP[tname])
    stim = Stimulus(f"3V3 at {level} V, {tname}", [(0.0, "low"), (R.C_START, R.toggle())], WRITES, maxstep=R.STEP)
    tr = sim(comps, p, [Prim("rail", ("3V3", level))], T_FAULT, stim, 2.4)
    row = {"case": "threshold", "corner": tname, "what": f"{level:g}", "detector": "with U13"}
    row.update(read(tr, p, T_FAULT))
    return row


FIELDS = ["case", "corner", "what", "detector", "sim", "suspect", "ran", "on_at_end", "t_valve", "t_led", "t_relay", "rail", "drv_en_max", "a_enabled", "n2_max",
          "alive_after", "g2_end"]


def load(path: Path) -> list[dict]:
    rows = []
    with path.open() as fh:
        for r in csv.DictReader(fh):
            for k in ("t_valve", "t_led", "t_relay", "rail", "drv_en_max", "n2_max", "g2_end"):
                r[k] = float(r[k]) if r.get(k) not in ("", None) else None
            for k in ("ran", "on_at_end", "a_enabled", "alive_after"):
                r[k] = {"True": True, "False": False}.get(r.get(k))
            rows.append(r)
    return rows


def span(vals: list[float | None], unit: str = "s") -> str:
    vals = [v for v in vals if v is not None]
    if not vals:
        return "at once"
    return f"{min(vals):.2f} to {max(vals):.2f} {unit}" if max(vals) - min(vals) >= 0.005 else f"{max(vals):.2f} {unit}"


def ms(vals: list[float | None]) -> str:
    vals = [0.0 if v is None else v for v in vals]
    return f"{max(vals) * 1e3:.1f} ms" if vals else "none"


def summarise(rows: list[dict]) -> list[str]:
    ok = [r for r in rows if r["sim"] == "ok"]
    out = ["<!-- Written by hardware/sim/failsafe/failsafe.py ldo. Do not edit. -->", "",
           f"{len(rows)} runs, {len(rows) - len(ok)} did not simulate. Solver check: {sum(1 for r in ok if r['suspect'])} runs show the over-current latch setting with no cause.", "",
           "U3 shorted input to output: `3V3` is joined to `V5`. Zone 1 runs when the fault appears, unless the row says otherwise. "
           "\"Without U13\" is the board as review A2 left it.", ""]
    rails = [r["rail"] for r in ok if r["case"] == "dead" and r["rail"] is not None]
    if rails:
        out += [f"`3V3` after the fault: {min(rails):.2f} to {max(rails):.2f} V over the corners.", ""]

    out += ["## The processor is dead: the pin stops for good 0.5 s after the fault", "",
            "| Detector | Pin stops | Valve still on 5 s later | Valve current ends | Gate drive ends | Relay coil current ends |", "|---|---|---|---|---|---|"]
    for det in DETECTOR:
        for stop in ("low", "high", "hiz"):
            sel = [r for r in ok if r["case"] == "dead" and r["detector"] == det and r["what"] == stop]
            ref = "after the stop" if det == "without U13" else "after the fault"
            valve = ms([r["t_valve"] for r in sel]) if det == "with U13" else span([r["t_valve"] for r in sel])
            ledt = ms([r["t_led"] for r in sel]) if det == "with U13" else span([r["t_led"] for r in sel])
            out.append(f"| {det} | {stop} | {sum(1 for r in sel if r['on_at_end'])} of {len(sel)} corners | {valve} {ref} | {ledt} {ref} | "
                       f"{span([r['t_relay'] for r in sel])} {ref} |")
    out.append("")

    out += ["## The processor is erratic: the pin keeps toggling, nothing writes the expander", "",
            "| Detector | Keep-alive after the fault, high / low | Valve still on 5.5 s after the fault | Valve current ends after the fault |", "|---|---|---|---|"]
    for det in DETECTOR:
        for w in ERRATIC:
            sel = [r for r in ok if r["case"] == "erratic" and r["detector"] == det and r["what"] == w]
            on = sum(1 for r in sel if r["on_at_end"])
            off = [r["t_valve"] for r in sel if not r["on_at_end"]]
            out.append(f"| {det} | {w} | {on} of {len(sel)} corners | {'never' if on == len(sel) else (ms(off) if det == 'with U13' else span(off))} |")
    out.append("")

    out += ["## The pin is joined to its supply and the rail swings between 2.0 V and `V5`", "",
            "| Detector | Rail pulled down at | Valve still on 5.5 s after the fault | Valve current ends after the fault |", "|---|---|---|---|"]
    for det in DETECTOR:
        for hz in HICCUP_HZ:
            sel = [r for r in ok if r["case"] == "pin joined to its supply" and r["detector"] == det and r["what"] == f"{hz:g} Hz"]
            on = sum(1 for r in sel if r["on_at_end"])
            off = [r["t_valve"] for r in sel if not r["on_at_end"]]
            out.append(f"| {det} | {hz:g} Hz | {on} of {len(sel)} corners | {'never' if on == len(sel) else (ms(off) if det == 'with U13' else span(off))} |")
    out.append("")

    out += ["## No edges: U3 shorted from power-up, no valve asked for", "",
            "| Detector | Keep-alive pin | Drive enabled at any time | Expander out of reset at any time | Reservoir of A, highest (V) | Gate of Q2, highest (V) |", "|---|---|---|---|---|---|"]
    for det in DETECTOR:
        for kind in C.NO_EDGE:
            sel = [r for r in ok if r["case"] == "no edges" and r["detector"] == det and r["what"] == kind]
            if not sel:
                continue
            out.append(f"| {det} | {kind} | {sum(1 for r in sel if r['a_enabled'])} of {len(sel)} corners | {sum(1 for r in sel if r['alive_after'])} of {len(sel)} | "
                       f"{max(r['n2_max'] for r in sel):.2f} | {max(r['g2_end'] for r in sel):.2f} |")
    out.append("")

    out += ["## The step itself: the keep-alive stopped 3.4 s before U3 shorts", "",
            "| Detector | Pin | Drive enabled after the fault | Valve on after the fault | Reservoir of A, highest (V) | Gate of Q2, highest (V) |", "|---|---|---|---|---|---|"]
    for det in DETECTOR:
        for level in ("low", "high", "hiz"):
            sel = [r for r in ok if r["case"] == "the step itself" and r["detector"] == det and r["what"] == f"pin {level}"]
            if not sel:
                continue
            name = {"low": "driven low", "high": "joined to its supply", "hiz": "high-impedance"}[level]
            out.append(f"| {det} | {name} | {sum(1 for r in sel if r['a_enabled'])} of {len(sel)} corners | {sum(1 for r in sel if r['on_at_end'])} of {len(sel)} | "
                       f"{max(r['n2_max'] for r in sel):.2f} | {max(r['g2_end'] for r in sel):.2f} |")
    out.append("")

    out += ["## Where U13 acts", "",
            "The regulator's set point is stepped to the level in the first column while zone 1 runs with typical parts. Each cell gives the voltage "
            "`3V3` then has at the board, a little under the set point, and what the valve does: \"off\" means the valve current ended, after the time shown.", "",
            "| Set point (V) | " + " | ".join(f"U13 and divider: {t}" for t in TRIP) + " |", "|---|" + "---|" * len(TRIP)]
    for level in LEVELS:
        cells = []
        for t in TRIP:
            r = next((x for x in ok if x["case"] == "threshold" and x["corner"] == t and x["what"] == f"{level:g}"), None)
            cells.append("did not simulate" if r is None else f"{r['rail']:.3f} V: " + ("runs on" if r["on_at_end"] else f"off after {(r['t_valve'] or 0) * 1e3:.1f} ms"))
        out.append(f"| {level:g} | " + " | ".join(cells) + " |")
    out.append("")
    return out


def cmd_ldo(args) -> int:
    path = RESULTS / f"{args.tag}.csv"
    if not args.summary_only:
        comps = load_netlist(Path(args.netlist))
        if "U13" not in comps:
            raise SystemExit("this netlist has no U13: nothing to compare")
        jobs = []
        for det in DETECTOR:
            jobs += [(job_dead, (c, p, stop, det)) for c, p in grid().items() for stop in ("low", "high", "hiz")]
            jobs += [(job_erratic, (c, p, w, det)) for c, p in FEW.items() for w in ERRATIC]
            jobs += [(job_hiccup, (c, p, hz, det)) for c, p in FEW.items() for hz in HICCUP_HZ]
            jobs += [(job_noedge, (c, p, kind, det)) for c, p in grid().items() if "1.5 mV/K" not in c for kind in C.NO_EDGE]
            jobs += [(job_step, (c, p, level, det)) for c, p in FEW.items() for level in ("low", "high", "hiz")]
        jobs += [(job_level, (t, level)) for t in TRIP for level in LEVELS]
        if args.cases:
            jobs = [j for j in jobs if j[0].__name__.removeprefix("job_") in args.cases]
        print(f"{len(jobs)} runs", flush=True)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            rows = list(pool.map(lambda j: j[0](comps, *j[1]), jobs))
        RESULTS.mkdir(exist_ok=True)
        with path.open("w", newline="") as fh:
            w = csv.DictWriter(fh, fieldnames=FIELDS, lineterminator="\n")
            w.writeheader()
            for r in rows:
                w.writerow({k: ("" if r.get(k) is None else (f"{r[k]:.4g}" if isinstance(r[k], float) else r[k])) for k in FIELDS})
    lines = summarise(load(path))
    (RESULTS / f"{args.tag}.md").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def register(sub) -> None:
    c = sub.add_parser("ldo", help="a shorted 3.3 V regulator, with and without the over-voltage detector U13")
    c.add_argument("--netlist", default=str(NETLIST))
    c.add_argument("--tag", default="ldo")
    c.add_argument("--cases", nargs="*", help="only these kinds of run: dead, erratic, hiccup, noedge, step, level")
    c.add_argument("--summary-only", action="store_true", help="write the tables again from the result file, without running anything")
    c.add_argument("--jobs", type=int, default=os.cpu_count())
    c.set_defaults(fn=cmd_ldo)
