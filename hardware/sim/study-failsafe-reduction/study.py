#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Reset path of the reduced fail-safe of docs/design/failsafe-reduction-study.md.

    uv run hardware/sim/study-failsafe-reduction/study.py all
    uv run hardware/sim/study-failsafe-reduction/study.py deck stop-low nominal   # one deck, to read

THIS IS A STUDY. The circuit here is not in hardware/netlist.yaml and is not
the product. It is variant V2 of the study: one retriggerable one-shot
(74LVC1G123 type) on the keep-alive pin in place of the two discrete
detectors of revision A. Its output is DRV_EN: it releases the reset of the
TCA9539, switches the opto LED supply through an inverter and Q3, and K1
follows the expander bit RLY_ON through Q1. A second supply supervisor of
the type of U12 pulls DRV_EN low while 3V3 is under its threshold.

Every model is behavioural. The figures each one rests on are quoted beside
it. Nothing here has been built or measured. Needs ngspice (47 was used).
Results go to results/. See README.md.
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field, replace
from pathlib import Path

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results"
TIMEOUT = 600

# --------------------------------------------------------------------------
# Parameters
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class P:
    """One set of part values. Sources are in README.md."""

    name: str = "nominal"
    vcc: float = 3.3  # the 3V3 rail in normal use: 3.20 to 3.40 V. 3.6 V is run as well.
    # Timing parts of the one-shot: two 100 kohm in parallel and 4.7 uF X5R.
    # The capacitor range is the allowance the design document uses for the
    # same part as C22 and C51 (section 7.3): tolerance, temperature and bias.
    rt1: float | None = 100e3
    rt2: float | None = 100e3
    ct: float | None = 4.7e-6
    # tW = K x R x C. Nexperia 74LVC1G123 rev 8, table 9 note 3: K = 1 for
    # C > 10 nF; at 10 kohm and 0.1 uF 1.0 ms typical and 1.05 ms at most.
    # The sheet gives no minimum. 0.9 to 1.1 is this study's allowance.
    k: float = 1.0
    # Schmitt inputs A, B, CLR at VCC 3.0 to 3.6 V: VT+ 1.20 / 1.58 / 1.90 V,
    # VT- 1.08 / 1.40 / 1.70 V (same sheet, table 8). As fractions of VCC.
    vtp: float = 1.58 / 3.3
    vtm: float = 1.40 / 3.3
    # AO3400A VGS(th) 0.65 / 1.05 / 1.45 V and AO3401A -0.5 / -0.9 / -1.3 V,
    # as hardware/sim/failsafe/model.py.
    vtn: float = 1.05
    vtpf: float = 0.9
    # TCA9539 RESET: VIL 0.3 x VCC, VIH 0.7 x VCC (TI SCPS202C 6.3). Reset is
    # taken only below rst x VCC: 0.3 keeps the bits longest. Power-on reset
    # falling threshold 0.75 V at least (6.5): the bits are kept down to it.
    rst: float = 0.3
    vporf: float = 0.75
    # G5NB-1A-E DC5: 125 ohm, may hold down to 3.6 mA (model.py). Inductance
    # assumed, Omron gives none.
    k1_l: float = 0.2
    gpio_ro: float = 30.0
    # What the one-shot output does below its supply range. False: low below
    # 1.0 V ("power-on reset on outputs"). True: taken as high below 1.65 V,
    # the lowest supply the sheet covers. The sheet says nothing in between.
    pess: bool = False
    # The second supervisor on DRV_EN, a TLV803EA30 like U12: 3.08 V, -2 to
    # +2 % over temperature, hysteresis 1.2 % typical, output defined from
    # 0.7 V of supply (TI SLVSES2J, 7.5 and the feature list). Its 200 ms of
    # delay after the rail recovers is left out: releasing at once is the
    # bad direction.
    sv: bool = True
    vit2: float = 3.08
    faults: frozenset = frozenset()


CORNERS = {
    "short": dict(ct=3.3e-6, k=0.9, rt1=99e3, rt2=99e3),
    "nominal": dict(),
    "long": dict(ct=5.2e-6, k=1.1, rt1=101e3, rt2=101e3),
}


def corner(name: str, vcc: float = 3.3, **more) -> P:
    return P(name=f"{name}, {vcc:.1f} V", vcc=vcc, **{**CORNERS[name], **more})


def tw(p: P) -> float:
    """Pulse width the data sheet formula gives for these parts."""
    rs = [r for r in (p.rt1, p.rt2) if r]
    r = 1.0 / sum(1.0 / x for x in rs) if rs else math.inf
    return p.k * r * (p.ct or 0.0)


# --------------------------------------------------------------------------
# Scenarios
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Scn:
    name: str
    tend: float = 2.0
    step: float = 50e-6
    # 3V3 as (time, fraction of P.vcc or absolute volts if absolute=True)
    rail: tuple = ((0.0, 0.0), (1e-3, 1.0))
    rail_abs: bool = False
    vbuck: float = 5.28
    # Keep-alive pin: square wave of phases hi, lo while run = 1, else the
    # level lvl; driven while oe = 1, high impedance otherwise.
    hi: float = 5e-3
    lo: float = 5e-3
    run: tuple = ((0.0, 0.0),)
    lvl: tuple = ((0.0, 0.0),)
    oe: tuple = ((0.0, 0.0), (20e-3, 0.0), (20.001e-3, 1.0))
    t_write: float | None = None  # firmware writes a valve bit and RLY_ON
    bridge: tuple | None = None  # ("pp", ohms, hertz) or ("zc", ohms)
    t_ref: float = 0.0  # the event the times are counted from


