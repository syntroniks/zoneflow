"""Builds an ngspice deck of the ZoneFlow fail-safe circuits from netlist.yaml.

The connectivity comes from `hardware/netlist.yaml`: every part named in
SCOPE is emitted with the nets the netlist gives its pins. This file adds
only what a netlist cannot hold: a behavioural model for each part type, the
supply rails, and the ESP32 pins as sources and loads.

Nothing here is a vendor model. Each model states the data sheet figures it
was fitted to. No board exists; these runs check the circuit as drawn.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[3]
NETLIST = REPO / "hardware" / "netlist.yaml"

# Parts of the fail-safe circuits, by the job they do. Connectivity is read
# from the netlist; this list only says which parts are inside the analysis.
SCOPE = {
    "detector A": "R33 R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36".split(),
    "logic": "U6 C19 U7 C20 R53 R87 R54 D8".split(),
    "3V3 over-voltage detector": "U13 R13 R100 C55 C56".split(),
    "LED supply switch": "Q3 R25 R26 C21".split(),
    "read-back": "R92 R93 R94 R95".split(),
    "expander": "U5 C18 RN1 R96".split(),
    "output LEDs (Z1 and MV)": "R101 U101 R102 D102 R901 U901 R902 D902".split(),
    "relay drive": "K1 D5 Q1 R11 R12 Q2".split(),
    "detector B": "R89 C51 D22 R14 R90 C50".split(),
    "thresholds and zero-cross (share U6)": "R48 R49 R50 R51 C32 R52 C33 R55 R56 C34 D9 R88 R57 R58".split(),
    "module": ["U4"],
}

RAILS = {"GND": "0", "3V3": "n3v3", "V5": "nv5", "V_BUCK": "nvbuck"}
# The read-back on GPIO42 was DRV_EN_RB until review A2 moved R92 to V_DRV.
A_READBACK = ("V_DRV_RB", "DRV_EN_RB")


def a_readback(comps: dict) -> str:
    nets = set(comps["U4"]["pins"].values())
    return next(n for n in A_READBACK if n in nets)


def scope_refs(comps: dict | None = None) -> list[str]:
    """Every part of the analysis. With a netlist, only the parts it has: the
    netlist of pull request #20 and the one of review A2 have no U13."""
    return [r for refs in SCOPE.values() for r in refs if comps is None or r in comps]


def load_netlist(path: Path = NETLIST) -> dict[str, dict]:
    data = yaml.safe_load(Path(path).read_text())
    return {c["ref"]: c for c in data["components"]}


def pin_id(key: str) -> str:
    """'3 RESET' -> '3'; 'G' -> 'G'; 'coil+' -> 'coil+'; '1 anode' -> '1'."""
    first = key.split()[0]
    return first if re.fullmatch(r"\d+", first) else key


def node_of(net: str) -> str:
    if net in RAILS:
        return RAILS[net]
    return "n_" + re.sub(r"[^a-z0-9]", "_", net.lower())


def parse_r(value: str) -> float:
    m = re.match(r"([\d.]+)\s*([RkM])", value)
    if not m:
        raise ValueError(f"cannot read a resistance from {value!r}")
    return float(m.group(1)) * {"R": 1.0, "k": 1e3, "M": 1e6}[m.group(2)]


def parse_c(value: str) -> float:
    m = re.match(r"([\d.]+)\s*([pnu])F", value)
    if not m:
        raise ValueError(f"cannot read a capacitance from {value!r}")
    return float(m.group(1)) * {"p": 1e-12, "n": 1e-9, "u": 1e-6}[m.group(2)]


