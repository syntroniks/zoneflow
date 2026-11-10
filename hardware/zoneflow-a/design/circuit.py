# SPDX-License-Identifier: CERN-OHL-P-2.0
"""ZoneFlow rev A schematic: where every symbol sits and which connections are drawn as wires.

Parts, values, LCSC numbers and the net on every pin come from hardware/netlist.yaml
through hwflow.capture. This file never states a connection that the netlist does not
have: a wire between two pins of different nets is refused, and KiCad's exported netlist
is compared with the netlist after every build.

Coordinates are in units of 1.27 mm (``P``), x to the right and y down.
"""

import sys
from pathlib import Path

import yaml

from hwflow import committed, parity
from hwflow.capture import DOWN, LEFT, RIGHT, UP, Capture
from hwflow.design import Design

HERE = Path(__file__).resolve().parent
BOARD = HERE.parent
HW = BOARD.parent
sys.path.insert(0, str(HERE))
import symbols  # noqa: E402

TABLE = committed.load_table(HERE / "parts.toml")  # parts, builds and the mounting holes

G = 2.54
POWER = {"GND": "power:GND", "3V3": "ZoneFlow:3V3", "V5": "ZoneFlow:V5", "V_BUCK": "ZoneFlow:V_BUCK",
         "VIN_DC": "ZoneFlow:VIN_DC", "VBUS": "ZoneFlow:VBUS", "V_DRV": "ZoneFlow:V_DRV"}

SHEETS = {
    "power": "F1 F4 D1 D2 R1 C1 C2 C3 U1 C4 L1 C5 C6 R2 R3 R4 R5 R6 C7 C8 R7 R8 R9 R10 C9",
    "usb": "J5 R15 R16 U2 R97 R98 C52 C53 C13 D4 D3 C10 U3 C11 C12 R17",
    "mcu": "U4 C14 C15 R18 C16 U12 C54 SW1 R19 SW2 R20 D6 R21 R22",
    "failsafe": "R33 R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U6.1 U6.2 U6.3 U6.5 C19 U7 C20 Q3 R25 R26 C21 R92 "
                "R52 C33 R53 R54 D8 R87",
    "isense": "R37 U8 C25 R38 R39 C26 R40 R41 C27 R42 R43 C28 R44 R45 C29 R46 C30 R47 C31 R48 R49 R50 R51 C32 "
              "R55 R56 C34 D9 R88 R57 R58 U6.4",
    "expander": "U5 C18 RN1 RN2 RN3 R96 R27 R28 K1 D5 Q1 R11 R12 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 D23 JP1",
    "sensors": "D12 R74 R75 D15 C44 R76 D13 R77 R78 D16 C45 R79 D14 R80 R81 D17 C46 R82 U11 C47 C49 D24 R83 R85 R86 "
               "D19 R84 D20 C48 F3 D18",
    "rtc": "U10 Y1 C36 C35 D10 D11 R59 BT1",
    "display": "J7 DS1 R60 Q4 R61 R62 R63 C37 C38 SW3 SW4 SW5 R64 R69 C39 R65 R70 C40 R66 R71 C41 R67 R72 C42 R68 R73 C43",
    "connectors": "J1 J2 J3 J4 J6 F2 C17 R23 R24 U9 J8 R99 " + " ".join(f"TP{i}" for i in range(1, 31)),
}
CHANNEL = "F101 D101 Q101 R105 C101 U101 R101 R103 R104 R102 D102"
INSTANCES = {"Z1": 100, "Z2": 200, "Z3": 300, "Z4": 400, "Z5": 500, "Z6": 600, "Z7": 700, "Z8": 800, "MV": 900}


def refs(spec):
    out = []
    for tok in spec.split():
        out.append((tok.split(".")[0], int(tok.split(".")[1])) if "." in tok else tok)
    return out


U = 1.27  # half the 2.54 mm grid: every coordinate below is in these units


def P(x, y):
    return (round(x * U, 2), round(y * U, 2))


def lead(s, pin, to, d, route="auto"):
    """Wire from a pin to a point and put the net's label, port or power symbol there."""
    s.wire(pin, to, route=route)
    s.label(to, d)


# --------------------------------------------------------------------------- output channel
def channel(s):
    """One 24 VAC output. Drawn as zone 1; the nine instances differ only in their references."""
    s.frame(*P(48, 46), *P(120, 100), "Drive: opto LED and indicator")
    s.frame(*P(122, 46), *P(214, 112), "Power stage: triac in the return leg of the valve")
    # Two branches hang from V_DRV and meet at the expander pin.
    r102 = s.attach("R102", 1, P(66, 60), DOWN)
    d102 = s.attach("D102", "A", P(66, 74), DOWN)
    s.wire(r102[2], d102["A"], label=P(66, 70))
    r101 = s.attach("R101", 1, P(88, 60), DOWN)
    u101 = s.place("U101", P(108, 76), ref_at=P(103, 70), value_at=P(104, 82))
    s.wire(r101[2], u101[1], route="vh", label=P(92, 74))
    s.wire(P(54, 92), P(98, 92), net="ZD_Z1_N")
    s.label(P(54, 92), LEFT)
    s.wire(d102["K"], P(66, 92))
    s.wire(u101[2], P(98, 78))
    s.wire(P(98, 78), P(98, 92))
    # Gate current comes from T2 through the opto-triac and R103.
    r103 = s.attach("R103", 2, P(126, 74), RIGHT)
    s.wire(u101[6], r103[2], label=P(116, 74))
    s.wire(r103[1], P(132, 60), route="hv")
    s.wire(P(132, 60), P(178, 60), net="T2_Z1")
    s.label(P(140, 60), RIGHT)
    q101 = s.place("Q101", P(154, 84))
    s.wire(q101["A2"], P(154, 60))
    s.wire(u101[4], P(140, 78), label=P(118, 78))
    s.wire(P(140, 78), q101["G"], route="vh")
    r104 = s.attach("R104", 1, P(140, 92), DOWN)
    s.wire(P(140, 86), r104[1])
    # Snubber and clamp, both returned to ISNS so that a shorted part shows as current in the shunt.
    r105 = s.attach("R105", 1, P(168, 66), DOWN)
    s.wire(P(168, 60), r105[1])
    c101 = s.attach("C101", 1, P(168, 84), DOWN)
    s.wire(r105[2], c101[1], label=P(168, 78))
    f101 = s.attach("F101", 2, P(178, 60), RIGHT)
    s.wire(f101[1], P(208, 60))
    s.label(P(208, 60), RIGHT)
    d101 = s.attach("D101", 1, P(196, 78), DOWN)
    s.wire(P(196, 60), d101[1])
    s.wire(P(140, 106), P(208, 106), net="ISNS")
    s.label(P(208, 106), RIGHT)
    for pin in (r104[2], q101["A1"], c101[2], d101[2]):
        s.wire(pin, (pin.pos[0], P(0, 106)[1]))
    s.note("OUTPUT CHANNEL (hardware design section 6). Nine instances: Z1 to Z8 and MV.\n"
           "The valve sits between a COM terminal and OUT. Current returns through PTC F1, the triac\n"
           "and the shared shunt (ISNS) to GND. The gate is fired from T2 through opto-triac U1 and R3,\n"
           "so gate current has the polarity of T2: quadrants I and III, which every triac supports.\n"
           "\n"
           "Q1  T810-600B-TR, 8 A, three-quadrant, DPAK. Pin 1 A1 (T1) to ISNS, pin 2 and tab A2 (T2), pin 3 gate.\n"
           "    The tab is net T2: give it its own copper island of 2 cm2 or more (section 6.7).\n"
           "U1  MOC3023, random phase. LED current 7.1 mA worst case, 12 mA typical; it needs 5.6 mA at -20 C.\n"
           "R1  150R sets the opto LED current. R2 680R gives the indicator LED 1.6 mA.\n"
           "R3  100R limits the opto-triac to 0.45 A at the 45 V peak. R4 1k holds the gate at T1.\n"
           "R5 + C1  47R and 10 nF snubber. F1 PTC 1.1 A hold, 40 A maximum. D1 SMBJ48CA clamps at 77 V.\n"
           "D2  lights when the expander pin is low AND V_DRV is on: the real drive state.\n"
           "\n"
           "ZD_N is low to turn the output on. V_DRV is removed by the fail-safe circuit.",
           P(14, 116))
    s.note("4-ZONE BUILD: instances Z5 to Z8 are not fitted\n(KiCad variant \"4-zone\"). Z1 to Z4 and MV always are.",
           P(124, 116))


# --------------------------------------------------------------------------- real-time clock
def rtc(s):
    s.frame(*P(40, 30), *P(190, 104), "Real-time clock with coin-cell backup")
    u10 = s.place("U10", P(130, 72))
    y1 = s.attach("Y1", 1, P(108, 69), DOWN)
    s.wire(u10["OSCI"], y1[1], route="hv")
    s.wire(u10["OSCO"], y1[2], route="hv")
    c36 = s.attach("C36", 1, P(96, 72), DOWN)
    s.wire(P(108, 68), c36[1], route="hv")
    s.label(P(100, 68), RIGHT)
    s.label(P(116, 76), RIGHT)
    # Supply: 3V3 or the cell, whichever is higher, through silicon diodes.
    s.wire(u10["VDD"], P(130, 48))
    s.wire(P(110, 48), P(146, 48), net="VDD_RTC")
    s.label(P(132, 48), RIGHT)
    d10 = s.attach("D10", "K", P(110, 48), LEFT)
    lead(s, d10["A"], P(98, 44), UP, route="hv")
    d11 = s.attach("D11", "K", P(110, 56), LEFT)
    s.wire(d11["K"], P(114, 56))
    s.wire(P(114, 56), P(114, 48))
    r59 = s.attach("R59", 2, P(92, 56), LEFT)
    s.wire(r59[2], d11["A"], label=P(93, 56))
    bt1 = s.place_pin("BT1", 1, P(72, 64))
    s.wire(r59[1], bt1[1], route="hv")
    s.label(P(74, 56), RIGHT)
    c35 = s.attach("C35", 1, P(146, 52), DOWN)
    s.wire(P(146, 48), c35[1])
    s.flag(P(140, 48))
    lead(s, u10["SCL"], P(164, 68), RIGHT)
    lead(s, u10["SDA"], P(164, 70), RIGHT)
    lead(s, u10["~{INT}"], P(164, 76), RIGHT)
    s.note("REAL-TIME CLOCK (hardware design section 14).\n"
           "U10 PCF8563T at I2C address 0x51 on I2C0. It keeps time down to 1.0 V.\n"
           "D10 and D11 are silicon diodes, not Schottky: a Schottky leaks as much as the RTC draws.\n"
           "VDD_RTC is about 2.7 V from 3V3 and 2.6 V from the cell. R59 limits the current into the\n"
           "cell to under 1 mA if D11 fails short.\n"
           "Y1 32.768 kHz, 12.5 pF. C36 18 pF with the 25 pF inside OSCO gives an 11.7 pF load.\n"
           "CLKO is not used: firmware writes 0x00 to register 0x0D to turn the 32 kHz output off.\n"
           "BT1 holds a CR2032, fitted at final assembly. Back-up current 0.25 uA typical: about ten years.",
           P(14, 110))



