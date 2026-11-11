"""Scenarios, the ngspice runner and the readings a firmware would take.

Two kinds of run:

- **self-test**: the sequence of design section 19.7, steps 1 to 9, with one
  short run of zone 1 in the middle. It says which step finds a fault.
- **crash**: zone 1 is running, then the keep-alive pin stops low, stops high
  or goes high-impedance, and the firmware never writes again. It says
  whether the valve goes off and when.
"""

from __future__ import annotations

import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

from model import Deck, Params, Prim, Stimulus, a_readback, pin_id

STEP = 0.5e-3  # output grid of every run

# ---- self-test timeline, seconds (design section 19.7) -------------------
T_IDLE = 0.30          # step 1: read both read-backs, address the expander
T_HIGH0, T_HIGH1 = 0.35, 1.35   # step 2: pin held high for 1.0 s
# Steps 2b and 5b are not in the sequence as review A2 found it. The review
# adds them. 2b: the pin is made an input for 0.5 s and must read low all
# the time. 5b: RLY_ON is set and then released without being driven low;
# R12 must discharge the gate of Q1, so an output switched on afterwards
# must draw no current.
T_FLOAT0, T_FLOAT1 = 1.45, 1.95
T_START = 2.05         # step 3: toggling starts, 100 ms after step 2b
T_START_LIMIT = 0.150  # both read-backs and the acknowledge within 150 ms
T_PORTS = T_START + 0.16    # step 4a: read the input ports
T_CONFIG = T_START + 0.17   # step 4b, 4c: output and configuration registers
T_WELD0, T_WELD1 = T_START + 0.25, T_START + 0.35   # step 5: output on with RLY_ON low
T_BLEED0 = T_START + 0.37   # step 5b: RLY_ON high for 20 ms ...
T_BLEED1 = T_START + 0.39   # ... its pin made an input ...
T_BLEED2 = T_START + 0.40   # ... and the output on for 100 ms
T_BLEED3 = T_START + 0.50
T_RELAY = T_START + 0.53    # step 6: RLY_ON high
T_RUN = T_START + 0.67      # step 7: valve bit low
T_END = T_START + 1.31      # step 8: valve bit high, 40 ms later RLY_ON low
T_STOP = T_START + 1.37     # step 9: keep-alive low, time both detectors
T_STOP_WAIT = 3.0
T_SELFTEST_END = T_STOP + T_STOP_WAIT + 0.04
T_FAULT_IN_RUN = T_START + 0.91  # a fault that appears while the valve runs

# ---- crash timeline ------------------------------------------------------
C_START = 0.05
C_CONFIG = 0.30
C_VALVE = 0.36
C_FAULT_IN_RUN = 0.90
C_STOP = 1.40
C_END = 5.40

# Firmware limits of section 19.7, step 9, as review A2 leaves them. Two
# tests on each release time: it must be inside limits that hold for every
# board (the corner results of corners.py), and within a band of the value
# this board gave when it was commissioned. The band is what finds an open
# bleed resistor, which doubles the time. It has to be wide enough for one
# board over temperature: 0.55 to 1.38 times its value at 27 C in the corner
# runs.
TA_LIMITS = (0.15, 1.0)
TB_LIMITS = (0.05, 1.6)
LEARNED_BAND = (0.5, 1.5)
# The limits of the sequence as the review found it.
TA_LIMITS_OLD = (0.3, 1.0)
TB_LIMITS_OLD = (0.08, 1.0)

# What counts as "the valve can be on". Chosen on the unsafe side:
# the relay is taken to close at the lowest current that may operate it
# and to hold down to the lowest current that may hold it; any opto LED
# current above 0.25 mA is taken to fire the triac (the MOC3023 guarantees
# firing at 5 mA and gives no lower limit; 0.25 mA is 5 % of that).
I_PULL = 27e-3
I_HOLD = 3.6e-3
I_LED = 0.25e-3

