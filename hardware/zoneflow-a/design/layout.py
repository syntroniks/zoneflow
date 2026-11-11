# SPDX-License-Identifier: CERN-OHL-P-2.0
"""Writes design/board.toml for ZoneFlow rev A: placement, hand-routed copper, zones, silkscreen.

Run by ``make`` before anything reads board.toml. The board description of this board is not
typed by hand, because most of it repeats or is computed: nine output columns, nine LED cells,
the fan-out lanes of the valve outputs, eighteen gate lines, a bus with eighteen stubs. This
script is the source; ``board.toml`` is what it writes and what the board builder reads, and is
committed so that a reviewer can read the numbers without running anything.

    python design/layout.py <board directory>

Board coordinates are those of the mechanical contract, hardware/mechanical.yaml: origin at the
bottom-left corner seen from the component side, x right, y up. The reasons for every choice here
are in docs/design/hardware-layout.md.

Only the standard library is used.
"""

import math
import sys
from pathlib import Path

HEAD = '''# Board description for ZoneFlow rev A. Lengths in millimetres.
#
# WRITTEN BY design/layout.py. Do not edit: change layout.py and run make.
#
# Board coordinates are those of the mechanical contract, hardware/mechanical.yaml:
# origin at the bottom-left corner of the outline seen from the component side,
# x to the right along the terminal edge, y up towards the antenna. Rotation is
# counter-clockwise seen from the top side.
#
# The reasons for the placement, the widths and the routing are in
# docs/design/hardware-layout.md. No board has been made.

# Net classes, in order of priority. circuit.py assigns nets to them (NETCLASS).
[netclass.Default]
track_width = 0.2
clearance = 0.2
via_diameter = 0.6
via_drill = 0.3

# 24 VAC paths that carry valve or supply current. Trunks are wider; see [[track]].
[netclass.AC24]
track_width = 1.0
clearance = 0.5
via_diameter = 1.0
via_drill = 0.5

# The shunt node. It carries the valve current back and is within 3 V of ground, so it may come
# closer to ground than a 24 VAC net may. Against a 24 VAC net the larger clearance applies.
[netclass.Shunt]
track_width = 1.0
clearance = 0.25
via_diameter = 1.0
via_drill = 0.5

# At 24 VAC potential, milliamps: gates, opto outputs, snubber mid-points.
[netclass.AC24S]
track_width = 0.3
clearance = 0.5
via_diameter = 0.6
via_drill = 0.3

# Rectified input, up to 45 V, and the buck converter's switch node.
[netclass.HVDC]
track_width = 0.5
clearance = 0.4
via_diameter = 0.8
via_drill = 0.4

# Sensor terminals and the sensor supply output.
[netclass.Field]
track_width = 0.4
clearance = 0.5
via_diameter = 0.8
via_drill = 0.4

[netclass.Power]
track_width = 0.4
clearance = 0.2
via_diameter = 0.8
via_drill = 0.4

[netclass.USB]
track_width = 0.25
clearance = 0.2
via_diameter = 0.6
via_drill = 0.3

[board]
origin = [90.0, 50.0]    # where the outline's top-left corner sits on the KiCad page
size = [114.0, 110.0]    # mechanical contract, board.outline
y_up = true
corner_radius = 1.0      # the contract allows up to 1.5
layers = 4
thickness = 1.6
'''

place: dict[str, tuple] = {}
notes: list[tuple[str, list[str]]] = []   # (comment, refs) in output order
_cur: list[str] = []


def block(title):
    """Start a group of placements with a comment that goes into board.toml above them."""
    global _cur
    _cur = []
    notes.append((title, _cur))


def at(ref, x, y, rot=0, side="F"):
    if ref in place:
        raise SystemExit(f"{ref} placed twice")
    place[ref] = (round(x, 3), round(y, 3), rot, side)
    _cur.append(ref)


def row(refs, x, y, dx=3.1, rot=0, dy=0.0):
    for i, r in enumerate(refs.split()):
        at(r, x + i * dx, y + i * dy, rot)


def col(refs, x, y, dy=-1.6, rot=0):
    for i, r in enumerate(refs.split()):
        at(r, x, y + i * dy, rot)


# =============================================================================================
# Placement
# =============================================================================================
# ---- fixed by the mechanical contract ------------------------------------------------------
block("Fixed by hardware/mechanical.yaml, revision 2. The mechanical check compares these.")
at("J5", 8.0, 2.675, 0)
at("J1", 17.805, 5.9, 0)
at("J2", 30.225, 5.9, 0)
at("J3", 58.295, 5.9, 0)
at("J4", 78.335, 5.9, 0)
at("J6", 109.25, 3.08, 0)      # 1.0 mm further in than contract revision 2: see hardware-layout.md
at("F4", 15.0, 12.6, 180)      # pad 1, AC_L_IN, towards J1 pin 1
at("F1", 27.0, 12.6, 0)        # pad 1, AC_N, towards J1 pin 2
at("D6", 12.0, 42.0, 0)
at("SW4", 22.0, 61.5, 0)
at("SW5", 48.0, 61.5, 0)
at("SW3", 85.0, 88.0, 0)
at("J7", 60.44, 88.43, 0)
at("U4", 103.0, 103.25, 0)
at("C1", 36.0, 30.0, 180, "B")    # can towards -x; pad 1 (+) at y = 32.5
at("H1", 5.5, 105.0, 0)
at("H2", 75.0, 106.0, 0)
at("H3", 4.5, 12.0, 0)
at("H4", 109.5, 12.0, 0)
block("Free by the contract, positions chosen here.\nBT1: 1.3 mm above the reserved position, to clear the opto row, turned so that + is at the left.\nK1: under the left edge, long axis along y, contacts at the bottom, coil at the top.")
at("BT1", 95.0, 65.3, 180)
at("K1", 2.45, 37.0, 270, "B")

