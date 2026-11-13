#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Wider fault injection on variants V1 and V2 of the fail-safe reduction study.

    uv run hardware/sim/study-failsafe-reduction/faults_wide.py            # every fault, both variants, and the trip run
    uv run hardware/sim/study-failsafe-reduction/faults_wide.py --variant V1 --ids ct_leak_100k wd_p7_open_holds
    uv run hardware/sim/study-failsafe-reduction/faults_wide.py --variant V1 --deck u5_p00_low stop-low

THIS IS A STUDY. It belongs to docs/design/failsafe-reduction-review.md, the
independent review of the study. The circuit is the one study.py builds:
variant V2, and V1 made from it by taking out the LED supply switch and the
relay. Neither is in hardware/netlist.yaml and neither is the product.

study.py runs eleven faults of the parts V2 adds. This file follows the
method of review A2 on the same deck: every component open and shorted,
every pin of the new chip stuck, open and bridged to its neighbour, the pins
of the transistors and of the expander that the reset path uses, every net
shorted to ground and to 3V3, leaks onto the timing node, and bridges from
the keep-alive net to its neighbours on the module. Each fault is present
from power-up and goes through seven runs: three ways for the keep-alive to
stop with a valve open, and four tests a firmware could make.

Two limits, both stated in the result file as well:

- The one-shot is a behavioural model written for the study. A fault on one
  of its pins shows what the MODEL does. Where the data sheet does not say
  what the part does, the row is marked "model" and the review counts it as
  not established.
- Faults that the deck cannot represent (a supply pin open, a short that
  loads an ideal source) are listed with basis "reading" and no run.

Needs ngspice (47 was used). Results go to results/faults-wide.*.
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from dataclasses import dataclass, replace
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import study as S  # noqa: E402

# --------------------------------------------------------------------------
# A fault is a set of edits to the deck that study.py writes
# --------------------------------------------------------------------------

SHORT = 0.05  # ohm, a solder bridge
T_FLT = 0.6  # a pin that "opens and holds its level" opens here, with the valve running


@dataclass(frozen=True)
class F:
    id: str
    group: str
    label: str
    drop: tuple = ()  # element names removed from the deck
    add: tuple = ()  # lines added
    sub: tuple = ()  # (element, old node, new node): one pin of one element moved to another node
    p: tuple = ()  # overrides of study.P, as (name, value) pairs
    keys: frozenset = frozenset()  # fault keys that study.deck knows itself
    basis: str = "model"  # "circuit": does not depend on the one-shot model. "model": does. "reading": no run
    v: str = "12"  # the variants the fault exists in: "1", "2" or "12"
    ack: str = "q"  # the node whose level decides whether the expander answers on I2C: its reset input as it sees it
    reading: str = ""  # for basis "reading": what happens, and why
    note: str = ""


def mutate(text: str, f: F) -> str:
    out = []
    for line in text.split("\n"):
        name = line.split(" ", 1)[0] if line else ""
        if name in f.drop:
            continue
        for el, old, new in f.sub:
            if name == el:
                head, rest = line.split(" ", 1)
                rest = re.sub(rf"V\({re.escape(old)}\)", f"V({new})", rest) if el.startswith("B") else re.sub(rf"(?<![\w.]){re.escape(old)}(?![\w.])", new, rest)
                line = f"{head} {rest}"
        if line.startswith(".model NAO"):
            out.extend(f.add)
        out.append(line)
    return "\n".join(out)


# V1 from the V2 deck: no inverter, no Q3 (V_DRV is 3V3), no relay, and the
# read-back R92 on DRV_EN (study section 7.1). VK1 stays as a dummy so that
# the output columns are the same.
V1_BASE = F(
    "v1", "variant", "V1",
    drop=("MINV", "CGI", "R25", "RDL", "CG3", "MQ3", "R26", "C21", "MQ1", "LK1", "RK1", "VK1", "D5", "RLA"),
    add=("RVD vcc vdrv 1m", "VK1 k1x 0 0", "RK1X k1x 0 1k"),
    sub=(("R92", "vdrv", "q"),),
)


def build(p: "S.P", s: "S.Scn", f: F, variant: str) -> str:
    text = S.deck(p, s)
    if variant == "V1":
        text = mutate(text, V1_BASE)
    text = mutate(text, f)
    # one more column: the reset input of the expander as the expander sees it
    text = text.replace(".model NAO", f"BACK ack 0 V = V({f.ack})\n.model NAO", 1)
    assert text.count(" v(rlyon)\n") == 1
    return text.replace(" v(rlyon)\n", " v(rlyon) v(ack)\n")


def floating(node: str, way: str, src: str | None = None) -> tuple:
    """Lines that leave `node` floating. way: low, high, or holds (connected to src until T_FLT)."""
    if way == "low":
        return (f"RFL{node} {node} 0 10meg", f"CFL{node} {node} 0 5p")
    if way == "high":
        return (f"RFL{node} {node} vcc 10meg", f"CFL{node} {node} 0 5p")
    return (
        f"VFL{node} fl{node} 0 PWL(0 1 {T_FLT:g} 1 {T_FLT + 1e-6:g} 0)",
        f"SFL{node} {src} {node} fl{node} 0 FLTSW",
        f"CFL{node} {node} 0 5p",
        f"RFL{node} {node} 0 1e13",
        ".model FLTSW SW(VT=0.5 VH=0 RON=1 ROFF=1e13)",
    )