@dataclass
class Params:
    """Everything a corner or a Monte Carlo sample may change."""

    temp: float = 27.0
    v33: float = 3.3
    v5: float = 4.95
    vbuck: float = 5.28
    # multiplier on the value of a part, by reference designator
    scale: dict[str, float] = field(default_factory=dict)
    # AO3400A: VGS(th) 0.65 / 1.05 / 1.45 V at 25 C, 250 uA (AOS rev 3).
    # Fig. 1 of that sheet gives about 10 A at VGS = 2 V: K = 22 to 28 A/V2.
    # Fig. 2 moves by about -0.2 V from 25 to 125 C at 1 A: -2 mV/K. The sheet
    # has no threshold curve; vt_tc is swept from -1.5 to -4.5 mV/K in corners.
    vtn: dict[str, float] = field(default_factory=dict)  # per FET, at 25 C
    vtn_default: float = 1.05
    vt_tc: float = -3.0e-3
    kn: float = 25.0
    # AO3401A: VGS(th) -0.5 / -0.9 / -1.3 V, 85 mohm max at -2.5 V (AOS rev 3.1)
    vtp: float = 0.9
    kp: float = 10.0
    # BAT54S (Nexperia, 1 July 2022). IS = 2.2e-7, N = 1.05, EG = 0.69, XTI = 2
    # gives 0.22 uA at 27 C, 19 uA at 85 C and 196 uA at 125 C: Fig. 2 shows
    # 0.25 to 0.7, 18 to 35 and 180 to 300 uA (typical) from 1 to 10 V.
    # IS = 1.5e-8 gives the maximum forward voltages of Table 7 (240, 320 and
    # 400 mV at 0.1, 1 and 10 mA): the high-drop, low-leakage part.
    sch_is: float = 2.2e-7
    sch_is_ref: dict[str, float] = field(default_factory=dict)  # per package
    # LM2901 (TI SLCS006S): IIB 25 nA typical, 250 nA max at 25 C, 500 nA over
    # temperature. VIO 2 mV typical, 7 mV max at 25 C, 15 mV over temperature.
    # IOL 16 mA typical, 6 mA min at VOL 1.5 V; VOL 150 mV typ at 4 mA.
    lm_ib: float = 25e-9
    lm_vos: dict[int, float] = field(default_factory=dict)  # per comparator
    lm_isink: float = 16e-3
    # 74HC132 (Nexperia rev 8): VT+ 0.7..1.5 V at 2.0 V and 1.7..3.15 V at
    # 4.5 V; VT- 0.3..1.0 V and 0.9..2.2 V. Linear in VCC between those rows
    # this is VT+ = 0.37..0.71 x VCC and VT- = 0.19..0.49 x VCC at 3.3 V,
    # typical 0.55 and 0.36. Output 80 ohm: from VOH 3.98 V at 4 mA, 4.5 V.
    hc_vtp: float = 0.55
    hc_vtm: float = 0.36
    hc_ro: float = 80.0
    # TCA9539 (TI SCPS202C): RESET VIL 0.3 x VCC, VIH 0.7 x VCC. The model
    # treats the device as reset only below rst_frac x VCC. 0.3 is the choice
    # that keeps the bits longest, the bad direction for safety.
    rst_frac: float = 0.3
    exp_ron_lo: float = 25.0   # sinks 8 mA at 0.5 V worst: 62 ohm; typical lower
    exp_ron_hi: float = 40.0
    # ESP32-S3 (datasheet v2.2, table 5-4): 45 kohm pulls (typical only),
    # VIH 0.75 x VDD, VIL 0.25 x VDD, 40 mA at VOH 2.64 V at the top strength.
    gpio_ro: float = 30.0
    gpio_rpd: float = 45e3
    gpio_rpu: float = 45e3
    # G5NB-1A-E DC5 (Omron): 125 ohm +/-10 % at 23 C, must operate 75 %,
    # must release 10 % of rated. In current: operates by 3.75 V / 112.5 ohm
    # = 33 mA at the latest and 3.75 / 137.5 = 27 mA at the earliest; may hold
    # down to 0.5 V / 137.5 ohm = 3.6 mA. Inductance assumed, Omron gives none.
    k1_r: float = 125.0
    k1_l: float = 0.2
    # TPS3701 (TI SBVS240C): INB positive threshold 397 / 400 / 403 mV,
    # hysteresis 5.2 mV typical, input current +/-25 nA at most (positive
    # here is into the pin), output low 250 mV at most at 5 mA with 5 V of
    # supply, output off below the under-voltage lock-out of 1.3 to 1.7 V.
    ov_vit: float = 0.400
    ov_hys: float = 5.2e-3
    ov_ib: float = 1e-9
    ov_uvlo: float = 1.7
    # MOC3023 LED: 1.15 V typical at 10 mA (onsemi rev 3)
    led_is: float = 6.9e-13
    led_n: float = 1.9
    ext: dict[str, object] = field(default_factory=dict)  # extra sources
    # USB attached: V5 does not fall below this when V_BUCK is missing.
    usb: float = 0.0
    # The 24 VAC on the zero-cross input. Off, the input sits at 0 V and
    # AC_ZC is low; a run that involves the zero-cross nets turns it on.
    ac: bool = False

    def value(self, ref: str, nominal: float) -> float:
        return nominal * self.scale.get(ref, 1.0)

    def vt_n(self, ref: str) -> float:
        return self.vtn.get(ref, self.vtn_default) + self.vt_tc * (self.temp - 25.0)

    def vt_p(self) -> float:
        return self.vtp + self.vt_tc * (self.temp - 25.0)


@dataclass
# STATE_RC. The comparators and the gates decide through a delay of 22 and
# 47 us, where the real parts take 1.3 us and 10 ns. The delay is there for
# the solver, not for the circuit. U7 holds a latch, and a latch has two
# valid states. Without a delay a single bad Newton iterate inside one time
# step can flip an input switch, the cross-coupled gate follows within the
# same iteration, and the solver converges on the other state: the latch
# sets although no accepted time point ever showed its set input low. With
# the delay the output of a gate depends on a capacitor voltage, which one
# iterate cannot move. Review A2 found this in corner runs where Q2 sits at
# its threshold with coil current flowing. run.py also checks every run for
# a latch that set with no cause.


class Prim:
    """One change to the circuit, switched in at the fault time.

    kind: open   (ref, pin)            the pin leaves its net
          short  (ref, pin, ref2/pin2) two pins of one part are bridged
          bridge (netA, netB, ohms)    two nets are bridged
          stuck  (ref, what)           an IC output or function is stuck
          float  (ref, pin, 'hi'|'lo') leakage on a pin that has been opened
          drive  (ref, pin, 'hi'|'lo') a module pin is driven as an output
          rail   (name, volts)         a supply steps to another voltage
    """

    kind: str
    args: tuple