# ---- left edge: USB, relay pads, zero-cross clamp. Low parts only near the relay pins --------
block("USB-C: CC pull-downs beside the receptacle, VBUS capacitor and shield resistor on its right.")
at("R15", 1.81, 2.5, 90)
at("R16", 1.81, 5.6, 90)
at("C13", 14.15, 2.6, 90)
at("R17", 14.15, 5.7, 90)
at("U2", 3.6, 17.7, 0)
block("Around the relay pins (K1 is on the bottom side): solder jumper across the contacts,\ncoil diode, and the zero-cross clamp fed from AC_L. Nothing here is taller than 1.5 mm.")
at("JP1", 7.15, 22.0, 270)
at("TP22", 3.2, 21.7, 0)
at("R55", 3.2, 27.7, 90)
at("R56", 6.2, 28.7, 90)
at("C34", 7.9, 28.7, 90)
at("D9", 3.0, 31.9, 0)
at("R88", 6.9, 31.9, 90)
at("D5", 4.8, 34.9, 0)

# ---- power entry, above the fuse well ---------------------------------------------------------
block("Power entry: surge clamp, rectifier, surge resistor. C1 is under this area, on the bottom.")
at("D1", 16.3, 21.8, 0)
at("D2", 25.2, 20.3, 180)
at("R1", 29.4, 24.5, 0)
block("Buck converter. From the right: C1 (bottom side), the fuse F5 on end, the input capacitors C2 and C3\nat pins 1 and 2, U1, the bootstrap capacitor at pins 7 and 8, the inductor. F5 is 3.5 mm from the\npads of C1, which are soldered from this side.")
at("F5", 31.1, 34.255, 270)    # pad 1, VIN_DC, at the top; pad 2, VIN_U1, level with the capacitor pads
at("C2", 27.55, 29.4, 270)
at("C3", 27.55, 34.2, 90)
at("U1", 21.5, 33.0, 180)
at("C4", 16.7, 31.73, 270)
at("L1", 12.4, 31.2, 180)
block("Buck output: C6 under the output pad of L1, C5 beside it, and the clamp D25 above the pad.")
at("C6", 12.4, 26.55, 0)
at("C5", 17.2, 26.55, 0)
at("D25", 12.9, 36.75, 0)      # cathode, V_BUCK, above the output pad of L1
block("Feedback and ripple network above U1, at the FB pin. R2 at the RON pin. Under-voltage divider\nR7, R8 beside F5: it stays on VIN_DC, before the fuse.")
at("R6", 18.1, 36.6, 0)
at("C8", 21.1, 36.6, 0)
at("R2", 24.1, 36.6, 180)
at("C7", 18.1, 38.15, 180)
at("R5", 21.1, 38.15, 180)
at("R4", 24.1, 38.15, 180)
at("R3", 24.1, 39.7, 0)
at("R8", 27.1, 37.3, 0)
at("R7", 27.1, 38.85, 180)
at("TP20", 23.9, 28.0, 0)
at("TP2", 20.9, 28.0, 0)
block("Between the fuse well and the channel columns: valve-common TVS, shunt, and the input monitor\ndivider around the pads of C1. Only low parts within 2.5 mm of those pads.")
at("D23", 36.7, 11.6, 90)
at("R37", 36.3, 19.25, 270)
at("TP17", 36.3, 25.1, 0)
at("TP1", 38.0, 30.0, 0)
at("R9", 37.0, 34.75, 0)
at("R10", 37.0, 36.35, 0)
at("C9", 37.0, 37.95, 0)

# ---- right edge ---------------------------------------------------------------------------------
block("Qwiic: ESD array and PTC at the connector.")
at("C17", 111.3, 7.3, 0)
at("F2", 105.65, 16.8, 0)
at("U9", 110.4, 17.35, 0)
block("Sensor terminals: TVS and series resistor of each input, sensor supply TVS, pull-ups.")
at("R81", 104.46, 21.55, 90)
at("D14", 109.31, 21.55, 0)
at("R75", 104.46, 26.2, 90)
at("D12", 109.31, 26.2, 0)
at("R78", 104.46, 30.85, 90)
at("D13", 109.31, 30.85, 0)
at("D18", 109.31, 35.5, 0)
at("R74", 109.1, 40.55, 0)
at("R77", 109.1, 42.95, 0)
at("R80", 109.1, 46.0, 0)

# ---- band above the LED row, left: USB power and 3.3 V regulator -------------------------------
block("5 V OR-ing diodes and the 3.3 V regulator.")
at("D4", 5.0, 46.9, 0)
at("D3", 5.0, 50.6, 0)
at("C10", 8.2, 54.4, 0)
at("U3", 14.5, 50.1, 0)
at("C11", 20.0, 48.4, 90)
at("C12", 20.2, 52.5, 90)
at("TP3", 3.0, 54.4, 0)
at("TP4", 20.0, 55.4, 0)

# ---- current-sense amplifier, between the two buttons ------------------------------------------
block("Current-sense amplifier. Channel A (low range) on the left, channel B on the right.")
at("U8", 35.0, 62.6, 90)
at("C25", 35.0, 57.65, 0)
col("R39 C26", 30.2, 66.0, -1.6)
at("R38", 30.2, 62.8, 180)     # shunt side towards the amplifier: Kelvin tracks end here
at("R40", 30.2, 61.2, 180)
col("R41 C27", 30.2, 59.6, -1.6)
col("R43 C28 R42 R44 R45 C29", 39.8, 66.0, -1.6)

# ---- sensor supply, right of the Home button ----------------------------------------------------
block("Sensor supply switch, its protection and the PTC.")
at("U11", 56.8, 65.5, 0)
at("D24", 62.6, 65.5, 0)
at("D19", 69.8, 65.5, 0)
at("R84", 75.9, 65.5, 0)
at("F3", 75.4, 61.5, 0)
at("D20", 68.7, 61.6, 0)
at("C48", 63.2, 61.6, 0)
col("C47 R83 R86", 56.2, 62.3, -1.6)
col("C49 R85", 59.4, 62.3, -1.6)
at("TP25", 60.2, 58.4, 0)
at("TP30", 72.0, 58.2, 0)

# ---- under the module ----------------------------------------------------------------------------
block("Keep-alive detector A and the comparators, under the module. KA comes down from module pin 31\nto the via between R29 and R33.")
at("R33", 111.4, 88.5, 0)
at("R29", 107.6, 88.5, 180)
at("C22", 104.3, 88.4, 180)
at("D7", 100.6, 87.9, 0)
at("C23", 96.8, 87.0, 180)
at("U6", 108.0, 82.3, 0)
row("R30 R91 R31", 95.3, 85.0, 3.2)
row("R36 R32 C24", 95.3, 83.4, 3.2)
row("TP9 R35 R34", 95.0, 81.4, 3.35)
row("R50 R49 R48", 95.3, 79.4, 3.2)
row("R52 C32 R51", 95.3, 77.8, 3.2)
row("R53 C33", 95.3, 76.2, 3.2)
row("C19 R57 R58", 105.2, 76.3, 3.1)
at("TP8", 101.6, 75.8, 0)