def pwl(points) -> str:
    return "PWL(" + " ".join(f"{t:.9g} {v:.6g}" for t, v in points) + ")"


def step_at(t: float, before: float, after: float):
    return ((0.0, before), (t, before), (t + 1e-6, after))


T_START, T_WRITE, T_STOP = 0.05, 0.35, 1.0


def run_then(stop: str, **kw) -> Scn:
    """Firmware runs a valve, then the keep-alive ends in the way named."""
    lvl = 1.0 if stop == "high" else 0.0
    oe = ((0.0, 0.0), (20e-3, 0.0), (20.001e-3, 1.0))
    if stop == "hiz":
        oe = oe + ((T_STOP, 1.0), (T_STOP + 1e-6, 0.0))
    return Scn(
        name=f"stop-{stop}",
        run=((0.0, 0.0), (T_START, 0.0), (T_START + 1e-6, 1.0), (T_STOP, 1.0), (T_STOP + 1e-6, 0.0)),
        lvl=((0.0, lvl),),
        oe=oe,
        t_write=T_WRITE,
        t_ref=T_STOP,
        **kw,
    )


def last_edge(s: Scn) -> float:
    """Time of the last edge the pin made before the stop."""
    per = s.hi + s.lo
    n = math.floor((T_STOP - T_START) / per)
    t = T_START + n * per
    # within the period: rises at 0, falls at hi
    return t + s.hi if T_STOP - t > s.hi else t


# --------------------------------------------------------------------------
# The deck
# --------------------------------------------------------------------------

COLUMNS = ["vcc", "ka", "q", "vdrv", "iled", "ik1", "em", "rc", "arb", "clr", "rlyon"]