class Deck:
    """Collects SPICE lines. `at(ref, pin)` is the node a pin connects to."""

    def __init__(self, comps: dict[str, dict], params: Params, prims: list[Prim], t_fault: float):
        self.comps = comps
        self.p = params
        self.prims = prims
        self.tf = t_fault
        self.lines: list[str] = []
        self.pins: dict[tuple[str, str], str] = {}
        self.opened: set[tuple[str, str]] = set()
        self.stuck: set[tuple[str, str]] = set()
        self.rails: dict[str, float] = {}
        self.n = 0
        self.refs = scope_refs(comps)
        for ref in self.refs:
            for key, net in comps[ref]["pins"].items():
                if net != "NC":
                    self.pins[(ref, pin_id(key))] = node_of(net)
        for prim in prims:
            if prim.kind == "open":
                self.opened.add(prim.args[:2])
            elif prim.kind == "stuck":
                self.stuck.add(prim.args[:2])
            elif prim.kind == "rail":
                self.rails[prim.args[0]] = prim.args[1]

    # ------------------------------------------------------------ helpers
    def add(self, line: str) -> None:
        self.lines.append(line)

    def uid(self) -> int:
        self.n += 1
        return self.n

    def net(self, ref: str, pin: str) -> str:
        """The net node, whatever happens to the pin."""
        return self.pins[(ref, pin)]

    def at(self, ref: str, pin: str) -> str:
        """The node at the part's own pin: private if the pin can be opened."""
        if (ref, pin) in self.opened:
            return f"p_{ref.lower()}_{re.sub(r'[^a-z0-9]', '', pin.lower())}"
        return self.pins[(ref, pin)]

    def has(self, ref: str, pin: str) -> bool:
        return (ref, pin) in self.pins

    def is_stuck(self, ref: str, what: str) -> bool:
        return (ref, what) in self.stuck

    # ------------------------------------------------------------ parts
    def resistor(self, ref: str) -> None:
        r = self.p.value(ref, parse_r(self.comps[ref]["value"]))
        self.add(f"R{ref} {self.at(ref, '1')} {self.at(ref, '2')} {r:g}")

    def array(self, ref: str) -> None:
        r = parse_r(self.comps[ref]["value"])
        for a, b in (("1", "8"), ("2", "7"), ("3", "6"), ("4", "5")):
            if self.has(ref, a) and self.has(ref, b):
                self.add(f"R{ref}_{a} {self.at(ref, a)} {self.at(ref, b)} {self.p.value(ref, r):g}")

    def capacitor(self, ref: str) -> None:
        c = self.p.value(ref, parse_c(self.comps[ref]["value"]))
        self.add(f"C{ref} {self.at(ref, '1')} {self.at(ref, '2')} {c:g}")

    def bat54s(self, ref: str) -> None:
        a, k, m = self.at(ref, "1"), self.at(ref, "2"), self.at(ref, "3")
        model = f"SCH_{ref}" if ref in self.p.sch_is_ref else "SCH"
        self.add(f"D{ref}A {a} {m} {model}")
        self.add(f"D{ref}B {m} {k} {model}")

    def diode(self, ref: str, model: str) -> None:
        self.add(f"D{ref} {self.at(ref, 'A')} {self.at(ref, 'K')} {model}")

    @staticmethod
    def _sp(x: str) -> str:
        # ln(1 + exp(x)) without overflow
        return f"(({x}) > 40 ? ({x}) : ln(1 + exp({x})))"

    def _ekv(self, vg: str, vs: str, vd: str, vt: float, k: float) -> str:
        # Symmetric square-law FET with a smooth sub-threshold corner.
        # 0.067 V = 2 n kT/q with n = 1.3.
        f = self._sp(f"(V({vg})-V({vs})-{vt:g})/0.067")
        r = self._sp(f"(V({vg})-V({vd})-{vt:g})/0.067")
        return f"{k / 2:g}*((0.067*{f})**2 - (0.067*{r})**2)"

    def nfet(self, ref: str) -> None:
        g, s, d = self.at(ref, "G"), self.at(ref, "S"), self.at(ref, "D")
        self.add(f"B{ref} {d} {s} I = {self._ekv(g, s, d, self.p.vt_n(ref), self.p.kn)}")
        self.add(f"D{ref}B {s} {d} DBODY")
        self.add(f"C{ref}GS {g} {s} 600p")
        self.add(f"C{ref}GD {g} {d} 50p")
        self.add(f"R{ref}GL {g} {s} 1e12")

    def pfet(self, ref: str) -> None:
        g, s, d = self.at(ref, "G"), self.at(ref, "S"), self.at(ref, "D")
        f = self._sp(f"(V({s})-V({g})-{self.p.vt_p():g})/0.067")
        r = self._sp(f"(V({d})-V({g})-{self.p.vt_p():g})/0.067")
        self.add(f"B{ref} {s} {d} I = {self.p.kp / 2:g}*((0.067*{f})**2 - (0.067*{r})**2)")
        self.add(f"D{ref}B {d} {s} DBODY")
        self.add(f"C{ref}GS {g} {s} 600p")
        self.add(f"C{ref}GD {g} {d} 100p")
        self.add(f"R{ref}GL {g} {s} 1e12")

    def lm2901(self, ref: str) -> None:
        vcc, gnd = self.at(ref, "3"), self.at(ref, "12")
        self.add(f"R{ref}Q {vcc} {gnd} 6k")  # 0.8 mA supply current
        for k, (out, inm, inp) in {1: ("1", "6", "7"), 2: ("2", "4", "5"), 3: ("14", "8", "9"), 4: ("13", "10", "11")}.items():
            o, m, pl = self.at(ref, out), self.at(ref, inm), self.at(ref, inp)
            for tag, node in (("M", m), ("P", pl)):
                # PNP input: the bias current flows out of the pin. An open
                # input drifts up until the clamp diode to the supply stops it.
                self.add(f"I{ref}B{k}{tag} {vcc} {node} {self.p.lm_ib:g}")
                self.add(f"D{ref}C{k}{tag} {node} {vcc} DSUB")
                self.add(f"D{ref}S{k}{tag} {gnd} {node} DSUB")
                self.add(f"C{ref}I{k}{tag} {node} {gnd} 3p")
            vos = self.p.lm_vos.get(k, 0.0)
            # works from 2 V of supply; the smooth edge keeps the solver going
            # when a fault takes the supply or the ground pin away
            on = f"0.5*(1+tanh((V({m})-V({pl})-({vos:g}))/0.3m))*0.5*(1+tanh((V({vcc})-V({gnd})-2)/0.1))"
            if self.is_stuck(ref, f"out{k}_off"):
                on = f"({on})*(1-V(flt))"
            # The decision goes through a 22 us delay before it reaches the
            # output. See STATE_RC.
            d = f"{ref.lower()}_d{k}"
            self.add(f"B{ref}D{k} {d}x 0 V = {on}")
            self.add(f"R{ref}D{k} {d}x {d} 1k")
            self.add(f"C{ref}D{k} {d} 0 22n")
            self.add(f"B{ref}O{k} {o} {gnd} I = {self.p.lm_isink:g}*V({d})*tanh(max(V({o})-V({gnd}),0)/0.5)")
            if self.is_stuck(ref, f"out{k}_low"):
                self.add(f"S{ref}F{k} {o} {gnd} flt 0 FSWC20")

    def hc132(self, ref: str) -> None:
        vcc, gnd = self.at(ref, "14"), self.at(ref, "7")
        self.add(f"R{ref}Q {vcc} {gnd} 330k")
        for k, (a, b, y) in {1: ("1", "2", "3"), 2: ("4", "5", "6"), 3: ("9", "10", "8"), 4: ("12", "13", "11")}.items():
            for pin in (a, b):
                node = self.at(ref, pin)
                s = f"{ref.lower()}_s{pin}"
                self.add(f"S{ref}I{pin} {s} one {node} {gnd} HCSCH")
                self.add(f"R{ref}I{pin} {s} 0 1k")
                self.add(f"C{ref}I{pin} {node} {gnd} 3.5p")
                self.add(f"D{ref}U{pin} {node} {vcc} DPROT")
                self.add(f"D{ref}L{pin} {gnd} {node} DPROT")
            yi = f"{ref.lower()}_y{k}i"
            expr = f"V({gnd}) + (V({vcc})-V({gnd}))*(1 - V({ref.lower()}_s{a})*V({ref.lower()}_s{b}))"
            # The gate's decision goes through a 47 us delay before it
            # reaches the output. See STATE_RC.
            self.add(f"B{ref}Y{k} {yi}x 0 V = {expr}")
            self.add(f"R{ref}Y{k}d {yi}x {yi}d 1k")
            self.add(f"C{ref}Y{k}d {yi}d 0 47n")
            self.add(f"E{ref}Y{k} {yi}b 0 {yi}d 0 1")
            if self.is_stuck(ref, f"y{k}_high") or self.is_stuck(ref, f"y{k}_low"):
                # a stuck output: the driver lets go when the fault starts,
                # so that the fault does not short the supply through it
                self.add(f"S{ref}D{k} {yi}b {yi} nflt 0 FSWO")
            else:
                self.add(f"R{ref}Y{k}x {yi}b {yi} 1")
            self.add(f"R{ref}Y{k} {yi} {self.at(ref, y)} {self.p.hc_ro:g}")
            if self.is_stuck(ref, f"y{k}_high"):
                self.add(f"S{ref}F{k} {yi} {vcc} flt 0 FSWC")
            if self.is_stuck(ref, f"y{k}_low"):
                self.add(f"S{ref}F{k} {yi} {gnd} flt 0 FSWC")

    # Expander pins that the model drives: name -> pin number
    EXP_PINS = {"z1": "4", "mv": "8", "rly": "9"}

    def tca9539(self, ref: str) -> None:
        vcc, gnd, rst = self.at(ref, "24"), self.at(ref, "12"), self.at(ref, "3")
        self.add(f"R{ref}Q {vcc} {gnd} 100k")
        self.add(f"C{ref}RST {rst} {gnd} 5p")
        cond = f"(V({vcc})-V({gnd})) > 1.4"
        if self.is_stuck(ref, "reset_ignored"):
            cond += f" && ((V({rst})-V({gnd})) > {self.p.rst_frac:g}*(V({vcc})-V({gnd})) || V(flt) > 0.5)"
        else:
            cond += f" && (V({rst})-V({gnd})) > {self.p.rst_frac:g}*(V({vcc})-V({gnd}))"
        self.add(f"B{ref}AL u5_alive_x 0 V = ({cond}) ? 1 : 0")
        self.add(f"R{ref}AL u5_alive_x u5_alive 1k")
        self.add(f"C{ref}AL u5_alive 0 5n")
        self.add(f"B{ref}NAL u5_nalive 0 V = 1 - V(u5_alive)")
        self.add(f"B{ref}WR u5_wr 0 V = V(u5_alive)*V(mcu_ok)")
        for name, pin in self.EXP_PINS.items():
            if not self.has(ref, pin):
                continue
            node = self.at(ref, pin)
            # two register bits per pin: oe (1 = output) and lv (output level)
            for bit, reset_to in (("oe", "0"), ("lv", "one")):
                q = f"u5_{bit}_{name}"
                self.add(f"C{q} {q} 0 1n")
                # a write needs the command window and a device out of reset
                self.add(f"S{q}s {q} {q}_s cmd_{bit}_{name}_1 0 LSW")
                self.add(f"S{q}sa {q}_s one u5_wr 0 LSWR")
                self.add(f"S{q}c {q} {q}_c cmd_{bit}_{name}_0 0 LSW")
                self.add(f"S{q}ca {q}_c 0 u5_wr 0 LSWR")
                self.add(f"S{q}r {q} {reset_to} u5_nalive 0 LSWR")
            # pin driver: output enabled and level low -> sink; level high -> source
            self.add(f"Su5_{name}_oe {node} u5_{name}_d u5_oe_{name} 0 PINOE")
            self.add(f"Su5_{name}_lo u5_{name}_d {gnd} one u5_lv_{name} PINLO")
            self.add(f"Su5_{name}_hi u5_{name}_d {vcc} u5_lv_{name} 0 PINHI")
            self.add(f"Cu5_{name} {node} {gnd} 5p")
            if self.is_stuck(ref, f"{name}_low"):
                self.add(f"S{ref}F{name} {node} {gnd} flt 0 FSWC20")
            if self.is_stuck(ref, f"{name}_high"):
                self.add(f"S{ref}F{name} {node} {vcc} flt 0 FSWC20")

    def tps3701(self, ref: str) -> None:
        """Comparator B only: OUTB sinks while INB is above the threshold.
        Comparator A is not used on this board; its output is not connected."""
        vdd, gnd, inb, outb = self.at(ref, "5"), self.at(ref, "2"), self.at(ref, "4"), self.at(ref, "6")
        t = ref.lower()
        self.add(f"R{ref}Q {vdd} {gnd} 500k")   # 8 uA typical, 11 uA at most
        self.add(f"I{ref}B {inb} {gnd} {self.p.ov_ib:g}")
        self.add(f"C{ref}I {inb} {gnd} 3p")
        self.add(f"S{ref}C {t}_c one {inb} {gnd} OVCMP")
        self.add(f"R{ref}C {t}_c 0 1k")
        on = f"V({t}_c)*0.5*(1+tanh((V({vdd})-V({gnd})-{self.p.ov_uvlo:g})/0.05))"
        if self.is_stuck(ref, "outb_off"):
            on = f"({on})*(1-V(flt))"
        # 10 us in the data sheet; 22 us here, as for the comparators of U6.
        self.add(f"B{ref}D {t}_dx 0 V = {on}")
        self.add(f"R{ref}D {t}_dx {t}_d 1k")
        self.add(f"C{ref}D {t}_d 0 22n")
        self.add(f"S{ref}O {outb} {gnd} {t}_d 0 OVOUT")
        if self.is_stuck(ref, "outb_low"):
            self.add(f"S{ref}F {outb} {gnd} flt 0 FSWC20")

    def opto_led(self, ref: str, tag: str) -> None:
        self.add(f"V{tag} {self.at(ref, '1')} {ref.lower()}_a 0")
        self.add(f"D{ref} {ref.lower()}_a {self.at(ref, '2')} DOPTO")

    def relay(self, ref: str) -> None:
        r = self.p.value(ref, self.p.k1_r) * (1 + 0.0039 * (self.p.temp - 23.0))
        self.add(f"L{ref} {self.at(ref, 'coil+')} k1_m {self.p.k1_l:g}")
        self.add(f"R{ref} k1_m k1_m2 {r:g}")
        self.add(f"VK1 k1_m2 {self.at(ref, 'coil-')} 0")

    def module(self, ref: str, ka: dict) -> None:
        """The ESP32 pins on the nets of the scope, found by net name."""
        by_net = {net: pin_id(key) for key, net in self.comps[ref]["pins"].items()}
        # keep-alive pin: push-pull through gpio_ro while ka_oe is high
        pin = by_net["KA"]
        self.ka_pin = pin
        node = self.at(ref, pin)
        self.add("BKA ka_int 0 V = V(ka_lvl)*V(n3v3)")
        self.add("BKAOE ka_drv 0 V = V(ka_oe)*V(mcu_ok)")
        self.add(f"SKA ka_int {node} ka_drv 0 GPIOSW")
        self.add(f"CKA {node} 0 2p")
        if ka.get("pullup"):
            # MTCK (GPIO39) has its weak pull-up on after reset (table 2-1, note 7)
            self.add(f"SKAPU n3v3 ka_pu {'ka_noe'} 0 FSWC")
            self.add(f"RKAPU ka_pu {node} {self.p.gpio_rpu:g}")
        for net, tag, pulldown in ((a_readback(self.comps), "arb", True), ("RLY_B_RB", "brb", False), ("OC_FAULT_N", "ocrb", False), ("AC_ZC", "zcrb", False)):
            pin = by_net[net]
            node = self.at(ref, pin)
            self.add(f"C{tag} {node} 0 2p")
            self.add(f"D{tag} {node} n3v3 DPROT")
            if pulldown:
                self.add(f"R{tag} {node} 0 {self.p.gpio_rpd:g}")
        self.by_net = by_net

    # ------------------------------------------------------------ build
    def build(self, stim: "Stimulus", tend: float, vectors: list[str]) -> str:
        p = self.p
        self.lines = [f"* generated by hardware/sim/failsafe: {stim.name}"]
        add = self.add
        tf = self.tf
        add(f".options temp={p.temp:g} tnom=27 method=gear reltol=2e-3 abstol=1e-10 vntol=2e-5 gmin=1e-12 itl4=60")
        # fault timing: flt goes 0 -> 1 at the fault time, nflt is its inverse
        if tf <= 0:
            add("VFLT flt 0 1")
        else:
            add(f"VFLT flt 0 PWL(0 0 {tf:.9g} 0 {tf + 2e-6:.9g} 1)")
        add("BNFLT nflt 0 V = 1 - V(flt)")
        add("VONE one 0 1")
        # Supplies, chained as on the board: V_BUCK feeds V5 through D3, and
        # U3 makes 3V3 from V5. Neither stage can sink current, so a rail
        # that a fault pulls up stays up, and a rail that loses its source
        # takes the next one with it. A "rail" fault steps the set point of
        # one stage at the fault time.
        def setpoint(name: str, volts: float) -> str:
            if name in self.rails:
                t0 = max(tf, 2e-3)
                return f"PWL(0 0 1m {volts:g} {t0:.9g} {volts:g} {t0 + 1e-3:.9g} {self.rails[name]:g})"
            return f"PWL(0 0 1m {volts:g})"

        def one_way(tag: str, src: str, rail: str) -> None:
            # 0.1 ohm forwards up to 1.5 A, 10 Mohm backwards. 1.5 A is the
            # typical current limit of U1 (LM5164) and about what U3 gives
            # into a short: a shorted rail collapses, as it would on the
            # board, and does not deliver tens of amps.
            add(f"B{tag} {src} {rail} I = (V({src},{rail}) > 0) ? min(V({src},{rail})/0.1, 1.5) : V({src},{rail})/1e7")

        add(f"VVBUCK nvbuck_i 0 {setpoint('V_BUCK', p.vbuck)}")
        one_way("VBUCKO", "nvbuck_i", "nvbuck")
        usb = f"max(V(nvbuck)-{p.vbuck - p.v5:g}, {p.usb:g})" if p.usb else f"max(V(nvbuck)-{p.vbuck - p.v5:g}, 0)"
        add(f"BV5 nv5_i 0 V = {usb}")
        one_way("V5O", "nv5_i", "nv5")
        add(f"V3V3SET n3v3_set 0 {setpoint('3V3', p.v33)}")
        add("B3V3 n3v3_i 0 V = max(min(V(n3v3_set), V(nv5)-0.4), 0)")
        one_way("3V3O", "n3v3_i", "n3v3")
        # The capacitance the board has on each rail outside the scope (C5,
        # C6; C10; C11, C14) and a load for the rest of the board. Without
        # them an edge on V_DRV would dip a rail that is stiff in reality.
        add("CVBUCKB nvbuck 0 44u")
        add("RVBUCKL nvbuck 0 100")
        add("CV5B nv5 0 4.7u")
        add("C3V3B n3v3 0 32u")
        add("R3V3L n3v3 0 33")
        # The supervisor U12 (TLV803EA29) holds the processor in reset while
        # 3V3 is below its threshold, 2.871 to 2.989 V, and for 130 to 270 ms
        # after it has recovered. In reset the pins are inputs and nothing
        # is written. The model takes the lowest threshold. Once the
        # supervisor has reset the processor, the model keeps it in reset
        # for the rest of the run. A real processor boots again, and from
        # then on it is a firmware that is alive, which is not what a run
        # looks at. Letting the model carry on from where the stimulus was
        # is wrong in a way that matters: under a fault that collapses the
        # supply whenever the relay transistors conduct, the pin would be
        # driven high again after every recovery, and those edges alone
        # keep both detectors charged. Power-up is not counted as a reset.
        add("BMCUR mcu_raw 0 V = V(n3v3) > 2.871 ? 1 : 0")
        add("BMCUN mcu_dis 0 V = (time > 5m) ? 1 - V(mcu_raw) : 0")
        add("CMCUT mcu_t 0 1u IC=1")
        add("RMCUT mcu_t 0 1e12")
        add("SMCUD mcu_t 0 mcu_dis 0 LSWR")
        add("BMCU mcu_ok 0 V = (V(mcu_t) > 0.5 ? 1 : 0)*V(mcu_raw)")
        # environment that the scope touches but does not contain
        # 24 VAC as a trapezoid of the same peak: the zero-cross input clamps
        # at a few volts, so only the crossings matter, and a sine costs the
        # solver ten times the steps.
        add("VACL n_ac_l 0 PULSE(-34 34 0 3m 3m 7m 20m)" if p.ac else "VACL n_ac_l 0 0")
        add("EISN n_isense_lo_s 0 n_vb 0 1")
        add("RISN n_isense_lo_s n_isense_lo 50")
        stim.emit(self)
        # parts
        for ref in self.refs:
            c = self.comps[ref]
            value = c["value"]
            if ref == "U4":
                self.module(ref, stim.ka)
            elif ref.startswith("RN"):
                self.array(ref)
            elif re.fullmatch(r"R\d+", ref):
                self.resistor(ref)
            elif re.fullmatch(r"C\d+", ref):
                self.capacitor(ref)
            elif value.startswith("BAT54S"):
                self.bat54s(ref)
            elif value.startswith("1N4148"):
                self.diode(ref, "DSW")
            elif value.startswith("LED"):
                self.diode(ref, "DLED")
            elif value.startswith("AO3400A"):
                self.nfet(ref)
            elif value.startswith("AO3401A"):
                self.pfet(ref)
            elif value.startswith("LM2901"):
                self.lm2901(ref)
            elif value.startswith("74HC132"):
                self.hc132(ref)
            elif value.startswith("TCA9539"):
                self.tca9539(ref)
            elif value.startswith("TPS3701"):
                self.tps3701(ref)
            elif value.startswith("MOC3023"):
                self.opto_led(ref, "LEDZ1" if ref == "U101" else "LEDMV")
            elif value.startswith("G5NB"):
                self.relay(ref)
            else:
                raise ValueError(f"no model for {ref}: {value}")
        # opened pins: a switch between the pin and its net, open after tf
        for ref, pin in sorted(self.opened):
            add(f"SO{self.uid()} {self.at(ref, pin)} {self.net(ref, pin)} nflt 0 FSWO")
            # a lifted pin keeps a little stray capacitance and almost no leak
            add(f"RO{self.uid()} {self.at(ref, pin)} 0 1e13")
            add(f"CO{self.uid()} {self.at(ref, pin)} 0 1p")
        for prim in self.prims:
            if prim.kind == "short":
                ref, a, b = prim.args[:3]
                ohms = prim.args[3] if len(prim.args) > 3 else 0.01
                add(f"SS{self.uid()} {self.at(ref, a)} ss{self.n} flt 0 FSWC")
                add(f"RS{self.n} ss{self.n} {self.at(ref, b)} {ohms:g}")
            elif prim.kind == "bridge":
                a, b, ohms = prim.args
                na = a if a.startswith("x_") else node_of(a)
                nb = b if b.startswith("x_") else node_of(b)
                add(f"SB{self.uid()} {na} sb{self.n} flt 0 FSWC")
                add(f"RB{self.n} sb{self.n} {nb} {max(ohms, 0.01):g}")
            elif prim.kind == "float":
                ref, pin, way = prim.args
                rail = "n3v3" if way == "hi" else "0"
                add(f"RF{self.uid()} {self.at(ref, pin)} {rail} 10meg")
            elif prim.kind == "drive":
                ref, pin, way = prim.args
                rail = "n3v3" if way == "hi" else "0"
                add(f"SD{self.uid()} {self.at(ref, pin)} sd{self.n} flt 0 FSWC")
                add(f"RD{self.n} sd{self.n} {rail} {p.gpio_ro:g}")
        # models
        add(f".model SCH D(IS={p.sch_is:g} N=1.05 RS=2.5 CJO=10p BV=30 IBV=2u EG=0.69 XTI=2)")
        for ref, i_s in p.sch_is_ref.items():
            add(f".model SCH_{ref} D(IS={i_s:g} N=1.05 RS=2.5 CJO=10p BV=30 IBV=2u EG=0.69 XTI=2)")
        add(".model DSW D(IS=2.5n N=1.8 RS=0.6 BV=100 CJO=2p)")
        add(".model DBODY D(IS=1e-11 N=1.05 RS=0.05 CJO=50p)")
        add(".model DSUB D(IS=1e-14 N=1 RS=50)")
        add(".model DPROT D(IS=1e-14 N=1 RS=30)")
        add(f".model DOPTO D(IS={p.led_is:g} N={p.led_n:g} RS=3 CJO=20p)")
        add(".model DLED D(IS=3.1e-20 N=2 RS=5 CJO=20p)")
        vtp, vtm = p.hc_vtp * p.v33, p.hc_vtm * p.v33
        add(f".model HCSCH SW(VT={(vtp + vtm) / 2:g} VH={(vtp - vtm) / 2:g} RON=1 ROFF=1e9)")
        add(f".model GPIOSW SW(VT=0.5 VH=0 RON={p.gpio_ro:g} ROFF=1e11)")
        add(f".model OVCMP SW(VT={p.ov_vit - p.ov_hys / 2:g} VH={p.ov_hys / 2:g} RON=1 ROFF=1e9)")
        add(".model OVOUT SW(VT=0.5 VH=0 RON=30 ROFF=1e11)")
        add(".model FSWC SW(VT=0.5 VH=0 RON=10m ROFF=1e13)")
        add(".model FSWC20 SW(VT=0.5 VH=0 RON=20 ROFF=1e13)")
        add(".model FSWO SW(VT=0.5 VH=0 RON=1m ROFF=1e13)")
        add(".model LSW SW(VT=0.5 VH=0 RON=1k ROFF=1e13)")
        add(".model LSWR SW(VT=0.5 VH=0 RON=10 ROFF=1e13)")
        add(".model PINOE SW(VT=0.5 VH=0 RON=1 ROFF=1e11)")
        add(f".model PINLO SW(VT=0.5 VH=0 RON={p.exp_ron_lo:g} ROFF=1e11)")
        add(f".model PINHI SW(VT=0.5 VH=0 RON={p.exp_ron_hi:g} ROFF=1e11)")
        add(f".tran 0.5m {tend:g} 0 {stim.maxstep:g} uic")
        add(".control")
        add("set noaskquit")
        add("run")
        add("linearize")
        add("set wr_singlescale")
        add("set wr_vecnames")
        add("wrdata out.txt " + " ".join(vectors))
        add("quit")
        add(".endc")
        add(".end")
        return "\n".join(self.lines) + "\n"


