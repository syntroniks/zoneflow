"""Corner analysis of the two keep-alive detectors, with no fault fitted.

Three questions, each over tolerance, temperature, supply and the shape of
the keep-alive waveform:

1. Does normal toggling hold gate drive and the relay? (hold)
2. After the pin stops, when is the valve off? (release)
3. Can anything that is not toggling enable drive? (no edges)

Two sets of runs: every combination of the extremes that matter ("grid"),
and random samples inside the same ranges ("mc"). The ranges and where each
comes from are in RANGES and in README.md.
"""

from __future__ import annotations

import csv
import itertools
import os
import random
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

import run as R
from model import NETLIST, Params, Stimulus, load_netlist

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"

# name: (low, high, source)
RANGES = {
    "temp": (-20.0, 70.0, "design section 3.3: -20 C outside to 70 C inside the enclosure"),
    "v33": (3.0, 3.6, "review brief: the full ESP32-S3 supply range; U3 itself gives 3.20 to 3.40 V"),
    "vbuck": (5.16, 5.40, "design section 4.6"),
    "r": (0.985, 1.015, "1 % parts plus 100 ppm/K over 45 K"),
    "c_res": (0.60, 1.15, "C23, C50: 10 uF X5R 25 V 0805 at up to 3 V. Tolerance 10 %, temperature 15 %, bias and ageing: an allowance, not read from a curve"),
    "c_pump": (0.65, 1.15, "C22, C51: 4.7 uF X5R 25 V 0805, under 1 V of bias. Allowance as above"),
    "sch_is": (1.5e-8, 6.6e-7, "BAT54S: 1.5e-8 A fits the maximum forward voltages of table 7; 6.6e-7 A is three times the typical leakage of Fig. 2"),
    "vtn": (0.65, 1.45, "AO3400A VGS(th) at 25 C, AOS rev 3"),
    "vt_tc": (-4.5e-3, -1.5e-3, "threshold drift per kelvin: the data sheet gives none. Fig. 2 moves -2 mV/K at 1 A; small-signal MOSFETs are usually -2 to -4 mV/K"),
    "vos": (-15e-3, 15e-3, "LM2901 input offset over temperature, TI SLCS006S"),
    "lm_ib": (25e-9, 500e-9, "LM2901 input bias current, typical to maximum over temperature"),
    "gpio_ro": (16.0, 70.0, "ESP32-S3: 40 mA at 0.66 V drop at the highest drive strength; 10 mA setting about four times that"),
    "k1_r": (0.9, 1.1, "G5NB coil resistance tolerance"),
    "hc_vtp": (0.45, 0.71, "74HC132 VT+ as a fraction of VCC at 3.3 V"),
    "hc_vtm": (0.19, 0.45, "74HC132 VT- as a fraction of VCC at 3.3 V"),
    "edge": (1e-3, 20e-3, "keep-alive contract: an edge at least every 20 ms; the brief asks for 1 to 20 ms"),
}
BLEED = ("R30", "R91", "R14", "R90")
RES = ("R29", "R31", "R32", "R33", "R34", "R35", "R36", "R89", "R93", "R94", "R95", "R92", "R12", "R11", "R25", "R26")

T0, T_CFG, T_VALVE, T_STOP, T_AFTER = 0.05, 0.75, 0.81, 1.80, 3.6
I_KEEP = 16e-3        # Omron's holding figure, 40 % of rated voltage
I_LED_NEED = 5.6e-3   # MOC3023 trigger current at -20 C, design section 6.4


@dataclass
class Corner:
    name: str
    params: Params
    thi: float
    tlo: float
    stop: str
    stop_shift: float = 0.0


def make(name: str, *, temp: float, v33: float, vbuck: float, bleed: float, r: float, c_res: float, c_pump: float, sch_is: float, vtn: float,
         vt_tc: float, vos: float, lm_ib: float, gpio_ro: float, k1_r: float, hc_vtp: float, hc_vtm: float, thi: float, tlo: float, stop: str,
         shift: float = 0.0, scale_extra: dict | None = None) -> Corner:
    scale = {ref: bleed for ref in BLEED} | {ref: r for ref in RES} | {"C23": c_res, "C50": c_res, "C22": c_pump, "C51": c_pump, "K1": k1_r}
    scale.update(scale_extra or {})
    p = Params(temp=temp, v33=v33, v5=min(5.1, vbuck - 0.3), vbuck=vbuck, scale=scale, sch_is=sch_is, vtn_default=vtn, vt_tc=vt_tc,
               lm_vos={1: vos}, lm_ib=lm_ib, gpio_ro=gpio_ro, hc_vtp=hc_vtp, hc_vtm=min(hc_vtm, hc_vtp - 0.09))
    return Corner(name, p, thi, tlo, stop, shift)