def deck(p: P, s: Scn) -> str:
    f = p.faults
    L = 0.15
    H = 1.0 - (1.0 - L) * math.exp(-p.k)
    vmid, vhalf = (p.vtp + p.vtm) / 2, (p.vtp - p.vtm) / 2 * p.vcc
    rail = [(t, v if s.rail_abs else v * p.vcc) for t, v in s.rail]
    a = []
    add = a.append
    add(f"* STUDY, not the product. Variant V2 of docs/design/failsafe-reduction-study.md. {s.name}, {p.name}")
    add("* --- supplies")
    add(f"V3V3 vcc 0 {pwl(rail)}")
    add(f"VBUCK vbuck 0 DC {s.vbuck:g}")
    add("* --- ESP32-S3 GPIO17 with the pull-down R33")
    add(f"VSQ sq 0 PULSE(0 1 {T_START:g} 1u 1u {s.hi:g} {s.hi + s.lo:g})")
    add(f"VRUN run 0 {pwl(s.run)}")
    add(f"VLVL lvl 0 {pwl(s.lvl)}")
    add(f"VOE oe 0 {pwl(s.oe)}")
    add("BKA kab 0 V = V(vcc)*(V(run)*V(sq) + (1-V(run))*V(lvl))")
    add("SPIN kab ka oe 0 GPIOSW")
    add("R33 ka 0 2.2k")
    add("CKA ka 0 10p")
    if s.bridge:
        kind, ohms = s.bridge[0], s.bridge[1]
        if kind == "pp":  # a push-pull 3.3 V output that toggles by itself
            hz = s.bridge[2]
            add(f"VBSQ bsq 0 PULSE(0 1 0.1 1u 1u {0.5 / hz:g} {1 / hz:g})")
            add("BBR brn 0 V = V(vcc)*V(bsq)")
            add(f"RBR brn ka {max(ohms, 0.01):g}")
        else:  # AC_ZC: open collector, 10 kohm to 3V3, mains frequency
            add("VZSQ zsq 0 PULSE(0 1 0.1 1u 1u 10m 20m)")
            add("RZP vcc zc 10k")
            add("SZC zc 0 zsq 0 TRG")
            add(f"RBR zc ka {max(ohms, 0.01):g}")
    add("* --- U13 over-voltage detector (as revision A) on the CLR input, pull-up R53")
    add("R13 vcc ovd 1meg")
    add("R100 ovd 0 110k")
    add("C55 ovd 0 10n")
    add("BOVX ovx 0 V = V(ovd) - 0.400")
    add("BUVX uvx 0 V = V(vcc) - 1.7")
    add("SOV clr ov1 ovx 0 CMP30")
    add("SUV ov1 0 uvx 0 CMP30")
    add("R53 vcc clr 10k")
    add("CCLR clr 0 10p")
    add("* --- one-shot, 74LVC1G123 type: B = KA, A low, Q = DRV_EN")
    add(f"BBX bx 0 V = V(ka) - {vmid:g}*V(vcc)")
    add("SBH vcc bd bx 0 SCHM")
    add("RBD bd 0 10k")
    add("CBD bd 0 100p")
    add(f"BCX cx 0 V = V(clr) - {vmid:g}*V(vcc)")
    add("* a rising edge of B with CLR high, or of CLR with B high, triggers (function table)")
    add("SAN bd an cx 0 SCHM")
    add("RAN an 0 10k")
    add("CED an ed 1n")
    add("RED ed 0 10k")
    add("* Q flip-flop: set by a trigger, cleared at the end of the pulse, by CLR low and at power-on")
    add("CQF qff 0 10n")
    add("STQ vcc qff ed 0 TRG")
    add(f"BHX hx 0 V = V(rc) - {H:g}*V(vcc)")
    add("BNDX ndx 0 V = 0.5*V(vcc) - V(dis)")
    add("SR1 qff r1 hx 0 CMP1K")
    add("SR2 r1 0 ndx 0 CMP1K")
    add("SRC qff 0 0 cx CMP1K")
    if p.pess:
        add("BPEX pex 0 V = 1.65 - V(vcc)")
        add("SPE vcc qff pex 0 CMP30")
    else:
        add("BPX px 0 V = 1.0 - V(vcc)")
        add("SRP qff 0 px 0 CMP1K")
    add("* a trigger discharges the timing capacitor to the lower reference, then it charges")
    add("CDI dis 0 10n")
    add("STD vcc dis ed 0 TRG")
    add(f"BLX lx 0 V = {L:g}*V(vcc) - V(rc)")
    add("SRD dis 0 lx 0 CMP1K")
    add("BDX dx 0 V = V(dis) - 0.5*V(vcc)")
    add("SDIS rc 0 dx 0 CMP30")
    if p.rt1 and "rt1_open" not in f and "rt_open" not in f:
        add(f"RT1 vcc rc {p.rt1:g}")
    if p.rt2 and "rt_open" not in f:
        add(f"RT2 vcc rc {p.rt2:g}")
    add("RTL rc 0 1e10")
    add(f"CT rc 0 {('10p' if 'ct_open' in f else format(p.ct, 'g'))} IC=0")
    if "ct_short" in f:
        add("RCTS rc 0 0.05")
    add("BQX qx 0 V = V(qff) - 0.5*V(vcc)")
    if "q_stuck_high" in f:
        add("RQS vcc qo 1")
    else:
        add("SQH vcc qo qx 0 CMP30")
        add("SQL qo 0 0 qx CMP30")
    add("RQO qo 0 1e9")
    add("* --- DRV_EN: series resistor, pull-down, and the second supervisor (open drain)")
    if "rs_open" not in f:
        add("RS qo q 1k")
    add("RPDN q 0 100k")
    add("CQO q 0 20p")
    if p.sv:
        add(f"BSVX svx 0 V = {p.vit2:g} - V(vcc)")
        add("BSVV svv 0 V = V(vcc) - 0.7")
        add("SSV1 q sv1 svx 0 SVC")
        add("SSV2 sv1 0 svv 0 CMP30")
    if "sv_stuck_low" in f:
        add("RSVS q 0 30")
    add("* --- inverter and LED supply switch Q3, bleed R26, read-back R92 to GPIO42")
    add("MINV drvn q 0 0 NAO")
    add("CGI q 0 600p")
    if "qinv_short" in f:
        add("RQIS drvn 0 0.05")
    if "r25_open" not in f:
        add("R25 vcc drvn 100k")
    add("RDL drvn 0 1e10")
    add("CG3 drvn vcc 600p")
    add("MQ3 vdrv drvn vcc vcc PAO")
    if "q3_short" in f:
        add("RQ3S vcc vdrv 0.05")
    add("R26 vdrv 0 10k")
    add("C21 vdrv 0 100n")
    add("R92 vdrv arb 4.7k")
    add("RPD arb 0 45k")
    add("* --- TCA9539: one valve bit and RLY_ON, written once by the firmware")
    add(f"VWR wr 0 {pwl(((0, 0), (s.t_write, 0), (s.t_write + 1e-6, 1), (s.t_write + 1e-3, 1), (s.t_write + 1.001e-3, 0)) if s.t_write else ((0, 0),))}")
    add("BWX wx 0 V = V(q) - 0.7*V(vcc)")
    add(f"BRX rx 0 V = {p.rst:g}*V(vcc) - V(q)")
    add(f"BPVX pvx 0 V = {p.vporf:g} - V(vcc)")
    add("CEM em 0 100n")
    add("SE1 vcc e1 wr 0 TRG")
    add("SE2 e1 em wx 0 CMP30")
    if "exp_ignores_reset" not in f:
        add("SER em 0 rx 0 CMP1K")
    add("SEP em 0 pvx 0 CMP1K")
    add("BEX ex 0 V = V(em) - 1.0")
    add("SZD zd 0 ex 0 PINLO")
    add("SRH vcc rlyon ex 0 PINHI")
    add("RN1 zd vdrv 100k")
    add("* --- one opto LED with its indicator")
    add("R101 vdrv ol1 150")
    add("VLED ol1 ola 0")
    add("DO ola zd DOPTO")
    add("R102 vdrv leda 680")
    add("DL leda zd DLED")
    add("* --- K1 on the expander bit")
    add("R11 rlyon rg1 1k")
    add("R12 rg1 0 100k")
    add("CG1 rg1 0 600p")
    add("MQ1 rlya rg1 0 0 NAO")
    add(f"LK1 vbuck k1a {p.k1_l:g} IC=0")
    add("RK1 k1a k1b 125")
    add("VK1 k1b rlya 0")
    add("D5 rlya vbuck DSW")
    add("RLA rlya 0 1e9")
    add("* --- models")
    add(f".model NAO NMOS(LEVEL=1 VTO={p.vtn:g} KP=25)")
    add(f".model PAO PMOS(LEVEL=1 VTO={-p.vtpf:g} KP=10)")
    add(f".model GPIOSW SW(VT=0.5 VH=0 RON={p.gpio_ro:g} ROFF=1e11)")
    add(f".model SCHM SW(VT=0 VH={vhalf:g} RON=10 ROFF=1e11)")
    add(".model TRG SW(VT=0.6 VH=0.05 RON=100 ROFF=1e12)")
    add(".model CMP1K SW(VT=0 VH=1m RON=1k ROFF=1e12)")
    add(".model CMP30 SW(VT=0 VH=1m RON=30 ROFF=1e12)")
    add(".model SVC SW(VT=-0.02 VH=0.02 RON=30 ROFF=1e12)")
    add(".model PINLO SW(VT=0 VH=0.1 RON=25 ROFF=1e11)")
    add(".model PINHI SW(VT=0 VH=0.1 RON=40 ROFF=1e11)")
    add(".model DOPTO D(IS=6.9e-13 N=1.9 RS=3 CJO=20p)")
    add(".model DLED D(IS=3.1e-20 N=2 RS=5 CJO=20p)")
    add(".model DSW D(IS=2.5n N=1.8 RS=0.6 BV=100 CJO=2p)")
    add(".options method=gear reltol=1e-4 abstol=1e-10 vntol=1e-5 itl4=60")
    add(f".tran {s.step:g} {s.tend:g} uic")
    add(".control")
    add("set noaskquit")
    add("run")
    add("linearize")
    add("wrdata out.txt v(vcc) v(ka) v(q) v(vdrv) i(VLED) i(VK1) v(em) v(rc) v(arb) v(clr) v(rlyon)")
    add(".endc")
    add(".end")
    return "\n".join(a) + "\n"