# --------------------------------------------------------------------------- USB and power OR-ing
def usb(s):
    s.frame(*P(22, 30), *P(130, 106), "USB-C device port")
    s.frame(*P(134, 14), *P(224, 54), "Power OR-ing and 3.3 V regulator")
    j5 = s.place("J5", P(40, 60))
    # VBUS: four contacts joined, then along the top to the OR-ing diode.
    for n in ("A4", "A9", "B4", "B9"):
        s.wire(j5[n], (P(54, 0)[0], j5[n].pos[1]))
    s.wire(P(54, 46), P(54, 52), net="VBUS")
    s.wire(P(54, 46), P(54, 40))
    d4 = s.attach("D4", "A", P(144, 40), RIGHT)
    s.wire(P(54, 40), d4["A"])
    s.label(P(74, 40), UP)
    s.flag(P(86, 40))  # VBUS comes in through connector pins, which are passive
    c13 = s.attach("C13", 1, P(64, 42), DOWN)
    s.wire(P(64, 40), c13[1])
    # D- and D+: both contacts of each, through the ESD array, then the series footprints.
    u2 = s.place("U2", P(80, 58), ref_at=P(64, 62.5), value_at=P(64, 64.5))
    s.wire(j5["B7"], u2[1], label=P(62, 58))  # U2 passes each line through: pins 1 and 6, 3 and 4
    s.wire(j5["A7"], P(56, 58), route="hv")
    s.wire(j5["A6"], u2[3], label=P(62, 60))
    s.wire(j5["B6"], P(58, 60), route="hv")
    r98 = s.attach("R98", 1, P(94, 58), RIGHT)
    s.wire(u2[6], r98[1], label=P(85, 58))
    s.wire(r98[2], P(118, 58))
    s.label(P(118, 58), RIGHT)
    c53 = s.attach("C53", 1, P(108, 60), DOWN)
    s.wire(P(108, 58), c53[1])
    r97 = s.attach("R97", 1, P(94, 76), RIGHT)
    s.wire(u2[4], P(88, 60))
    s.wire(P(88, 60), r97[1], route="vh", label=P(88, 76))
    s.wire(r97[2], P(118, 76))
    s.label(P(118, 76), RIGHT)
    c52 = s.attach("C52", 1, P(108, 78), DOWN)
    s.wire(P(108, 76), c52[1])
    # Configuration channel pull-downs and the shell.
    r15 = s.attach("R15", 1, P(62, 70), DOWN)
    s.wire(j5["A5"], r15[1], route="hv", label=P(52, 66))
    r16 = s.attach("R16", 1, P(72, 72), DOWN)
    s.wire(j5["B5"], r16[1], route="hv", label=P(52, 68))
    for n in ("A1", "A12", "B1", "B12"):
        s.wire(j5[n], (j5[n].pos[0], P(0, 90)[1]))
    s.wire(P(36, 90), P(42, 90), net="GND")
    s.label(P(38, 90), DOWN)
    r17 = s.attach("R17", 1, P(46, 92), DOWN)
    s.wire(j5["SH"], r17[1], label=P(46, 89))
    # OR-ing: whichever of V_BUCK and VBUS is higher feeds V5.
    d3 = s.attach("D3", "A", P(144, 28), RIGHT)
    lead(s, d3["A"], P(140, 24), UP, route="hv")
    s.wire(d3["K"], P(156, 28))
    s.wire(P(156, 28), P(156, 40))
    u3 = s.place("U3", P(192, 40))
    s.wire(d4["K"], u3["VI"])
    s.label(P(162, 40), UP)
    s.flag(P(168, 40))
    c10 = s.attach("C10", 1, P(176, 42), DOWN)
    s.wire(P(176, 40), c10[1])
    s.wire(u3["VO"], P(220, 40))
    s.label(P(220, 40), UP)
    c11 = s.attach("C11", 1, P(206, 42), DOWN)
    s.wire(P(206, 40), c11[1])
    c12 = s.attach("C12", 1, P(214, 42), DOWN)
    s.wire(P(214, 40), c12[1])
    s.note("USB-C AND POWER OR-ING (hardware design section 5).\n"
           "J5 is a USB 2.0 device port. R15 and R16, 5.1k on CC1 and CC2, mark the board as a device.\n"
           "U2 clamps D+, D- and VBUS: place it at J5. R97 and R98 are 0R, C52 and C53 are not fitted:\n"
           "series and shunt footprints for EMC work, placed within a few millimetres of the module pins.\n"
           "D3 (from V_BUCK) and D4 (from VBUS) OR the two sources onto V5: about 4.95 V from 24 VAC,\n"
           "4.1 to 4.9 V from USB. Neither source can feed the other. USB alone runs the logic, not the valves.\n"
           "U3 LDL1117S33R: 3V3 is 3.20 to 3.40 V. Load 162 mA typical, 498 mA peak. Give the tab copper.\n"
           "USB ground is circuit ground, which is one leg of the 24 VAC transformer: read section 5.3\n"
           "and the safety page before connecting a mains-powered computer.",
           P(14, 110))


# --------------------------------------------------------------------------- 24 VAC input and logic supply
def power(s):
    s.frame(*P(20, 28), *P(92, 96), "24 VAC input: a fuse in each leg, surge clamp")
    s.frame(*P(96, 28), *P(184, 96), "Half-wave rectifier, bulk capacitor, supply monitor")
    s.frame(*P(186, 28), *P(319, 96), "LM5164 buck converter, 5.28 V")
    # Both transformer legs are fused. AC_N becomes circuit ground behind F1.
    f4 = s.attach("F4", 1, P(40, 50), RIGHT)
    s.wire(P(28, 50), f4[1])
    s.label(P(28, 50), LEFT)
    d2 = s.attach("D2", "A", P(74, 50), RIGHT)
    s.wire(f4[2], d2["A"])
    d1 = s.attach("D1", 1, P(58, 58), DOWN)
    s.wire(P(58, 50), d1[1])
    s.wire(P(66, 50), P(66, 40))
    s.wire(P(66, 40), P(78, 40))
    s.label(P(78, 40), RIGHT)
    f1 = s.attach("F1", 1, P(40, 80), RIGHT)
    s.wire(P(28, 80), f1[1])
    s.label(P(28, 80), LEFT)
    s.wire(f1[2], P(58, 80))
    s.label(P(58, 80), DOWN)
    s.flag(P(52, 80))
    # Rectifier, surge resistor and the bulk capacitor.
    r1 = s.attach("R1", 1, P(100, 50), RIGHT)
    s.wire(d2["K"], r1[1], label=P(84, 50))
    u1 = s.place("U1", P(216, 56), ref_at=P(208, 46), value_at=P(221, 65))
    s.wire(r1[2], u1["VIN"])
    s.label(P(110, 50), UP)
    s.flag(P(116, 50))
    for ref, x in (("C1", 122), ("C2", 136), ("C3", 150)):
        c = s.attach(ref, 1, P(x, 54), DOWN)
        s.wire(P(x, 50), c[1])
    r9 = s.attach("R9", 1, P(164, 54), DOWN)
    s.wire(P(164, 50), r9[1])
    r10 = s.attach("R10", 1, P(164, 70), DOWN)
    s.wire(r9[2], r10[1])
    c9 = s.attach("C9", 1, P(172, 70), DOWN)
    s.wire(P(164, 64), P(172, 64), net="VIN_MON")
    s.wire(P(172, 64), c9[1])
    s.wire(P(172, 64), P(172, 60))
    s.label(P(172, 60), RIGHT)
    # Buck converter: under-voltage lockout, on-time, bootstrap, ripple injection, feedback.
    r7 = s.attach("R7", 1, P(190, 52), DOWN)
    s.wire(P(190, 50), r7[1])
    r8 = s.attach("R8", 1, P(190, 66), DOWN)
    s.wire(r7[2], r8[1])
    s.wire(P(190, 60), P(196, 60), net="UVLO")
    s.wire(P(196, 60), u1["EN/UVLO"], route="vh", label=P(197, 54))
    r2 = s.attach("R2", 1, P(200, 66), DOWN)
    s.wire(u1["RON"], P(200, 60), label=P(201, 60))
    s.wire(P(200, 60), r2[1])
    c4 = s.attach("C4", 1, P(234, 42), RIGHT)
    s.wire(u1["BST"], P(230, 50), label=P(227, 50))
    s.wire(P(230, 50), c4[1], route="vh")
    s.wire(c4[2], P(246, 42))
    s.wire(P(246, 42), P(246, 54))
    l1 = s.attach("L1", 1, P(256, 54), RIGHT)
    s.wire(u1["SW"], l1[1], label=P(227, 54))
    r6 = s.attach("R6", 1, P(252, 68), RIGHT)
    s.wire(P(250, 54), P(250, 68))
    s.wire(P(250, 68), r6[1])
    c7 = s.attach("C7", 1, P(266, 68), RIGHT)
    s.wire(r6[2], c7[1], label=P(259, 68))
    s.wire(c7[2], P(276, 68))
    s.wire(P(276, 68), P(276, 54))
    c8 = s.attach("C8", 1, P(262, 72), DOWN)
    s.wire(P(262, 68), c8[1])
    s.wire(c8[2], P(262, 82))
    s.wire(u1["FB"], P(242, 58), label=P(227, 58))
    s.wire(P(242, 58), P(242, 82))
    s.wire(P(242, 82), P(290, 82))
    s.wire(u1["PGOOD"], P(236, 60))
    s.wire(P(236, 60), P(236, 90))
    s.wire(P(236, 90), P(244, 90))
    s.label(P(244, 90), RIGHT)
    r3 = s.attach("R3", 1, P(290, 58), DOWN)
    s.wire(P(290, 54), r3[1])
    r4 = s.attach("R4", 1, P(290, 70), DOWN)
    s.wire(r3[2], r4[1], label=P(290, 67))
    s.wire(r4[2], P(290, 82))
    r5 = s.attach("R5", 1, P(290, 84), DOWN)
    s.wire(P(290, 82), r5[1])
    s.wire(l1[2], P(310, 54))
    s.flag(P(282, 54))
    for ref, x in (("C5", 300), ("C6", 310)):
        c = s.attach(ref, 1, P(x, 58), DOWN)
        s.wire(P(x, 54), c[1])
    s.label(P(296, 54), UP)
    s.note("24 VAC INPUT AND LOGIC SUPPLY (hardware design section 4).\n"
           "\n"
           "F4, F1   3 A time-lag fuses in holders, one in each transformer leg (section 5.3 says why two).\n"
           "         Terminal 2, AC_N, becomes circuit GND behind F1. Both holders sit in the fuse well behind J1.\n"
           "D1       SMCJ48CA: stands off 48 V, above the 45.3 V peak of 32 VAC. Clamps at 77.4 V.\n"
           "D2, R1   Half-wave rectifier. R1 is the surge-rated SR2512JK (5 %), not the FK (1 %) part:\n"
           "         it takes 237 to 385 W for about 1 ms when the board is plugged in at the mains peak.\n"
           "C1       470 uF 63 V, through-hole, on the bottom side lying down. VIN_DC is 26 to 31 V at 24 VAC\n"
           "         and 44 V at 32 VAC with no load. Ripple 0.16 A typical, 0.48 A at the peak corner.\n"
           "R9, R10  VIN_MON = VIN_DC / 31 to GPIO3: about 0.97 V at 24 VAC, 1.43 V at 32 VAC.",
           P(22, 104))
    s.note("U1       LM5164, 100 V synchronous buck (TI SNVSAU4D). It survives the 77.4 V clamp level.\n"
           "         V_BUCK = 1.2 V x (1 + (R3 + R4) / R5) = 5.28 V, 5.16 to 5.40 V over tolerance.\n"
           "R2       47k sets the on-time: 281 kHz.   R7, R8: on at 9.0 V, off at 8.4 V.\n"
           "C4       2.2 nF bootstrap. The datasheet fixes this value; a larger one damages the part.\n"
           "R6, C7, C8   Type 3 ripple injection: 24 mV at FB at 34 V in, 13 mV at 10 V.\n"
           "L1       47 uH, Isat 2.0 A. Ripple 0.34 A, peak 0.99 A against a 1.25 A current limit.\n"
           "Load on V_BUCK: 0.28 A typical, 0.78 A peak (section 4.2). It also feeds the relay coil and\n"
           "the sensor supply directly, and V5 through D3 on the USB sheet.\n"
           "\n"
           "LAYOUT: C2 and C3 at U1 pins 2 and 1 with the shortest loop. Keep the SW node small and away\n"
           "from the current-sense parts. PWR_FLAG marks nets that are fed through a passive part.",
           P(178, 104))


