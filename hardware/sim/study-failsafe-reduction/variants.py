"""The variants of docs/design/failsafe-reduction-study.md as lists of parts.

STUDY, not the product. Nothing here changes hardware/netlist.yaml. The
reference designators on the "removed" side are those of revision A. The new
parts have names, not designators: nobody has assigned any.
"""

from __future__ import annotations

from dataclasses import dataclass

# Revision A parts by what they do (hardware-design.md sections 7, 8, 9 and 10).
DETECTOR_A = "R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36".split()  # block F without the pull-down R33
LOGIC = "U7 C20".split()  # the NAND gates: over-current latch and DRV_EN
LED_SWITCH = "Q3 R25 R26 C21".split()
DETECTOR_B = "Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95".split()  # relay detector, its transistor and read-back
RELAY = "K1 D5 Q1 R11 R12 D23 JP1".split()
LATCH = "R52 C33 R54 D8 R87".split()  # what only the over-current latch uses; R53 stays as a pull-up


@dataclass(frozen=True)
class New:
    name: str
    part: str
    lcsc: str
    pads: int
    like: str | None  # a revision A part with the same footprint, for the area
    area_mm2: float | None = None  # used when no part on the board has the footprint


# The one-chip detector with what makes it safe to use alone.
ONE_SHOT = [
    New("U_WD", "74LVC1G123DP,125 retriggerable one-shot, TSSOP-8", "C513291", 8, None, 19.0),
    New("C_DEC", "100 nF 50 V X7R 0603", "C14663", 2, "C20"),
    New("R_T1", "100k 1% 0603", "C25803", 2, "R25"),
    New("R_T2", "100k 1% 0603", "C25803", 2, "R25"),
    New("C_T", "4.7 uF 25 V X5R 0805", "C1779", 2, "C22"),
    New("R_S", "1k 1% 0603", "C21190", 2, "R11"),
    New("R_PD", "100k 1% 0603", "C25803", 2, "R25"),
    New("U_SV", "TLV803EA30DBZR supervisor, as U12, SOT-23", "C5218924", 3, "U12"),
]
INVERTER = [New("Q_INV", "AO3400A N-MOSFET SOT-23", "C20917", 3, "Q1")]


@dataclass(frozen=True)
class Variant:
    key: str
    title: str
    removed: tuple[str, ...]
    added: tuple[New, ...]


VARIANTS = [
    Variant(
        "V1",
        "One-chip detector resets the expander. No LED supply switch, no relay, no over-current latch",
        tuple(DETECTOR_A + LOGIC + LED_SWITCH + DETECTOR_B + RELAY + LATCH),
        tuple(ONE_SHOT),
    ),
    Variant(
        "V2",
        "One-chip detector: expander reset, LED supply switch, K1 on the expander bit. No second detector, no latch",
        tuple(DETECTOR_A + LOGIC + DETECTOR_B + LATCH),
        tuple(ONE_SHOT + INVERTER),
    ),
    Variant(
        "V3",
        "V2 with the relay detector of revision A kept: two layers. No latch",
        tuple(DETECTOR_A + LOGIC + LATCH),
        tuple(ONE_SHOT + INVERTER),
    ),
    Variant("D", "Revision A with the relay detector removed and nothing else changed", tuple(DETECTOR_B), ()),
    Variant("L", "Revision A with the over-current latch removed and nothing else changed", tuple(LATCH), ()),
]

ALL_REFS = sorted({r for v in VARIANTS for r in v.removed} | {n.like for v in VARIANTS for n in v.added if n.like})