# ---- left of the module, top edge ---------------------------------------------------------------
block("Left of the module, at the top edge: USB series resistors, ADC filters, encoder switch filter,\nreset network, supervisor, bulk capacitor. Everything here is under 3.5 mm.")
at("R98", 89.2, 100.0, 90)
at("R97", 90.9, 100.0, 90)
at("C53", 89.2, 103.2, 90)
at("C52", 90.9, 103.2, 90)
at("R46", 87.5, 100.0, 90)
at("C30", 87.5, 103.2, 90)
at("R47", 85.8, 100.0, 90)
at("C31", 85.8, 103.2, 90)
at("R66", 84.1, 100.0, 90)
at("R71", 82.4, 100.0, 90)
at("C41", 82.4, 103.2, 90)
at("TP16", 80.3, 100.3, 0)
at("R18", 84.1, 103.2, 90)
at("C16", 84.1, 106.4, 90)
at("C14", 90.9, 106.3, 180)
at("C15", 87.5, 106.4, 90)
at("U12", 81.3, 106.95, 90)
at("C54", 80.6, 103.4, 90)

# ---- encoder ------------------------------------------------------------------------------------
block("Encoder pull-ups and filters, below the encoder.")
row("R64 R65", 81.4, 77.3, 7.2)
row("R69 C39", 81.4, 75.7, 3.1)
row("R70 C40", 88.6, 75.7, 3.1)
block("Debug header pads, between the display cable and the encoder.")
at("J8", 74.2, 93.5, 0)

# ---- real-time clock ----------------------------------------------------------------------------
block("Real-time clock, next to the positive contact of the cell holder.")
at("U10", 66.0, 72.0, 0)
at("Y1", 60.0, 73.1, 90)
at("C36", 60.6, 69.8, 0)
at("C35", 70.9, 71.0, 90)
at("D10", 72.4, 75.05, 0)
at("D11", 77.25, 75.05, 0)
at("R59", 78.0, 77.1, 0)
at("TP23", 62.3, 76.3, 0)

# ---- sensor input clamps ------------------------------------------------------------------------
block("Sensor input clamps and filters, above the display cable.")
row("D15 D16 D17", 60.2, 107.4, 4.0)
row("C44 C45 C46", 60.1, 104.1, 4.0, rot=90)
row("R76 R79 R82", 60.1, 101.05, 4.0, rot=90)

# ---- under the display --------------------------------------------------------------------------
block("I/O expander with its pull-up and pull-down networks.")
at("U5", 35.0, 72.5, 0)
at("RN1", 28.6, 74.5, 0)
at("RN2", 41.4, 74.5, 0)
at("RN3", 41.4, 70.3, 0)
at("R96", 28.4, 71.4, 0)
at("R27", 28.4, 69.8, 0)
at("R28", 28.4, 68.2, 0)
at("C18", 35.0, 77.6, 0)
row("TP21 TP26 TP27 TP28 TP29", 29.0, 80.4, 3.0)
block("Fail-safe gate, drive-enable switch and fault LED.")
at("U7", 20.0, 74.0, 0)
at("C20", 20.0, 79.6, 0)
at("Q3", 13.6, 76.5, 0)
col("R25 R26 C21 R92", 13.6, 73.6, -1.6)
col("R54 R87", 9.9, 76.9, -1.6)
at("D8", 9.9, 79.2, 0)
row("TP7 TP10 TP11 TP12", 12.0, 82.6, 3.0)
block("Keep-alive detector B and the relay driver. More than 10 mm from detector A.")
at("R89", 46.0, 78.6, 0)
at("C51", 49.6, 78.5, 0)
at("D22", 53.6, 78.0, 0)
at("C50", 53.5, 75.1, 0)
row("R14 R90", 46.6, 75.6, 3.2)
at("Q2", 53.6, 72.2, 0)
at("Q1", 48.6, 72.0, 0)
row("R11 R12", 44.6, 69.2, 3.2)
row("R93 R94 R95", 51.0, 69.2, 3.2)
at("TP24", 44.5, 72.4, 0)
block("Display connector: decoupling, reset pull-up and the backlight switch.")
at("R63", 54.9, 86.3, 0)
at("C38", 54.9, 87.9, 0)
at("C37", 54.9, 89.5, 0)
at("R60", 54.0, 92.6, 0)
at("Q4", 54.5, 96.0, 0)
row("R61 R62", 50.0, 93.6, 0, dy=1.6)
block("Reset and boot buttons, boot pull-up, serial resistor, I2C and Qwiic pull-ups.")
at("SW1", 48.0, 105.5, 0)
at("SW2", 48.0, 99.4, 0)
at("R19", 42.4, 99.4, 90)
at("R99", 42.4, 105.5, 90)
row("R21 R22", 38.2, 77.6, 3.2)
row("R23 R24", 108.3, 74.7, 3.1)
block("Status LED resistor, in the light-pipe strip beside the LED.")
at("R20", 15.1, 42.0, 180)
block("Button pull-ups and filters.")
col("R67 R72 C42", 13.9, 64.6, -1.6)
row("R68 R73 C43", 44.0, 66.1, 3.2)
block("Remaining test pads.")
at("TP5", 31.5, 20.5, 0)
at("TP6", 25.9, 80.4, 0)
col("TP13 TP14 TP15", 25.1, 69.4, 2.8)
row("TP18 TP19", 44.0, 81.6, 3.0)

