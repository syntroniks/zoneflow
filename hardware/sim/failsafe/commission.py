"""Commissioning: can firmware tell a healthy board from one with an open
bleed resistor, and how wide must the band of step 9 be?

Step 9 of design section 19.7 compares both release times with values the
board stored when it was commissioned. The firmware engineer asked (pull
request #37, questions 3 and 4) what a board must show before it may store
those values, so that a board with one bleed resistor already open does not
store the fault as its normal, and whether a ratio of the two times helps.

Each board is one set of part values drawn inside the ranges of corners.py.
It is run healthy, with R30 open (detector A, the same as R91 open) and with
R14 open (detector B, the same as R90 open), at seven temperatures. A run
toggles the keep-alive at the firmware's 5 ms edges for 1 s, stops it low
and times both read-backs, which is what a commissioning cycle does. At the
temperatures a board may be commissioned at, the run then toggles for
another second and stops the pin high, to see whether the ratio of the two
kinds of stop says anything about the bleed.

Two sets of boards: random draws ("mc"), and every combination of the
extremes that move a release time ("grid"), with the two reservoir
capacitors also at opposite ends.

The capacitors of the model do not change with temperature: a board keeps
the values it was drawn with. An X5R part moves by up to 15 % over its
range, so the temperature factors found here are a lower bound on what a
bench will find.
"""

from __future__ import annotations

import csv
import itertools
import os
import random
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
from pathlib import Path

import corners as C
import run as R
from model import NETLIST, Params, Stimulus, load_netlist

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"

TEMPS = (-20.0, 0.0, 10.0, 25.0, 40.0, 55.0, 70.0)
# Board temperatures at which a board may be commissioned (design section
# 19.7): the window the die temperature rule is meant to enforce.
COMMISSION_TEMPS = (10.0, 25.0, 40.0)
STATES = {"healthy": {}, "A open": {"R30": 1e6}, "B open": {"R14": 1e6}}
T0, T_TOGGLE, T_WAIT_LOW, T_WAIT_HIGH = 0.05, 1.0, 3.4, 3.8
T_LIMIT = 3.0   # step 9 gives up after 3 s


def boards_mc(n: int, seed: int) -> list[tuple[str, Params]]:
    rng = random.Random(seed)

    def u(key: str) -> float:
        lo, hi, _ = C.RANGES[key]
        return rng.uniform(lo, hi)

    def logu(key: str) -> float:
        lo, hi, _ = C.RANGES[key]
        return lo * (hi / lo) ** rng.random()

    out = []
    for i in range(n):
        scale = {ref: u("r") for ref in C.BLEED + C.RES} | {"C23": u("c_res"), "C50": u("c_res"), "C22": u("c_pump"), "C51": u("c_pump"), "K1": u("k1_r")}
        vbuck = u("vbuck")
        vtp = u("hc_vtp")
        p = Params(v33=u("v33"), v5=min(5.1, vbuck - 0.3), vbuck=vbuck, scale=scale, sch_is=logu("sch_is"),
                   sch_is_ref={"D7": logu("sch_is"), "D22": logu("sch_is")}, vtn={"Q1": u("vtn"), "Q2": u("vtn")}, vt_tc=u("vt_tc"),
                   lm_vos={1: u("vos")}, lm_ib=logu("lm_ib"), gpio_ro=u("gpio_ro"), hc_vtp=vtp, hc_vtm=min(u("hc_vtm"), vtp - 0.09))
        out.append((f"mc {i}", p))
    return out


def boards_grid() -> list[tuple[str, Params]]:
    """Extremes of what moves a release time. The reservoirs of the two
    detectors are taken at the same end and at opposite ends: a ratio of the
    two times is hurt most when they differ."""
    out = []
    lo_r, hi_r = C.RANGES["c_res"][0], C.RANGES["c_res"][1]
    lo_p, hi_p = C.RANGES["c_pump"][0], C.RANGES["c_pump"][1]
    for v33, (c23, c50), vtn, vt_tc, sch in itertools.product(
            (3.0, 3.6), ((lo_r, lo_r), (hi_r, hi_r), (lo_r, hi_r), (hi_r, lo_r)), (0.65, 1.45), (-4.5e-3, -1.5e-3), (1.5e-8, 6.6e-7)):
        small_a, small_b = c23 == lo_r, c50 == lo_r
        scale = {"R30": 0.985 if small_a else 1.015, "R91": 0.985 if small_a else 1.015, "R14": 0.985 if small_b else 1.015, "R90": 0.985 if small_b else 1.015,
                 "C23": c23, "C50": c50, "C22": lo_p if small_a else hi_p, "C51": lo_p if small_b else hi_p}
        p = Params(v33=v33, v5=4.86 if small_a else 5.1, vbuck=5.16 if small_a else 5.40, scale=scale, sch_is=sch, vtn_default=vtn, vt_tc=vt_tc,
                   lm_vos={1: 15e-3 if small_a else -15e-3}, lm_ib=25e-9 if small_a else 500e-9, gpio_ro=70.0 if small_b else 16.0)
        out.append((f"grid 3V3 {v33} V, C23 x{c23:g}, C50 x{c50:g}, Vth {vtn} V at {vt_tc * 1e3:g} mV/K, IS {sch:g}", p))
    return out