VECTORS = {
    "ka": "v(n_ka)", "n2": "v(n_ka_n2)", "kaok": "v(n_ka_ok)", "drv_en": "v(n_drv_en)",
    "drv_en_n": "v(n_drv_en_n)", "vdrv": "v(n_v_drv)", "alive": "v(u5_alive)",
    "z1": "v(n_zd_z1_n)", "mv": "v(n_zd_mv_n)", "rlyon": "v(n_rly_on)", "g1": "v(n_rly_g1)",
    "g2": "v(n_rly_g2)", "rlyb": "v(n_rly_b)", "icoil": "i(VK1)", "iz1": "i(VLEDZ1)",
    "imv": "i(VLEDMV)", "faultn": "v(n_fault_n)", "v33": "v(n3v3)", "vbuck": "v(nvbuck)",
    "trip": "v(n_oc_trip_n)",
}
READBACKS = {"arb": None, "brb": "RLY_B_RB", "oc": "OC_FAULT_N", "kapin": "KA"}  # arb: see model.a_readback


def toggle(period_high: float = 5e-3, period_low: float = 5e-3):
    return ("toggle", period_high, period_low)


def selftest_stimulus(ka_pullup: bool = False) -> Stimulus:
    segs = [(0.0, "low"), (T_HIGH0, "high"), (T_HIGH1, "low"), (T_FLOAT0, "hiz"), (T_FLOAT1, "low"), (T_START, toggle()), (T_STOP, "low")]
    w = [(T_CONFIG, "oe", n, 1) for n in ("z1", "mv", "rly")]
    w += [(T_CONFIG, "lv", "rly", 0), (T_WELD0, "lv", "z1", 0), (T_WELD1, "lv", "z1", 1)]
    w += [(T_BLEED0, "lv", "rly", 1), (T_BLEED1, "oe", "rly", 0), (T_BLEED2, "lv", "z1", 0), (T_BLEED3, "lv", "z1", 1)]
    w += [(T_BLEED3 + 0.005, "lv", "rly", 0), (T_BLEED3 + 0.01, "oe", "rly", 1), (T_RELAY, "lv", "rly", 1)]
    w += [(T_RUN, "lv", "z1", 0), (T_END, "lv", "z1", 1), (T_END + 0.04, "lv", "rly", 0)]
    w += [(T_END + 0.045, "oe", n, 0) for n in ("z1", "mv", "rly")]
    return Stimulus("self-test sequence, section 19.7", segs, w, maxstep=STEP, pullup=ka_pullup)


def crash_stimulus(stop: str, ka=None, ka_pullup: bool = False) -> Stimulus:
    """stop: 'low', 'high' or 'hiz'. The firmware is gone after C_STOP."""
    segs = [(0.0, "low"), (C_START, ka or toggle()), (C_STOP, stop)]
    w = [(C_CONFIG, "oe", n, 1) for n in ("z1", "mv", "rly")]
    w += [(C_CONFIG, "lv", "rly", 0), (C_CONFIG + 0.02, "lv", "rly", 1), (C_VALVE, "lv", "z1", 0)]
    return Stimulus(f"crash, pin stops {stop}", segs, w, maxstep=STEP, pullup=ka_pullup)


class Trace:
    def __init__(self, names: list[str], rows: list[list[float]]):
        self.t = [r[0] for r in rows]
        self.col = {n: [r[i + 1] for r in rows] for i, n in enumerate(names)}
        self.tend = self.t[-1]
        self.suspect: float | None = None   # see latch_set_without_cause
        self.rerun = False                   # the run was repeated with a finer step

    def idx(self, t: float) -> int:
        return max(0, min(len(self.t) - 1, round(t / STEP)))

    def at(self, t: float, name: str) -> float:
        return self.col[name][self.idx(t)]

    def window(self, t0: float, t1: float, name: str) -> list[float]:
        return self.col[name][self.idx(t0): self.idx(t1) + 1]


TIMEOUT = 90  # seconds of wall time for one run