# --------------------------------------------------------------------------- sensor inputs and sensor supply
def sensor_input(s, y, name, tvs, rpu, rser, clamp, cap, rout):
    """One field input: terminal on the left, GPIO on the right."""
    d = s.attach(tvs, 1, P(44, y + 4), DOWN)
    s.wire(P(30, y), P(66, y), net=f"T_{name}")
    s.label(P(30, y), LEFT)
    s.wire(P(44, y), d[1])
    s.attach(rpu, 2, P(56, y), UP)
    rs = s.attach(rser, 1, P(66, y), RIGHT)
    ro = s.attach(rout, 1, P(108, y), RIGHT)
    s.wire(rs[2], ro[1], label=P(88, y))
    dc = s.place(clamp, P(80, y - 8))
    s.wire(dc[3], P(80, y))
    s.wire(dc[1], P(72, y - 8))
    s.label(P(72, y - 8), DOWN)
    s.wire(dc[2], P(88, y - 8))
    s.label(P(88, y - 8), UP)
    c = s.attach(cap, 1, P(100, y + 2), DOWN)
    s.wire(P(100, y), c[1])
    s.wire(ro[2], P(126, y))
    s.label(P(126, y), RIGHT)


def sensors(s):
    s.frame(*P(20, 20), *P(150, 136), "Three sensor inputs: RAIN, FLOW, AUX")
    s.frame(*P(20, 142), *P(226, 222), "Switched, current-limited 5 V sensor supply")
    sensor_input(s, 44, "RAIN", "D12", "R74", "R75", "D15", "C44", "R76")
    sensor_input(s, 80, "FLOW", "D13", "R77", "R78", "D16", "C45", "R79")
    sensor_input(s, 116, "AUX", "D14", "R80", "R81", "D17", "C46", "R82")
    # Sensor supply: V_BUCK, current-limited switch, miswire protection, PTC, terminal.
    def Q(x, y):  # the supply block, moved as one
        return P(x + 12, y)

    u11 = s.place("U11", Q(72, 172), ref_at=Q(64, 164), value_at=Q(72, 164))
    s.wire(u11["IN"], Q(38, 168))
    s.label(Q(38, 168), UP)
    c47 = s.attach("C47", 1, Q(46, 170), DOWN)
    s.wire(Q(46, 168), c47[1])
    s.wire(u11["EN"], Q(56, 172))
    s.wire(Q(56, 172), Q(56, 192))
    s.wire(Q(56, 192), Q(28, 192))
    s.label(Q(28, 192), LEFT)
    r86 = s.attach("R86", 1, Q(36, 194), DOWN)
    s.wire(Q(36, 192), r86[1])
    s.wire(u11["~{FAULT}"], Q(60, 174))
    s.wire(Q(60, 174), Q(60, 212))
    s.wire(Q(60, 212), Q(28, 212))
    s.label(Q(28, 212), LEFT)
    s.attach("R85", 2, Q(48, 212), UP)
    r83 = s.attach("R83", 1, Q(88, 178), DOWN)
    s.wire(u11["ILIM"], Q(88, 174), label=Q(83, 174))
    s.wire(Q(88, 174), r83[1])
    d19 = s.attach("D19", "A", Q(118, 168), RIGHT)
    s.wire(u11["OUT"], d19["A"], label=Q(84, 168))
    d24 = s.attach("D24", "K", Q(100, 172), DOWN)
    s.wire(Q(100, 168), d24["K"])
    c49 = s.attach("C49", 1, Q(110, 172), DOWN)
    s.wire(Q(110, 168), c49[1])
    r84 = s.attach("R84", 1, Q(134, 168), RIGHT)
    s.wire(d19["K"], r84[1], label=Q(125, 168))
    f3 = s.attach("F3", 1, Q(170, 168), RIGHT)
    s.wire(r84[2], f3[1], label=Q(141, 168))
    d20 = s.attach("D20", "K", Q(152, 172), DOWN)
    s.wire(Q(152, 168), d20["K"])
    c48 = s.attach("C48", 1, Q(162, 172), DOWN)
    s.wire(Q(162, 168), c48[1])
    s.wire(f3[2], Q(198, 168))
    s.label(Q(198, 168), RIGHT)
    d18 = s.attach("D18", 1, Q(186, 172), DOWN)
    s.wire(Q(186, 168), d18[1])
    s.note("SENSOR INPUTS (hardware design section 13).\n"
           "\n"
           "Each terminal has a 10k pull-up to V5 (0.5 mA wetting\n"
           "current), 10k in series, a BAT54S clamp to 3V3 and GND,\n"
           "22 nF and 1k into the GPIO. A closed contact or an\n"
           "open-collector sensor reads low. The filter passes 200 Hz.\n"
           "It does not remove the 1 to 2 ms bounce of a reed-switch\n"
           "meter; firmware does.\n"
           "\n"
           "32 VAC on a signal terminal for ever is survived: the TVS\n"
           "does not conduct below 48 V, and the pull-up and series\n"
           "resistors are 1206 parts rated 200 V that dissipate 0.11 W.\n"
           "The clamp passes 4.2 mA peak into 3V3.\n"
           "\n"
           "D12, D13, D14, D18: SMBJ48CA at each field terminal,\n"
           "clamping at 77.4 V. Place each at its terminal, with a PCB\n"
           "spark gap where the layout allows.\n"
           "\n"
           "The GND terminals of J4 are circuit ground, which is the\n"
           "transformer leg AC_N.",
           P(156, 24))
    s.note("SENSOR SUPPLY (section 13.5). One 5V terminal for flow meters with electronics, fed from V_BUCK:\n"
           "off on USB power. GPIO45 (SENS_PWR_EN, a strapping pin held low by R86) turns it on.\n"
           "U11 TPS2553 with R83 = 200k limits at 135 mA (116 to 158 mA) and reports on SENS_FLT_N.\n"
           "Terminal voltage about 4.8 V at 20 mA, 4.5 V at 50 mA. Treat 100 mA as the usable current.\n"
           "D19 blocks a +45 V miswire. D20 clamps a negative one and F3 (0.2 A hold) trips.\n"
           "D24 keeps the OUT pin of U11 above -0.3 V while F3 is tripping.",
           P(84, 196))


def pin_label(s, pin, length=12):
    """Wire straight out of a pin and put the net's label or port at the end."""
    d = pin.outward
    end = (round(pin.pos[0] + d[0] * length * U, 2), round(pin.pos[1] + d[1] * length * U, 2))
    s.wire(pin, end)
    s.label(end, d)


