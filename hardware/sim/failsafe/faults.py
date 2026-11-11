"""The single-fault list, written from the netlist and the package pinouts.

Every part in the scope gets each pin opened and each pair of pins bridged
that the package makes possible. ICs also get each output stuck. Then come
the faults of the board itself: a net stuck at a rail, a leak onto a
reservoir, a bridge at the module pins, a supply at the wrong voltage.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

from model import SCOPE, Prim, a_readback, pin_id, scope_refs

# Pins next to each other on one side of the package.
SOIC14 = [(i, i + 1) for i in range(1, 7)] + [(i, i + 1) for i in range(8, 14)]
TSSOP24 = [(i, i + 1) for i in range(1, 12)] + [(i, i + 1) for i in range(13, 24)]
MODULE_SIDES = (range(1, 15), range(15, 27), range(27, 41))

# Inputs with no DC path of their own: an open pin holds its last level, or
# leaks to one rail or the other. Each is run three ways.
HIGH_Z = {
    "U7": ["1", "2", "4", "5", "9", "10", "12", "13"],
    "U5": ["3"],
    "Q1": ["G"], "Q2": ["G"], "Q3": ["G"],
}
IC_OUTPUT_FAULTS = {
    "U6": [("out1_low", "output 1 (KA_OK) shorted low"), ("out1_off", "output 1 (KA_OK) never pulls low"),
           ("out2_low", "output 2 (OC_TRIP_N) shorted low"), ("out2_off", "output 2 never pulls low"),
           ("out3_low", "output 3 (OC_TRIP_N) shorted low"), ("out3_off", "output 3 never pulls low"),
           ("out4_low", "output 4 (AC_ZC) shorted low"), ("out4_off", "output 4 never pulls low")],
    "U7": [(f"y{k}_{lvl}", f"output {k} ({net}) stuck {lvl}") for k, net in ((1, "FAULT"), (2, "FAULT_N"), (3, "DRV_EN_N"), (4, "DRV_EN")) for lvl in ("high", "low")],
    "U5": [("reset_ignored", "RESET input ignored, bits kept"), ("z1_low", "pin P00 (Z1) stuck low"), ("z1_high", "pin P00 (Z1) stuck high"),
           ("mv_low", "pin P04 (MV) stuck low"), ("rly_high", "pin P05 (RLY_ON) stuck high"), ("rly_low", "pin P05 (RLY_ON) stuck low")],
}


@dataclass
class Fault:
    fid: str
    group: str
    part: str
    text: str
    prims: list[Prim] = field(default_factory=list)
    ac: bool = False                  # needs the 24 VAC source
    welded: bool = False              # relay contact closed whatever the coil does
    shorted_output: bool = False      # triac, TVS or opto-triac failed short
    ext: dict[str, str] = field(default_factory=dict)
    usb: float = 0.0                  # V5 held up by USB at this voltage
    by_inspection: str = ""           # not simulated; the reason is the result


# What a simulated row cannot say by itself. Shown beside the result.
REMARKS = {
    "U6.12.open": "The model finds ground through the input diodes, about 0.9 V up, and keeps working. A real LM2901 without ground is not defined: its low level may not read as low at U7, and then detector A is stuck on. Detector B covers that.",
    "U7.7.open": "As simulated. A real 74HC132 without ground is not defined.",
    "U7.14.open": "The model powers U7 through the clamp diodes of its inputs, from the pull-ups R36 and R53, at about 2.7 V, and it keeps working. A real 74HC132 on such a supply is a bench question.",
    "U5.12.open": "As simulated. A real TCA9539 without ground is not defined.",
    "U7.2-3.bridge": "A gate output tied to its own input: the model oscillates and the runs did not finish. Judged by hand in review A2, section 3.4: drive cannot stay enabled.",
    "U7.5-6.bridge": "As pins 2 and 3: the same two nets.",
    "U7.8-9.bridge": "A gate output tied to its own input: the model oscillates and the runs did not finish. Judged by hand in review A2, section 3.4: drive cannot stay enabled.",
    "R55.short": "Puts 24 VAC on 3V3 through the clamp diode D9. Every logic part is then outside its ratings and the row means nothing. A 1206 resistor rated 200 V does not fail short; a solder bridge across it would do this.",
    "rail.3V3.high": "Both detectors let go at 4.95 V, if the keep-alive stops. The processor is outside its ratings and whether it stops is not known: finding M5.",
    "R93.open": "The read-back loses its pull-up and reads low for good: a false alarm on the safe side. Q2 itself works.",
    "Q1.S-D.short": "Q1 conducts whatever its gate does. K1 then follows Q2 alone.",
    "D5.open": "Nothing in the model. On the board Q1 or Q2 would avalanche at every release, 0.16 mJ by the design document's estimate, and one of them may fail short in time. That fault has its own row.",
    "net.RLY_A~3V3": "The coil then sees V_BUCK less 3V3, about 2 V, whatever Q1 and Q2 do. That cannot pull the relay in and may hold it: Omron gives 40 % of the rated voltage as a holding figure. With Q1 and Q2 on, the bridge also shorts 3V3 to ground, and the supervisor resets the processor.",
    "net.RLY_B~3V3": "With Q2 on this shorts 3V3 to ground, and the supervisor resets the processor.",
    "K1.coil.short": "Q1 and Q2 then short V_BUCK whenever both conduct. The supply collapses, the supervisor resets the processor and the expander loses its bits. K1 cannot close with no voltage across its coil.",
    "D5.short": "The same as a shorted coil.",
    "R93.short": "V_BUCK is then on the drain of Q2 with no coil in between, and Q2 shorts the supply as soon as it conducts.",
    "U7.9-10.bridge": "The gate output and the comparator output fight over the node, and which one wins at power-up depends on the two parts. Either way detector A stays on once it has been enabled.",
}


def group_of(ref: str) -> str:
    return next(g for g, refs in SCOPE.items() if ref in refs)


def pins_of(comps: dict, ref: str) -> dict[str, str]:
    return {pin_id(k): net for k, net in comps[ref]["pins"].items() if net != "NC"}


def ac_nets(*nets: str) -> bool:
    return any(n in ("AC_ZC", "ZC_DIV", "ZC_CLAMP", "AC_L") for n in nets)


def open_faults(ref: str, pin: str, label: str, group: str, net: str) -> list[Fault]:
    base = f"{ref}.{pin}.open"
    if pin in HIGH_Z.get(ref, []):
        return [
            Fault(base + ".hold", group, ref, f"{label} open, holds its last level", [Prim("open", (ref, pin))], ac_nets(net)),
            Fault(base + ".hi", group, ref, f"{label} open, leaks high", [Prim("open", (ref, pin)), Prim("float", (ref, pin, "hi"))], ac_nets(net)),
            Fault(base + ".lo", group, ref, f"{label} open, leaks low", [Prim("open", (ref, pin)), Prim("float", (ref, pin, "lo"))], ac_nets(net)),
        ]
    return [Fault(base, group, ref, f"{label} open", [Prim("open", (ref, pin))], ac_nets(net))]


def component_faults(comps: dict) -> list[Fault]:
    out: list[Fault] = []
    for ref in scope_refs():
        if ref == "U4":
            continue
        c, group, pins = comps[ref], group_of(ref), pins_of(comps, ref)
        value = c["value"]
        keys = {pin_id(k): k for k in c["pins"]}

        def label(pin: str) -> str:
            return f"{ref} pin {keys[pin]} ({pins[pin]})"

        if re.fullmatch(r"[RC]\d+", ref) or value.startswith(("1N4148", "LED")):
            a, b = list(pins)[:2]
            out.append(Fault(f"{ref}.open", group, ref, f"{ref} open", [Prim("open", (ref, a))], ac_nets(*pins.values())))
            out.append(Fault(f"{ref}.short", group, ref, f"{ref} short", [Prim("short", (ref, a, b))], ac_nets(*pins.values())))
        elif ref.startswith("RN"):
            out.append(Fault(f"{ref}.1.open", group, ref, f"{ref} element 1-8 ({pins['1']}) open", [Prim("open", (ref, "1"))]))
            out.append(Fault(f"{ref}.1.short", group, ref, f"{ref} element 1-8 ({pins['1']}) short", [Prim("short", (ref, "1", "8"))]))
            out.append(Fault(f"{ref}.others", group, ref, f"{ref} elements 2 to 4 open or short, pins 1-2, 2-3, 3-4 bridged", by_inspection=(
                "The other three elements pull the pins of Z2 to Z4 up in the same way. A bridge between two valve pins makes two "
                "outputs follow one bit; it adds no path that keeps a pin low after the expander is reset.")))
        elif value.startswith("MOC3023"):
            out.append(Fault(f"{ref}.led.open", group, ref, f"{ref} LED open", [Prim("open", (ref, "1"))]))
            out.append(Fault(f"{ref}.led.short", group, ref, f"{ref} LED short", [Prim("short", (ref, "1", "2"))]))
        elif value.startswith("BAT54S") or value.startswith(("AO3400A", "AO3401A")):
            names = list(pins)
            for pin in names:
                out += open_faults(ref, pin, label(pin), group, pins[pin])
            for i, a in enumerate(names):
                for b in names[i + 1:]:
                    out.append(Fault(f"{ref}.{a}-{b}.short", group, ref, f"{ref} pins {keys[a]} and {keys[b]} bridged ({pins[a]} to {pins[b]})",
                                     [Prim("short", (ref, a, b))], ac_nets(pins[a], pins[b])))
        elif value.startswith(("LM2901", "74HC132", "TCA9539")):
            modelled = set(pins) if ref != "U5" else {"3", "4", "8", "9", "12", "24"}
            for pin in sorted(modelled, key=int):
                out += open_faults(ref, pin, label(pin), group, pins[pin])
            pairs = SOIC14 if ref != "U5" else TSSOP24
            skipped = []
            for a, b in pairs:
                a, b = str(a), str(b)
                if pins.get(a) == pins.get(b):
                    continue
                if ref == "U5" and not ({a, b} <= modelled | {"2"}):
                    skipped.append(f"{keys[a]}-{keys[b]}")
                    continue
                out.append(Fault(f"{ref}.{a}-{b}.bridge", group, ref, f"{ref} pins {keys[a]} and {keys[b]} bridged ({pins[a]} to {pins[b]})",
                                 [Prim("bridge", (pins[a], pins[b], 0.01))], ac_nets(pins[a], pins[b])))
            if skipped:
                out.append(Fault(f"{ref}.other-bridges", group, ref, f"{ref} other adjacent pins bridged: " + ", ".join(skipped), by_inspection=(
                    "Not simulated: these pins are valve bits of other channels, spare pins, the address straps and the I2C bus. Two valve pins "
                    "bridged make two outputs follow one bit. A stuck bus stops the firmware from writing, and it must then stop the keep-alive. "
                    "None of these pins can hold RESET high or drive a valve pin low while the device is in reset.")))
            for what, text in IC_OUTPUT_FAULTS[ref]:
                out.append(Fault(f"{ref}.{what}", group, ref, f"{ref} {text}", [Prim("stuck", (ref, what))], ac=ref == "U6" and "4" in what))
        elif value.startswith("G5NB"):
            out.append(Fault("K1.coil.open", group, ref, "K1 coil open", [Prim("open", (ref, "coil+"))]))
            out.append(Fault("K1.coil.short", group, ref, "K1 coil short", [Prim("short", (ref, "coil+", "coil-"))]))
            out.append(Fault("K1.welded", group, ref, "K1 contact welded, or JP1 bridged", welded=True))
        else:
            raise ValueError(f"no fault list for {ref}")
    out.append(Fault("output.short", "output devices", "Q101 D101 U101 C101", "triac, TVS, opto-triac output or snubber capacitor shorted", shorted_output=True))
    return out


# Sources that stand for nets outside the scope, for bridge faults.
EXT_SOURCES = {
    # Octal PSRAM data strobe on GPIO37 (ESP32-S3 datasheet v2.2, table 2-14).
    # It toggles whenever the CPU reads the PSRAM. 500 Hz stands in for tens
    # of MHz, to keep the run short: both detectors are rectifiers, and above
    # a few tens of hertz the result does not depend on the rate (the RATE
    # lines of keepalive_faults.cir show that up to 1 MHz).
    "x_dqs": "VXDQS {node}_s 0 PULSE(0 3.3 1m 5u 5u 1m 2m)\nRXDQS {node}_s {node} 30",
    # Backlight PWM on GPIO21, LEDC. 1 to 20 kHz on the board, 500 Hz here.
    "x_bl": "VXBL {node}_s 0 PULSE(0 3.3 1m 5u 5u 1m 2m)\nRXBL {node}_s {node} 30",
    # A button or encoder net at the module pin: 3.3 kohm pull-up, 10 kohm in
    # series and 100 nF (for example R68, R73, C43). A closed contact pulls
    # the pin low through the 10 kohm.
    "x_btn": "RXBTN n3v3 {node} 13.3k\nCXBTN {node} 0 100n",
    # UART0 TXD: the ROM prints its boot log at 115200 baud after every
    # reset. Idle high; 500 Hz here stands in for the bit stream of a log
    # that repeats in a reset loop.
    "x_txd": "VXTXD {node}_s 0 PULSE(3.3 0 1m 5u 5u 1m 2m)\nRXTXD {node}_s {node} 30",
}


def board_faults(comps: dict) -> list[Fault]:
    out: list[Fault] = []
    u4 = pins_of(comps, "U4")
    all_u4 = {pin_id(k): net for k, net in comps["U4"]["pins"].items()}
    by_net = {net: pin for pin, net in u4.items()}
    g = "module"
    ka = by_net["KA"]
    out += [Fault(f"U4.{ka}.open", g, "U4", f"U4 pin {ka} (KA) not connected to the board", [Prim("open", ("U4", ka))])]
    for net, name in ((a_readback(comps), "gate-drive read-back"), ("RLY_B_RB", "relay read-back"), ("OC_FAULT_N", "over-current flag")):
        for way in ("hi", "lo"):
            out.append(Fault(f"U4.{by_net[net]}.drive.{way}", g, "U4", f"GPIO of the {name} driven {'high' if way == 'hi' else 'low'} as an output by mistake",
                             [Prim("drive", ("U4", by_net[net], way))]))
        out.append(Fault(f"U4.{by_net[net]}.open", g, "U4", f"U4 pin {by_net[net]} ({net}) not connected to the board", [Prim("open", ("U4", by_net[net]))]))
    # bridges from the keep-alive pin to its neighbours on the module edge
    side = next(s for s in MODULE_SIDES if int(ka) in s)
    for other in (int(ka) - 1, int(ka) + 1):
        if other not in side:
            continue
        net = all_u4[str(other)]
        key = next(k for k in comps["U4"]["pins"] if pin_id(k) == str(other))
        if net == "NC" and key.split()[1] in ("IO35", "IO36", "IO37"):
            for ohms in (0.01, 1e3, 4.7e3, 10e3, 22e3):
                out.append(Fault(f"KA~{key.split()[1]}.{ohms:g}", g, "U4", f"KA bridged to module pin {key} (octal PSRAM line, toggling), {ohms:g} ohm",
                                 [Prim("bridge", ("KA", "x_dqs", ohms))], ext={"x_dqs": EXT_SOURCES["x_dqs"]}))
        elif net.startswith(("BTN_", "ENC_")):
            out.append(Fault(f"KA~{net}", g, "U4", f"KA bridged to module pin {key} ({net}, contact open)", [Prim("bridge", ("KA", "x_btn", 0.01))], ext={"x_btn": EXT_SOURCES["x_btn"]}))
            out.append(Fault(f"KA~{net}.20Hz", g, "U4", f"KA bridged to module pin {key} ({net}, contact worked at 20 Hz)", [Prim("bridge", ("KA", "x_btn", 0.01))],
                             ext={"x_btn": EXT_SOURCES["x_btn"] + "\nVXBTNP {node}_p 0 PULSE(0 1 1m 5u 5u 25m 50m)\nSXBTN {node} {node}_r {node}_p 0 FSWC\nRXBTNR {node}_r 0 10k"}))
        elif net in ("OC_FAULT_N", "AC_ZC", "DRV_EN_RB", "V_DRV_RB"):
            out.append(Fault(f"KA~{net}", g, "U4", f"KA bridged to module pin {key} ({net})", [Prim("bridge", ("KA", net, 0.01))], ac=net == "AC_ZC"))
        else:
            out.append(Fault(f"KA~{net}", g, "U4", f"KA bridged to module pin {key} ({net})", by_inspection="Neighbour net outside the model; see the review text."))
    # bridges from KA to nets that toggle with no control loop behind them
    g = "board"
    out.append(Fault("KA~AC_ZC.net", g, "KA", "KA bridged to AC_ZC anywhere on the board", [Prim("bridge", ("KA", "AC_ZC", 0.01))], ac=True))
    for ohms in (0.01, 1e3):
        out.append(Fault(f"KA~TFT_BL.{ohms:g}", g, "KA", f"KA bridged to TFT_BL (backlight PWM, keeps running), {ohms:g} ohm",
                         [Prim("bridge", ("KA", "x_bl", ohms))], ext={"x_bl": EXT_SOURCES["x_bl"]}))
    out.append(Fault("KA~U0TXD", g, "KA", "KA bridged to U0TXD (ROM boot log, prints in a reset loop)", [Prim("bridge", ("KA", "x_txd", 0.01))], ext={"x_txd": EXT_SOURCES["x_txd"]}))
    # every net of the scope stuck at ground or at the logic rail
    nets = sorted({net for ref in scope_refs() if ref != "U4" for net in pins_of(comps, ref).values()} - {"GND", "3V3", "V5", "V_BUCK", "AC_L", "COM"})
    modelled = set(nets) - {"ZD_Z2_N", "ZD_Z3_N", "ZD_Z4_N", "EXP_INT_N", "I2C0_SCL", "I2C0_SDA", "G_Z1", "OGT_Z1", "G_MV", "OGT_MV", "P06", "P07"}
    modelled -= {n for n in nets if re.fullmatch(r"(ZD_Z[5-8]_N|P1\d|VARIANT_N|SENS_FLT_N)", n)}
    for net in sorted(modelled):
        for rail in ("GND", "3V3"):
            out.append(Fault(f"net.{net}~{rail}", g, net, f"net {net} shorted to {rail}", [Prim("bridge", (net, rail, 0.01))], ac_nets(net)))
    # leakage onto the nodes that have no DC path from a rail by design
    for net, rail, ohms in (("KA_N2", "3V3", 47e3), ("KA_N2", "3V3", 100e3), ("KA_N2", "3V3", 220e3), ("KA_INP", "3V3", 100e3), ("KA_INP", "3V3", 330e3),
                            ("RLY_G2", "3V3", 47e3), ("RLY_G2", "3V3", 100e3), ("RLY_G2", "3V3", 330e3), ("RLY_G2", "V_BUCK", 100e3), ("RLY_G2", "V_BUCK", 330e3),
                            ("RLY_G2", "V_BUCK", 1e6), ("KB_N1", "3V3", 100e3), ("KA_N1", "3V3", 100e3)):
        out.append(Fault(f"leak.{net}~{rail}.{ohms:g}", g, net, f"leak of {ohms / 1e3:g} kohm from {rail} to {net}", [Prim("bridge", (net, rail, ohms))]))
    # supplies
    g = "supplies"
    out.append(Fault("rail.3V3.high", g, "3V3", "U3 shorted input to output: 3V3 at V5, 4.95 V. The processor is taken to keep running until the stop", [Prim("bridge", ("3V3", "V5", 0.05))]))
    out.append(Fault("rail.3V3.low", g, "3V3", "3V3 falls to 2.0 V: the supervisor holds the processor in reset", [Prim("rail", ("3V3", 2.0))]))
    out.append(Fault("rail.3V3.off", g, "3V3", "3V3 falls to 0 V", [Prim("rail", ("3V3", 0.0))]))
    out.append(Fault("rail.V_BUCK.low", g, "V_BUCK", "V_BUCK falls to 3.0 V, V5 and 3V3 with it", [Prim("rail", ("V_BUCK", 3.0))]))
    out.append(Fault("rail.V_BUCK.off", g, "V_BUCK", "V_BUCK falls to 0 V on 24 VAC alone: every rail goes", [Prim("rail", ("V_BUCK", 0.0))]))
    out.append(Fault("rail.V_BUCK.off.usb", g, "V_BUCK", "V_BUCK falls to 0 V with USB attached: the logic stays up (F5 has opened, section 4.8)", [Prim("rail", ("V_BUCK", 0.0))], usb=4.6))
    out.append(Fault("rail.V_BUCK.clamp", g, "V_BUCK", "V_BUCK at 10.3 V, V5 at 10 V: the highest the clamp D25 allows", [Prim("rail", ("V_BUCK", 10.3))]))
    return out


def all_faults(comps: dict) -> list[Fault]:
    faults = component_faults(comps) + board_faults(comps)
    seen: set[str] = set()
    for f in faults:
        if f.fid in seen:
            raise ValueError(f"fault id {f.fid} used twice")
        seen.add(f.fid)
    return faults