class Oscillates(Exception):
    """The run did not finish in TIMEOUT. In this model that happens when a
    fault ties a logic output to its own input and the gate oscillates at
    megahertz: the time step collapses. The fault is then judged by hand."""


# Nets and parts of the over-current latch. A fault on one of them may set
# the latch by itself, so the check below does not apply to it.
LATCH_REFS = ("U7", "U6", "R53")
LATCH_NETS = ("FAULT", "FAULT_N", "OC_TRIP_N", "OC_CMP", "OC_FAULT_N")


def _on_latch(p: Prim) -> bool:
    if p.kind == "rail":
        return True
    if p.kind == "bridge":
        return p.args[0] in LATCH_NETS or p.args[1] in LATCH_NETS or {p.args[0], p.args[1]} == {"3V3", "V5"}
    return p.args[0] in LATCH_REFS


def latch_set_without_cause(tr: Trace, prims: list[Prim]) -> float | None:
    """The time at which the over-current latch set although its set input,
    OC_TRIP_N, was high before and after. In this model that is a solver
    artefact: see STATE_RC in model.py. None if there is no such moment, or
    if the fault itself is on the latch or on a supply."""
    if any(_on_latch(p) for p in prims):
        return None
    fn, trip, v33 = tr.col["faultn"], tr.col["trip"], tr.col["v33"]
    for i in range(40, len(tr.t) - 2):
        if v33[i] < 2.5 or v33[i - 1] < 2.5:
            continue
        if fn[i] < 0.3 * v33[i] and fn[i - 1] > 0.7 * v33[i - 1] and all(trip[j] > 0.7 * v33[j] for j in range(i - 6, i + 2)):
            return tr.t[i]
    return None


def _ngspice(work: Path, text: str, names: list[str], tend: float) -> Trace | None | str:
    (work / "deck.cir").write_text(text)
    out = work / "out.txt"
    out.unlink(missing_ok=True)
    try:
        done = subprocess.run(["ngspice", "-b", "deck.cir"], cwd=work, capture_output=True, text=True, timeout=TIMEOUT)
    except subprocess.TimeoutExpired:
        return "timeout"
    # An aborted run still writes a file: linearize pads it to the end.
    if "aborted" in done.stdout + done.stderr or not out.exists():
        return None
    rows = [[float(x) for x in line.split()] for line in out.read_text().splitlines()[1:] if line.strip()]
    if rows and rows[-1][0] >= tend - 2 * STEP:
        return Trace(names, rows)
    return None


def simulate(comps: dict, params: Params, prims: list[Prim], t_fault: float, stim: Stimulus, tend: float, keep: Path | None = None) -> Trace | None:
    deck = Deck(comps, params, prims, t_fault)
    u4 = {net: pin_id(k) for k, net in comps["U4"]["pins"].items()}
    names = list(VECTORS) + list(READBACKS)
    vectors = list(VECTORS.values()) + [f"v({deck.at('U4', u4[net or a_readback(comps)])})" for net in READBACKS.values()]
    text = deck.build(stim, tend, vectors)
    if keep:
        keep.write_text(text)
    tran = f" 0 {stim.maxstep:g} uic"
    assert text.count(tran) == 1
    with tempfile.TemporaryDirectory(prefix="failsafe-") as tmp:
        work = Path(tmp)
        for extra in ("", " gmin=1e-11 rshunt=1e12", " gmin=1e-10 rshunt=1e11 reltol=5e-3", " gmin=1e-9 rshunt=1e10 reltol=1e-2 method=trap"):
            tr = _ngspice(work, text.replace("itl4=60", "itl4=60" + extra, 1), names, tend)
            if tr == "timeout":
                raise Oscillates from None
            if tr is None:
                continue
            if latch_set_without_cause(tr, prims) is None:
                return tr
            # Run it again with a tenth of the time step and a tighter
            # tolerance. If the latch still sets, say so in the result.
            strict = text.replace("reltol=2e-3", "reltol=5e-4", 1).replace(tran, f" 0 {stim.maxstep / 10:g} uic", 1)
            again = _ngspice(work, strict, names, tend)
            if isinstance(again, Trace):
                again.suspect = latch_set_without_cause(again, prims)
                again.rerun = True
                return again
            tr.suspect = latch_set_without_cause(tr, prims)
            return tr
    return None