def grid() -> list[Corner]:
    """Every combination of the extremes that move the result one way."""
    out = []
    waves = {"1/1 ms": (1e-3, 1e-3), "5/5 ms": (5e-3, 5e-3), "20/20 ms": (20e-3, 20e-3), "1 ms high, 20 ms low": (1e-3, 20e-3), "20 ms high, 1 ms low": (20e-3, 1e-3)}
    for temp, v33, caps, vtn, vt_tc, sch, (wname, (thi, tlo)), stop in itertools.product(
            (-20.0, 27.0, 70.0), (3.0, 3.6), ("small", "large"), (0.65, 1.45), (-4.5e-3, -1.5e-3), (1.5e-8, 6.6e-7), waves.items(), ("low", "high", "hiz")):
        small = caps == "small"
        out.append(make(
            f"{temp:g} C, 3V3 {v33} V, {caps} capacitors, Vth {vtn} V at {vt_tc * 1e3:g} mV/K, Schottky IS {sch:g}, {wname}, stops {stop}",
            temp=temp, v33=v33, vbuck=5.16 if small else 5.40, bleed=0.985 if small else 1.015, r=1.0,
            c_res=RANGES["c_res"][0] if small else RANGES["c_res"][1], c_pump=RANGES["c_pump"][0] if small else RANGES["c_pump"][1],
            sch_is=sch, vtn=vtn, vt_tc=vt_tc, vos=15e-3 if small else -15e-3, lm_ib=25e-9 if small else 500e-9,
            gpio_ro=70.0 if small else 16.0, k1_r=1.1 if small else 0.9, hc_vtp=0.55, hc_vtm=0.36, thi=thi, tlo=tlo, stop=stop))
    return out


def monte_carlo(n: int, seed: int) -> list[Corner]:
    rng = random.Random(seed)

    def u(key: str) -> float:
        lo, hi, _ = RANGES[key]
        return rng.uniform(lo, hi)

    def logu(key: str) -> float:
        lo, hi, _ = RANGES[key]
        return lo * (hi / lo) ** rng.random()

    out = []
    for i in range(n):
        # every part gets its own value; the two FETs and the two diode
        # packages are independent parts
        scale = {ref: u("r") for ref in BLEED + RES} | {"C23": u("c_res"), "C50": u("c_res"), "C22": u("c_pump"), "C51": u("c_pump"), "K1": u("k1_r")}
        vbuck = u("vbuck")
        vtp = u("hc_vtp")
        p = Params(temp=rng.choice((-20.0, 0.0, 27.0, 50.0, 70.0)), v33=u("v33"), v5=min(5.1, vbuck - 0.3), vbuck=vbuck, scale=scale,
                   sch_is=logu("sch_is"), sch_is_ref={"D7": logu("sch_is"), "D22": logu("sch_is")},
                   vtn={"Q1": u("vtn"), "Q2": u("vtn")}, vt_tc=u("vt_tc"), lm_vos={1: u("vos")}, lm_ib=logu("lm_ib"),
                   gpio_ro=u("gpio_ro"), hc_vtp=vtp, hc_vtm=min(u("hc_vtm"), vtp - 0.09))
        out.append(Corner(f"sample {i}", p, logu("edge"), logu("edge"), rng.choice(("low", "high", "hiz")), rng.uniform(0, 0.04)))
    return out