def contact(node: str) -> tuple:
    """A push contact as ENC_B and BTN_BACK have it: 3.3 kohm to 3V3, 10 kohm to the pin, closing at 20 Hz."""
    return ("VCSQ csq 0 PULSE(0 1 0.1 1u 1u 25m 50m)", "RCPU vcc cnt 3.3k", "SCNT cnt 0 csq 0 TRG", f"RCSER cnt {node} 10k")


def pushpull(ohms: float, hz: float) -> tuple:
    return (f"VBSQ bsq 0 PULSE(0 1 0.1 1u 1u {0.5 / hz:g} {1 / hz:g})", "BBR brn 0 V = V(vcc)*V(bsq)", "RBRO brn bro 30", f"RBR bro ka {max(ohms, 0.01):g}")


# Thresholds of the one-shot inputs that make a bridge easiest to hold: 1.20 V rising on a 3.6 V rail.
EASY = (("vcc", 3.6), ("vtp", 1.20 / 3.6), ("vtm", 1.08 / 3.6))


def fault_list(variant: str = "V2") -> list[F]:
    v1 = variant == "V1"
    rb = "q" if v1 else "vdrv"  # the node the read-back R92 is on
    L: list[F] = [F("none", "reference", "no fault", basis="circuit")]
    a = L.append

    # ---- U_WD, the one-shot: every pin ----
    g = "U_WD one-shot, pins"
    a(F("wd_p1_high", g, "pin 1 A open and leaking high, or stuck high", drop=("SAN",), note="function table: A high inhibits the trigger"))
    a(F("wd_p1_low", g, "pin 1 A open and leaking low", basis="reading", reading="No effect: A is at ground in V2 anyway. If the open pin picks up a signal, each falling edge on A while KA is high is a trigger. Not established how a floating Schmitt input behaves here."))
    a(F("wd_p2_low", g, "pin 2 B open and leaking low", sub=(("BBX", "ka", "bfl"),), add=floating("bfl", "low")))
    a(F("wd_p2_high", g, "pin 2 B open and leaking high", sub=(("BBX", "ka", "bfl"),), add=floating("bfl", "high")))
    a(F("wd_p2_holds", g, "pin 2 B opens during a run and holds its level", sub=(("BBX", "ka", "bfl"),), add=floating("bfl", "holds", "ka")))
    a(F("wd_p3_low", g, "pin 3 CLR open and leaking low", sub=(("BCX", "clr", "cfl"), ("SAN", "cx", "cx")), add=floating("cfl", "low")))
    a(F("wd_p3_high", g, "pin 3 CLR open and leaking high", sub=(("BCX", "clr", "cfl"),), add=floating("cfl", "high"), note="U13 can no longer clear the one-shot"))
    a(F("wd_p4_open", g, "pin 4 GND open", basis="reading", reading="Not established. The chip has no ground and its output cannot be relied on to go low. The TI sheet says pin 6 'connects only to the external capacitor', so the board gives the chip no second ground. Count it as DRV_EN stuck high: the valve stays on, and the read-back shows it at rest."))
    a(F("wd_p5_high", g, "pin 5 Q stuck high", keys=frozenset({"q_stuck_high"})))
    a(F("wd_p5_low", g, "pin 5 Q stuck low", add=("RQSL qo 0 1",)))
    a(F("wd_p5_open", g, "pin 5 Q open", keys=frozenset({"rs_open"}), basis="circuit"))
    a(F("wd_p6_open", g, "pin 6 CEXT open", keys=frozenset({"ct_open"})))
    a(F("wd_p7_open_holds", g, "pin 7 REXT/CEXT open, the internal node holds or leaks low", drop=("RT1", "RT2", "CT"), add=("CT rc 0 10p IC=0",), note="the TI sheet allows +-0.25 uA into this pin in either direction"))
    a(F("wd_p7_open_high", g, "pin 7 REXT/CEXT open, the internal node leaks high (0.25 uA)", drop=("RT1", "RT2", "CT"), add=("CT rc 0 10p IC=0", "IP7 vcc rc DC 0.25u", "DP7 rc vcc DSW")))
    a(F("wd_p8_open", g, "pin 8 VCC open", basis="reading", reading="Not established. The inputs have no diode to VCC (they accept 5.5 V with VCC at 0 V). The timing pin may feed the chip through the timing resistors, 66 uA at most. Expected: Q never goes high, drive never enables, found at the start test."))
    g = "U_WD one-shot, bridges between adjacent pins"
    a(F("wd_p12_bridge", g, "pins 1 and 2 bridged (A to B: KA to ground)", add=(f"RBRG ka 0 {SHORT}",), basis="circuit"))
    a(F("wd_p23_bridge", g, "pins 2 and 3 bridged (B to CLR: KA to the clear net)", add=(f"RBRG ka clr {SHORT}",)))
    a(F("wd_p34_bridge", g, "pins 3 and 4 bridged (CLR to ground)", add=(f"RBRG clr 0 {SHORT}",)))
    a(F("wd_p56_bridge", g, "pins 5 and 6 bridged (Q to CEXT)", add=(f"RBRG qo 0 {SHORT}",), note="taken as Q to ground: both sheets draw CEXT as the grounded side of the capacitor"))
    a(F("wd_p67_bridge", g, "pins 6 and 7 bridged (the timing capacitor shorted)", keys=frozenset({"ct_short"})))
    a(F("wd_p78_bridge", g, "pins 7 and 8 bridged (REXT/CEXT to VCC)", add=(f"RBRG vcc rc {SHORT}",), note="the model cannot end a pulse before the capacitor has been discharged"))

    # ---- timing parts ----
    g = "timing parts"
    a(F("rt1_open", g, "R_T1 open", keys=frozenset({"rt1_open"})))
    a(F("rt1_short", g, "R_T1 shorted (a solder bridge across it: REXT/CEXT to 3V3)", add=(f"RBRG vcc rc {SHORT}",), note="the same node as pins 7 and 8 bridged"))
    a(F("rt_open", g, "R_T1 and R_T2 both open, a double fault", keys=frozenset({"rt_open"})))
    a(F("ct_open", g, "C_T open", keys=frozenset({"ct_open"})))
    a(F("ct_short", g, "C_T shorted", keys=frozenset({"ct_short"})))
    for r in ("22k", "47k", "100k", "150k", "220k", "470k", "1meg"):
        a(F(f"ct_leak_{r}", g, f"leak of {r.replace('meg', ' M').replace('k', ' k')}ohm across C_T (a cracked capacitor, residue from REXT/CEXT to ground)", add=(f"RLK rc 0 {r}",)))
    a(F("rc_leak_47k_3v3", g, "leak of 47 kohm from 3V3 to REXT/CEXT", add=("RLK vcc rc 47k",)))

    # ---- DRV_EN network and second supervisor ----
    g = "DRV_EN network and U_SV"
    a(F("rs_open", g, "R_S open", keys=frozenset({"rs_open"}), basis="circuit"))
    a(F("rs_short", g, "R_S shorted", drop=("RS",), add=(f"RS qo q {SHORT}",), basis="circuit", note="U_SV then pulls against the output of the one-shot directly"))
    a(F("rpd_open", g, "R_PD open", drop=("RPDN",), basis="circuit"))
    a(F("rpd_short", g, "R_PD shorted", add=(f"RBRG q 0 {SHORT}",), basis="circuit"))
    a(F("sv_low", g, "U_SV output stuck low, or its pins 1 and 2 bridged (DRV_EN to ground)", keys=frozenset({"sv_stuck_low"}), basis="circuit"))
    a(F("sv_never", g, "U_SV output open or never pulling, or U_SV without supply", p=(("sv", False),), basis="circuit", note="with a good rail. The brownout run of study.py shows the same circuit on a low rail"))
    a(F("sv_p23_bridge", g, "U_SV pins 2 and 3 joined (DRV_EN to 3V3; across the body, not adjacent)", add=(f"RBRG vcc q {SHORT}",), basis="circuit"))
    a(F("cdec_short", g, "C_DEC shorted", basis="reading", reading="3V3 is shorted. The board does not start."))
    a(F("cdec_open", g, "C_DEC open", basis="reading", reading="No effect that the model shows. The discharge of 4.7 uF at each trigger then draws its current from the rail without local decoupling."))

    # ---- U13 on the clear input ----
    g = "U13 and the clear input"
    a(F("u13_low", g, "U13 output stuck low", add=("RBRG clr 0 1",)))
    a(F("u13_never", g, "U13 output open or never pulling", drop=("SOV", "SUV"), note="over-voltage protection lost"))
    a(F("r53_open_low", g, "R53 open, the clear net leaks low", drop=("R53",), add=("RFLC clr 0 10meg",)))
    a(F("r53_open_high", g, "R53 open, the clear net leaks high", drop=("R53",), add=("RFLC vcc clr 10meg",)))
    a(F("r53_short", g, "R53 shorted (the clear net to 3V3)", drop=("R53",), add=(f"R53 vcc clr {SHORT}",), note="U13 can no longer clear the one-shot"))
    a(F("r13_open", g, "R13 open", drop=("R13",), note="over-voltage protection lost"))
    a(F("r13_short", g, "R13 shorted", drop=("R13",), add=(f"R13 vcc ovd {SHORT}",)))
    a(F("r100_open", g, "R100 open", drop=("R100",)))
    a(F("r100_short", g, "R100 or C55 shorted", add=(f"RBRG ovd 0 {SHORT}",), note="over-voltage protection lost"))

    # ---- inverter and LED supply switch ----
    g = "Q_INV and Q3"
    a(F("qinv_g_low", g, "Q_INV gate open and leaking low", sub=(("MINV", "q", "gfl"),), add=floating("gfl", "low"), basis="circuit", v="2"))
    a(F("qinv_g_high", g, "Q_INV gate open and leaking high", sub=(("MINV", "q", "gfl"),), add=floating("gfl", "high"), basis="circuit", v="2"))
    a(F("qinv_d_open", g, "Q_INV drain or source open", drop=("MINV",), basis="circuit", v="2"))
    a(F("qinv_ds", g, "Q_INV drain and source joined", keys=frozenset({"qinv_short"}), basis="circuit", v="2"))
    a(F("qinv_gs", g, "Q_INV pins 1 and 2 bridged (gate to source: DRV_EN to ground)", add=(f"RBRG q 0 {SHORT}",), basis="circuit", v="2"))
    a(F("qinv_gd", g, "Q_INV gate and drain joined (DRV_EN to DRV_EN_N)", add=(f"RBRG q drvn {SHORT}",), basis="circuit", v="2"))
    a(F("r25_open", g, "R25 open", keys=frozenset({"r25_open"}), basis="circuit", v="2"))
    a(F("r25_short", g, "R25 shorted, or Q3 pins 1 and 2 bridged (gate to source: DRV_EN_N to 3V3)", basis="reading", reading="Q_INV then joins 3V3 to ground whenever DRV_EN is high: 3.3 V across 48 mohm. The rail collapses at the first keep-alive edge and the board resets, at every attempt. No valve runs. V2 has nothing that limits this current; in revision A a gate output drives the node.", v="2"))
    a(F("q3_g_low", g, "Q3 gate open and leaking low", sub=(("MQ3", "drvn", "g3fl"),), add=floating("g3fl", "low"), basis="circuit", v="2"))
    a(F("q3_g_high", g, "Q3 gate open and leaking high", sub=(("MQ3", "drvn", "g3fl"),), add=floating("g3fl", "high"), basis="circuit", v="2"))
    a(F("q3_sd", g, "Q3 source and drain joined (V_DRV to 3V3)", keys=frozenset({"q3_short"}), basis="circuit", v="2"))
    a(F("q3_gd", g, "Q3 gate and drain joined (DRV_EN_N to V_DRV)", basis="reading", reading="With DRV_EN high, Q_INV pulls V_DRV to ground and Q3 conducts into it: 3V3 is shorted through both transistors, as with R25 shorted. With DRV_EN low, Q3 is connected as a diode and V_DRV rests about one threshold under 3V3. No valve runs.", v="2"))
    a(F("q3_open", g, "Q3 source or drain open", drop=("MQ3",), basis="circuit", v="2"))
    a(F("r26_open", g, "R26 open", drop=("R26",), basis="circuit", v="2"))
    a(F("r26_short", g, "R26 or C21 shorted (V_DRV to ground)", basis="reading", reading="Q3 joins 3V3 to ground whenever DRV_EN is high: 3.3 V across 60 mohm. As R25 shorted: the rail collapses at the first edge. No valve runs.", v="2"))

    # ---- read-back ----
    g = "read-back"
    a(F("r92_open", g, "R92 open", drop=("R92",), basis="circuit", note="the read-back reads low always"))
    a(F("r92_short", g, "R92 shorted", drop=("R92",), add=(f"R92 {rb} arb {SHORT}",), basis="circuit", note="a mis-driven GPIO42 is no longer limited"))
    a(F("gpio42_high", g, "GPIO42 driven high by mistake (a firmware fault, not a component)", add=("VG42 g42 0 DC 3.3", "RG42 g42 arb 30"), basis="circuit",
        note="through R92 onto DRV_EN, against the 1 kohm of R_S" if v1 else "through R92 onto V_DRV"))

    # ---- expander ----
    g = "U5 expander"
    a(F("u5_rst_low", g, "U5 pin 3 RESET open and leaking low", sub=(("BWX", "q", "rfl"), ("BRX", "q", "rfl")), add=floating("rfl", "low"), basis="circuit", ack="rfl"))
    a(F("u5_rst_high", g, "U5 pin 3 RESET open and leaking high", sub=(("BWX", "q", "rfl"), ("BRX", "q", "rfl")), add=floating("rfl", "high"), basis="circuit", ack="rfl"))
    a(F("u5_rst_holds", g, "U5 pin 3 RESET opens during a run and holds its level", sub=(("BWX", "q", "rfl"), ("BRX", "q", "rfl")), add=floating("rfl", "holds", "q"), basis="circuit", ack="rfl"))
    a(F("u5_rst_ignored", g, "U5 ignores its reset input", keys=frozenset({"exp_ignores_reset"}), basis="circuit", ack="vcc"))
    a(F("u5_p05_high", g, "U5 pin P05 (RLY_ON) stuck high, or RLY_ON shorted to 3V3", add=("RBRG vcc rlyon 1",), basis="circuit", v="2"))
    a(F("u5_p00_low", g, "U5 valve pin stuck low, or a valve net shorted to ground", add=("RBRG zd 0 1",), basis="circuit"))
    for r in ("220", "1k", "2.2k", "4.7k"):
        a(F(f"zd_leak_{r}", g, f"leak of {r.replace('k', ' k') if 'k' in r else r + ' '}ohm from a valve net to ground", add=(f"RLK zd 0 {r}",), basis="circuit", note="LED current, not valve current: the MOC3023 fires for certain at 5 mA and the sheet gives no current below which it does not"))
    a(F("u5_p34_bridge", g, "U5 pins 3 and 4 bridged (RESET to P00: DRV_EN to the valve net of zone 1)", add=(f"RBRG q zd {SHORT}",), basis="circuit"))
    a(F("u5_vcc_open", g, "U5 pin 24 VCC or pin 12 GND open", basis="reading", reading="Not established what the part does. In V1 nothing else stands between the valve pins and the opto LEDs: count it as able to leave a valve on." if v1 else "Not established what the part does. The LED supply switch Q3 does not depend on it: the valve is off after the keep-alive stops. K1 depends on the gate of Q1, which R12 discharges if the pin floats."))

    # ---- relay drive ----
    g = "K1 drive"
    a(F("q1_ds", g, "Q1 drain and source joined, or RLY_A shorted to ground", add=(f"RBRG rlya 0 {SHORT}",), basis="circuit", v="2"))
    a(F("q1_gd", g, "Q1 gate and drain joined (RLY_G1 to RLY_A)", add=(f"RBRG rg1 rlya {SHORT}",), basis="circuit", v="2"))
    a(F("q1_gs", g, "Q1 pins 1 and 2 bridged (gate to source), or R12 shorted", add=(f"RBRG rg1 0 {SHORT}",), basis="circuit", v="2"))
    a(F("q1_g_high", g, "Q1 gate open and leaking high, or RLY_G1 shorted to 3V3", sub=(("MQ1", "rg1", "g1fl"),), add=floating("g1fl", "high"), basis="circuit", v="2"))
    a(F("q1_g_holds", g, "Q1 gate opens during a run and holds its level", sub=(("MQ1", "rg1", "g1fl"),), add=floating("g1fl", "holds", "rg1"), basis="circuit", v="2"))
    a(F("r11_open", g, "R11 open", drop=("R11",), basis="circuit", v="2"))
    a(F("r12_open", g, "R12 open", drop=("R12",), basis="circuit", note="the gate of Q1 keeps its charge when the expander is reset", v="2"))
    a(F("d5_open", g, "D5 open", basis="reading", reading="No flyback path: Q1 takes the coil's kick at every release, against its 30 V rating. The deck does not finish a run without the diode. K1 still follows Q1. Nothing reports it, as in revision A.", v="2"))
    a(F("k1_weld", g, "K1 contact welded, or JP1 bridged", basis="reading", reading="COM is live whatever the coil does. Gate drive still goes: the valve is off after the keep-alive stops. The back-up against a shorted output device is lost. Found by the weld check, as in revision A.", v="2"))

    # ---- nets ----
    g = "nets shorted to a rail"
    a(F("ka_gnd", g, "KA shorted to ground, or R33 shorted", add=(f"RBRG ka 0 {SHORT}",), basis="circuit"))
    a(F("ka_3v3", g, "KA shorted to 3V3", add=(f"RBRG vcc ka {SHORT}",)))
    a(F("r33_open", g, "R33 open", drop=("R33",), add=("RKAF ka 0 1e9",), note="the net floats when the pin does"))
    a(F("clr_3v3", g, "the clear net shorted to 3V3", add=(f"RBRG vcc clr {SHORT}",), note="as R53 shorted"))
    a(F("qo_3v3", g, "the output of U_WD shorted to 3V3", keys=frozenset({"q_stuck_high"})))
    a(F("drven_3v3", g, "DRV_EN shorted to 3V3", add=(f"RBRG vcc q {SHORT}",), basis="circuit"))
    a(F("drven_gnd", g, "DRV_EN shorted to ground", add=(f"RBRG q 0 {SHORT}",), basis="circuit"))
    a(F("drvn_gnd", g, "DRV_EN_N shorted to ground", add=(f"RBRG drvn 0 {SHORT}",), basis="circuit", v="2"))
    a(F("drven_leak_1k", g, "leak of 1 kohm from 3V3 to DRV_EN", add=("RLK vcc q 1k",), basis="circuit"))
    a(F("drven_leak_4k7", g, "leak of 4.7 kohm from 3V3 to DRV_EN", add=("RLK vcc q 4.7k",), basis="circuit"))
    a(F("ka_leak_1k", g, "leak of 1 kohm from 3V3 to KA", add=("RLK vcc ka 1k",)))

    # ---- bridges from KA to its neighbours on the module ----
    g = "bridges onto KA"
    a(F("ka_encb", g, "KA bridged to ENC_B or BTN_BACK (module pins 9 and 11): a contact behind 3.3 kohm and 10 kohm, closing at 20 Hz", add=contact("ka"), p=EASY))
    a(F("ka_back_no_r72", g, "the same with the 10 kohm series resistor shorted as well, a double fault", add=("VCSQ csq 0 PULSE(0 1 0.1 1u 1u 25m 50m)", "RCPU vcc ka 3.3k", "SCNT ka 0 csq 0 TRG"), p=EASY))
    for ohms in (0, 100, 470, 1000, 2200, 3300, 4700, 10000):
        a(F(f"ka_pp_{ohms}", g, f"KA bridged through {ohms} ohm to a push-pull 3.3 V output at 1 kHz (30 ohm source), easiest thresholds", add=pushpull(ohms, 1000.0), p=EASY))
    a(F("ka_pp_0_hard", g, "the same through 0 ohm, hardest thresholds (1.90 V rising on a 3.2 V rail)", add=pushpull(0, 1000.0), p=(("vcc", 3.2), ("vtp", 1.90 / 3.2), ("vtm", 1.70 / 3.2))))
    return [f for f in L if ("1" if v1 else "2") in f.v]