# --------------------------------------------------------------------------- display and controls
def contact_filter(s, y, rpu, rser, cap):
    """Pull-up for 1 mA of contact current, 10k and 100 nF between a switch contact and its GPIO."""
    s.attach(rpu, 2, P(84, y), UP)
    rs = s.attach(rser, 1, P(92, y), RIGHT)
    s.wire(P(84, y), rs[1])
    s.wire(rs[2], P(120, y))
    s.label(P(120, y), RIGHT)
    c = s.attach(cap, 1, P(106, y + 2), DOWN)
    s.wire(P(106, y), c[1])


def display(s):
    s.frame(*P(24, 24), *P(138, 166), "Encoder and buttons: five debounced contacts, active low")
    s.frame(*P(184, 36), *P(316, 114), "Display connector and backlight switch")
    for y, parts in ((44, ("R64", "R69", "C39")), (70, ("R65", "R70", "C40")), (96, ("R66", "R71", "C41")),
                     (122, ("R67", "R72", "C42")), (148, ("R68", "R73", "C43"))):
        contact_filter(s, y, *parts)
    sw3 = s.place("SW3", P(50, 62), mirror="y")
    s.wire(sw3["A"], P(84, 44), route="vh", label=P(60, 44))
    s.wire(sw3["B"], P(62, 64))
    s.wire(P(62, 64), P(84, 70), route="vh", label=P(64, 70))
    s.wire(sw3["C"], P(68, 62))
    s.label(P(68, 62), DOWN)
    s.wire(sw3["S1"], P(36, 60))
    s.wire(P(36, 60), P(84, 96), route="vh", label=P(40, 96))
    s.wire(sw3["S2"], P(40, 64))
    s.label(P(40, 64), DOWN)
    for ref, y in (("SW4", 122), ("SW5", 148)):
        sw = s.place(ref, P(50, y), mirror="y")
        s.wire(sw[1], P(84, y), label=P(58, y))
        s.wire(sw[2], P(42, y))
        s.label(P(42, y), DOWN)
    # Display: the panel tail plugs into J7. Pin names on J7 are the panel's.
    j7 = s.place("J7", P(250, 70))
    for name in ("CS", "RS", "SCL", "SDA"):
        pin_label(s, j7[name], 14)
    s.wire(j7["RST"], P(204, 58))
    s.label(P(204, 58), LEFT)
    s.attach("R63", 2, P(212, 58), UP)
    s.wire(j7["IOVCC"], P(238, 64))
    s.wire(j7["VCC"], P(238, 66))
    s.wire(P(238, 66), P(238, 64))
    s.wire(P(238, 64), P(208, 64))
    s.label(P(208, 64), UP)
    for ref, x in (("C38", 228), ("C37", 218)):
        c = s.attach(ref, 1, P(x, 66), DOWN)
        s.wire(P(x, 64), c[1])
    r60 = s.attach("R60", 2, P(226, 82), LEFT)
    s.wire(r60[2], j7["LEDA"], label=P(229, 82))
    s.wire(r60[1], P(214, 82))
    s.label(P(214, 82), UP)
    q4 = s.place("Q4", P(232, 98))
    s.wire(j7["LEDK"], q4["D"], route="hv")
    s.label(P(234, 90), RIGHT)
    r61 = s.attach("R61", 2, P(216, 98), LEFT)
    s.wire(r61[2], q4["G"], label=P(217, 98))
    r62 = s.attach("R62", 1, P(222, 100), DOWN)
    s.wire(P(222, 98), r62[1])
    s.wire(r61[1], P(200, 98))
    s.label(P(200, 98), LEFT)
    for n in ("1", "12"):
        s.wire(j7[n], (j7[n].pos[0], P(0, 100)[1]))
    s.wire(P(250, 100), P(252, 100), net="GND")
    s.label(P(250, 100), DOWN)
    s.place("DS1", P(290, 62))
    s.note("DS1 is the panel itself. It has no footprint:\nits 12-way tail plugs into J7 at final assembly.", P(270, 68))
    s.note("DISPLAY (hardware design section 15).\n"
           "DS1 HS20HS072RX, 2.0 inch 240 x 320 IPS, ST7789, write-only 4-wire SPI on SPI2:\n"
           "SCLK GPIO12, MOSI GPIO11, CS GPIO10, DC GPIO13, RST GPIO14. Start at 15 MHz.\n"
           "J7 is a bottom-contact flip-lock connector. Its pad 1 is panel pin 1 (GND): the pad nearest the\n"
           "terminal-block edge, at y = 85.68 in the mechanical contract. The contacts of the tail are on the\n"
           "display face; folded under the panel they face the board. Check both on a sample panel.\n"
           "Backlight: four LEDs in parallel, about 3.0 V. R60 33R from V5 gives 59 mA at 24 VAC and 27 mA\n"
           "at the lowest USB voltage. Q4 switches the cathode under PWM from GPIO21, 1 to 20 kHz.\n"
           "R62 holds Q4 off in reset. R63 holds RST high. C37 and C38 at J7 pins 8 and 9.\n"
           "LAYOUT: keep J7 within about 40 mm of module pins 18 to 22. TFT_BL keeps toggling when the\n"
           "firmware hangs: it must never run next to KA (section 8.5).",
           P(184, 120))
    s.note("CONTROLS (section 15.4).\n"
           "SW3 EC11J1525402: 30 detents, 15 pulses per turn, push switch.\n"
           "SW4 Back and SW5 Manual / Home: 6 x 6 mm, 10 mm actuator.\n"
           "Each contact: 3.3k pull-up (1 mA, the encoder's minimum),\n"
           "then 10k and 100 nF: 1.0 ms closing, 1.3 ms opening.\n"
           "All five inputs read low when the contact is closed.",
           P(140, 26))


# --------------------------------------------------------------------------- MCU
def mcu(s):
    s.frame(*P(18, 30), *P(108, 72), "Reset: supervisor, RC delay, button")
    s.frame(*P(18, 78), *P(108, 112), "Boot strap (GPIO0)")
    s.frame(*P(18, 118), *P(108, 144), "Status LED (GPIO46)")
    s.frame(*P(226, 30), *P(284, 66), "I2C0 pull-ups")
    u4 = s.place("U4", P(160, 100))
    skip = {"3V3", "GND", "IO35", "IO36", "IO37"}
    for p in u4.all_pins():
        if p.pin.name not in skip:
            pin_label(s, p, 12)
    s.wire(u4["3V3"], P(140, 74))
    s.wire(P(140, 74), P(140, 56))
    s.wire(P(140, 56), P(112, 56))
    s.label(P(112, 56), UP)
    for ref, x in (("C15", 130), ("C14", 120)):
        c = s.attach(ref, 1, P(x, 58), DOWN)
        s.wire(P(x, 56), c[1])
    for p in u4.pins("GND"):
        s.wire(p, (p.pos[0], P(0, 136)[1]))
    s.wire(P(158, 136), P(162, 136), net="GND")
    s.label(P(160, 136), DOWN)
    # Reset: U12 holds EN low below 3.0 V and for 200 ms after; R18 and C16 are the module's reference circuit.
    u12 = s.place("U12", P(50, 50))
    lead(s, u12["VDD"], P(34, 46), UP, route="hv")
    s.place("C54", P(24, 56))
    s.wire(u12["~{RESET}"], P(100, 50))
    s.label(P(100, 50), RIGHT)
    s.attach("R18", 2, P(70, 50), UP)
    c16 = s.attach("C16", 1, P(80, 52), DOWN)
    s.wire(P(80, 50), c16[1])
    sw1 = s.attach("SW1", 1, P(92, 52), DOWN)
    s.wire(P(92, 50), sw1[1])
    # Boot.
    s.attach("R19", 2, P(70, 96), UP)
    s.wire(P(70, 96), P(100, 96), net="BOOT")
    s.label(P(100, 96), RIGHT)
    sw2 = s.attach("SW2", 1, P(92, 98), DOWN)
    s.wire(P(92, 96), sw2[1])
    # Status LED.
    r20 = s.attach("R20", 1, P(56, 132), RIGHT)
    s.wire(P(44, 132), r20[1])
    s.label(P(44, 132), LEFT)
    d6 = s.attach("D6", "A", P(72, 132), RIGHT)
    s.wire(r20[2], d6["A"], label=P(63, 132))
    s.wire(d6["K"], P(86, 132))
    s.label(P(86, 132), DOWN)
    # I2C0 pull-ups.
    r21 = s.place_pin("R21", 1, P(238, 44))
    r22 = s.place_pin("R22", 1, P(250, 44))
    s.wire(r21[2], P(238, 60))
    s.wire(P(238, 60), P(260, 60))
    s.label(P(260, 60), RIGHT)
    s.wire(r22[2], P(250, 54))
    s.wire(P(250, 54), P(260, 54))
    s.label(P(260, 54), RIGHT)
    s.note("MCU (hardware design section 12; every pin with its reset state is in hardware/pinmap.yaml).\n"
           "\n"
           "U4 ESP32-S3-WROOM-1-N16R8: 16 MB flash, 8 MB octal PSRAM, which takes GPIO35 to 37.\n"
           "Rated to 65 C, or 85 C with PSRAM ECC on: firmware must enable it. C14 and C15 at pin 2.\n"
           "\n"
           "No valve, relay or enable function is on a GPIO. The valve outputs are on the I2C expander,\n"
           "which the fail-safe circuit holds in reset. Only KA (GPIO38) and I2C0 can influence a valve.\n"
           "\n"
           "Strapping pins:\n"
           "  GPIO0   BOOT: R19 pull-up and button SW2. Low at reset selects download mode.\n"
           "  GPIO3   VIN_MON: analogue input, held low at reset by its 10k divider leg.\n"
           "  GPIO45  SENS_PWR_EN: 100k to GND and one CMOS input. Never add a pull-up to this net.\n"
           "  GPIO46  STATUS_LED: R20 and D6 to GND can only pull down.\n"
           "\n"
           "ADC1 only: GPIO1 ADC_LO, GPIO2 ADC_HI, GPIO3 VIN_MON, GPIO7 RLY_B_RB.\n"
           "GPIO7 and GPIO42 read back the two keep-alive detectors. They stay inputs for the life of the\n"
           "firmware. GPIO40 reads the over-current latch through 10k.\n"
           "USB: GPIO19 D-, GPIO20 D+, through the series footprints on the USB sheet.\n"
           "\n"
           "U12 TLV803EA30: open drain, pulls EN low while 3V3 is under 3.0 V (2.94 to 3.06 V) and for\n"
           "130 to 270 ms after. R18 10k and C16 1 uF delay EN by about 10 ms. SW1 is reset.\n"
           "\n"
           "LAYOUT: the 18 x 6 mm antenna end of the module is beyond the top board edge. No copper on any\n"
           "layer under or beside it. Route KA from pin 31 with ground on both sides, at least 1 mm from\n"
           "any other signal and never next to TFT_BL, AC_ZC or the buck converter (section 8.5).",
           P(200, 74))