def one(comps: dict, name: str, base: Params, state: str, temp: float) -> dict:
    p = replace(base, temp=temp, scale={**base.scale, **STATES[state]})
    both = temp in COMMISSION_TEMPS
    t_stop = T0 + T_TOGGLE
    segs = [(0.0, "low"), (T0, R.toggle()), (t_stop, "low")]
    tend = t_stop + T_WAIT_LOW
    if both:
        t2 = tend
        t_stop2 = t2 + T_TOGGLE
        segs += [(t2, R.toggle()), (t_stop2, "high")]
        tend = t_stop2 + T_WAIT_HIGH
    stim = Stimulus(f"{name}, {state}, {temp:g} C", segs, [], maxstep=R.STEP)
    row = {"board": name, "state": state, "temp": temp, "v33": p.v33, "c23": p.scale["C23"], "c50": p.scale["C50"], "c22": p.scale["C22"], "c51": p.scale["C51"],
           "vth_q2": p.vtn.get("Q2", p.vtn_default), "vt_tc": p.vt_tc}
    try:
        tr = R.simulate(comps, p, [], 0.0, stim, tend)
    except R.Oscillates:
        tr = None
    if tr is None:
        row["sim"] = "failed"
        return row

    def first(name_: str, want: int, t0: float, t1: float) -> float | None:
        t = t0
        while t <= t1:
            if R.gpio(tr.at(t, name_), p.v33) == want:
                return t - t0
            t += 1e-3
        return None

    row.update(sim="ok", suspect="" if tr.suspect is None else f"{tr.suspect:.4f}",
               t_en_a=first("arb", 1, T0, t_stop), t_en_b=first("brb", 0, T0, t_stop),
               t_a=first("arb", 0, t_stop, t_stop + T_LIMIT), t_b=first("brb", 1, t_stop, t_stop + T_LIMIT))
    if both:
        row.update(t_a_high=first("arb", 0, t_stop2, t_stop2 + T_WAIT_HIGH - 0.05), t_b_high=first("brb", 1, t_stop2, t_stop2 + T_WAIT_HIGH - 0.05))
    return row


FIELDS = ["board", "state", "temp", "v33", "c23", "c50", "c22", "c51", "vth_q2", "vt_tc", "sim", "suspect", "t_en_a", "t_en_b", "t_a", "t_b", "t_a_high", "t_b_high"]


def load(path: Path) -> list[dict]:
    rows = []
    with path.open() as fh:
        for r in csv.DictReader(fh):
            for k in FIELDS[2:10] + FIELDS[12:]:
                r[k] = float(r[k]) if r.get(k) not in ("", None) else None
            rows.append(r)
    return rows


# ---- the rules that section 19.7 gives the firmware, judged on the runs ----
# A board may store its release times only if both lie inside these limits.
# They are the range of the healthy boards at 10 to 40 C with a few percent
# of room.
LEARN_TA = (0.33, 0.85)      # s
LEARN_TB = (0.12, 1.25)      # s
# The firmware engineer asked whether a limit on tB / tA would help. These
# two are tried and not adopted: the first is the widest that rejects no
# healthy board of the Monte Carlo set, the second is tighter.
RATIO_WIDE = (0.30, 2.0)
RATIO_TIGHT = (0.35, 1.6)
BAND = (0.40, 1.70)          # step 9, times the stored value
BAND_A2 = (0.5, 1.5)         # the band of review A2, for comparison
TA_ABS = (0.15, 1.0)
TB_ABS = (0.05, 1.6)


def may_learn(r: dict) -> bool:
    if r["t_a"] is None or r["t_b"] is None:
        return False
    return LEARN_TA[0] <= r["t_a"] <= LEARN_TA[1] and LEARN_TB[0] <= r["t_b"] <= LEARN_TB[1]


def ratio_ok(r: dict, limits: tuple[float, float]) -> bool:
    return r["t_a"] is not None and r["t_b"] is not None and limits[0] <= r["t_b"] / r["t_a"] <= limits[1]