# ------------------------------------------------------------ readings


def gpio(v: float, vdd: float) -> int | None:
    """An ESP32 input: 1 above 0.75 VDD, 0 below 0.25 VDD, else not defined."""
    if v >= 0.75 * vdd:
        return 1
    if v <= 0.25 * vdd:
        return 0
    return None


def port(v: float, vcc: float) -> int | None:
    """A TCA9539 input: 1 above 0.7 VCC, 0 below 0.3 VCC."""
    if v >= 0.7 * vcc:
        return 1
    if v <= 0.3 * vcc:
        return 0
    return None


def relay_closed(icoil: list[float], forced: bool = False) -> list[bool]:
    state, out = False, []
    for i in icoil:
        if i >= I_PULL:
            state = True
        elif i <= I_HOLD:
            state = False
        out.append(state or forced)
    return out


@dataclass
class Check:
    step: str
    ok: bool | None          # None: a reading was between the logic levels
    text: str


@dataclass
class SelfTest:
    checks: list[Check] = field(default_factory=list)
    t_a: float | None = None
    t_b: float | None = None
    ran: bool = False        # valve current flowed in step 7

    def first(self, steps: tuple[str, ...] | None = None, definite: bool = True) -> Check | None:
        for c in self.checks:
            if steps and c.step not in steps:
                continue
            if c.ok is False or (not definite and c.ok is None):
                return c
        return None


# The steps of design section 19.7 as review A2 leaves them, and as it found
# them: without the float test and the gate bleed check, with the old limits
# of the stop test.
NEW_STEPS = ("1 idle", "2 pin-high", "2b float", "3 start", "4 expander", "5 weld check", "5b gate bleed", "6 relay", "7 run", "valve current", "8 end", "9 stop")
OLD_STEPS = ("1 idle", "2 pin-high", "3 start", "4 expander", "5 weld check", "6 relay", "7 run", "valve current", "8 end", "9 stop (old limits)")


def want(st: SelfTest, step: str, text: str, got, expected) -> None:
    st.checks.append(Check(step, None if got is None else got == expected, text))