# --------------------------------------------------------------------------
# The runs for one fault
# --------------------------------------------------------------------------

T_START, T_WRITE, T_STOP = S.T_START, S.T_WRITE, S.T_STOP
# Limits a firmware stop test would use (study section 7.6: "near 0.1 to 0.35 s").
STOP_EARLY, STOP_LATE = 0.10, 0.35


def scenarios() -> dict[str, S.Scn]:
    return {
        "idle": S.Scn(name="idle", tend=1.2),
        "float": S.Scn(name="float", tend=1.2, oe=((0.0, 0.0),)),
        "pin-high": S.Scn(name="pin-high", tend=2.5, lvl=((0.0, 0.0), (0.3, 0.0), (0.300001, 1.0))),
        "stop-low": replace(S.run_then("low"), tend=4.0),
        "stop-high": replace(S.run_then("high"), tend=4.0),
        "stop-hiz": replace(S.run_then("hiz"), tend=4.0),
    }


def params(f: F, corner: str) -> S.P:
    ov = dict(f.p)
    vcc = ov.pop("vcc", 3.3)
    return replace(S.corner(corner, vcc, faults=f.keys), **ov)


def simulate(f: F, p: S.P, s: S.Scn, variant: str = "V2") -> S.Trace | None:
    """As study.simulate, with the fault's edits. None if the run did not finish."""
    import subprocess
    import tempfile

    with tempfile.TemporaryDirectory(prefix="zfstudy") as d:
        work = Path(d)
        (work / "deck.cir").write_text(build(p, s, f, variant))
        try:
            done = subprocess.run(["ngspice", "-b", "deck.cir"], cwd=work, capture_output=True, text=True, timeout=S.TIMEOUT)
        except subprocess.TimeoutExpired:
            return None
        out = work / "out.txt"
        if "aborted" in done.stdout + done.stderr or not out.exists():
            return None
        rows = [[float(x) for x in line.split()] for line in out.read_text().splitlines() if line.strip()]
    if not rows or rows[-1][0] < s.tend - 2 * s.step:
        return None
    tr = S.Trace(rows)
    tr.c["ack"] = [r[1 + 2 * len(S.COLUMNS)] for r in rows]
    return tr