# --------------------------------------------------------------------------- expander and valve-common relay
def expander(s):
    s.frame(*P(22, 34), *P(150, 112), "16-bit I2C expander, address 0x74, held in reset by DRV_EN")
    s.frame(*P(22, 126), *P(150, 208), "Defined levels on every expander pin while it is in reset")
    s.frame(*P(154, 34), *P(319, 144), "Valve-common relay: coil current passes Q1 and Q2 in series")
    u5 = s.place("U5", P(90, 80))
    for p in u5.all_pins():
        if p.pin.name not in ("VCC", "GND", "A0", "A1"):
            pin_label(s, p, 10)
    s.wire(u5["A0"], P(72, 76))
    s.wire(u5["A1"], P(72, 78))
    s.wire(P(72, 76), P(72, 78))
    s.label(P(72, 78), DOWN)
    s.place("C18", P(40, 52))
    # Pull-ups to V_DRV on the nine valve pins, pull-downs on the four spare pins, the variant strap.
    for ref, y in (("RN1", 150), ("RN2", 172)):
        rn = s.place(ref, P(72, y), rot=270)
        for n in "1234":
            pin_label(s, rn[n], 8)
        for n in "5678":
            s.wire(rn[n], (P(80, 0)[0], rn[n].pos[1]))
        s.wire(P(80, y - 4), P(80, y + 2), net="V_DRV")
        s.wire(P(80, y - 4), P(80, y - 8))
        s.label(P(80, y - 8), UP)
    r96 = s.attach("R96", 1, P(66, 194), RIGHT)
    s.wire(P(60, 194), r96[1])
    s.label(P(60, 194), LEFT)
    s.wire(r96[2], P(80, 194))
    s.label(P(80, 194), UP)
    rn3 = s.place("RN3", P(124, 150), rot=270)
    for n in "1234":
        pin_label(s, rn3[n], 6)
    for n in "5678":
        s.wire(rn3[n], (P(132, 0)[0], rn3[n].pos[1]))
    s.wire(P(132, 146), P(132, 152), net="GND")
    s.wire(P(132, 152), P(132, 156))
    s.label(P(132, 156), DOWN)
    s.attach("R27", 2, P(130, 188), UP)
    r28 = s.attach("R28", 1, P(130, 190), DOWN)
    s.wire(P(130, 188), r28[1])
    s.wire(P(130, 188), P(120, 188))
    s.label(P(120, 188), LEFT)
    s.note("R28: 8-zone build only.\nNot fitted: bit P14 reads high,\nfirmware runs four zones.", P(96, 196))
    # Relay. Q1 follows the expander bit, so it needs DRV_EN. Q2 has its own detector on KA.
    k1 = s.place("K1", P(260, 60), ref_at=P(270, 58), value_at=P(270, 60))
    d5 = s.attach("D5", "K", P(242, 56), DOWN)
    s.wire(d5["A"], P(242, 72))
    s.wire(P(242, 72), P(256, 72), label=P(244, 72))
    q1 = s.place("Q1", P(254, 84))
    s.wire(k1["A2"], q1["D"])
    r11 = s.attach("R11", 2, P(238, 84), LEFT)
    s.wire(r11[2], q1["G"], label=P(239, 84))
    r12 = s.attach("R12", 1, P(244, 86), DOWN)
    s.wire(P(244, 84), r12[1])
    s.wire(r11[1], P(224, 84))
    s.label(P(224, 84), LEFT)
    q2 = s.place("Q2", P(254, 128))
    s.wire(q1["S"], q2["D"])
    s.wire(P(256, 112), P(244, 112))
    s.label(P(244, 112), LEFT)
    s.attach("R93", 2, P(270, 104), UP)
    r94 = s.attach("R94", 1, P(280, 104), RIGHT)
    s.wire(P(256, 104), r94[1])
    s.wire(r94[2], P(306, 104))
    s.label(P(306, 104), RIGHT)
    r95 = s.attach("R95", 1, P(296, 106), DOWN)
    s.wire(P(296, 104), r95[1])
    # The relay detector: a diode pump straight into the gate of Q2. No supply rail reaches that gate.
    r89 = s.attach("R89", 1, P(166, 124), RIGHT)
    s.wire(P(160, 124), r89[1])
    s.label(P(160, 124), LEFT)
    c51 = s.attach("C51", 1, P(182, 124), RIGHT)
    s.wire(r89[2], c51[1], label=P(173, 124))
    d22 = s.place_pin("D22", 3, P(202, 124), rot=180, mirror="y", ref_at=P(204, 131), value_at=P(204, 133))
    s.wire(c51[2], d22[3], label=P(190, 124))
    s.wire(d22[1], P(192, 128))
    s.label(P(192, 128), DOWN)
    s.wire(d22[2], q2["G"], label=P(210, 128))
    for ref, x in (("C50", 222), ("R14", 232), ("R90", 242)):
        c = s.attach(ref, 1, P(x, 130), DOWN)
        s.wire(P(x, 128), c[1])
    # Contacts: AC_L in, COM out. D23 clamps the valve common; JP1 only if K1 is not fitted.
    s.wire(k1["14"], P(266, 44))
    s.wire(P(266, 44), P(306, 44))
    s.label(P(306, 44), RIGHT)
    d23 = s.attach("D23", 1, P(280, 46), DOWN)
    s.wire(P(280, 44), d23[1])
    s.wire(k1["13"], P(306, 72), route="vh")
    s.label(P(306, 72), RIGHT)
    jp1 = s.place("JP1", P(296, 58), rot=90, ref_at=P(298, 57), value_at=P(298, 59))
    s.wire(jp1[2], P(296, 44))
    s.wire(jp1[1], P(296, 72))
    s.note("VALVE-COMMON RELAY (hardware design section 7).\n"
           "K1 G5NB-1A-E, 5 V 125R coil fed from V_BUCK, feeds AC_L to the COM terminals. With K1 open no valve\n"
           "has power, whatever a shorted triac, TVS or opto-triac does. It closes only when Q1 AND Q2 conduct.\n"
           "Q1: on while expander bit RLY_ON is high. DRV_EN low resets the expander and R12 turns Q1 off in 0.1 ms.\n"
           "Q2: on while the relay detector sees edges on KA. R89, C51, D22 pump the reservoir C50; R14 and R90\n"
           "    (two, so that one open resistor only doubles the time) drain it. Gate about 2.7 V while toggling.\n"
           "    Q2 lets go 0.12 to 0.95 s after the last edge, within 1.4 s if the pin stops high.\n"
           "R93, R94, R95: read-back of the node between Q1 and Q2 on GPIO7. 3.05 V = Q2 off, 0 V = Q2 on.\n"
           "D23 SMBJ48CA clamps COM while K1 is open and takes the inductive kick when it opens under load.\n"
           "JP1: open. Bridge only if the relay is not fitted (then the fault analysis of section 8.5 is void).\n"
           "LAYOUT: no test pad on RLY_G2, KB_N1 or KB_N0. Surround the gate node of Q2 with ground: a leakage\n"
           "path from any rail to that node is the only way to hold Q2 on. Keep this pump (R89, C51, D22) at\n"
           "least 10 mm from the gate-drive pump (R29, C22, D7), and turn C51 at right angles to C22.\n"
           "K1 is through-hole, inserted from the bottom side.",
           P(156, 150))
    s.note("EXPANDER (section 8.2). U5 TCA9539: every pin is an input while RESET is low, which is whenever no\n"
           "valve runs. A valve is on when its bit is an output and low: P00 to P03 Z1 to Z4, P04 MV, P10 to P13\n"
           "Z5 to Z8. P05 RLY_ON is on when high. P14 reads the build variant, P15 the sensor supply fault.\n"
           "RN1, RN2, R96 hold the valve pins at V_DRV, not 3V3, so an off opto LED never sees reverse voltage.\n"
           "RN3 holds the four spare pins low. RN2 is fitted on both builds.",
           P(24, 212))


def hang(s, ref, x, y, gap=2):
    """A two-terminal part hanging from the wire at (x, y); its other end gets its own label or ground."""
    part = s.attach(ref, 1, P(x, y + gap), DOWN)
    s.wire(P(x, y), part[1])
    return part