class Stimulus:
    """What the firmware does: the keep-alive pin and the expander writes.

    ka: list of (t_start, mode) with mode 'low', 'high', 'hiz' or
        ('toggle', t_high, t_low). The last entry lasts to the end.
    writes: list of (t, bit, name, value) with bit 'oe' or 'lv'.
    """

    def __init__(self, name: str, segments: list, writes: list, maxstep: float = 1e-4, pullup: bool = False):
        self.name = name
        self.segments = segments
        self.writes = writes
        self.maxstep = maxstep
        self.ka = {"pullup": pullup}

    @staticmethod
    def _pwl(points: list[tuple[float, float]]) -> str:
        out, last = [], None
        for t, v in points:
            if last is not None and t <= last:
                t = last + 1e-6
            out.append(f"{t:.9g} {v:g}")
            last = t
        return "PWL(" + " ".join(out) + ")"

    def emit(self, deck: Deck) -> None:
        add = deck.add
        static, oe, terms = [(0.0, 0.0)], [(0.0, 1.0)], []
        level, drive = 0.0, 1.0
        for i, (t, mode) in enumerate(self.segments):
            t_next = self.segments[i + 1][0] if i + 1 < len(self.segments) else None
            new_level = 1.0 if mode == "high" else 0.0
            new_drive = 0.0 if mode == "hiz" else 1.0
            if t > 0:
                static += [(t, level), (t + 1e-6, new_level)]
                oe += [(t, drive), (t + 1e-6, new_drive)]
            else:
                static, oe = [(0.0, new_level)], [(0.0, new_drive)]
            level, drive = new_level, new_drive
            if isinstance(mode, tuple):
                _, thi, tlo = mode
                sel = [(0.0, 0.0), (t, 0.0), (t + 1e-6, 1.0)]
                if t_next is not None:
                    sel += [(t_next, 1.0), (t_next + 1e-6, 0.0)]
                add(f"VKAP{i} ka_p{i} 0 PULSE(0 1 {t:g} 1u 1u {thi:g} {thi + tlo:g})")
                add(f"VKAS{i} ka_s{i} 0 {self._pwl(sel)}")
                terms.append(f"V(ka_p{i})*V(ka_s{i})")
        add(f"VKAST ka_static 0 {self._pwl(static)}")
        add(f"VKAOE ka_oe 0 {self._pwl(oe)}")
        add("BKANOE ka_noe 0 V = 1 - V(ka_oe)")
        add("BKALVL ka_lvl 0 V = V(ka_static)" + "".join(" + " + t for t in terms))
        # expander write commands: a 2 ms window per write
        cmds: dict[str, list[float]] = {}
        for t, bit, name, value in self.writes:
            cmds.setdefault(f"cmd_{bit}_{name}_{int(value)}", []).append(t)
        for name in Deck.EXP_PINS:
            for bit in ("oe", "lv"):
                for value in (0, 1):
                    key = f"cmd_{bit}_{name}_{value}"
                    pts = [(0.0, 0.0)]
                    for t in sorted(cmds.get(key, [])):
                        pts += [(t, 0.0), (t + 1e-6, 1.0), (t + 2e-3, 1.0), (t + 2e-3 + 1e-6, 0.0)]
                    add(f"V{key} {key} 0 {self._pwl(pts)}" if len(pts) > 1 else f"V{key} {key} 0 0")
        # sources outside the board's own circuit, for bridge faults
        for name, spec in deck.p.ext.items():
            add(spec.format(node=name))