def high(tr: S.Trace, name: str, level: float, t0: float, t1: float) -> bool:
    return tr.peak(name, t0, t1) > level


def judge(f: F, runs: dict, variant: str = "V2") -> dict:
    """Read the seven runs of one fault. runs: {(scenario, corner): (P, Scn, Trace or None)}."""
    r: dict = {"unfinished": [k for k, v in runs.items() if v[2] is None]}

    def get(sc, corner="long"):
        return runs.get((sc, corner), (None, None, None))

    # the three crashes
    ends, k1, vd, bits = {}, {}, {}, {}
    for stop in ("low", "high", "hiz"):
        p, s, tr = get(f"stop-{stop}")
        if tr is None:
            ends[stop] = k1[stop] = None
            continue
        rel = S.release(tr, s, S.last_edge(s))
        ends[stop], k1[stop] = rel["led"], rel["k1"]
        vd[stop] = tr.at("vdrv", s.tend - 0.01) > 1.0
        bits[stop] = tr.at("em", s.tend - 0.01) > 1.0
    p, s, tr = get("stop-low")
    ran = tr is not None and tr.peak("iled", T_WRITE + 0.05, T_STOP) > 5e-3
    dropped = ran and tr.at("iled", T_STOP - 0.005) < 5e-3
    r.update(ends=ends, k1=k1, ran=ran, dropped=dropped)
    r["valve_on"] = any(v == math.inf for v in ends.values())
    r["k1_on"] = variant == "V2" and any(v == math.inf for v in k1.values())
    r["vdrv_on"] = variant == "V2" and any(vd.values())
    # the highest LED current with nothing asked for: at rest, and 3 s after each stop
    p_, s_, tr_ = get("idle")
    r["i_rest"] = tr_.peak("iled", 0.4, s_.tend) if tr_ is not None else None
    r["bits_on"] = any(bits.values())

    # the tests
    found = []
    p, s, tr = get("idle")
    if tr is not None and (high(tr, "arb", 0.75 * p.vcc, 0.4, s.tend) or high(tr, "ack", 0.7 * p.vcc, 0.4, s.tend)):
        found.append("idle")
    p, s, tr = get("float")
    if tr is not None and (high(tr, "arb", 0.75 * p.vcc, 0.4, s.tend) or high(tr, "ack", 0.7 * p.vcc, 0.4, s.tend) or high(tr, "ka", 0.75 * p.vcc, 0.4, s.tend)):
        found.append("float")
    p, s, tr = get("pin-high")
    if tr is not None and (high(tr, "arb", 0.75 * p.vcc, 0.3 + 0.5, s.tend) or high(tr, "ack", 0.7 * p.vcc, 0.3 + 0.5, s.tend)):
        found.append("pin-high")
    for corner in ("short", "long"):
        p, s, tr = get("stop-low", corner)
        if tr is None:
            continue
        t_chk = T_START + 0.15
        if not (tr.at("arb", t_chk) > 0.75 * p.vcc and tr.at("ack", t_chk) > 0.7 * p.vcc) or not tr.peak("iled", T_WRITE + 0.05, T_STOP) > 5e-3:
            if "start" not in found:
                found.append("start")
        le = S.last_edge(s)
        early = tr.at("arb", le + STOP_EARLY) < 0.75 * p.vcc
        late = tr.at("arb", le + STOP_LATE + 0.05) > 0.3 * p.vcc or tr.at("ack", le + STOP_LATE + 0.05) > 0.7 * p.vcc
        if (early or late) and "start" not in found and "stop" not in found:
            found.append("stop")
    r["found"] = found
    return r