# --------------------------------------------------------------------------
# Running and reading
# --------------------------------------------------------------------------


class Trace:
    def __init__(self, rows):
        self.t = [r[0] for r in rows]
        self.c = {n: [r[1 + 2 * i] for r in rows] for i, n in enumerate(COLUMNS)}

    def last_above(self, name: str, level: float, t0: float = 0.0) -> float | None:
        """Last time after t0 at which the signal is above the level."""
        out = None
        for t, v in zip(self.t, self.c[name]):
            if t >= t0 and v > level:
                out = t
        return out

    def first_above(self, name: str, level: float, t0: float = 0.0) -> float | None:
        for t, v in zip(self.t, self.c[name]):
            if t >= t0 and v > level:
                return t
        return None

    def peak(self, name: str, t0: float = 0.0, t1: float = math.inf) -> float:
        return max((v for t, v in zip(self.t, self.c[name]) if t0 <= t <= t1), default=0.0)

    def at(self, name: str, t: float) -> float:
        i = min(range(len(self.t)), key=lambda j: abs(self.t[j] - t))
        return self.c[name][i]


def simulate(p: P, s: Scn) -> Trace:
    with tempfile.TemporaryDirectory(prefix="zfstudy") as d:
        work = Path(d)
        (work / "deck.cir").write_text(deck(p, s))
        done = subprocess.run(["ngspice", "-b", "deck.cir"], cwd=work, capture_output=True, text=True, timeout=TIMEOUT)
        out = work / "out.txt"
        # An aborted run still writes a file: linearize pads it to the end.
        if "aborted" in done.stdout + done.stderr or not out.exists():
            raise RuntimeError(f"ngspice failed: {s.name}, {p.name}\n{done.stdout[-2000:]}\n{done.stderr[-2000:]}")
        rows = [[float(x) for x in line.split()] for line in out.read_text().splitlines() if line.strip()]
    if not rows or rows[-1][0] < s.tend - 2 * s.step:
        raise RuntimeError(f"run not finished: {s.name}, {p.name}")
    return Trace(rows)


# Levels the readings use.
I_DARK = 0.5e-3  # LED current counted as "no gate drive". The MOC3023 fires for certain at 5 mA; no lower limit is given.
I_HOLD = 3.6e-3  # the lowest coil current that may still hold K1 (hardware/sim/failsafe/run.py)


def after(tr: Trace, name: str, level: float, t_ref: float) -> float | None:
    """Seconds after t_ref until the signal is below the level for good. None: it never was above."""
    t = tr.last_above(name, level, t_ref)
    return None if t is None else t - t_ref + (tr.t[1] - tr.t[0])


def ms(x: float | None, nd: int = 0) -> str:
    if x is None:
        return "never on"
    if x == math.inf:
        return "**stays on**"
    return f"{x * 1e3:.{nd}f}"