def fmt(x: float | None) -> str:
    return "none" if x is None else f"{x:.2f}"


def rng(vals: list[float]) -> str:
    vals = [v for v in vals if v is not None]
    return "none" if not vals else f"{min(vals):.2f} to {max(vals):.2f}"


def summarise(rows: list[dict]) -> list[str]:
    ok = [r for r in rows if r["sim"] == "ok"]
    idx = {(r["board"], r["state"], r["temp"]): r for r in ok}
    boards = sorted({r["board"] for r in ok}, key=lambda b: (b.split()[0], len(b), b))
    sets = {"Monte Carlo": [b for b in boards if b.startswith("mc")], "grid of extremes": [b for b in boards if b.startswith("grid")]}
    out = ["<!-- Written by hardware/sim/failsafe/failsafe.py commission. Do not edit. -->", "",
           f"{len(rows)} runs, {len(rows) - len(ok)} did not simulate. Solver check: {sum(1 for r in ok if r['suspect'])} runs show the over-current latch setting with no cause.", "",
           "A open: R30 open (the same as R91 open). B open: R14 open (the same as R90 open). Times in seconds, read at 1 ms; the firmware reads at 5 ms.", ""]

    def ratio(r: dict, a: str, b: str) -> float | None:
        return None if r.get(a) is None or r.get(b) is None or r[b] == 0 else r[a] / r[b]

    # 1. what each observable looks like at the commissioning temperatures
    out += ["## What a commissioning cycle can measure, board at 10 to 40 C", "",
            "| Boards | State | tA | tB | tB / tA | tA high stop / tA | tB high stop / tB | tA / enable time of A | enable time of A (ms) | enable time of B (ms) |", "|---|---|---|---|---|---|---|---|---|---|"]
    for sname, bs in sets.items():
        for state in STATES:
            sel = [idx[(b, state, t)] for b in bs for t in COMMISSION_TEMPS if (b, state, t) in idx]
            if not sel:
                continue
            out.append(f"| {sname} | {state} | {rng([r['t_a'] for r in sel])} | {rng([r['t_b'] for r in sel])} | {rng([ratio(r, 't_b', 't_a') for r in sel])} | "
                       f"{rng([ratio(r, 't_a_high', 't_a') for r in sel])} | {rng([ratio(r, 't_b_high', 't_b') for r in sel])} | "
                       f"{rng([ratio(r, 't_a', 't_en_a') for r in sel])} | {rng([None if r['t_en_a'] is None else r['t_en_a'] * 1e3 for r in sel])} | "
                       f"{rng([None if r['t_en_b'] is None else r['t_en_b'] * 1e3 for r in sel])} |")
    out.append("")

    # 2. candidate checks: how many faulty boards each lets through
    checks = {
        "tA inside its learning limits": lambda r: r["t_a"] is not None and LEARN_TA[0] <= r["t_a"] <= LEARN_TA[1],
        "tB inside its learning limits": lambda r: r["t_b"] is not None and LEARN_TB[0] <= r["t_b"] <= LEARN_TB[1],
        "both: the rule of section 19.7": may_learn,
        f"both, and tB / tA from {RATIO_WIDE[0]:g} to {RATIO_WIDE[1]:g}": lambda r: may_learn(r) and ratio_ok(r, RATIO_WIDE),
        f"both, and tB / tA from {RATIO_TIGHT[0]:g} to {RATIO_TIGHT[1]:g}": lambda r: may_learn(r) and ratio_ok(r, RATIO_TIGHT),
    }
    out += [f"## The learning limits: tA {LEARN_TA[0]} to {LEARN_TA[1]} s, tB {LEARN_TB[0]} to {LEARN_TB[1]} s", "",
            "Share of the commissioning cycles (one board at one temperature of 10, 25 or 40 C) that a rule accepts. "
            "The last two rows add a limit on the ratio of the two times, which section 19.7 does not use.", "",
            "| Boards | Rule | Healthy accepted | A open accepted | B open accepted |", "|---|---|---|---|---|"]
    for sname, bs in sets.items():
        for cname, fn in checks.items():
            cells = []
            for state in STATES:
                sel = [idx[(b, state, t)] for b in bs for t in COMMISSION_TEMPS if (b, state, t) in idx]
                cells.append(f"{sum(1 for r in sel if fn(r))} of {len(sel)}")
            out.append(f"| {sname} | {cname} | " + " | ".join(cells) + " |")
    out.append("")

    # 2b. the same with reservoirs that track each other
    out += ["The same for the boards whose two reservoir capacitors, C23 and C50, are within 10 % of each other, as two parts from one reel may be:", "",
            "| Boards | Rule | Healthy accepted | A open accepted | B open accepted |", "|---|---|---|---|---|"]
    for sname, bs in sets.items():
        for cname, fn in checks.items():
            cells = []
            for state in STATES:
                sel = [idx[(b, state, t)] for b in bs for t in COMMISSION_TEMPS if (b, state, t) in idx]
                sel = [r for r in sel if 0.9 <= r["c50"] / r["c23"] <= 1.1]
                cells.append(f"{sum(1 for r in sel if fn(r))} of {len(sel)}")
            out.append(f"| {sname} | {cname} | " + " | ".join(cells) + " |")
    out.append("")

    # 3. which faulty boards get through, and what they then do
    out += ["## The faulty boards that the rules accept", "",
            "A board that commissions with one bleed resistor open keeps the times it measured. The absolute limits of step 9 still apply to it at every stop test.", "",
            "| Boards | State | Accepted | Their Q2 threshold at 25 C (V) | Their reservoir, times nominal | Longest tA and tB of an accepted board, -20 to 70 C | Of them outside the absolute limits at some temperature |",
            "|---|---|---|---|---|---|---|"]
    for sname, bs in sets.items():
        for state, cap in (("A open", "c23"), ("B open", "c50")):
            acc = sorted({b for b in bs for t in COMMISSION_TEMPS if (b, state, t) in idx and may_learn(idx[(b, state, t)])})
            n = len([b for b in bs if any((b, state, t) in idx for t in COMMISSION_TEMPS)])
            if not acc:
                out.append(f"| {sname} | {state} | 0 of {n} boards | | | | |")
                continue
            allr = [idx[(b, state, t)] for b in acc for t in TEMPS if (b, state, t) in idx]
            outside = sum(1 for b in acc if any((b, state, t) in idx and not (
                idx[(b, state, t)]["t_a"] is not None and idx[(b, state, t)]["t_b"] is not None and TA_ABS[0] <= idx[(b, state, t)]["t_a"] <= TA_ABS[1]
                and TB_ABS[0] <= idx[(b, state, t)]["t_b"] <= TB_ABS[1]) for t in TEMPS))
            ta = [r["t_a"] if r["t_a"] is not None else 9.99 for r in allr]
            tb = [r["t_b"] if r["t_b"] is not None else 9.99 for r in allr]
            vth = [idx[(b, state, 25.0)]["vth_q2"] for b in acc if (b, state, 25.0) in idx]
            cv = [idx[(b, state, 25.0)][cap] for b in acc if (b, state, 25.0) in idx]
            out.append(f"| {sname} | {state} | {len(acc)} of {n} boards | {rng(vth)} | {rng(cv)} | tA {max(ta):.2f}, tB {max(tb):.2f} | {outside} of {len(acc)} |")
    out.append("")

    # 4. the band of step 9: a healthy board against its own stored value
    out += ["## The band of step 9", "",
            "Each release time of a board at each temperature from -20 to 70 C, divided by the time the same healthy board stored when it was commissioned.", "",
            f"The band is {BAND[0]:g} to {BAND[1]:g}. The column in brackets is the band of review A2, {BAND_A2[0]:g} to {BAND_A2[1]:g}.", "",
            f"| Boards | Commissioned at | Healthy tA / stored | Healthy tB / stored | Healthy stop tests outside the band | (outside {BAND_A2[0]:g} to {BAND_A2[1]:g}) | A opens later: tA / stored | of them inside the band (not found at that temperature) | boards never found at any temperature | B opens later: tB / stored | of them inside the band | boards never found at any temperature |",
            "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for sname, bs in sets.items():
        for tc in COMMISSION_TEMPS:
            ha, hb, bad, bad2, fa, fb = [], [], 0, 0, [], []
            miss_a = miss_b = never_a = never_b = n_b = 0
            for b in bs:
                ref = idx.get((b, "healthy", tc))
                if ref is None or not may_learn(ref):
                    continue
                n_b += 1
                found_a = found_b = False
                for t in TEMPS:
                    h = idx.get((b, "healthy", t))
                    if h and h["t_a"] is not None and h["t_b"] is not None:
                        ra, rb = h["t_a"] / ref["t_a"], h["t_b"] / ref["t_b"]
                        ha.append(ra)
                        hb.append(rb)
                        bad += not (BAND[0] <= ra <= BAND[1] and BAND[0] <= rb <= BAND[1])
                        bad2 += not (BAND_A2[0] <= ra <= BAND_A2[1] and BAND_A2[0] <= rb <= BAND_A2[1])
                    a = idx.get((b, "A open", t))
                    if a:
                        ra = (a["t_a"] if a["t_a"] is not None else 9.99) / ref["t_a"]
                        fa.append(min(ra, 9.99))
                        inside = BAND[0] <= ra <= BAND[1] and a["t_a"] is not None and TA_ABS[0] <= a["t_a"] <= TA_ABS[1]
                        miss_a += inside
                        found_a |= not inside
                    bb = idx.get((b, "B open", t))
                    if bb:
                        rb = (bb["t_b"] if bb["t_b"] is not None else 9.99) / ref["t_b"]
                        fb.append(min(rb, 9.99))
                        inside = BAND[0] <= rb <= BAND[1] and bb["t_b"] is not None and TB_ABS[0] <= bb["t_b"] <= TB_ABS[1]
                        miss_b += inside
                        found_b |= not inside
                never_a += not found_a
                never_b += not found_b
            if not ha:
                continue
            out.append(f"| {sname} | {tc:g} C | {rng(ha)} | {rng(hb)} | {bad} of {len(ha)} | ({bad2}) | {rng(fa)} | {miss_a} of {len(fa)} | {never_a} of {n_b} | "
                       f"{rng(fb)} | {miss_b} of {len(fb)} | {never_b} of {n_b} |")
    out.append("")

    # 4b. where an open bleed hides inside the band
    out += ["Where a bleed resistor that opens after commissioning stays inside the band, by board temperature at the stop test. "
            "Each cell is for boards commissioned at 10, 25 and 40 C.", "",
            "| Boards | Stop test at | A open inside the band | B open inside the band |", "|---|---|---|---|"]
    for sname, bs in sets.items():
        for t in TEMPS:
            cells_a, cells_b = [], []
            for tc in COMMISSION_TEMPS:
                na = nb = ma = mb = 0
                for b in bs:
                    ref = idx.get((b, "healthy", tc))
                    if ref is None or not may_learn(ref):
                        continue
                    a, bb = idx.get((b, "A open", t)), idx.get((b, "B open", t))
                    if a:
                        na += 1
                        ma += a["t_a"] is not None and BAND[0] <= a["t_a"] / ref["t_a"] <= BAND[1] and TA_ABS[0] <= a["t_a"] <= TA_ABS[1]
                    if bb:
                        nb += 1
                        mb += bb["t_b"] is not None and BAND[0] <= bb["t_b"] / ref["t_b"] <= BAND[1] and TB_ABS[0] <= bb["t_b"] <= TB_ABS[1]
                cells_a.append(f"{ma}")
                cells_b.append(f"{mb}")
            out.append(f"| {sname} | {t:g} C | {', '.join(cells_a)} of {na} | {', '.join(cells_b)} of {nb} |")
    out.append("")

    # 5. how much the commissioning temperature itself matters
    out += ["## How far the stored value moves with the temperature at commissioning", "",
            "The release time of a healthy board at 10 C and at 40 C, divided by its time at 25 C.", "",
            "| Boards | tA at 10 C | tA at 40 C | tB at 10 C | tB at 40 C |", "|---|---|---|---|---|"]
    for sname, bs in sets.items():
        cells = []
        for key in ("t_a", "t_b"):
            for t in (10.0, 40.0):
                v = [idx[(b, "healthy", t)][key] / idx[(b, "healthy", 25.0)][key] for b in bs
                     if (b, "healthy", t) in idx and (b, "healthy", 25.0) in idx and idx[(b, "healthy", t)][key] and idx[(b, "healthy", 25.0)][key]]
                cells.append(rng(v))
        out.append(f"| {sname} | " + " | ".join(cells) + " |")
    out.append("")
    return out


def cmd_commission(args) -> int:
    path = RESULTS / f"{args.tag}.csv"
    if not args.summary_only:
        comps = load_netlist(Path(args.netlist))
        boards = boards_mc(args.samples, args.seed) + boards_grid()
        jobs = [(name, p, state, t) for name, p in boards for state in STATES for t in TEMPS]
        print(f"{len(boards)} boards, {len(jobs)} runs", flush=True)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            rows = list(pool.map(lambda j: one(comps, *j), jobs))
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
    c = sub.add_parser("commission", help="what a board may store at commissioning, and the band of step 9")
    c.add_argument("--netlist", default=str(NETLIST))
    c.add_argument("--tag", default="commission")
    c.add_argument("--samples", type=int, default=200)
    c.add_argument("--seed", type=int, default=20261110)
    c.add_argument("--summary-only", action="store_true", help="write the tables again from the result file, without running anything")
    c.add_argument("--jobs", type=int, default=os.cpu_count())
    c.set_defaults(fn=cmd_commission)