def verdict(f: F, r: dict) -> str:
    if r["unfinished"] and not r["ran"] and r["ends"].get("low") is None:
        return "run did not finish"
    rest = r["i_rest"] is not None and r["i_rest"] > S.I_DARK
    if r["valve_on"] or rest:
        return "**VALVE ON WITH NOTHING ASKED FOR**" if rest else "**VALVE STAYS ON**"
    parts = []
    if not r["ran"]:
        parts.append("no valve runs")
    elif r["dropped"]:
        parts.append("the valve drops during the run")
    if r["k1_on"]:
        parts.append("valve off, K1 stays closed")
    if r["vdrv_on"]:
        parts.append("valve off, LED supply stays on")
    if r["bits_on"] and not r["k1_on"]:
        parts.append("valve off, expander keeps its bits")
    return "; ".join(parts) if parts else "valve off"


def fmt(x) -> str:
    if x is None:
        return "-"
    if x == math.inf:
        return "**on**"
    return f"{x:.2f}"


def write(name: str, header: list, rows: list, title: str, notes: list) -> None:
    """As study.write, with this file named as the writer."""
    S.write(name, header, rows, title, notes)
    md = S.RESULTS / f"{name}.md"
    md.write_text(md.read_text().replace("Written by `study.py`; do not edit.", "Written by `faults_wide.py` for the review of the study; do not edit.", 1))