def jobs(items):
    with ThreadPoolExecutor(max_workers=max(1, (os.cpu_count() or 2) // 2)) as ex:
        return list(ex.map(lambda it: (it, simulate(*it)), items))


def release(tr: Trace, s: Scn, t_ref: float) -> dict:
    end = s.tend - 5 * s.step

    def held(name, level):
        t = tr.last_above(name, level, t_ref)
        return t is not None and t >= end

    return {
        "drv_en": math.inf if held("q", 1.0) else after(tr, "q", 1.0, t_ref),
        "led": math.inf if held("iled", I_DARK) else after(tr, "iled", I_DARK, t_ref),
        "k1": math.inf if held("ik1", I_HOLD) else after(tr, "ik1", I_HOLD, t_ref),
        "bits": math.inf if held("em", 1.0) else after(tr, "em", 1.0, t_ref),
    }


def write(name: str, header: list[str], rows: list[list], title: str, notes: list[str]) -> None:
    RESULTS.mkdir(exist_ok=True)
    with (RESULTS / f"{name}.csv").open("w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(header)
        w.writerows(rows)
    md = [f"# {title}", "", "Study, not the product. Written by `study.py`; do not edit.", ""]
    md += notes + [""]
    md.append("| " + " | ".join(header) + " |")
    md.append("|" + "---|" * len(header))
    for r in rows:
        md.append("| " + " | ".join(str(x) for x in r) + " |")
    (RESULTS / f"{name}.md").write_text("\n".join(md) + "\n")
    print(f"wrote results/{name}.csv and .md ({len(rows)} rows)")


# --------------------------------------------------------------------------
# The runs
# --------------------------------------------------------------------------


def run_release() -> None:
    """Time to off after the keep-alive stops low, high and high-impedance, at the corners."""
    items = []
    for stop in ("low", "high", "hiz"):
        for cn in CORNERS:
            for vcc in (3.2, 3.3, 3.6):
                for vtn, vtpf, lk in ((1.05, 0.9, 0.2), (0.65, 1.3, 0.4), (1.45, 0.5, 0.1)):
                    items.append((corner(cn, vcc, vtn=vtn, vtpf=vtpf, k1_l=lk), run_then(stop)))
    rows = []
    for (p, s), tr in jobs(items):
        t_ref = last_edge(s)
        r = release(tr, s, t_ref)
        t_on = tr.first_above("vdrv", 0.9 * p.vcc, T_START)
        rows.append(
            [s.name, p.name, f"{p.vtn:g}", f"{p.vtpf:g}", f"{p.k1_l:g}", f"{tw(p) * 1e3:.0f}",
             f"{(t_on - T_START) * 1e3:.2f}" if t_on else "never", ms(r["drv_en"]), ms(r["bits"]), ms(r["led"]), ms(r["k1"]),
             f"{tr.peak('iled', T_WRITE + 0.05, T_STOP) * 1e3:.1f}", f"{tr.peak('ik1', T_WRITE + 0.2, T_STOP) * 1e3:.1f}"]
        )
    write(
        "release",
        ["stop", "corner", "Q1 and inverter VGS(th), V", "Q3 VGS(th), V", "K1 inductance, H", "tW by formula, ms",
         "V_DRV on after the first edge, ms", "DRV_EN low after the last edge, ms", "expander bits gone, ms",
         "LED current under 0.5 mA, ms", "K1 coil under 3.6 mA, ms", "LED current while running, mA", "coil current while running, mA"],
        rows,
        "Release after the keep-alive stops",
        [
            "The keep-alive toggles at 5 ms per phase from 50 ms, the firmware writes one valve bit and `RLY_ON` at 350 ms,",
            "and the keep-alive stops at 1.0 s: driven low, driven high, or the pin goes high-impedance (a reset of the",
            "digital core, `EN` low, or download mode). Times are counted from the last edge the pin made.",
            "The triac conducts to the end of its half-cycle after the LED goes dark: add up to 10 ms at 50 Hz.",
            "K1 parts its contacts within 10 ms (Omron) of the coil current falling; the column is the coil current only.",
        ],
    )
    summary = []
    for stop in ("low", "high", "hiz"):
        sel = [r for r in rows if r[0] == f"stop-{stop}"]
        for col, label in ((7, "DRV_EN low"), (9, "LED dark"), (10, "K1 coil released")):
            vals = [float(r[col]) for r in sel]
            summary.append([stop, label, f"{min(vals):.0f}", f"{max(vals):.0f}"])
    write("release-summary", ["stop", "what", "earliest, ms", "latest, ms"], summary, "Release after the keep-alive stops: range over the corners",
          ["From `release.csv`: 81 runs, three ways of stopping times 27 corners (timing parts, 3.2 to 3.6 V, transistor thresholds, coil inductance)."])


def run_rates() -> None:
    """What a slow or lopsided keep-alive does."""
    items = []
    cases = [("1 Hz", 0.5, 0.5), ("2 Hz", 0.25, 0.25), ("3 Hz", 1 / 6, 1 / 6), ("5 Hz", 0.1, 0.1), ("10 Hz", 0.05, 0.05), ("25 Hz", 0.02, 0.02),
             ("one edge per 20 ms", 0.02, 0.02), ("100 Hz, the firmware", 5e-3, 5e-3), ("1 ms high, 20 ms low", 1e-3, 20e-3), ("20 ms high, 1 ms low", 20e-3, 1e-3),
             ("1 kHz", 0.5e-3, 0.5e-3), ("100 kHz", 5e-6, 5e-6)]
    for label, hi, lo in cases:
        for cn in CORNERS:
            s = Scn(name=label, hi=hi, lo=lo, tend=4.0 if hi + lo > 0.05 else 1.5, step=2e-6 if hi < 1e-4 else 50e-6,
                    run=((0.0, 0.0), (T_START, 0.0), (T_START + 1e-6, 1.0)), t_write=None)
            if hi < 1e-4:
                s = replace(s, tend=0.6)
            items.append((corner(cn), s))
    rows = []
    for (p, s), tr in jobs(items):
        t0 = T_START + 0.3
        q = [v for t, v in zip(tr.t, tr.c["q"]) if t >= t0]
        duty = sum(1 for v in q if v > 0.7 * p.vcc) / len(q)
        gaps = sum(1 for a, b in zip(q, q[1:]) if a > 0.7 * p.vcc >= b)
        verdict = "held without a gap" if gaps == 0 and duty > 0.99 else ("never enabled" if duty == 0 else f"pulses: high {duty * 100:.0f} % of the time, {gaps} resets of the expander in {s.tend - t0:.1f} s")
        rows.append([s.name, p.name, f"{tw(p) * 1e3:.0f}", verdict])
    write("rates", ["keep-alive", "corner", "tW by formula, ms", "DRV_EN"], rows, "Keep-alive rate",
          ["The pin toggles from 50 ms with the phases named and never stops. `DRV_EN` is judged from 0.35 s to the end of the run.",
           "A gap in `DRV_EN` resets the expander, so a valve bit does not survive it."])


def run_static() -> None:
    """Steady levels, single edges and supply ramps with nothing running."""
    items = []
    ramps = {"1 ms": 1e-3, "100 ms": 0.1, "1 s": 1.0, "10 s": 10.0}
    for rn, tr_ in ramps.items():
        for pin in ("high impedance", "tied to 3V3 through 100 ohm", "driven low"):
            for pess in (False, True):
                tend = tr_ + 1.2
                sc = Scn(name=f"ramp {rn}, pin {pin}", tend=tend, step=max(50e-6, tend / 40000), rail=((0.0, 0.0), (tr_, 1.0)),
                         oe=((0.0, 1.0),) if pin != "high impedance" else ((0.0, 0.0),), lvl=((0.0, 1.0 if pin.startswith("tied") else 0.0),))
                for cn in ("short", "long"):
                    items.append((corner(cn, 3.3, pess=pess, gpio_ro=100.0 if pin.startswith("tied") else 30.0), sc))
    # one rising edge and then stuck high; one 1 ms pulse; with nothing written to the expander
    one_edge = Scn(name="one rising edge, then high", tend=1.5, lvl=((0.0, 0.0), (0.3, 0.0), (0.300001, 1.0)), t_ref=0.3)
    one_pulse = Scn(name="one pulse of 1 ms", tend=1.5, lvl=((0.0, 0.0), (0.3, 0.0), (0.300001, 1.0), (0.301, 1.0), (0.301001, 0.0)), t_ref=0.3)
    for sc in (one_edge, one_pulse):
        for cn in ("short", "long"):
            items.append((corner(cn), sc))
    rows = []
    for (p, s), tr in jobs(items):
        hi_q = tr.last_above("q", 1.0)
        first = tr.first_above("q", 1.0)
        width = (hi_q - first) if hi_q is not None else 0.0
        rows.append([s.name, p.name, "taken as high below 1.65 V" if p.pess else "low below 1.0 V",
                     f"{tr.peak('q'):.2f}", f"{width * 1e3:.0f}", f"{tr.peak('vdrv'):.2f}", f"{tr.peak('iled') * 1e3:.3f}", f"{tr.peak('ik1') * 1e3:.3f}"])
    write("static", ["case", "corner", "one-shot output below its supply range", "highest DRV_EN, V", "DRV_EN high for, ms", "highest V_DRV, V", "highest LED current, mA", "highest coil current, mA"],
          rows, "No keep-alive: supply ramps, a steady pin, one edge",
          ["Nothing is written to the expander in these runs, as on a board whose firmware is not running.",
           "`DRV_EN` high with no valve bit set lights no LED and closes no relay: the last two columns show it."])


def run_brownout() -> None:
    """A valve runs and 3V3 sags. U12 holds EN low below its threshold, so the pin goes high-impedance."""
    items = []
    shapes = {
        "dip to 2.5 V for 300 ms": ((0, 0), (1e-3, 3.3), (1.0, 3.3), (1.001, 2.5), (1.3, 2.5), (1.301, 3.3)),
        "dip to 2.9 V for 50 ms": ((0, 0), (1e-3, 3.3), (1.0, 3.3), (1.001, 2.9), (1.05, 2.9), (1.051, 3.3)),
        "dip to 1.2 V for 300 ms": ((0, 0), (1e-3, 3.3), (1.0, 3.3), (1.002, 1.2), (1.3, 1.2), (1.302, 3.3)),
        "falls to 0 V in 1 s": ((0, 0), (1e-3, 3.3), (1.0, 3.3), (2.0, 0.0)),
        "falls to 0 V in 10 s": ((0, 0), (1e-3, 3.3), (1.0, 3.3), (11.0, 0.0)),
        "falls to 1.5 V in 50 ms and stays": ((0, 0), (1e-3, 3.3), (1.0, 3.3), (1.05, 1.5)),
    }
    lo, hi = 3.018, 3.142  # TLV803EA30: 3.08 V, -2 to +2 % (TI SLVSES2J 7.5)
    for label, pts in shapes.items():
        for sv, vit, vit2 in ((True, lo, hi), (True, hi, lo), (False, lo, hi), (False, hi, lo)):
            # the time the rail crosses the threshold of U12; from then the pin is high-impedance
            tc = None
            for (t0, v0), (t1, v1) in zip(pts, pts[1:]):
                if t0 >= 1.0 and v0 >= vit > v1:
                    tc = t0 + (v0 - vit) / (v0 - v1) * (t1 - t0)
                    break
            tend = pts[-1][0] + 1.0
            sc = Scn(name=label, tend=tend, step=max(50e-6, tend / 60000), rail=pts, rail_abs=True,
                     run=((0.0, 0.0), (T_START, 0.0), (T_START + 1e-6, 1.0)), t_write=T_WRITE,
                     oe=((0.0, 0.0), (20e-3, 0.0), (20.001e-3, 1.0), (tc, 1.0), (tc + 10e-6, 0.0)), t_ref=tc)
            for cn in ("short", "long"):
                for pess in (False, True):
                    tag = f"{cn}, U12 at {vit:.3f} V" + (f", second supervisor at {vit2:.3f} V" if sv else ", no second supervisor")
                    items.append((replace(corner(cn, 3.3, pess=pess, sv=sv, vit2=vit2), name=tag), sc))
    rows = []
    for (p, s), tr in jobs(items):
        r = release(tr, s, 1.0)
        late = tr.peak("iled", s.t_ref + 0.4)
        t_dark = r["led"] if r["led"] not in (None, math.inf) else 0.0
        rows.append([s.name, p.name, "taken as high below 1.65 V" if p.pess else "low below 1.0 V", f"{(s.t_ref - 1.0) * 1e3:.1f}",
                     ms(r["led"], 1), ms(r["k1"], 1), ms(r["bits"], 1), f"{late * 1e3:.2f}", f"{tr.at('vcc', 1.0 + t_dark):.2f}"])
    write("brownout", ["3V3", "corner", "one-shot output below its supply range", "EN low after the sag begins, ms", "LED current under 0.5 mA after the sag begins, ms",
                       "K1 coil under 3.6 mA after the sag begins, ms", "expander bits gone after the sag begins, ms", "highest LED current later than 0.4 s after EN low, mA", "3V3 when the LED went dark, V"],
          rows, "A valve runs and 3V3 sags",
          ["The keep-alive toggles and one valve bit and `RLY_ON` are set. At 1.0 s the rail changes as named. The supervisor U12 pulls `EN` low when",
           "the rail passes its threshold, and the pin is high-impedance from then on: the processor does not come back within the run.",
           "`V_BUCK` stays at 5.28 V, so the relay coil has its supply throughout, which is the bad case. The rows with \"no second supervisor\"",
           "are the circuit without the part that pulls `DRV_EN` low on a low rail: they show what it is for."])


def run_overvoltage() -> None:
    """U3 shorts: 3V3 goes to 5.07 V and the processor keeps the pin toggling."""
    items = []
    for label, hi, lo in (("keeps toggling at 100 Hz", 5e-3, 5e-3), ("toggles at 1 kHz", 0.5e-3, 0.5e-3), ("toggles at 25 Hz", 20e-3, 20e-3)):
        for vhigh in (4.66, 5.07):
            sc = Scn(name=f"3V3 to {vhigh:.2f} V, pin {label}", tend=2.0, hi=hi, lo=lo, rail=((0, 0), (1e-3, 3.3), (1.0, 3.3), (1.0001, vhigh)), rail_abs=True,
                     run=((0.0, 0.0), (T_START, 0.0), (T_START + 1e-6, 1.0)), t_write=T_WRITE, t_ref=1.0)
            for cn in ("short", "long"):
                items.append((corner(cn), sc))
    rows = []
    for (p, s), tr in jobs(items):
        r = release(tr, s, s.t_ref)
        rows.append([s.name, p.name, ms(r["drv_en"], 2), ms(r["led"], 2), ms(r["k1"], 1), f"{tr.peak('q', s.t_ref + 0.3):.2f}", f"{tr.peak('iled', s.t_ref + 0.3) * 1e3:.3f}"])
    write("overvoltage", ["case", "corner", "DRV_EN low after the fault, ms", "LED current under 0.5 mA, ms", "K1 coil under 3.6 mA, ms", "highest DRV_EN later than 0.3 s after, V", "highest LED current later than 0.3 s after, mA"],
          rows, "The 3.3 V regulator shorts while a valve runs",
          ["U13 (TPS3701, threshold 4.04 V, 1 ms filter, as in revision A) holds the CLR input of the one-shot low. The pin keeps toggling: a processor",
           "above its ratings may do that. Revision A removes drive through the over-current latch in this fault; here the same detector acts on CLR."])


def run_bridges() -> None:
    """A net that toggles by itself, bridged onto the keep-alive net while the pin is high-impedance."""
    items = []
    for kind, label, hz in (("pp", "push-pull 3.3 V output, 1 kHz (backlight)", 1000.0), ("pp", "push-pull 3.3 V output, 20 Hz", 20.0), ("zc", "zero-cross signal AC_ZC, 50 Hz", 50.0)):
        for ohms in (0, 100, 470, 1000, 2200, 3300, 4700, 10000):
            for vtlabel, vtp, vtm in (("lowest thresholds", 1.20 / 3.6, 1.08 / 3.6), ("highest thresholds", 1.90 / 3.2, 1.70 / 3.2)):
                sc = replace(run_then("hiz"), name=label, bridge=(kind, ohms, hz), tend=2.5)
                items.append((replace(corner("long", 3.6 if vtlabel.startswith("lowest") else 3.2, vtp=vtp, vtm=vtm), name=f"{ohms}|{vtlabel}"), sc))
    rows = []
    for (p, s), tr in jobs(items):
        ohms, vtlabel = p.name.split("|")
        r = release(tr, s, T_STOP)
        rows.append([s.name, ohms, vtlabel, f"{tr.peak('ka', T_STOP + 0.05):.2f}", "**valve stays on**" if r["led"] == math.inf else f"off after {ms(r['led'])} ms"])
    write("bridges", ["net bridged to KA", "bridge, ohm", "one-shot input", "highest level on KA after the stop, V", "valve"], rows,
          "A toggling net bridged onto the keep-alive net",
          ["A valve runs, then the pin goes high-impedance at 1.0 s, as in every reset of the digital core. R33, 2.2 kohm, is the only thing that holds",
           "the net. The one-shot needs the net above its rising threshold, 1.20 to 1.90 V at 3.0 to 3.6 V. \"Lowest thresholds\" is 1.20 V on a 3.6 V rail, \"highest\" 1.90 V on a 3.2 V rail."])


def run_faults() -> None:
    """Single faults of the new parts, with a valve running and the keep-alive then stopping."""
    faults = [
        ("none", "none"),
        ("rt1_open", "one timing resistor open"),
        ("rt_open", "both timing resistors open"),
        ("ct_short", "timing capacitor shorted"),
        ("ct_open", "timing capacitor open"),
        ("q_stuck_high", "one-shot output stuck high"),
        ("qinv_short", "inverter transistor shorted drain to source"),
        ("q3_short", "Q3 shorted source to drain"),
        ("r25_open", "R25 open"),
        ("exp_ignores_reset", "expander ignores its reset input"),
        ("rs_open", "series resistor R_S open"),
        ("sv_stuck_low", "second supervisor output stuck low"),
    ]
    items = []
    for key, label in faults:
        for stop in ("low", "high"):
            sc = replace(run_then(stop), tend=4.0, name=f"{label}|{stop}")
            items.append((corner("long", 3.3, faults=frozenset() if key == "none" else frozenset({key})), sc))
    rows = []
    for (p, s), tr in jobs(items):
        label, stop = s.name.split("|")
        r = release(tr, s, last_edge(s))
        ran = tr.peak("iled", T_WRITE + 0.05, T_STOP) > 5e-3
        arb_rest = tr.at("arb", s.tend - 0.01)
        rows.append([label, stop, "yes" if ran else "no", ms(r["drv_en"]), ms(r["led"]), ms(r["k1"]),
                     "high" if arb_rest > 0.75 * p.vcc else "low"])
    write("faults", ["fault", "pin stops", "valve ran", "DRV_EN low after, ms", "LED current under 0.5 mA after, ms", "K1 coil under 3.6 mA after, ms", "GPIO42 read-back 3 s after the stop"],
          rows, "Single faults of the new parts",
          ["Long corner, 3.3 V. The fault is present from power-up. \"Stays on\" means the signal was still present at the end of the run, 3 s after the stop.",
           "The read-back column is what the idle test of the firmware would read: high with the keep-alive stopped is a failed test."])


RUNS = {"release": run_release, "rates": run_rates, "static": run_static, "brownout": run_brownout,
        "overvoltage": run_overvoltage, "bridges": run_bridges, "faults": run_faults}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=[*RUNS, "all", "deck"])
    ap.add_argument("args", nargs="*")
    ns = ap.parse_args()
    if ns.what == "deck":
        stop = (ns.args[0] if ns.args else "stop-low").removeprefix("stop-")
        sys.stdout.write(deck(corner(ns.args[1] if len(ns.args) > 1 else "nominal"), run_then(stop)))
        return 0
    for name in RUNS if ns.what == "all" else [ns.what]:
        RUNS[name]()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