# =============================================================================================
# Groups: the two halves of an output channel
# =============================================================================================
GROUPS = '''
# The nine output channels are nine instances of one sheet. Two groups place them, because the
# two halves of a channel are on different pitches.
#
# channel_power: PTC, TVS, triac and snubber, in a column 7.1 mm wide above the terminals.
# The origin of an instance is the centre line of its column at y = 0.
[group.channel_power]
sheet = "channel"

[group.channel_power.at]
"/MV/" = [42.85, 0]
"/Z1/" = [49.95, 0]
"/Z2/" = [57.05, 0]
"/Z3/" = [64.15, 0]
"/Z4/" = [71.25, 0]
"/Z5/" = [78.35, 0]
"/Z6/" = [85.45, 0]
"/Z7/" = [92.55, 0]
"/Z8/" = [99.65, 0]

[group.channel_power.parts]
D1 = [-1.25, 11.53, 270]   # TVS, pad 1 (OUT) up
C1 = [2.05, 9.58, 270]     # snubber capacitor, ISNS end down
R5 = [2.05, 13.06, 270]    # snubber resistor, T2 end up
F1 = [0, 19.93, 90]        # PTC, pad 1 (OUT) down
Q1 = [0, 29.44, 270]       # triac, tab down, leads up: A1 right, gate left
R4 = [1.5, 36.9, 0]        # gate to A1
R3 = [-1.8, 37.3, 0]       # T2 to the opto output

# channel_logic: the LED, its resistor, the opto LED resistor and the opto-triac. The origin of
# an instance is its LED, whose position the mechanical contract fixes (y = 42, pitch 9.5).
[group.channel_logic]
sheet = "channel"

[group.channel_logic.at]
"/MV/" = [26.0, 42.0]
"/Z1/" = [35.5, 42.0]
"/Z2/" = [45.0, 42.0]
"/Z3/" = [54.5, 42.0]
"/Z4/" = [64.0, 42.0]
"/Z5/" = [73.5, 42.0]
"/Z6/" = [83.0, 42.0]
"/Z7/" = [92.5, 42.0]
"/Z8/" = [102.0, 42.0]

[group.channel_logic.parts]
D2 = [0, 0, 0]             # LED, cathode left
R1 = [-3.1, 0, 0]          # opto LED resistor
R2 = [3.1, 0, 180]         # LED resistor
U1 = [0, 8.8, 90]          # opto-triac: LED side down, pin 1 left; pin 4 (gate) top right
'''

# =============================================================================================
# Copper
# =============================================================================================

COLS = {name: 42.85 + 7.1 * k for k, name in enumerate(["MV", "Z1", "Z2", "Z3", "Z4", "Z5", "Z6", "Z7", "Z8"])}
PINS = {"MV": "J2.3", "Z1": "J2.4", "Z2": "J2.5", "Z3": "J2.6", "Z4": "J2.7",
        "Z5": "J3.1", "Z6": "J3.2", "Z7": "J3.3", "Z8": "J3.4"}
PIN_X = {"MV": 37.845, "Z1": 41.655, "Z2": 45.465, "Z3": 49.275, "Z4": 53.085,
         "Z5": 58.295, "Z6": 62.105, "Z7": 65.915, "Z8": 69.725}

def toml_points(pts):
    out = []
    for p in pts:
        if isinstance(p, str):
            out.append(f'"{p}"')
        elif isinstance(p, dict):
            out.append("{via = [%g, %g]}" % tuple(round(v, 3) for v in p["via"]))
        else:
            out.append("[%g, %g]" % (round(p[0], 3), round(p[1], 3)))
    return "[" + ", ".join(out) + "]"

def track(pts, layer="F.Cu", width=None, via=None, table="track", comment=None, stay=False, via_to=None):
    s = []
    if comment:
        s += [f"# {c}" for c in comment.split("\n")]
    s.append(f"[[{table}]]")
    s.append(f'layer = "{layer}"')
    if width is not None:
        s.append(f"width = {width:g}")
    if via is not None:
        s.append(f"via = [{via[0]:g}, {via[1]:g}]")
    if stay:
        s.append("stay = true")
    if via_to:
        s.append(f'via_to = "{via_to}"')
    s.append(f"points = {toml_points(pts)}")
    return "\n".join(s) + "\n"

# ---- one channel column, relative to its centre line -------------------------------------------
LEDX = {"MV": 26.0, "Z1": 35.5, "Z2": 45.0, "Z3": 54.5, "Z4": 64.0, "Z5": 73.5, "Z6": 83.0, "Z7": 92.5, "Z8": 102.0}
NUM = {"MV": 9, "Z1": 1, "Z2": 2, "Z3": 3, "Z4": 4, "Z5": 5, "Z6": 6, "Z7": 7, "Z8": 8}
G = "group.channel_power.track"
group_tracks = [
    "# Copper of one channel column, repeated nine times. Offsets from the column centre at y = 0.",
    track(["C1.1", "R5.2"], width=0.3, table=G, comment="Snubber mid-point."),
    track(["D1.1", [-1.25, 16.54], "F1.1"], width=1.2, table=G, comment="Output: TVS to the PTC."),
    track(["D1.1", [-1.25, 14.6], [0.5, 14.7]], width=0.6, table=G),
    track(["F1.1", [0.5, 16.2], {"via": [0.5, 14.7]}], width=0.6, via=(0.6, 0.3), table=G,
          comment="Two output vias down to the fan-out lane on In2.Cu, joined there."),
    track(["F1.1", {"via": [-3.12, 16.54]}, [-3.12, 15.6], [0.5, 15.6], [0.5, 14.7]], width=0.6, via=(0.6, 0.3),
          via_to="In2.Cu", table=G),
    track(["F1.2", "Q1.2"], width=3.0, table=G, comment="T2: PTC to the triac tab, and vias to the island on B.Cu."),
    track(["F1.2", {"via": [-3.12, 22.7]}], width=1.0, table=G),
    track(["F1.2", {"via": [-3.12, 24.1]}], width=1.0, table=G),
]
for vx, vy in ((-1.5, 26.9), (1.5, 26.9), (-1.5, 29.7), (1.5, 29.7)):
    group_tracks.append(track(["Q1.2", {"via": [vx, vy]}], width=0.6, via=(0.6, 0.3), table=G))