def pool(fn, items):
    """Processes, not threads: reading the result files is the slow part, and each worker returns
    only what it read off its traces, so that no trace is kept."""
    import os
    from concurrent.futures import ProcessPoolExecutor

    with ProcessPoolExecutor(max_workers=max(1, (os.cpu_count() or 2) * 3 // 4)) as ex:
        return list(ex.map(fn, items))


def one_fault(job: tuple) -> tuple:
    """All runs of one fault, judged. job: (variant, fault)."""
    variant, f = job
    scn = scenarios()
    runs = {}
    for name, corner in [(n, "long") for n in scn] + [("stop-low", "short")]:
        p, s = params(f, corner), scn[name]
        runs[(name, corner)] = (p, s, simulate(f, p, s, variant))
    return f.id, judge(f, runs, variant)


def one_trip(job: tuple) -> list:
    variant, corner, f, s = job
    p = S.corner(corner)
    tr = simulate(f, p, s, variant)
    if tr is None:
        return [variant, f.label, p.name, "run did not finish", "-", "-", "-"]
    t_dark = S.after(tr, "iled", S.I_DARK, s.t_ref)
    t_low = next((t for t, v in zip(tr.t, tr.c["q"]) if t >= s.t_ref and v < 0.3 * p.vcc), None)
    back = tr.first_above("q", 0.7 * p.vcc, (t_low or s.t_ref) + 1e-5)
    return [variant, f.label, p.name, f"{(t_low - s.t_ref) * 1e3:.2f}" if t_low else "never", f"{t_dark * 1e3:.2f}" if t_dark else "never lit",
            f"{(back - s.t_ref) * 1e3:.2f}" if back else "never", f"{tr.peak('iled', s.t_ref + 0.02) * 1e3:.3f}"]


def run_variant(variant: str, ids: list[str] | None) -> int:
    faults = fault_list(variant)
    assert len({f.id for f in faults}) == len(faults)
    scn = scenarios()
    if ids:
        faults = [f for f in faults if f.id in ids]
    sim_faults = [f for f in faults if f.basis != "reading"]
    n_runs = len(sim_faults) * (len(scn) + 1)
    by = dict(pool(one_fault, [(variant, f) for f in sim_faults]))
    v2 = variant == "V2"
    header = ["id", "group", "fault", "basis", "result", "valve current ends after the last edge: pin low / high / high-impedance, s"]
    header += ["K1 coil released: pin low / high / high-impedance, s"] if v2 else []
    header += ["LED current at rest, mA", "a valve ran", "found by", "note"]
    rows = []
    for f in faults:
        if f.basis == "reading":
            rows.append([f.id, f.group, f.label, "reading", f.reading, "-"] + (["-"] if v2 else []) + ["-", "-", "-", f.note])
            continue
        r = by[f.id]
        note = f.note + ((" " if f.note else "") + "Runs that did not finish: " + ", ".join(f"{a} ({b})" for a, b in r["unfinished"]) if r["unfinished"] else "")
        row = [f.id, f.group, f.label, f.basis, verdict(f, r), " / ".join(fmt(r["ends"][k]) for k in ("low", "high", "hiz"))]
        row += [" / ".join(fmt(r["k1"][k]) for k in ("low", "high", "hiz"))] if v2 else []
        row += ["-" if r["i_rest"] is None else f"{r['i_rest'] * 1e3:.2f}", "yes" if r["ran"] else "no", ", ".join(r["found"]) or "not found", note]
        rows.append(row)
    if ids:
        for row in rows:
            print(" | ".join(str(x) for x in row))
        return 0
    sim = [r for r in rows if r[3] != "reading"]
    on = [r for r in sim if r[4].startswith("**VALVE")]
    other = [r for r in on if "double fault" in r[2] or r[0].startswith("ka_pp") or r[0] == "gpio42_high"]
    layer = [r for r in sim if "stays" in r[4].lower() and r not in on or "keeps" in r[4]]
    what = "the expander reset only: no LED supply switch, no relay" if not v2 else "expander reset, LED supply switch, K1 on the expander bit"
    notes = [
        f"Variant {variant} ({what}). {len(rows)} faults: {len(sim)} simulated ({n_runs} ngspice runs), {len(rows) - len(sim)} by reading only.",
        "Each fault is present from power-up, except the pins that \"open during a run\", which open at 0.6 s. Long corner, 3.3 V, unless the row says otherwise;",
        "the stop test is also run at the short corner. A valve bit" + (" and `RLY_ON` are" if v2 else " is") + " written at 0.35 s and the keep-alive stops at 1.0 s.",
        "",
        f"- **{len(on)} simulated faults leave the valve on** with nothing asking for it: after the keep-alive stops in at least one of the three ways, or from power-up. {len(on) - len(other)} of them are single faults of a component, a pin or a net. The other {len(other)} are the push-pull bridges onto `KA` and the double faults.",
    ]
    if v2:
        notes.append(f"- {len(layer)} more leave the LED supply, the expander's bits or K1 on with the valve off.")
    notes += [
        "- \"Valve on\" means opto LED current above 0.5 mA, the study's own limit. The MOC3023 fires for certain at 5 mA and its sheet gives no current below which it does not.",
        "- Basis \"model\": the result depends on the behavioural model of the one-shot. \"circuit\": it does not. \"reading\": no run, the cell says why.",
        "- \"Found by\" names tests a firmware could make with the read-back on GPIO42 and the expander's acknowledge. The expander is taken to answer while its reset input, as it sees it, is above 0.7 x VCC.",
        "  idle: the read-back is high or the expander answers, with the pin driven low and no edge since power-up.",
        "  float: the same with the pin high-impedance, or the pin reads high. pin-high: either is still high 0.5 s after the pin was driven high and left there.",
        f"  start: 150 ms after the first edge the read-back is low or the expander does not answer, or no valve current flows. stop: after a run, the read-back falls earlier than {STOP_EARLY:g} s, or it is still high or the expander still answers {STOP_LATE + 0.05:g} s after the last edge.",
        "  The idle test here starts from power-up. A fault that sticks the one-shot only after its first trigger passes it; the stop test finds it at the end of the first run.",
        "  None of these tests measures valve current. A valve that is on at rest is found by the current measurement with every output off, which the model does not run.",
        "- \"**on**\" in a time column: still on at the end of the run, 3 s after the stop.",
    ]
    write(f"faults-wide-{variant.lower()}", header, rows, f"Wider fault injection on {variant}", notes)
    print(f"{variant}: {len(on)} faults leave the valve on: " + ", ".join(r[0] for r in on))
    return 0


def run_trip() -> None:
    """The window comparator of revision A on the clear input of the one-shot: a short without the latch.

    Not in the study. The outputs of U6B and U6C stay on the net that the
    study turns into the clear input (it is OC_TRIP_N today, with R53 and
    U13 on it). An over-current then pulls CLR low for as long as the
    current is above the trip level. The expander is reset and forgets its
    bits, which is the memory that the latch of revision A had.
    """
    items = []
    for variant in ("V1", "V2"):
        for corner in ("short", "long"):
            for label, t0, dur in (("0.1 ms, pin low at the time", 0.7062, 1e-4), ("3 ms, across a rising edge", 0.699, 3e-3), ("3 ms, pin high at the time", 0.701, 3e-3), ("8 ms", 0.702, 8e-3)):
                f = F("trip", "trip", label, add=(f"VTRP trp 0 PWL(0 0 {t0:.6f} 0 {t0 + 1e-5:.6f} 1 {t0 + dur:.6f} 1 {t0 + dur + 1e-5:.6f} 0)", "STRP clr 0 trp 0 TRIPSW", ".model TRIPSW SW(VT=0.5 VH=0.1 RON=60 ROFF=1e12)"))
                s = S.Scn(name=label, tend=1.5, run=((0.0, 0.0), (T_START, 0.0), (T_START + 1e-6, 1.0)), t_write=T_WRITE, t_ref=t0)
                items.append((variant, corner, f, s))

    rows = pool(one_trip, items)
    write("trip-via-clear", ["variant", "CLR held low for", "corner", "DRV_EN low after the trip, ms", "LED current under 0.5 mA after the trip, ms",
                               "DRV_EN high again after the trip, ms", "highest LED current later than 20 ms after the trip, mA"], rows,
            "An over-current trip on the clear input of the one-shot",
            ["Not in the study. The review's option: the window comparator of revision A (U6B, U6C behind R52 and C33) keeps its outputs on the net that becomes the",
             "clear input of the one-shot. The run pulls CLR low through 60 ohm for the time named, 0.7 s into a run with the keep-alive toggling throughout,",
             "as a comparator does while the current is above its trip level. The comparator and the current are not simulated here; `hardware/sim/isense.cir` has its 66 us.",
             "What the run shows: the expander is reset and forgets the valve bit, and drive comes back with nothing switched on.",
             "The results are printed every 0.05 ms, so a time of 0.05 ms means \"by the next point\"."])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--variant", choices=["V1", "V2"], help="one variant only")
    ap.add_argument("--ids", nargs="*", help="run only these faults and print; do not write the result files")
    ap.add_argument("--deck", nargs=2, metavar=("ID", "SCENARIO"), help="print one deck")
    ap.add_argument("--trip", action="store_true", help="only the trip run")
    ns = ap.parse_args()
    if ns.deck:
        variant = ns.variant or "V2"
        f = next(x for x in fault_list(variant) if x.id == ns.deck[0])
        sys.stdout.write(build(params(f, "long"), scenarios()[ns.deck[1]], f, variant))
        return 0
    if ns.trip:
        run_trip()
        return 0
    for variant in [ns.variant] if ns.variant else ["V1", "V2"]:
        run_variant(variant, ns.ids)
    if not ns.ids and not ns.variant:
        run_trip()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