def run_corner(comps: dict, c: Corner) -> dict:
    pull = any("IO39" in k for k, net in comps["U4"]["pins"].items() if net == "KA")
    t_stop = T_STOP + c.stop_shift
    segs = [(0.0, "low"), (T0, ("toggle", c.thi, c.tlo)), (t_stop, c.stop)]
    w = [(T_CFG, "oe", n, 1) for n in ("z1", "mv", "rly")] + [(T_CFG, "lv", "rly", 0), (T_CFG + 0.02, "lv", "rly", 1), (T_VALVE, "lv", "z1", 0)]
    stim = Stimulus(c.name, segs, w, maxstep=R.STEP, pullup=pull)
    tend = t_stop + T_AFTER
    tr = R.simulate(comps, c.params, [], 0.0, stim, tend)
    row = {"corner": c.name, "temp": c.params.temp, "v33": c.params.v33, "thi_ms": c.thi * 1e3, "tlo_ms": c.tlo * 1e3, "stop": c.stop}
    if tr is None:
        row["sim"] = "failed"
        return row
    vdd = c.params.v33

    def first(name: str, want: int, t0: float, t1: float) -> float | None:
        t = t0
        while t <= t1:
            if R.gpio(tr.at(t, name), vdd) == want:
                return t - t0
            t += 1e-3
        return None

    i0, i1, i2 = tr.idx(T_VALVE + 0.25), tr.idx(t_stop), len(tr.t) - 1
    hold_coil = min(tr.col["icoil"][i0:i1])
    hold_led = min(tr.col["iz1"][i0:i1])
    vt_hi = max(c.params.vt_n("Q1"), c.params.vt_n("Q2"))
    reads_ok = all(R.gpio(tr.at(t / 1000, "arb"), vdd) == 1 and R.gpio(tr.at(t / 1000, "brb"), vdd) == 0 and tr.at(t / 1000, "alive") > 0.5
                   for t in range(int((T_VALVE + 0.25) * 1000), int(t_stop * 1000), 10))
    closed = R.relay_closed(tr.col["icoil"])
    led = [i > R.I_LED for i in tr.col["iz1"]]
    valve = [a and b for a, b in zip(closed, led)]

    def last(series) -> float | None:
        for i in range(i2, i1 - 1, -1):
            if series[i]:
                return tr.t[i] - t_stop
        return None

    def first_below(name: str, level: float) -> float | None:
        for i in range(i1, i2 + 1):
            if tr.col[name][i] < level:
                return tr.t[i] - t_stop
        return None

    row.update(
        sim="ok", rerun=tr.rerun, suspect="" if tr.suspect is None else f"{tr.suspect:.4f}",
        t_en_a=first("arb", 1, T0, T_CFG), t_en_b=first("brb", 0, T0, T_CFG),
        hold_coil_ma=hold_coil * 1e3, hold_led_ma=hold_led * 1e3,
        hold_g2_margin=min(tr.col["g2"][i0:i1]) - c.params.vt_n("Q2"), hold_n2=min(tr.col["n2"][i0:i1]), vth_max=vt_hi,
        hold_q2_drop=max(tr.col["rlyb"][i0:i1]),
        reads_ok=reads_ok, on_at_end=any(valve[i2 - 100:]),
        t_valve_off=last(valve), t_led_off=last(led), t_relay_off=last(closed),
        t_early_relay=first_below("icoil", I_KEEP), t_early_led=first_below("iz1", I_LED_NEED),
        t_a=first("arb", 0, t_stop, tend), t_b=first("brb", 1, t_stop, tend),
    )
    return row


# ---- runs with no toggling: nothing may enable ---------------------------
NO_EDGE = {
    "pin low from power-up": [(0.0, "low")],
    "pin high from power-up": [(0.0, "high")],
    "pin high-impedance from power-up": [(0.0, "hiz")],
    "one rising edge, then high": [(0.0, "low"), (0.3, "high")],
    "one pulse of 60 us (a power-up glitch)": [(0.0, "low"), (0.3, "high"), (0.30006, "low")],
    "one pulse of 100 ms": [(0.0, "low"), (0.3, "high"), (0.4, "low")],
    "0.5 Hz": [(0.0, "low"), (0.3, ("toggle", 1.0, 1.0))],
    "1 Hz": [(0.0, "low"), (0.3, ("toggle", 0.5, 0.5))],
    "2 Hz": [(0.0, "low"), (0.3, ("toggle", 0.25, 0.25))],
    "3 Hz": [(0.0, "low"), (0.3, ("toggle", 1 / 6, 1 / 6))],
    "5 Hz": [(0.0, "low"), (0.3, ("toggle", 0.1, 0.1))],
}


def no_edge_corners() -> list[tuple[str, Params, list]]:
    out = []
    for temp, v33, caps, sch in itertools.product((-20.0, 27.0, 70.0), (3.0, 3.6), ("easy", "hard"), (1.5e-8, 6.6e-7)):
        # "easy": the reservoirs charge furthest: large pump, small reservoir
        easy = caps == "easy"
        scale = {"C22": 1.15 if easy else 0.65, "C51": 1.15 if easy else 0.65, "C23": 0.60 if easy else 1.15, "C50": 0.60 if easy else 1.15}
        scale |= {ref: 1.015 if easy else 0.985 for ref in BLEED}
        p = Params(temp=temp, v33=v33, scale=scale, sch_is=sch, vtn_default=0.65 if easy else 1.45, vt_tc=-4.5e-3 if easy else -1.5e-3,
                   lm_vos={1: -15e-3 if easy else 15e-3}, lm_ib=500e-9 if easy else 25e-9, gpio_ro=16.0)
        for name, segs in NO_EDGE.items():
            out.append((f"{name}; {temp:g} C, 3V3 {v33} V, {'charges easily' if easy else 'charges least'}, IS {sch:g}", p, segs, name))
    return out