group_tracks += [
    track(["R5.1", {"via": [3.25, 13.97]}], width=0.3, via=(0.6, 0.3), table=G,
          comment="T2 for the snubber resistor, from the island."),
    track(["Q1.1", "R4.2"], width=0.6, table=G, comment="A1: to the gate resistor, and vias to the ISNS plane on In2.Cu."),
    track(["Q1.1", {"via": [2.3, 36.0]}], width=0.6, via=(0.6, 0.3), table=G),
    track(["Q1.1", {"via": [2.3, 32.5]}], width=0.6, via=(0.8, 0.4), table=G),
    track(["Q1.3", [-2.28, 35.9], [0.675, 35.9], "R4.1"], width=0.3, table=G, comment="Gate."),
    track(["R4.1", {"via": [0.675, 38.4]}], width=0.3, via=(0.6, 0.3), table=G),
    track(["R3.1", {"via": [-2.71, 38.55]}], width=0.3, via=(0.6, 0.3), table=G, comment="T2 and the opto output."),
    track(["R3.2", {"via": [-0.89, 38.65]}], width=0.3, via=(0.6, 0.3), table=G),
]

# ---- fan-out lanes on In2 ------------------------------------------------------------------------
LANES = [12.75, 11.55, 10.35, 9.15, 7.95]
JOG, STEP = 1.2, 0.05
order = list(COLS)
via_x = {n: round(COLS[n] + 0.5, 3) for n in order}
pts = {n: [] for n in order}
active = []        # names, in index order
st = {}            # name -> dict(lane, from_lane, jog_start, jog_end)
last_end = -99.0
x = 37.0
while x <= 101.5:
    for n in list(active):
        if abs(x - via_x[n]) < STEP / 2:
            assert st[n]["lane"] == 0 and x >= st[n]["jog_end"], (n, st[n], x)
            pts[n] += [(via_x[n], LANES[0]), (via_x[n], 14.7)]
            active.remove(n)
            last_end = x
    for n in order:
        if abs(x - PIN_X[n]) < STEP / 2:
            occ = [max(st[t]["lane"], st[t]["from_lane"]) if x < st[t]["jog_end"] + 0.6 else st[t]["lane"] for t in active]
            lane = (max(occ) + 1) if occ else 0
            assert lane < len(LANES), (n, lane)
            st[n] = {"lane": lane, "from_lane": lane, "jog_start": -99, "jog_end": -99}
            pts[n] = [PINS[n], (PIN_X[n], LANES[lane])]
            active.append(n)
    for n in active:
        s_ = st[n]
        L_ = s_["lane"]
        if L_ == 0 or x < s_["jog_end"] + 0.3 or abs(x - PIN_X[n]) < 0.8:
            continue
        busy = False
        for t in active:
            if t == n:
                continue
            u = st[t]
            if x < u["jog_end"]:
                if u["lane"] == L_ - 1 or (u["from_lane"] == L_ - 1 and x < u["jog_start"] + 0.6):
                    busy = True
            elif u["lane"] == L_ - 1:
                busy = True
        if busy or (L_ - 1 == 0 and x < last_end + 1.25) or x + JOG > via_x[n] - 0.3:
            continue
        pts[n] += [(round(x, 3), LANES[L_]), (round(x + JOG, 3), LANES[L_ - 1])]
        s_.update(from_lane=L_, lane=L_ - 1, jog_start=x, jog_end=x + JOG)
    x = round(x + STEP, 3)
assert not active, active

def seg_dist(a, b, c, d):
    def pt_seg(p, q, r):
        vx, vy = r[0] - q[0], r[1] - q[1]
        n2 = vx * vx + vy * vy
        t = 0 if n2 == 0 else max(0, min(1, ((p[0] - q[0]) * vx + (p[1] - q[1]) * vy) / n2))
        return math.hypot(p[0] - q[0] - t * vx, p[1] - q[1] - t * vy)
    def side(p, q, r):
        return (q[0] - p[0]) * (r[1] - p[1]) - (q[1] - p[1]) * (r[0] - p[0])
    if side(a, b, c) * side(a, b, d) < 0 and side(c, d, a) * side(c, d, b) < 0:
        return 0.0      # the two segments cross
    return min(pt_seg(a, c, d), pt_seg(b, c, d), pt_seg(c, a, b), pt_seg(d, a, b))

polys = {n: [(PIN_X[n], 5.9)] + pts[n][1:] for n in order}
worst = 9.0
for i, a in enumerate(order):
    for b in order[i + 1:]:
        for p, q in zip(polys[a], polys[a][1:]):
            for r, s2 in zip(polys[b], polys[b][1:]):
                worst = min(worst, seg_dist(p, q, r, s2))
_ = (f"lanes: closest two fan-out tracks come is {worst:.3f} mm centre to centre (need 1.2)")
assert worst >= 1.19, worst

lane_tracks = ["# Valve outputs, from each terminal pin to the vias under its column, on In2.Cu. Later\n"
               "# outputs start lower and step up as earlier ones end, so no two cross. 0.9 mm wide,\n"
               "# 0.3 mm apart: design/rules.kicad_dru allows 0.25 mm between 24 VAC nets on this layer.\n"
               "# The inner copper must be 1 oz (35 um)."]
for n in order:
    lane_tracks.append(track(pts[n], layer="In2.Cu", width=0.9))

# ---- zones ---------------------------------------------------------------------------------------
def zone(net, layers, rect=None, **kw):
    s = ["[[zone]]", f'net = "{net}"', "layers = [" + ", ".join(f'"{l}"' for l in layers) + "]"]
    if rect:
        x0, y0, x1, y1 = rect
        s.append(f"outline = [[{x0:g}, {y0:g}], [{x1:g}, {y0:g}], [{x1:g}, {y1:g}], [{x0:g}, {y1:g}]]")
    for k, v in kw.items():
        s.append(f"{k} = {v}" if not isinstance(v, str) else f'{k} = "{v}"')
    return "\n".join(s) + "\n"