def read_selftest(tr: Trace, p: Params, welded: bool = False, shorted_output: bool = False, from_time: float = 0.0,
                  learned: tuple[float, float] | None = None) -> SelfTest:
    """Apply the pass conditions of section 19.7 to a self-test trace.

    learned: the release times (tA, tB) this board gave when it was healthy.
    """
    st = SelfTest()
    vdd = p.v33

    def rb(t: float, name: str):
        return gpio(tr.at(t, name), tr.at(t, "v33") or vdd)

    def ack(t: float) -> bool:
        return tr.at(t, "alive") > 0.5

    closed = relay_closed(tr.col["icoil"], welded)
    led = [i > I_LED or shorted_output for i in tr.col["iz1"]]
    valve = [c and l for c, l in zip(closed, led)]

    def flows(t0: float, t1: float) -> float:
        w = valve[tr.idx(t0): tr.idx(t1) + 1]
        return sum(w) / max(1, len(w))

    if from_time <= T_IDLE:
        want(st, "1 idle", "gate-drive read-back high with no keep-alive", rb(T_IDLE, "arb"), 0)
        want(st, "1 idle", "relay read-back not high with no keep-alive", rb(T_IDLE, "brb"), 1)
        want(st, "1 idle", "expander answers with no keep-alive", ack(T_IDLE), False)
        want(st, "2 pin-high", "gate-drive read-back high with the pin held high", rb(T_HIGH1, "arb"), 0)
        want(st, "2 pin-high", "relay read-back not high with the pin held high", rb(T_HIGH1, "brb"), 1)
        t, high = T_FLOAT0 + 5e-3, False
        while t <= T_FLOAT1:
            high |= rb(t, "kapin") == 1
            t += 1e-3
        want(st, "2b float", "keep-alive pin reads high while it is an input", high, False)
        want(st, "2b float", "gate-drive read-back high with the pin an input", rb(T_FLOAT1, "arb"), 0)
        want(st, "2b float", "relay read-back not high with the pin an input", rb(T_FLOAT1, "brb"), 1)
        ok = None
        t = T_START
        seen = {"a": False, "b": False, "k": False}
        while t <= T_START + T_START_LIMIT + 1e-9:
            a, b, k = rb(t, "arb"), rb(t, "brb"), ack(t)
            seen["a"] |= a == 1
            seen["b"] |= b == 0
            seen["k"] |= k
            if a == 1 and b == 0 and k:
                ok = True
                break
            t += 5e-3
        if ok is None:
            missing = [n for n, s in (("gate-drive read-back high", seen["a"]), ("relay read-back low", seen["b"]), ("expander acknowledge", seen["k"])) if not s]
            # a level that never became definite counts as not passed
            st.checks.append(Check("3 start", False, "within 150 ms no " + ", no ".join(missing or ["agreement of the three"])))
        else:
            st.checks.append(Check("3 start", True, ""))
        vcc = tr.at(T_PORTS, "v33") or vdd
        want(st, "4 expander", "valve pin Z1 does not read high as an input", port(tr.at(T_PORTS, "z1"), vcc), 1)
        want(st, "4 expander", "valve pin MV does not read high as an input", port(tr.at(T_PORTS, "mv"), vcc), 1)
        want(st, "4 expander", "RLY_ON does not read low as an input", port(tr.at(T_PORTS, "rlyon"), vcc), 0)
        want(st, "4 expander", "no acknowledge when writing the registers", ack(T_CONFIG + 3e-3), True)
        want(st, "5 weld check", "valve pin does not read low when driven low", port(tr.at(T_WELD0 + 0.02, "z1"), vcc), 0)
        want(st, "5 weld check", "current flows with RLY_ON low", flows(T_WELD0 + 5e-3, T_WELD1) > 0.1, False)
        want(st, "5b gate bleed", "current flows after RLY_ON was released without being driven low", flows(T_BLEED2 + 5e-3, T_BLEED3) > 0.1, False)
        want(st, "6 relay", "RLY_ON does not read high after it is set", port(tr.at(T_RELAY + 0.01, "rlyon"), vcc), 1)
        want(st, "6 relay", "current flows with every valve bit high", flows(T_RELAY + 0.02, T_RELAY + 0.12) > 0.1, False)
    # step 7: every control tick, 50 ms
    t = T_RUN + 0.04
    tick_fail = None
    while t < T_END - 1e-9:
        if t >= from_time:
            vcc = tr.at(t, "v33") or vdd
            reads = (("gate-drive read-back low during the run", rb(t, "arb"), 1), ("relay read-back high during the run", rb(t, "brb"), 0),
                     ("over-current flag low during the run", rb(t, "oc"), 1), ("expander stops answering during the run", ack(t), True),
                     ("valve pin Z1 not low during the run", port(tr.at(t, "z1"), vcc), 0), ("RLY_ON not high during the run", port(tr.at(t, "rlyon"), vcc), 1),
                     ("valve pin MV not high during the run", port(tr.at(t, "mv"), vcc), 1))
            for text, got, expected in reads:
                if got != expected and tick_fail is None:
                    tick_fail = Check("7 run", None if got is None else False, text)
        t += 0.05
    st.checks.append(tick_fail or Check("7 run", True, ""))
    st.ran = flows(max(T_RUN + 0.05, from_time + 0.05), T_END) > 0.9
    st.checks.append(Check("valve current", st.ran, "no valve current during the run"))
    want(st, "8 end", "expander does not answer at the end of the run", ack(T_END + 0.05), True)
    # step 9: sample both read-backs every 5 ms for up to 3 s
    t, t_a, t_b = T_STOP, None, None
    while t <= T_STOP + T_STOP_WAIT + 1e-9:
        if t_a is None and rb(t, "arb") == 0:
            t_a = t - T_STOP
        if t_b is None and rb(t, "brb") == 1:
            t_b = t - T_STOP
        t += 5e-3
    st.t_a, st.t_b = t_a, t_b
    for k, (name, tv, (lo, hi), (olo, ohi)) in enumerate((("gate-drive read-back", t_a, TA_LIMITS, TA_LIMITS_OLD), ("relay read-back", t_b, TB_LIMITS, TB_LIMITS_OLD))):
        if tv is None:
            st.checks.append(Check("9 stop", False, f"{name} does not release within 3 s"))
            st.checks.append(Check("9 stop (old limits)", False, f"{name} does not release within 3 s"))
            continue
        st.checks.append(Check("9 stop (old limits)", olo <= tv <= ohi, f"{name} releases after {tv:.2f} s, limits {olo} to {ohi} s"))
        if not lo <= tv <= hi:
            st.checks.append(Check("9 stop", False, f"{name} releases after {tv:.2f} s, limits {lo} to {hi} s"))
        elif learned and not LEARNED_BAND[0] * learned[k] <= tv <= LEARNED_BAND[1] * learned[k]:
            st.checks.append(Check("9 stop", False, f"{name} releases after {tv:.2f} s, this board's own value is {learned[k]:.2f} s"))
        else:
            st.checks.append(Check("9 stop", True, ""))
    want(st, "9 stop", "expander still answers 3 s after the stop", ack(T_STOP + T_STOP_WAIT), False)
    return st