def run_no_edge(comps: dict, item) -> dict:
    name, p, segs, kind = item
    stim = Stimulus(name, segs, [], maxstep=R.STEP)
    tr = R.simulate(comps, p, [], 0.0, stim, 6.0)
    row = {"corner": name, "kind": kind, "temp": p.temp, "v33": p.v33}
    if tr is None:
        row["sim"] = "failed"
        return row
    # Skip the first 10 ms: while the rail ramps, the gate model (thresholds
    # fixed at the final rail voltage) reports levels a real gate does not.
    i0 = tr.idx(0.010)
    row.update(sim="ok", a_enabled=max(tr.col["drv_en"][i0:]) > 0.3 * p.v33, n2_max=max(tr.col["n2"][i0:]), g2_max=max(tr.col["g2"][i0:]),
               vth_q2=p.vt_n("Q2"), b_on=max(tr.col["g2"][i0:]) > p.vt_n("Q2"), expander=max(tr.col["alive"][i0:]) > 0.5)
    return row


def write_csv(path: Path, rows: list[dict]) -> None:
    fields = list(dict.fromkeys(k for r in rows for k in r))
    with path.open("w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=fields, lineterminator="\n")
        w.writeheader()
        for r in rows:
            w.writerow({k: (f"{v:.4g}" if isinstance(v, float) else v) for k, v in r.items()})


def span(rows: list[dict], key: str, pick=None) -> str:
    vals = [(r[key], r) for r in rows if r.get("sim") == "ok" and r.get(key) is not None and (pick is None or pick(r))]
    if not vals:
        return "none"
    lo, hi = min(vals, key=lambda v: v[0]), max(vals, key=lambda v: v[0])
    return f"{lo[0]:.3g} to {hi[0]:.3g} (lowest: {lo[1]['corner']}; highest: {hi[1]['corner']})"


def summarise(tag: str, rows: list[dict]) -> list[str]:
    ok = [r for r in rows if r.get("sim") == "ok"]
    fw = [r for r in ok if abs(r["thi_ms"] - 5) < 0.01 and abs(r["tlo_ms"] - 5) < 0.01]
    out = [f"## {tag}: {len(rows)} runs, {len(rows) - len(ok)} did not simulate", ""]
    out.append(f"- valve still on 3.5 s after the stop: {sum(1 for r in ok if r['on_at_end'])} runs")
    for stop, limit in (("low", 0.95), ("hiz", 0.95), ("high", 1.4)):
        sel = [r for r in ok if r["stop"] == stop]
        late = [r for r in sel if (r["t_valve_off"] or 0) > limit]
        out.append(f"- pin stops {stop}: valve current ends after {span(sel, 't_valve_off')} s; {len(late)} of {len(sel)} runs later than {limit} s")
        out.append(f"  - gate drive alone ends after {span(sel, 't_led_off')} s")
        out.append(f"  - relay alone may hold for {span(sel, 't_relay_off')} s")
        out.append(f"  - earliest the relay can let go (coil below 16 mA): {span(sel, 't_early_relay')} s")
        out.append(f"  - read-back times the firmware would measure: tA {span(sel, 't_a')} s, tB {span(sel, 't_b')} s")
    out.append(f"- while toggling: lowest coil current {span(ok, 'hold_coil_ma')} mA (the coil resistance moves this figure; the voltage across Q2, below, is the test)")
    out.append(f"- while toggling: lowest opto LED current {span(ok, 'hold_led_ma')} mA (needs {I_LED_NEED * 1e3:g} mA)")
    out.append(f"- while toggling: gate of Q2 above its threshold by {span(ok, 'hold_g2_margin')} V")
    out.append(f"- while toggling: reservoir of detector A at least {span(ok, 'hold_n2')} V (releases below about 0.94 V)")
    out.append(f"- runs in which a read-back or the expander dropped out during the run: {sum(1 for r in ok if not r['reads_ok'])}")
    out.append(f"- solver check: {sum(1 for r in ok if r['rerun'])} runs repeated with a finer step, {sum(1 for r in ok if r['suspect'])} still show the latch setting with no cause")
    out.append(f"- while toggling: highest voltage across Q2 {span(ok, 'hold_q2_drop')} V (the coil loses this much)")
    out.append(f"- runs in which Q2 dropped more than 0.1 V while toggling, so that the relay was not fully driven: {sum(1 for r in ok if r['hold_q2_drop'] > 0.1)}")
    waves = sorted({(r['thi_ms'], r['tlo_ms']) for r in ok})
    if len(waves) <= 8:
        for thi, tlo in waves:
            sel = [r for r in ok if (r['thi_ms'], r['tlo_ms']) == (thi, tlo)]
            out.append(f"  - {thi:g} ms high, {tlo:g} ms low: gate of Q2 above its threshold by at least {min(r['hold_g2_margin'] for r in sel):.2f} V, "
                       f"relay not fully driven in {sum(1 for r in sel if r['hold_q2_drop'] > 0.1)} of {len(sel)} runs")
    out.append(f"- runs with LED current under {I_LED_NEED * 1e3:g} mA while toggling: {sum(1 for r in ok if r['hold_led_ma'] < I_LED_NEED * 1e3)}")
    if fw:
        out.append(f"- at the firmware's 5 ms edges: gate-drive read-back high after {span(fw, 't_en_a')} s, relay read-back low after {span(fw, 't_en_b')} s (limit 0.15 s); "
                   f"{sum(1 for r in fw if r['t_en_a'] is None or r['t_en_b'] is None or max(r['t_en_a'], r['t_en_b']) > 0.15)} of {len(fw)} runs over the limit")
        for stop in ("low", "hiz"):
            sel = [r for r in fw if r["stop"] == stop]
            bad_a = [r for r in sel if r["t_a"] is None or not R.TA_LIMITS[0] <= r["t_a"] <= R.TA_LIMITS[1]]
            bad_b = [r for r in sel if r["t_b"] is None or not R.TB_LIMITS[0] <= r["t_b"] <= R.TB_LIMITS[1]]
            out.append(f"- at 5 ms edges, pin stops {stop}: {len(bad_a)} of {len(sel)} runs outside the tA limits {R.TA_LIMITS}, {len(bad_b)} outside the tB limits {R.TB_LIMITS}")
    out.append("")
    return out


def cmd_corners(args) -> int:
    comps = load_netlist(Path(args.netlist))
    RESULTS.mkdir(exist_ok=True)
    text = ["<!-- Written by hardware/sim/failsafe/failsafe.py corners. Do not edit. -->", ""]
    sets = []
    if "grid" in args.sets:
        sets.append(("grid", grid()))
    if "mc" in args.sets:
        sets.append(("mc", monte_carlo(args.samples, args.seed)))
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for tag, corners in sets:
            print(f"{tag}: {len(corners)} runs", flush=True)
            rows = list(pool.map(lambda c: run_corner(comps, c), corners))
            write_csv(RESULTS / f"corners-{tag}.csv", rows)
            lines = summarise("Every combination of extremes" if tag == "grid" else f"Monte Carlo, seed {args.seed}", rows)
            text += lines
            print("\n".join(lines), flush=True)
        if "noedge" in args.sets:
            items = no_edge_corners()
            print(f"noedge: {len(items)} runs", flush=True)
            rows = list(pool.map(lambda it: run_no_edge(comps, it), items))
            write_csv(RESULTS / "corners-noedge.csv", rows)
            lines = ["## No toggling", "", "| Keep-alive pin | Detector A enabled | Reservoir of A, highest (V) | Gate of Q2, highest (V) | Q2 above its lowest threshold |", "|---|---|---|---|---|"]
            for kind in NO_EDGE:
                sel = [r for r in rows if r["kind"] == kind and r.get("sim") == "ok"]
                lines.append(f"| {kind} | {sum(1 for r in sel if r['a_enabled'])} of {len(sel)} runs | {max(r['n2_max'] for r in sel):.2f} | "
                             f"{max(r['g2_max'] for r in sel):.2f} | {sum(1 for r in sel if r['b_on'])} of {len(sel)} runs |")
            lines.append("")
            text += lines
            print("\n".join(lines), flush=True)
    (RESULTS / "corners.md").write_text("\n".join(text) + "\n")
    return 0


def register(sub) -> None:
    c = sub.add_parser("corners", help="tolerance, temperature and supply corners of both detectors")
    c.add_argument("--netlist", default=str(NETLIST))
    c.add_argument("--sets", nargs="*", default=["grid", "mc", "noedge"])
    c.add_argument("--samples", type=int, default=400)
    c.add_argument("--seed", type=int, default=20261110)
    c.add_argument("--jobs", type=int, default=os.cpu_count())
    c.set_defaults(fn=cmd_corners)