zones = ["# Ground: all of In1.Cu, and pours on both outer layers.",
         zone("GND", ["In1.Cu"], clearance=0.3, min_width=0.25, connect="solid", priority=0),
         zone("GND", ["F.Cu", "B.Cu"], clearance=0.3, min_width=0.25, connect="thermal", thermal_gap=0.3,
              thermal_width=0.4, priority=0),
         "# 3.3 V: In2.Cu above the LED row, and down both edges to the parts that need it there.",
         "[[zone]]\nnet = \"3V3\"\nlayers = [\"In2.Cu\"]\n"
         "outline = [[0.5, 109.5], [113.5, 109.5], [113.5, 5.0], [103.9, 5.0], [103.9, 45.3], [9.5, 45.3], "
         "[9.5, 16.0], [0.5, 16.0]]\nclearance = 0.3\nmin_width = 0.25\nconnect = \"solid\"\npriority = 1\n",
         "# Shunt return: In2.Cu under the triacs. Every A1 terminal drops into it through two vias.",
         zone("/ISNS", ["In2.Cu"], rect=(34.6, 17.6, 103.3, 39.1), clearance=0.5, min_width=0.3, connect="solid",
              priority=2),
         "# One island per triac tab on B.Cu, 6.5 x 31.6 mm, joined to the tab by six vias."]
for n in order:
    zones.append(zone(f"/{n}/T2", ["B.Cu"], rect=(COLS[n] - 3.25, 7.6, COLS[n] + 3.25, 39.0), clearance=0.5,
                      min_width=0.3, connect="solid", priority=3))

# ---- 24 VAC entry and other hand tracks ----------------------------------------------------------
hand = [
    "# 24 VAC entry. 2.0 mm: 3 A, the fuse rating, gives about 6 K rise in 1 oz outer copper.",
    track(["J1.1", "F4.1"], width=2.0),
    track(["J1.2", "F1.1"], width=2.0),
    track(["F4.2", [11.815, 21.8], "D1.1"], width=2.0, comment="AC_L: fuse to the surge clamp, the rectifier and the relay contact."),
    track(["F4.2", [11.815, 19.0], [23.2, 19.0], "D2.2"], width=1.0),
    track(["D1.1", [10.6, 21.8], [7.9, 24.5], "K1.13"], width=2.0),
    track(["K1.13", "JP1.1"], width=0.5),
    track(["K1.13", [3.2, 25.5], "R55.1"], width=0.5),
    track(["K1.14", [7.15, 16.3], [9.6, 13.8], [9.6, 10.6], [11.2, 9.0], [27.0, 9.0], [29.0, 7.2], "J2.1"], layer="B.Cu",
          width=2.0, comment="COM: relay contact to the two COM terminals, on B.Cu under the fuse well."),
    track(["J2.1", "J2.2"], layer="B.Cu", width=2.0),
    track(["J2.2", [34.035, 8.4], [35.6, 9.45], "D23.1"], width=1.5),
    track(["K1.14", "JP1.2"], width=0.5),
    track(["JP1.2", [5.5, 21.35], "TP22.1"], width=0.5),
    track(["F1.2", [30.185, 13.6], [36.0, 13.6], "D23.2"], width=2.0,
          comment="Return: shunt and valve-common TVS to the fuse in the AC_N leg."),
    track(["D23.2", "R37.2"], width=2.0),
    track(["D2.1", [27.2, 23.0], [26.5, 23.6], "R1.1"], width=1.0, comment="Rectifier to the surge resistor."),
    track(["R1.2", [33.75, 25.9], [33.75, 30.6], [35.0, 31.85], "C1.1"], width=1.0, comment="VIN_DC: surge resistor to C1, C1 to the fuse F5."),
    track(["C1.1", [34.1, 34.4], [34.1, 36.2], [33.6, 36.71], "F5.1"], width=1.5),
    track(["F5.1", [28.9, 38.3], "R7.1"], width=0.3),
    track(["F5.2", [29.2, 31.8], [27.55, 31.8]], width=1.5, comment="VIN_U1, behind the fuse: F5 to the input capacitors and pin 2 of U1."),
    track(["C3.1", "C2.1"], width=1.5),
    track(["C3.1", [25.9, 32.365], "U1.2"], width=1.0),
    track(["U1.3", [25.68, 33.635], [25.68, 36.3], [26.275, 36.9], "R8.1"], width=0.2,
          comment="UVLO: up the slot between the pins of U1 and the input capacitors to its divider."),
    track(["R8.1", "R7.2"], width=0.2),
    track(["U1.8", "C4.2"], width=0.6, comment="Switch node: pin 8, the bootstrap capacitor, the inductor. Kept small."),
    track(["C4.2", "L1.1"], width=0.6),
    track(["U1.7", "C4.1"], width=0.3),
    track(["L1.1", [15.5, 33.05], [16.95, 34.5], [16.95, 36.3], "R6.1"], width=0.25,
          comment="Switch node to the ripple resistor R6, which is at the feedback pin."),
    track(["L1.2", "D25.1"], width=2.0, comment="V_BUCK: the output pad of L1 to the clamp D25 and to C6 and C5."),
    track(["L1.2", [10.925, 28.0], "C6.1"], width=1.1),
    track(["L1.2", [11.5, 28.0], [15.3, 28.0], "C5.1"], width=0.5),
    track(["D25.2", {"via": [14.0, 38.7]}], width=1.0, via=(0.8, 0.4), comment="D25 anode to the ground plane: four vias."),
    track(["D25.2", {"via": [15.4, 38.7]}], width=1.0, via=(0.8, 0.4)),
    track(["D25.2", {"via": [14.0, 34.75]}], width=1.0, via=(0.8, 0.4)),
    track(["D25.2", {"via": [15.2, 34.75]}], width=1.0, via=(0.8, 0.4)),
    track(["C1.1", "R9.1"], width=0.4),
    track(["C1.1", "TP1.1"], width=0.4),
    "# ISNS: a strip through the lower pads of every TVS and snubber capacitor, then up to the\n"
    "# shunt. It carries surge and snubber current only. The triac currents take the In2.Cu plane.",
    track(["D901.2", [41.6, 8.63], [101.7, 8.63], "C801.2"], width=1.0),
    track(["D901.2", [41.6, 8.63], [38.65, 8.63], [38.65, 21.6], [37.6, 21.6], "R37.1"], width=0.5),
    track(["R37.1", {"via": [37.3, 23.7]}], width=1.0),
    track(["R37.1", {"via": [35.3, 23.7]}], width=1.0),
    track(["R37.1", "TP17.1"], width=0.6),
]