@dataclass
class Crash:
    ran: bool                 # the valve was on before the stop
    on_at_end: bool           # ... and still is 4 s later
    t_off: float | None       # last moment with valve current, after the stop
    led_at_end: bool          # opto LED current remains: gate drive not removed
    relay_at_end: bool        # relay may still be closed
    t_led_off: float | None
    t_relay_off: float | None
    q1_at_end: bool = False   # Q1 still has gate drive: the expander did not let go of RLY_ON
    q2_at_end: bool = False   # Q2 still conducts: detector B did not let go


def read_crash(tr: Trace, vt_n: float = 0.65, welded: bool = False, shorted_output: bool = False, pullup: bool = True) -> Crash:
    """vt_n: the lowest threshold an AO3400A may have, to judge Q1 and Q2.
    pullup: R93 is in place, so a low node between Q1 and Q2 means Q2 conducts."""
    closed = relay_closed(tr.col["icoil"], welded)
    led = [i > I_LED or shorted_output for i in tr.col["iz1"]]
    valve = [c and l for c, l in zip(closed, led)]
    i0, n = tr.idx(C_STOP), len(valve)
    tail = n - int(0.05 / STEP)

    def last(series: list[bool]) -> float | None:
        for i in range(n - 1, i0 - 1, -1):
            if series[i]:
                return tr.t[i] - C_STOP
        return None

    ran = sum(valve[tr.idx(C_STOP - 0.2): i0]) > 0.9 * (0.2 / STEP)
    # Q2 conducts if it pulls the node between the two transistors down
    # against R93, or if its gate is above the lowest threshold. Q1 counts
    # as on if its gate is still that far above ground and is not simply
    # tied to its source: it would conduct as soon as Q2 did.
    end = n - 1
    g1, g2, rlyb, vbuck = (tr.col[k][end] for k in ("g1", "g2", "rlyb", "vbuck"))
    q2 = (pullup and vbuck > 2.0 and rlyb < 0.5 * vbuck) or g2 > vt_n
    q1 = (g1 > vt_n and abs(g1 - rlyb) > 0.2) or (closed[end] and not welded)
    return Crash(ran, any(valve[tail:]), last(valve), any(led[tail:]) and not shorted_output, any(closed[tail:]), last(led), last(closed), q1, q2)