# --------------------------------------------------------------------------- fail-safe gate and over-current latch
def failsafe(s):
    s.frame(*P(20, 46), *P(204, 102), "Detector A: keep-alive edges on KA charge C23; U6A compares it with 1.32 V")
    s.frame(*P(20, 110), *P(164, 160), "Over-current window comparator on ISENSE_LO")
    s.frame(*P(208, 98), *P(319, 180), "Latch and drive enable")
    # Diode pump. No DC path from KA: C22 passes edges only.
    r29 = s.attach("R29", 1, P(46, 68), RIGHT)
    s.wire(P(30, 68), r29[1])
    s.label(P(30, 68), LEFT)
    hang(s, "R33", 38, 68)
    c22 = s.attach("C22", 1, P(62, 68), RIGHT)
    s.wire(r29[2], c22[1], label=P(53, 68))
    d7 = s.place_pin("D7", 3, P(82, 68), rot=180, mirror="y", ref_at=P(84, 75), value_at=P(84, 77))
    s.wire(c22[2], d7[3], label=P(70, 68))
    s.wire(d7[1], P(72, 72))
    s.label(P(72, 72), DOWN)
    r31 = s.attach("R31", 1, P(126, 72), RIGHT)
    s.wire(d7[2], r31[1])
    s.wire(P(92, 72), P(92, 62))
    s.wire(P(92, 62), P(100, 62))
    s.label(P(100, 62), RIGHT)
    for ref, x in (("C23", 98), ("R30", 108), ("R91", 118)):
        hang(s, ref, x, 72)
    u6a = s.place("U6", P(162, 74), unit=1)
    s.wire(r31[2], u6a["+"], label=P(140, 72))
    hang(s, "C24", 138, 72)
    s.wire(P(150, 72), P(150, 60))
    r32 = s.attach("R32", 1, P(156, 60), RIGHT)
    s.wire(P(150, 60), r32[1])
    s.wire(r32[2], P(174, 60))
    s.wire(P(174, 60), P(174, 74))
    r34 = s.place_pin("R34", 1, P(146, 84))
    r35 = s.attach("R35", 1, P(146, 94), DOWN)
    s.wire(r34[2], r35[1])
    s.wire(P(146, 92), P(156, 92))
    s.wire(P(156, 92), u6a["-"])
    s.label(P(156, 86), RIGHT)
    # KA_OK: open collector with pull-up R36. High while the reservoir is charged.
    s.wire(u6a[1], P(268, 74))
    s.label(P(268, 74), RIGHT)
    s.attach("R36", 2, P(184, 74), UP)
    # Window comparator: either output pulls OC_TRIP_N low when the current signal leaves VL..VH.
    r52 = s.attach("R52", 1, P(40, 136), RIGHT)
    s.wire(P(30, 136), r52[1])
    s.label(P(30, 136), LEFT)
    s.wire(r52[2], P(80, 136), label=P(62, 136))
    hang(s, "C33", 56, 136)
    u6b = s.place("U6", P(100, 126), unit=2)
    u6c = s.place("U6", P(100, 146), unit=3)
    s.wire(P(80, 128), P(80, 144), net="OC_CMP")
    s.wire(P(80, 128), u6b["-"])
    s.wire(P(80, 144), u6c["+"])
    s.wire(u6b["+"], P(72, 124))
    s.label(P(72, 124), LEFT)
    s.wire(u6c["-"], P(72, 148))
    s.label(P(72, 148), LEFT)
    s.wire(u6b[2], P(112, 126))
    s.wire(u6c[14], P(112, 146))
    s.wire(P(112, 126), P(112, 146))
    s.attach("R53", 2, P(124, 136), UP)
    s.wire(P(140, 136), P(140, 148), net="OC_TRIP_N")
    s.wire(P(140, 148), P(148, 148))
    s.label(P(148, 148), RIGHT)
    # Latch: OC_TRIP_N low sets FAULT, KA_OK low clears it. U7C: DRV_EN_N = NAND(KA_OK, FAULT_N).
    # The gate symbols carry only their reference; the part number is on the supply unit below.
    u7b = s.place("U7", P(226, 110), unit=2, ref_at=P(222, 104.5), show_value=False)
    u7a = s.place("U7", P(226, 134), unit=1, mirror="x", ref_at=P(222, 140), show_value=False)
    u7c = s.place("U7", P(270, 108), unit=3, ref_at=P(266, 102.5), show_value=False)
    u7d = s.place("U7", P(296, 108), unit=4, ref_at=P(292, 102.5), show_value=False)
    s.wire(P(112, 136), u7a[1])
    s.wire(P(196, 74), P(196, 108))
    s.wire(P(196, 108), u7b[4])
    s.wire(P(260, 74), P(260, 106))
    s.wire(P(260, 106), u7c[9])
    s.wire(u7b[6], u7c[10], label=P(240, 110))
    s.wire(P(238, 110), P(238, 118))
    s.wire(P(238, 118), P(214, 118))
    s.wire(P(214, 118), P(214, 132))
    s.wire(P(214, 132), u7a[2])
    r54 = s.attach("R54", 1, P(246, 134), RIGHT)
    s.wire(u7a[3], r54[1])
    s.wire(P(238, 134), P(238, 126))
    s.wire(P(238, 126), P(216, 126))
    s.wire(P(216, 126), P(216, 112))
    s.wire(P(216, 112), u7b[5])
    d8 = s.attach("D8", "A", P(260, 134), RIGHT)
    s.wire(r54[2], d8["A"], label=P(253, 134))
    s.wire(d8["K"], P(272, 134))
    s.label(P(272, 134), DOWN)
    s.wire(P(238, 134), P(238, 144))
    s.wire(P(238, 144), P(246, 144))
    s.label(P(246, 144), RIGHT)
    r87 = s.attach("R87", 1, P(252, 120), RIGHT)
    s.wire(P(250, 110), P(250, 120))
    s.wire(P(250, 120), r87[1])
    s.wire(r87[2], P(262, 120))
    s.label(P(262, 120), RIGHT)
    s.wire(u7c[8], P(284, 108))
    s.wire(P(284, 106), P(284, 110), net="DRV_EN_N")
    s.wire(P(284, 106), u7d[12])
    s.wire(P(284, 110), u7d[13])
    s.wire(u7d[11], P(308, 108))
    s.label(P(308, 108), RIGHT)
    r92 = s.attach("R92", 1, P(304, 112), DOWN)
    s.wire(P(304, 108), r92[1])
    s.wire(r92[2], P(304, 124))
    s.wire(P(304, 124), P(298, 124))
    s.label(P(298, 124), LEFT)
    # Q3 switches the opto LED supply. R25 keeps it off while 3V3 ramps.
    q3 = s.place("Q3", P(292, 154), mirror="x", ref_at=P(297, 152.5), value_at=P(297, 155))
    s.wire(P(280, 108), P(280, 154))
    s.label(P(280, 130), RIGHT)
    s.wire(P(280, 154), q3["G"])
    s.attach("R25", 2, P(272, 154), UP)
    s.wire(P(272, 154), P(280, 154), net="DRV_EN_N")
    s.wire(q3["D"], P(294, 164))
    s.wire(P(294, 164), P(314, 164))
    s.label(P(314, 164), UP)
    hang(s, "R26", 300, 164)
    hang(s, "C21", 308, 164)
    s.flag(P(304, 164))  # V_DRV is fed through the channel of Q3, a passive pin
    # Supply pins of the two logic packages.
    s.place("U6", P(42, 190), unit=5, ref_at=P(44, 189), value_at=P(44, 191.5))
    s.place("C19", P(62, 190))
    s.place("U7", P(86, 190), unit=5, ref_at=P(92, 189), value_at=P(92, 191.5))
    s.place("C20", P(108, 190))
    s.note("FAIL-SAFE GATE (hardware design section 8) and OVER-CURRENT LATCH (section 9).\n"
           "\n"
           "Detector A. Firmware toggles KA (GPIO38) every 5 ms; an edge at least every 20 ms is needed.\n"
           "Each rising edge pushes charge through C22 and D7 into C23. R30 and R91 (two, so that one open\n"
           "resistor only doubles the hold-up) drain it. U6A switches on at 1.55 V on KA_N2 and off at 0.94 V\n"
           "(R31, R32 hysteresis; reference 1.32 V from R34, R35). KA_OK is high 14 to 41 ms after the first\n"
           "edge and low 0.45 to 0.9 s after the last, 1.2 s if the pin stops high. A steady level, high or\n"
           "low, never enables it. R33 holds KA low while the ESP32 is in reset.\n"
           "\n"
           "DRV_EN = KA_OK AND NOT FAULT. Low does three things: Q3 removes V_DRV from every opto LED, the\n"
           "expander is held in reset, and the expander bit that drives relay transistor Q1 is released.\n"
           "DRV_EN_RB (through R92) lets firmware time detector A on GPIO42.\n"
           "\n"
           "Over-current. U6B pulls OC_TRIP_N low above VH (1.425 V), U6C below VL (0.188 V): 4.6 A peak in\n"
           "either polarity, 4.0 to 5.4 A over tolerance, about 70 us after a short begins (R52, C33: 47 us).\n"
           "U7A and U7B latch it. D8 lights while FAULT is set. Only KA_OK going low clears the latch.\n"
           "R87 stops GPIO40 from overdriving the latch node if it is ever left as an output.\n"
           "\n"
           "U7 is a 74HC132: Schmitt inputs for the slow edges of the open-collector comparators.",
           P(118, 166))
    s.note("LAYOUT (section 8.5): TP8 on KA_N2 keeps ground around it. No pad on KA_N1.\n"
           "Keep this pump (R29, C22, D7) at least 10 mm from the relay pump (R89, C51, D22).",
           P(22, 104), bold=False)