# Kelvin sense of the shunt (hardware design, section 10.2): one track from each pad of R37 to the
# amplifier input resistors, on B.Cu, kept out of every pour.
kelvin = [
    "# Kelvin sense of the shunt R37. Each track leaves from its own via at the left end of a shunt\n"
    "# pad, and runs on B.Cu: left under C1, up at x = 18.4 and 19.0, where no gate line crosses, then\n"
    "# right to the amplifier. Rule areas keep the ground pours away from both, so that the ground\n"
    "# sense has no second path.",
    track(["R37.1", {"via": [34.1, 22.15]}, [34.1, 23.6], [33.3, 24.4], [19.6, 24.4], [19.0, 25.0], [19.0, 57.7],
           [19.6, 58.3], [32.7, 58.3], [32.7, 62.8]], width=0.25, via=(0.6, 0.3)),
    track(["R38.1", {"via": [32.7, 62.8]}], width=0.25, via=(0.6, 0.3)),
    track(["R38.1", [32.7, 62.8], "R42.1"], width=0.25),
    track(["R37.2", {"via": [34.1, 16.35]}, [33.3, 17.15], [33.3, 23.0], [32.5, 23.8], [18.9, 23.8], [18.4, 24.3],
           [18.4, 58.3], [19.0, 58.9], [31.9, 58.9], [31.9, 61.2]], width=0.25, via=(0.6, 0.3)),
    track(["R40.1", {"via": [31.9, 61.2]}], width=0.25, via=(0.6, 0.3)),
    track(["R40.1", [31.9, 61.2], [32.3, 61.6], [38.6, 61.6], "R44.1"], width=0.25),
    "[[rule_area]]\nname = \"Kelvin sense: no pour\"\nlayers = [\"B.Cu\"]\nno_pour = true\n"
    "outline = [[32.7, 15.6], [34.8, 15.6], [34.8, 24.9], [19.5, 24.9], [19.5, 57.8], [33.2, 57.8], [33.2, 63.3], "
    "[31.4, 63.3], [31.4, 59.4], [17.9, 59.4], [17.9, 23.3], [32.7, 23.3]]\n",
    "[[rule_area]]\nname = \"Kelvin sense: no pour\"\nlayers = [\"F.Cu\"]\nno_pour = true\nrect = [30.5, 60.95, 39.5, 63.4]\n",
    "[[rule_area]]\nname = \"Kelvin sense: ground via not tied to the plane here\"\nlayers = [\"In1.Cu\"]\nno_pour = true\n"
    "rect = [31.2, 60.5, 32.6, 61.9]\n",
]

# Keep-alive, USB and the opto LED supply.
more = [
    "# Keep-alive (hardware design, section 8.5): from module pin 31 under the module to detector A,\n"
    "# then on B.Cu to detector B. The [[guard]] below keeps every other track 1 mm away.",
    track(["U4.31", {"via": [108.9, 97.08]}, [108.9, 91.0], [109.5, 90.4], [109.5, 88.5]], width=0.25, via=(0.6, 0.3)),
    track(["R29.1", {"via": [109.5, 88.5]}, [109.5, 80.0], [108.1, 78.6], [44.2, 78.6]], width=0.25, via=(0.6, 0.3)),
    track(["R33.1", [109.5, 88.5]], width=0.25),
    track(["R89.1", {"via": [44.2, 78.6]}], width=0.25, via=(0.6, 0.3)),
    "# USB D+ and D-, side by side on B.Cu from the ESD array up the left edge and across under the\n"
    "# display to the series resistors at the module. 0.25 mm tracks, 0.2 mm apart. Full speed only:\n"
    "# not impedance controlled.",
    track(["U2.1", {"via": [1.3, 18.65]}, [1.3, 19.9], [4.3, 22.9], [4.3, 97.7], [4.75, 98.15], [89.2, 98.15]],
          width=0.25, via=(0.6, 0.3)),
    track(["R98.1", {"via": [89.2, 98.15]}], width=0.25, via=(0.6, 0.3)),
    track(["U2.3", {"via": [1.3, 16.75]}, [1.9, 16.15], [4.15, 16.15], [4.75, 16.75], [4.75, 97.0], [5.45, 97.7],
           [88.0, 97.7], [88.2, 97.5], [90.45, 97.5], [90.9, 97.95], [90.9, 98.15]], width=0.25, via=(0.6, 0.3)),
    track(["R97.1", {"via": [90.9, 98.15]}], width=0.25, via=(0.6, 0.3)),
    "# V_DRV, the opto LED supply: one bus under the LED row, a stub to each resistor.",
    track(["R901.1", [22.075, 40.6], [105.925, 40.6], "R802.1"], width=0.4),
]
for n in order:
    k, x0 = NUM[n], LEDX[n]
    if n != "MV":
        more.append(track([f"R{k}01.1", [x0 - 3.925, 40.6]], width=0.3))
    if n != "Z8":
        more.append(track([f"R{k}02.1", [x0 + 3.925, 40.6]], width=0.3))
more += [
    "# Ground vias for pads where the fan-out finds no place by itself.",
    track(["J7.1", {"via": [56.4, 84.9]}], width=0.25, via=(0.6, 0.3)),
    track(["J5.A12", [11.25, 7.65], [14.15, 7.65], "R17.2"], width=0.3),
    "# 3.3 V at the display connector: pin 9 to pin 8, which has the via.",
    track(["J7.9", "J7.8"], width=0.25),
]

GL = "group.channel_logic.track"
logic_tracks = [
    "# Copper of one LED cell: resistor to LED, LED cathode to the opto LED cathode, resistor to the\n"
    "# opto LED anode. Offsets from the LED.",
    track(["R2.2", "D2.2"], width=0.25, table=GL),
    track(["D2.1", [-0.7875, 1.4], [0.0, 2.2], "U1.2"], width=0.25, table=GL),
    track(["R1.2", [-2.275, 1.2], [-2.54, 1.5], "U1.1"], width=0.25, table=GL),
]

# Gate and opto-output lines: from the two vias at the top of each column, sideways in the strip
# under the LED row, then up under the opto-triac to a via beside its pin. The opto-output lines
# take In2.Cu and the gate lines B.Cu: on one layer the eighteen would need more lanes than fit.
GATE_LANES = [39.75 + 0.8 * i for i in range(6)]

def lanes_for(jobs, what):
    done = []
    for xs, ys, xd, pad in sorted(jobs):
        for lane in GATE_LANES:
            poly = [(xs, ys), (xs, lane), (xd, lane), (xd, 53.4)]
            if all(seg_dist(a, b, c, d) >= 0.79 for other, _ in done
                   for a, b in zip(poly, poly[1:]) for c, d in zip(other, other[1:])):
                done.append((poly, pad))
                break
        else:
            raise SystemExit(f"no lane for the {what} line to {pad}")
    print(f"{what} lines: {len(done)} routed, highest lane y = {max(q[0][1][1] for q in done):.2f}")
    return done

ogt = lanes_for([(COLS[n] - 0.89, 38.65, LEDX[n] - 2.54, f"U{NUM[n]}01.6") for n in order], "opto-output")
gate = lanes_for([(COLS[n] + 0.675, 38.4, LEDX[n] + 2.54, f"U{NUM[n]}01.4") for n in order], "gate")
gate_tracks = ["# Opto-output lines of the nine channels on In2.Cu, gate lines on B.Cu: sideways in the strip\n"
               "# under the LED row, then under each opto-triac to a via beside its pin. 0.3 mm, 0.5 mm apart."]
for jobs, layer in ((ogt, "In2.Cu"), (gate, "B.Cu")):
    for poly, pad in jobs:
        (xs, ys), (_, lane), (xd, _), _ = poly
        gate_tracks.append(track([pad, {"via": [xd, 53.4]}, [xd, lane], [xs, lane], [xs, ys]], width=0.3,
                                 via=(0.6, 0.3), via_to=layer))
COPPER = "\n".join(group_tracks) + "\n" + "\n".join(lane_tracks) + "\n" + "\n".join(zones) + "\n" + "\n".join(hand) + "\n" + "\n".join(kelvin) + "\n" + "\n".join(more) + "\n" + "\n".join(logic_tracks) + "\n" + "\n".join(gate_tracks)

# =============================================================================================
# Route settings, silkscreen, and the file itself
# =============================================================================================
TAIL = '''
# ---- copper ---------------------------------------------------------------------------------------
# Layer use: F.Cu parts and signals, In1.Cu ground, In2.Cu supply planes and the valve fan-out,
# B.Cu signals, the triac heat islands and ground.
[route]
layers = ["F.Cu", "In2.Cu", "B.Cu"]   # In1.Cu is the ground plane: the autorouter only passes vias through it
# Nets the autorouter must leave alone: they connect through planes, pours or hand-routed copper.
exclude = ["GND", "3V3", "/ISNS", "/OUT_*", "/*/T2", "/*/SNB", "/AC_L_IN", "/AC_N", "/AC_L", "/COM",
           "/Power/VRECT", "VIN_DC", "/Power/VIN_U1", "/Power/UVLO", "/Power/SW", "/Power/BST", "/KA", "/*/G", "/*/OGT", "/*/OLA", "/*/LEDA"]

[route.layer_cost]
"In2.Cu" = 2.5              # In2.Cu is also the 3.3 V pour: use it only where it saves a long way round

# Where the autorouter may put neither track nor via: the valve block on B.Cu (heat islands) and
# on In2.Cu (shunt plane and fan-out lanes), which it cannot see because they are added afterwards.
[[route.keepout]]
layers = ["B.Cu", "In2.Cu"]
rect = [39.4, 7.3, 103.3, 37.9]

[[route.keepout]]
layers = ["In2.Cu"]
rect = [34.6, 17.6, 39.4, 39.1]

# A via beside every surface-mount pad of a plane net.
[[plane]]
net = "GND"
skip = ["U4.41", "U1.9", "R40.1", "R44.1", "J7.1", "J5.A12", "J5.B1"]   # exposed pads: vias by hand. R40, R44: Kelvin, see below

[[plane]]
net = "3V3"
# where the 3.3 V pour on In2.Cu is
within = [[0.9, 45.7, 113.1, 109.1], [104.4, 5.4, 113.1, 45.7], [0.9, 16.4, 9.1, 45.7]]

@COPPER@

# No other track or via within 1 mm of the keep-alive net (hardware design, section 8.5).
[[guard]]
net = "/KA"
distance = 1.0

# ---- silkscreen -----------------------------------------------------------------------------------
# 349 parts on one side: the designators of the small parts do not fit between them. They are left
# off the silkscreen and are on the assembly drawing (F.Fab) instead. Connectors, ICs, switches,
# fuses and the bottom-side parts keep theirs.
[silk]
hide = ["R*", "C[2-9]", "C[1-9][0-9]", "C[1-9][0-9][0-9]", "D*", "F[23]", "F[1-9]0[0-9]", "Q*", "L*", "Y*",
        "TP*", "H*", "U[1-9]0[0-9]", "JP*"]

[[text]]
text = "ZoneFlow rev A"
layer = "F.SilkS"
at = [22.0, 106.5]
size = 1.5
thickness = 0.25

[[text]]
text = "Not built, not tested. CERN-OHL-P-2.0"
layer = "F.SilkS"
at = [28.0, 104.2]
size = 1.0
thickness = 0.15

[[text]]
text = "3A T"
layer = "F.SilkS"
at = [21.0, 16.6]
size = 1.0
thickness = 0.15

[[text]]
text = "JP1: bridge only if K1 is not fitted"
layer = "B.SilkS"
at = [22.0, 20.5]
size = 1.0
thickness = 0.15

[drc_severity]
'''

def main(board_dir: Path) -> int:
    out = [HEAD.rstrip(), ""]
    out.append("# Placement: reference = [x, y, rotation, side]. x, y is the footprint anchor.")
    out.append("[place]")
    for title, refs in notes:
        if not refs:
            continue
        out.append("")
        for line in title.split("\n"):
            out.append(f"# {line}")
        for r in refs:
            x, y, rot, side = place[r]
            tail = f', "{side}"' if side != "F" else ""
            out.append(f"{r} = [{x:g}, {y:g}, {rot}{tail}]")
    out.append("")
    out.append(GROUPS.strip())
    out.append("")
    out.append(TAIL.replace("@COPPER@", COPPER).strip())
    dest = board_dir / "design" / "board.toml"
    text = "\n".join(out) + "\n"
    if not dest.exists() or dest.read_text(encoding="utf-8") != text:
        dest.write_text(text, encoding="utf-8")
    print(f"layout: {len(place)} placements and {len(COLS)} channel instances -> {dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]))