# --------------------------------------------------------------------------- current sense and zero-cross
def isense(s):
    s.frame(*P(20, 26), *P(214, 150), "Shunt and two difference amplifiers, both biased at VB")
    s.frame(*P(236, 26), *P(300, 98), "Bias and trip thresholds")
    s.frame(*P(20, 156), *P(184, 208), "Mains zero-cross and presence detector")
    # Shunt. R38 and R42 sense its ISNS pad, R40 and R44 its ground pad: Kelvin connections.
    r37 = s.attach("R37", 1, P(60, 58), DOWN)
    r38 = s.attach("R38", 1, P(112, 58), RIGHT)
    r42 = s.attach("R42", 1, P(112, 118), RIGHT)
    r40 = s.attach("R40", 1, P(112, 74), RIGHT)
    r44 = s.attach("R44", 1, P(112, 102), RIGHT)
    s.wire(P(30, 58), r38[1])
    s.label(P(30, 58), LEFT)
    s.wire(P(40, 58), P(40, 118))
    s.wire(P(40, 118), r42[1])
    s.wire(r37[2], P(60, 88))
    s.wire(P(60, 88), P(104, 88))
    s.wire(P(104, 88), P(104, 74))
    s.wire(P(104, 74), r40[1])
    s.wire(P(104, 88), P(104, 102))
    s.wire(P(104, 102), r44[1])
    s.wire(P(60, 88), P(60, 92))
    s.label(P(60, 92), DOWN)
    # Low gain, 133.3 mV/A: inrush, totals above 0.7 A rms, the over-current comparators.
    u8a = s.place("U8", P(150, 60), unit=1, mirror="x")
    s.wire(r38[2], u8a["-"], label=P(119, 58))
    s.wire(P(126, 58), P(126, 38))
    r39 = s.attach("R39", 1, P(134, 46), RIGHT)
    s.wire(P(126, 46), r39[1])
    s.wire(r39[2], P(164, 46))
    c26 = s.attach("C26", 1, P(134, 38), RIGHT)
    s.wire(P(126, 38), c26[1])
    s.wire(c26[2], P(164, 38))
    s.wire(P(164, 38), P(164, 60))
    s.wire(u8a[1], P(164, 60))
    s.wire(u8a["+"], P(132, 62))
    s.wire(P(132, 62), P(132, 74))
    s.wire(r40[2], P(132, 74), label=P(119, 74))
    r41 = hang(s, "R41", 132, 74, gap=4)
    s.wire(P(132, 76), P(142, 76))
    c27 = hang(s, "C27", 142, 76)
    s.wire(r41[2], c27[2])
    # High gain, 665.3 mV/A: one valve, open-circuit detection.
    u8b = s.place("U8", P(150, 116), unit=2)
    s.wire(r42[2], u8b["-"], label=P(119, 118))
    s.wire(P(126, 118), P(126, 138))
    r43 = s.attach("R43", 1, P(134, 130), RIGHT)
    s.wire(P(126, 130), r43[1])
    s.wire(r43[2], P(164, 130))
    c28 = s.attach("C28", 1, P(134, 138), RIGHT)
    s.wire(P(126, 138), c28[1])
    s.wire(c28[2], P(164, 138))
    s.wire(P(164, 138), P(164, 116))
    s.wire(u8b[7], P(164, 116))
    s.wire(u8b["+"], P(132, 114))
    s.wire(P(132, 114), P(132, 102))
    s.wire(r44[2], P(132, 102), label=P(119, 102))
    r45 = s.attach("R45", 1, P(132, 98), UP)
    s.wire(P(132, 102), r45[1])
    s.wire(P(132, 100), P(142, 100))
    c29 = s.attach("C29", 1, P(142, 98), UP)
    s.wire(P(142, 100), c29[1])
    s.wire(r45[2], P(132, 84))
    s.wire(c29[2], P(142, 84))
    s.wire(P(142, 84), P(152, 84))
    s.label(P(152, 84), RIGHT)
    # Outputs: anti-alias filters at the ADC pins, and the raw signals for the comparators and test pads.
    for y, r, c in ((60, "R46", "C30"), (116, "R47", "C31")):
        rr = s.attach(r, 1, P(178, y), RIGHT)
        s.wire(P(164, y), rr[1])
        s.wire(rr[2], P(204, y))
        s.label(P(204, y), RIGHT)
        hang(s, c, 194, y)
        s.wire(P(170, y), P(170, y - 10))
        s.wire(P(170, y - 10), P(180, y - 10))
        s.label(P(180, y - 10), RIGHT)
    s.place("U8", P(40, 134), unit=3, ref_at=P(42, 133), value_at=P(42, 135.5))
    s.place("C25", P(60, 134))
    # One resistor chain makes VH, VB and VL, so the trip window stays centred on the bias.
    r48 = s.place_pin("R48", 1, P(250, 42))
    r49 = s.attach("R49", 1, P(250, 54), DOWN)
    s.wire(r48[2], r49[1])
    r50 = s.attach("R50", 1, P(250, 68), DOWN)
    s.wire(r49[2], r50[1])
    r51 = s.attach("R51", 1, P(250, 82), DOWN)
    s.wire(r50[2], r51[1])
    s.wire(P(250, 50), P(264, 50))
    s.label(P(264, 50), RIGHT)
    s.wire(P(250, 64), P(284, 64))
    s.label(P(284, 64), RIGHT)
    hang(s, "C32", 274, 64)
    s.wire(P(250, 78), P(264, 78))
    s.label(P(264, 78), RIGHT)
    # Zero-cross: AC_L divided by 12, clamped by D9, compared with VL.
    r55 = s.attach("R55", 1, P(40, 180), RIGHT)
    s.wire(P(30, 180), r55[1])
    s.label(P(30, 180), LEFT)
    r88 = s.attach("R88", 1, P(76, 180), RIGHT)
    s.wire(r55[2], r88[1], label=P(47, 180))
    d9 = s.place("D9", P(62, 172))
    s.wire(d9[3], P(62, 180))
    s.wire(d9[1], P(54, 172))
    s.label(P(54, 172), DOWN)
    s.wire(d9[2], P(70, 172))
    s.label(P(70, 172), UP)
    u6d = s.place("U6", P(132, 182), unit=4)
    s.wire(r88[2], u6d["+"], label=P(83, 180))
    hang(s, "R56", 94, 180)
    hang(s, "C34", 104, 180)
    s.wire(u6d["-"], P(118, 184))
    s.label(P(118, 184), LEFT)
    s.wire(P(120, 180), P(120, 168))
    r57 = s.attach("R57", 1, P(126, 168), RIGHT)
    s.wire(P(120, 168), r57[1])
    s.wire(r57[2], P(144, 168))
    s.wire(P(144, 168), P(144, 182))
    s.wire(u6d[13], P(166, 182))
    s.label(P(166, 182), RIGHT)
    s.attach("R58", 2, P(154, 182), UP)
    s.note("CURRENT MEASUREMENT (hardware design section 10).\n"
           "R37 0.1R 1 % 2 W carries the return current of every valve: ISNS is the common T1 node of all triacs.\n"
           "0.23 W at 1.5 A rms. A 20 A short lifts ISNS to 2.8 V for a half-cycle; the 7.5k input resistors\n"
           "keep that inside the op-amp's 2 mA input limit.\n"
           "U8A: gain 10k / 7.5k = 1.333, 133.3 mV/A, linear to 5.8 A peak.  U8B: 49.9k / 7.5k = 6.653,\n"
           "665.3 mV/A, linear to 1.17 A peak. Both outputs are AC waveforms centred on VB, about 0.79 V.\n"
           "Firmware takes the rms over 100 ms windows; there is no rectifier. C26 to C29: 15 kHz.\n"
           "R46 with C30 and R47 with C31: 1.6 kHz at the ADC pins; place the capacitors at module pins 39, 38.\n"
           "\n"
           "LAYOUT: Kelvin connections. R38 and R42 go to the ISNS pad of R37, R40 and R44 to its ground pad\n"
           "with their own trace, not to the ground plane. Keep this block away from L1, the display clock\n"
           "and the antenna.",
           P(220, 104))
    s.note("THRESHOLDS (section 9.1). VH 1.425 V, VB 0.806 V, VL 0.188 V: trip at 0.619 V / 0.1333 V/A = 4.64 A peak.\n"
           "ZERO-CROSS (section 11). R55, R88, R56 divide AC_L by 12. D9 clamps; R88 halves the clamped level so\n"
           "the LM2901 input stays above -0.3 V (-0.18 V at -20 C and 32 VAC). U6D compares with VL; R57 adds\n"
           "30 mV of hysteresis. AC_ZC is a square wave at mains frequency, high while AC_L is above about 2.3 V.\n"
           "No edge for 25 ms means the 24 VAC is gone. R55 is a 1206 part rated 200 V.",
           P(190, 162))


# --------------------------------------------------------------------------- connectors, test pads, mechanical
TERMINAL_NOTE = ("Terminal order along the bottom edge, left to right as the installer sees it:\n"
                 "J1  24V~  24V~          transformer; pin 2 (AC_N) becomes circuit GND\n"
                 "J2  COM  COM  MV  1  2  3  4\n"
                 "J3  5  6  7  8          8-zone build only (KiCad variant \"4-zone\": not fitted)\n"
                 "J4  5V  FLOW  GND  RAIN  GND  AUX  GND\n"
                 "Headers KF2EDGR-3.81, plugs KF2EDGK-3.81 supplied loose. 300 V, 8 A.\n"
                 "Mark terminal 2 of J1 on the silkscreen.")


def connectors(s):
    s.frame(*P(20, 26), *P(160, 130), "Field terminals: pluggable 3.81 mm headers")
    s.frame(*P(166, 44), *P(300, 102), "Qwiic expansion port on its own bus, I2C1")
    s.frame(*P(220, 112), *P(300, 162), "Factory and debug header (pads only)")
    s.frame(*P(84, 138), *P(216, 216), "Test pads")
    s.frame(*P(238, 168), *P(318, 192), "Mounting holes (mechanical contract)")
    for ref, y in (("J1", 40), ("J2", 64), ("J3", 88), ("J4", 112)):
        j = s.place(ref, P(64, y))
        for p in j.all_pins():
            if s.net_of(p) != "GND":
                pin_label(s, p, 10)
    s.note(TERMINAL_NOTE, P(76, 36))
    # Qwiic: its own supply PTC, pull-ups and ESD array, so a faulty cable cannot hold 3V3 or I2C0 down.
    j6 = s.place("J6", P(274, 78))
    u9 = s.place("U9", P(232, 80), ref_at=P(235, 85.5), value_at=P(235, 87.5))
    # U9 passes each line through (pins 1 and 6, 3 and 4), so both sides carry the net's label.
    s.wire(u9[6], j6[3], label=P(256, 80))
    s.wire(u9[4], P(248, 82))
    s.wire(P(248, 82), P(248, 90))
    s.wire(P(248, 90), P(266, 90), label=P(250, 90))
    s.wire(P(266, 90), j6[4], route="vh")
    s.wire(j6[2], P(254, 78))
    s.wire(P(254, 78), P(254, 62))
    f2 = s.attach("F2", 2, P(214, 62), LEFT)
    s.wire(P(254, 62), f2[2], label=P(217, 62))
    s.wire(u9[5], P(232, 62))
    hang(s, "C17", 244, 62)
    lead(s, f2[1], P(202, 58), UP, route="hv")
    s.wire(P(178, 80), u9[1])
    s.label(P(178, 80), LEFT)
    s.attach("R23", 2, P(190, 80), UP)
    s.wire(u9[3], P(222, 82))
    s.wire(P(222, 82), P(222, 96))
    s.wire(P(222, 96), P(178, 96))
    s.label(P(178, 96), LEFT)
    s.attach("R24", 2, P(204, 96), UP)
    # Debug header: 3V3, GND, TXD0 through R99, RXD0, EN, IO0.
    j8 = s.place("J8", P(274, 134))
    lead(s, j8[1], P(262, 124), UP, route="hv")
    r99 = s.attach("R99", 2, P(246, 134), LEFT)
    s.wire(r99[2], j8[3], label=P(248, 134))
    s.wire(r99[1], P(232, 134))
    s.label(P(232, 134), LEFT)
    for n, x, y in ((4, 260, 144), (5, 264, 150), (6, 268, 156)):
        s.wire(j8[n], (P(x, 0)[0], j8[n].pos[1]))
        s.wire((P(x, 0)[0], j8[n].pos[1]), P(x, y))
        s.wire(P(x, y), P(232, y))
        s.label(P(232, y), LEFT)
    # Test pads, in the order of the table in section 16.3.
    for k in range(1, 31):
        x, y = 104 + 46 * ((k - 1) // 10), 150 + 6 * ((k - 1) % 10)
        s.place(f"TP{k}", P(x, y), rot=90, show_value=False, ref_at=P(x - 12, y))
    for i, (ref, hole) in enumerate(TABLE["mechanical"].items()):  # the mounting holes, from parts.toml
        s.extra(ref, hole["symbol"], P(246 + 18 * i, 180), value=hole["value"], footprint=hole["footprint"])
    s.note("QWIIC (section 16.2). F2 holds 0.125 A at 20 C and 0.08 A at 60 C: the port supplies 80 mA.\n"
           "R23, R24 4.7k. U9 clamps the bus lines; place it at J6. Pin order 1 GND, 2 3V3, 3 SDA, 4 SCL.",
           P(168, 104))
    s.note("TEST PADS: 1.5 mm, top side. Ground around TP8.\nNo pad may sit in the fuse well.", P(24, 140))
    s.note("H1 to H4: 3.4 mm holes for M3,\nat the positions of holes A to D.", P(240, 194))


# --------------------------------------------------------------------------- root sheet and the design
# Which sheet drives each net that crosses sheets: its port is an output there and an input elsewhere.
# A net that is not listed is passive (a bus, a supply path, a field terminal).
DRIVERS = {
    "power": "AC_L VIN_MON PGOOD",
    "mcu": "KA TFT_CS TFT_MOSI TFT_SCLK TFT_DC TFT_RST TFT_BL SENS_PWR_EN U0TXD_MCU",
    "failsafe": "KA_OK KA_N2 OC_TRIP_N FAULT DRV_EN DRV_EN_RB OC_FAULT_N",
    "isense": "ADC_LO ADC_HI ISENSE_LO ISENSE_HI VH VL VB AC_ZC",
    "expander": "ZD_Z1_N ZD_Z2_N ZD_Z3_N ZD_Z4_N ZD_Z5_N ZD_Z6_N ZD_Z7_N ZD_Z8_N ZD_MV_N COM RLY_B_RB RLY_B "
                "EXP_INT_N",
    "sensors": "SENS_RAIN SENS_FLOW SENS_AUX SENS_FLT_N SENS_5V",
    "rtc": "RTC_INT_N",
    "display": "ENC_A ENC_B ENC_SW BTN_BACK BTN_HOME",
    "connectors": "AC_L_IN AC_N T_RAIN T_FLOW T_AUX U0RXD",
}
# Passive ports drawn on the right of their sheet symbol (the default is the left).
RIGHT_SIDE = {
    "channel": "OUT_Z1 ISNS",
    "mcu": "I2C0_SDA I2C0_SCL QW_SDA QW_SCL",
    "usb": "USB_DP_MCU USB_DM_MCU",
    "expander": "P06 P07 P16 P17",
}
SHEET_LIST = [
    # sheet, instance name on the root sheet, title, paper, drawing function
    ("power", "Power", "24 VAC input and logic supply", "A3", power),
    ("usb", "USB", "USB-C and power OR-ing", "A4", usb),
    ("mcu", "MCU", "MCU: ESP32-S3 module, reset, boot, status", "A3", mcu),
    ("failsafe", "Failsafe", "Fail-safe gate and over-current latch", "A3", failsafe),
    ("isense", "CurrentSense", "Current sense and zero-cross", "A3", isense),
    ("expander", "Expander", "I/O expander and valve-common relay", "A3", expander),
    ("sensors", "Sensors", "Sensor inputs and sensor supply", "A3", sensors),
    ("rtc", "RTC", "Real-time clock", "A4", rtc),
    ("display", "Display", "Display and controls", "A3", display),
    ("connectors", "Connectors", "Connectors, test pads and mounting holes", "A3", connectors),
]
COLUMNS = [(26, ["connectors"]), (102, ["power", "usb", "sensors", "rtc", "display"]),
           (178, ["mcu", "failsafe", "isense"]), (256, ["expander"])]


def root(cap):
    r = cap.d.root
    r.paper = "A3"
    names = {sheet: inst for sheet, inst, *_ in SHEET_LIST}
    width, stub = P(40, 0)[0], P(3, 0)[0]
    for x, sheets in COLUMNS:
        y = 28
        for sheet in sheets:
            inst = cap.instantiate(sheet, names[sheet], P(x, y), width=width, stub=stub)
            y += round(inst.size[1] / U) + 10
    # The nine output channels, under the expander that drives them.
    for name in INSTANCES:
        inst = cap.instantiate("channel", name, P(256, y), width=width, stub=stub)
        y += round(inst.size[1] / U) + 6
    r.text("ZoneFlow rev A: block diagram", P(22, 20), size=3.0, bold=True)
    r.note("Each box is one schematic sheet. A pin name is the net name: pins with the same label are joined.\n"
           "Supply rails are not drawn here. They reach every sheet through power symbols:\n"
           "  GND      circuit ground. One transformer leg (AC_N behind F1), USB ground and sensor ground.\n"
           "  VIN_DC   rectified 24 VAC, 26 to 31 V, 44 V at 32 VAC with no load\n"
           "  V_BUCK   5.28 V from the LM5164, present only with 24 VAC. Also feeds the relay coil and the sensor supply.\n"
           "  V5       V_BUCK or VBUS through a Schottky diode each: about 4.95 V from 24 VAC, 4.1 to 4.9 V from USB\n"
           "  3V3      logic rail, 3.20 to 3.40 V\n"
           "  V_DRV    3V3 behind Q3: the opto LED supply, on only while the fail-safe circuit allows drive\n"
           "  VBUS     USB-C VBUS\n"
           "\n"
           "The nine output channels Z1 to Z8 and MV are nine instances of one sheet, channel.kicad_sch.\n"
           "References there are 101 to 111 style: the first digit is the channel (9 = master valve), so R301 is\n"
           "the opto LED resistor of zone 3 and Q901 the master valve triac.\n"
           "\n"
           "BUILD VARIANTS. The base design is the 8-zone build. KiCad variant \"4-zone\" does not fit\n"
           "instances Z5 to Z8 (44 parts), J3 and R28. C52 and C53 are footprints only on both builds.\n"
           "\n"
           "HOW A VALVE TURNS ON. Firmware toggles KA. Detector A (Failsafe sheet) raises DRV_EN, which releases\n"
           "the expander from reset and switches V_DRV on. Detector B (Expander sheet) turns on Q2. Firmware then\n"
           "sets RLY_ON (relay K1 feeds COM) and pulls a ZD_x_N pin low: the opto LED of that channel conducts and\n"
           "its triac fires from its own T2. Valve current returns through ISNS and the shunt on the CurrentSense\n"
           "sheet. When KA stops, both detectors let go within about a second, each on its own.\n"
           "\n"
           "Connectivity is taken from hardware/netlist.yaml. The build fails unless the netlist KiCad extracts\n"
           "from these sheets equals it pin for pin. This design has never been built.",
           P(22, 152), size=1.5)


def build() -> Design:
    symbols.write(BOARD)
    d = Design("zoneflow-a", "ZoneFlow irrigation controller, revision A", rev="A", date="2026-11-10",
               company="ZoneFlow (open hardware, CERN-OHL-P-2.0)",
               symbol_dirs=(str(BOARD / "lib"),), footprint_dirs=(str(BOARD / "lib"),))
    d.comments[2] = "Paper design. Never built, nothing measured."
    d.comments[3] = "Generated from design/circuit.py and hardware/netlist.yaml. Do not edit by hand."
    comps = yaml.safe_load((HW / "netlist.yaml").read_text())["components"]
    cap = Capture(d, comps, parity.load_parts(HERE / "parts.toml"), parity.load_bom(HW / "bom-preliminary.csv"),
                  power=POWER)
    d.capture = cap
    for sheet, nets in DRIVERS.items():
        for net in nets.split():
            cap.drivers[net] = sheet
    for sheet, nets in RIGHT_SIDE.items():
        for net in nets.split():
            cap.sides[(sheet, net)] = "right"
    for name, spec in SHEETS.items():
        cap.assign(name, refs(spec))
    cap.template("channel", INSTANCES, CHANNEL.split())
    for sheet, _inst, title, paper, fn in SHEET_LIST:
        cap.sheet(sheet, title, paper=paper, build=fn)
    cap.sheet("channel", "24 VAC output channel", paper="A4", build=channel)
    root(cap)
    cap.finish()
    builds = committed.builds(TABLE)
    cap.base_fitted = builds[committed.BASE]
    fitted = builds["4-zone"]
    d.variant("4-zone", dnp={c["ref"] for c in comps if c["variant"] in cap.base_fitted - fitted},
              fitted=fitted, description="4-zone build: channels 5 to 8, J3 and R28 not fitted")
    return d
