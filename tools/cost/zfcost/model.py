"""Landed cost of a ZoneFlow controller: data loading and the pricing rules.

Sourced facts are read from data/sourced.toml and data/jlcpcb-parts.csv;
assumptions from data/assumptions.toml. Every rule that turns them into money
is a function in this module, so that a test can check it alone.
"""

from __future__ import annotations

import csv
import math
import tomllib
from dataclasses import dataclass, field, replace
from pathlib import Path

TOOL_DIR = Path(__file__).resolve().parent.parent
REPO = TOOL_DIR.parent.parent
DATA = TOOL_DIR / "data"
BOM_CSV = REPO / "hardware" / "bom-preliminary.csv"

BUILDS = ("8-zone", "4-zone")
QTY_COLUMN = {"8-zone": "qty_8zone", "4-zone": "qty_4zone"}
SPARE_FUSE_LCSC = "C99550"


# --------------------------------------------------------------------------
# Data
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Part:
    """One row of the JLCPCB parts snapshot."""

    lcsc: str
    mpn: str
    library: str  # "basic" or "extended"
    preferred: bool
    stock: int
    least: int  # leastPatchNumber, read as the minimum order for assembly
    loss: int  # lossNumber, read as attrition parts per order
    min_purchase: int
    breaks: tuple[tuple[int, float], ...]

    @property
    def fee_extended(self) -> bool:
        """Extended and not preferred: the Economic feeder fee applies."""
        return self.library == "extended" and not self.preferred


@dataclass(frozen=True)
class Line:
    """One BOM line as the cost model sees it."""

    refs: str
    mpn: str
    lcsc: str
    qty: dict[str, int]
    kind: str  # smt, tht, plugged, loose, cell


def price_at(breaks, qty: int) -> float:
    """Unit price for an order of qty: the last break whose start is <= qty.

    Below the first break the first break's price applies.
    """
    if not breaks:
        raise ValueError("no price breaks")
    price = breaks[0][1]
    for start, p in breaks:
        if qty >= start:
            price = p
    return price


def parse_breaks(text: str) -> tuple[tuple[int, float], ...]:
    out = []
    for item in text.split(";"):
        start, price = item.split(":")
        out.append((int(start), float(price)))
    return tuple(sorted(out))


def load_parts(path: Path = DATA / "jlcpcb-parts.csv") -> dict[str, Part]:
    parts = {}
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            parts[r["lcsc"]] = Part(
                lcsc=r["lcsc"],
                mpn=r["mpn"],
                library=r["library"],
                preferred=r["preferred"] == "yes",
                stock=int(r["stock"]),
                least=int(r["least_patch_number"]),
                loss=int(r["loss_number"]),
                min_purchase=int(r["min_purchase"]),
                breaks=parse_breaks(r["price_breaks"]),
            )
    return parts


def load_sourced(path: Path = DATA / "sourced.toml") -> dict:
    return tomllib.loads(path.read_text())


def load_assumptions(path: Path = DATA / "assumptions.toml") -> dict:
    raw = tomllib.loads(path.read_text())["assumptions"]
    return {k: v["value"] for k, v in raw.items()}


def assumption_notes(path: Path = DATA / "assumptions.toml") -> dict[str, str]:
    raw = tomllib.loads(path.read_text())["assumptions"]
    return {k: v["why"] for k, v in raw.items()}


def classify(refs: str, lcsc: str, tht_refs, plugged_refs) -> str:
    first = refs.split()[0]
    if not lcsc:
        return "cell"
    if refs.endswith("plug"):
        return "loose"
    if first in tht_refs:
        return "tht"
    if first in plugged_refs:
        return "plugged"
    return "smt"


def load_bom(sourced: dict, path: Path = BOM_CSV) -> list[Line]:
    board = sourced["board"]
    lines = []
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            lines.append(
                Line(
                    refs=r["refs"],
                    mpn=r["mpn"],
                    lcsc=r["lcsc"],
                    qty={b: int(r[QTY_COLUMN[b]]) for b in BUILDS},
                    kind=classify(r["refs"], r["lcsc"], board["tht_refs"], board["plugged_refs"]),
                )
            )
    return lines


@dataclass
class Inputs:
    """Everything one calculation needs. Scenarios copy and change it."""

    lines: list[Line]
    parts: dict[str, Part]
    sourced: dict
    a: dict  # assumptions by name
    joints: dict[str, int] = field(default_factory=dict)  # SMT joints per board by build
    price_factor: dict[str, float] = field(default_factory=dict)  # lcsc -> multiplier
    basic_override: frozenset = frozenset()  # lcsc numbers treated as basic

    @classmethod
    def load(cls, overrides: dict | None = None) -> Inputs:
        sourced = load_sourced()
        a = load_assumptions()
        if overrides:
            for k, v in overrides.items():
                if k not in a:
                    raise KeyError(f"unknown assumption {k}")
                a[k] = v
        return cls(
            lines=load_bom(sourced),
            parts=load_parts(),
            sourced=sourced,
            a=a,
            joints=dict(sourced["board"]["smt_joints"]),
        )

    def copy(self, **changes) -> Inputs:
        new = replace(self, **changes)
        new.a = dict(new.a)
        new.joints = dict(new.joints)
        return new

    def unit_price(self, lcsc: str, qty: int) -> float:
        return price_at(self.parts[lcsc].breaks, qty) * self.price_factor.get(lcsc, 1.0)

    def fee_extended(self, lcsc: str) -> bool:
        return lcsc not in self.basic_override and self.parts[lcsc].fee_extended


# --------------------------------------------------------------------------
# Rules
# --------------------------------------------------------------------------


def assembled_order_qty(needed: int, part: Part, attrition: bool) -> int:
    """Parts JLCPCB buys for an assembly order of `needed` fitted parts."""
    if needed <= 0:
        return 0
    if not attrition:
        return needed
    return max(needed + part.loss, part.least)


def loose_order_qty(needed: int, part: Part) -> int:
    """Parts bought loose (not fitted): at least the minimum purchase."""
    if needed <= 0:
        return 0
    return max(needed, part.min_purchase)


def tiered(amount: float, tiers) -> float:
    """Cost of `amount` units priced by marginal tiers [[upper, rate], ...]."""
    cost, lower = 0.0, 0
    for upper, rate in tiers:
        if amount <= lower:
            break
        cost += (min(amount, upper) - lower) * rate
        lower = upper
    if amount > lower:
        cost += (amount - lower) * tiers[-1][1]
    return cost


def interpolate(x: float, xs, ys) -> float:
    """Piecewise-linear interpolation; flat below the first point, linear past the last."""
    if x <= xs[0]:
        return ys[0]
    for i in range(1, len(xs)):
        if x <= xs[i]:
            x0, x1, y0, y1 = xs[i - 1], xs[i], ys[i - 1], ys[i]
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    x0, x1, y0, y1 = xs[-2], xs[-1], ys[-2], ys[-1]
    return y1 + (y1 - y0) * (x - x1) / (x1 - x0)


def pcb_order(n: int, fab: dict, hole: str, round_up: bool) -> tuple[int, float]:
    """(boards ordered, USD) for n controllers from the quoted quantities."""
    qtys = fab["quantities"]
    prices = fab["via_" + hole]
    for q, p in zip(qtys, prices):
        if q >= n:
            if round_up or q == n:
                return q, p
            break
    if n > qtys[-1] or not round_up:
        # Beyond the quotes: scale the last quote's per-board price.
        return n, prices[-1] / qtys[-1] * n
    raise AssertionError("unreachable")


def split_orders(n: int, service: str, economic_max: int) -> list[tuple[int, str]]:
    """Split n boards into assembly orders: [(boards, service), ...]."""
    if service == "auto":
        service = "economic" if n <= economic_max else "standard"
    if service == "economic" and n > economic_max:
        k = math.ceil(n / economic_max)
        base, extra = divmod(n, k)
        return [(base + (1 if i < extra else 0), "economic") for i in range(k)]
    return [(n, service)]


@dataclass
class Cost:
    """Money of one batch, by category, USD for the whole batch."""

    n: int
    build: str
    orders: list[tuple[int, str]] = field(default_factory=list)
    smt_parts: float = 0.0
    hand_parts: float = 0.0  # through-hole fitted by the builder, plugs, display, spare fuse
    cell: float = 0.0
    pcb: float = 0.0
    assembly: float = 0.0
    extended_fee: float = 0.0  # part of `assembly`, shown apart
    freight: float = 0.0
    tariff: float = 0.0
    enclosure: float = 0.0
    labour: float = 0.0
    hours_print: float = 0.0
    hours_builder: float = 0.0
    weight_g: float = 0.0
    pcb_boards: int = 0
    lines: list[tuple[str, str, int, int, float]] = field(default_factory=list)  # mpn, kind, needed, ordered, usd

    CATEGORIES = ("smt_parts", "hand_parts", "cell", "pcb", "assembly", "freight", "tariff", "enclosure", "labour")

    @property
    def total(self) -> float:
        return sum(getattr(self, c) for c in self.CATEGORIES)

    @property
    def board_total(self) -> float:
        """The controller without the enclosure and labour."""
        return self.total - self.enclosure - self.labour

    def per_unit(self, cat: str | None = None) -> float:
        v = self.total if cat is None else getattr(self, cat)
        return v / self.n


def assembly_fees(boards: int, service: str, build: str, inp: Inputs, smt_types: list[str]) -> tuple[float, float]:
    """(assembly fees, of which extended-part fees) for one assembly order."""
    asm = inp.sourced["assembly"]
    joints = inp.joints[build] * boards
    if service == "economic":
        fee = asm["economic_setup"] + asm["economic_stencil"] + asm["confirm_placement"]
        fee += joints * asm["economic_joint"]
        ext = sum(1 for c in smt_types if inp.fee_extended(c)) * asm["economic_feeder_extended"]
    else:
        fee = asm["standard_setup_single"] + asm["standard_stencil_single"] + asm["confirm_placement"]
        fee += tiered(joints, asm["standard_joint_tiers"])
        area = inp.sourced["board"]["width_mm"] * inp.sourced["board"]["height_mm"] / 100.0
        fee += asm["standard_packing_base"] + asm["standard_packing_per_cm2"] * area * boards
        ext_types = sum(1 for c in smt_types if inp.parts[c].library == "extended")
        ext = ext_types * asm["standard_feeder_per_type"]
        fee += (len(smt_types) - ext_types) * asm["standard_feeder_per_type"]
    if inp.a["tht_by"] == "assembler":
        tht = sum(inp.sourced["board"]["tht_joints"][r] for r in tht_refs_fitted(inp, build))
        fee += tiered(tht * boards, asm["manual_joint_tiers"]) + asm["hand_soldering_labor_per_order"]
        fee += fixtures(boards, asm) * asm["fixture_each"]
    return fee + ext, ext


def fixtures(boards: int, asm: dict) -> int:
    for upper, count in asm["fixture_tiers"]:
        if boards <= upper:
            return count
    return asm["fixture_tiers"][-1][1]


def tht_refs_fitted(inp: Inputs, build: str) -> list[str]:
    out = []
    for line in inp.lines:
        if line.kind == "tht" and line.qty[build] > 0:
            out.extend(line.refs.split())
    return out


def batch_cost(n: int, build: str, inp: Inputs) -> Cost:
    """Landed cost of n controllers of one build, delivered to the US."""
    a = inp.a
    c = Cost(n=n, build=build)
    service = a["service"]
    if a["tht_by"] == "assembler":
        service = "standard"
    c.orders = split_orders(n, service, inp.sourced["assembly"]["economic_max_qty"])
    fab = inp.sourced["pcb_fab"]
    china_goods = 0.0
    for boards, svc in c.orders:
        assembled = [ln for ln in inp.lines if ln.qty[build] > 0 and ln.kind == "smt"]
        if a["tht_by"] == "assembler":
            assembled += [ln for ln in inp.lines if ln.qty[build] > 0 and ln.kind == "tht"]
        loose = [ln for ln in inp.lines if ln.qty[build] > 0 and ln.kind in ("loose", "plugged")]
        if a["tht_by"] == "builder":
            loose += [ln for ln in inp.lines if ln.qty[build] > 0 and ln.kind == "tht"]
        order_goods = 0.0
        for ln in assembled:
            need = ln.qty[build] * boards
            qty = assembled_order_qty(need, inp.parts[ln.lcsc], a["attrition_fields"])
            usd = qty * inp.unit_price(ln.lcsc, qty if a["tier_by_order_qty"] else need)
            c.smt_parts += usd
            order_goods += usd
            c.lines.append((ln.mpn, ln.kind, need, qty, usd))
        spare = [(SPARE_FUSE_LCSC, int(a["spare_fuses_per_unit"]))] if a["spare_fuses_per_unit"] else []
        for lcsc, per, mpn, kind in [(ln.lcsc, ln.qty[build], ln.mpn, ln.kind) for ln in loose] + [
            (s, k, inp.parts[s].mpn, "spare") for s, k in spare
        ]:
            need = per * boards
            qty = loose_order_qty(need, inp.parts[lcsc])
            usd = qty * inp.unit_price(lcsc, qty)
            c.hand_parts += usd
            order_goods += usd
            c.lines.append((mpn, kind, need, qty, usd))
        pcb_boards, pcb_usd = pcb_order(boards, fab, a["pcb_hole"], a["pcb_qty_round_up"])
        c.pcb += pcb_usd
        c.pcb_boards += pcb_boards
        smt_types = sorted({ln.lcsc for ln in inp.lines if ln.qty[build] > 0 and ln.kind == "smt"})
        fee, ext = assembly_fees(boards, svc, build, inp, smt_types)
        c.assembly += fee
        c.extended_fee += ext
        order_goods += pcb_usd + fee
        china_goods += order_goods
        weight = (
            a["packing_g_per_order"]
            + pcb_boards * fab["bare_board_g"]
            + boards * (a["components_g_per_board"] + a["loose_g_per_unit"])
        )
        c.weight_g += weight
        fr = inp.sourced["freight_us"]
        freight = interpolate(weight, fr["weight_g"], fr["usd"])
        c.freight += freight
        c.tariff += a["us_tariff_rate"] * (order_goods + (freight if a["tariff_on_freight"] else 0.0))
    # Coin cells from a US distributor, one order per batch.
    cell = inp.sourced["cell"]
    cells_needed = sum(ln.qty[build] for ln in inp.lines if ln.kind == "cell") * n
    if cells_needed:
        qty = max(cells_needed, cell["min_order"])
        c.cell = qty * price_at([tuple(b) for b in cell["price_breaks"]], qty) + a["us_distributor_shipping"]
    c.enclosure = enclosure_cost(n, inp)
    c.hours_print = n * inp.sourced["enclosure"]["print_hours_fast"]
    minutes = n * (a["minutes_enclosure"] + a["minutes_flash_test"])
    if a["tht_by"] == "builder":
        minutes += n * a["minutes_tht_solder"]
    minutes += len(c.orders) * a["minutes_per_order"]
    c.hours_builder = minutes / 60.0
    c.labour = c.hours_builder * a["labour_usd_per_hour"]
    return c


def enclosure_cost(n: int, inp: Inputs) -> float:
    e = inp.sourced["enclosure"]
    a = inp.a
    kg = n * e["petg_g_per_unit"] * a["petg_waste_factor"] / 1000.0
    usd = math.ceil(kg) * e["petg_usd_per_kg"]  # whole 1 kg spools
    if a["fastener"] == "insert":
        usd += math.ceil(n * e["inserts_per_unit"] / e["insert_pack_qty"]) * e["insert_pack_usd"]
    usd += n * e["screws_per_unit"] * a["screw_usd_each"]
    usd += n * a["label_usd_each"]
    return usd


# --------------------------------------------------------------------------
# Scenarios
# --------------------------------------------------------------------------

# Extended, non-preferred types that are left after the second pass
# (2026-11-12) and that a part of the basic or preferred library could replace
# if the owner accepts what docs/hardware/cost.md says each would cost in
# function or in margin. They are not in the revision B package.
NEEDS_DECISION = {
    "C83270": "TVS SMBJ6.0A (D25): a preferred or basic part whose sheet lacks the pulse curve of section 4.8",
    "C7519": "USBLC6-2SC6 (U2, U9): SRV05-4 (C7420376, preferred), with pin 5 of U2 taken off VBUS",
    "C435835": "LDL1117S33R (U3): AMS1117-3.3 (C6186, basic), which needs 4.6 V and may get 4.1 V from USB",
    "C55266": "TPS2553DBVR (U11): a basic P-channel switch, without the current limit and the fault flag",
}

# Issue #31 candidates priced here: drop the output snubbers (47 R + 10 nF per
# channel) and put each zone LED in series with its opto-triac LED (drops the
# LED's 680 R). Channels: 9 on the 8-zone board, 5 on the 4-zone board.
SNUBBER_R, SNUBBER_C, LED_R = "C17714", "C18741", "C23228"
CHANNELS = {"8-zone": 9, "4-zone": 5}


def scenario_needs_decision(inp: Inputs) -> Inputs:
    """The fee of the four types of NEEDS_DECISION removed. Part prices are left as they are."""
    return inp.copy(basic_override=frozenset(NEEDS_DECISION))


# The revision B package of docs/hardware/cost.md: seven changes to the
# circuit or the layout, each of which removes one fee-carrying type. A line
# is (LCSC number, change of quantity on the 8-zone board, on the 4-zone
# board). A number that is not on the BOM today is priced from
# data/revb-parts.csv. "all" removes the line.
REVB_PACKAGE = [
    # U12 goes: comparator A of U13 takes its job, with a two-resistor divider.
    ("supervisor", [("C1852114", "all"), ("C22935", 1, 1), ("C22807", 1, 1)], -3 + 4),
    # R60, 33 ohm 1206: three of the 100 ohm 0805 parts of the gate network in parallel.
    ("backlight resistor", [("C18005", "all"), ("C17408", 3, 3)], +4),
    # R84, 4.7 ohm 1206: two basic 10 ohm 1206 parts in parallel.
    ("sensor supply resistor", [("C17937", "all"), ("C17903", 2, 2)], +2),
    # 10 nF 100 V 0805 (nine snubbers and C48): the basic 100 nF 100 V part.
    ("snubber capacitor", [("C18741", "all"), ("C28233", 10, 6)], 0),
    # SW4, SW5: the basic 5.1 mm switch of SW1 and SW2, with longer caps.
    ("tactile switch", [("C381050", "all"), ("C318884", 2, 2)], 0),
    # C2, C3, 2.2 uF 100 V 1210: two series pairs of basic 4.7 uF 50 V 1206 parts, each with 1 Mohm across it.
    ("input capacitors", [("C55151", "all"), ("C29823", 4, 4), ("C22935", 4, 4)], +12),
    # F2, the Qwiic PTC: the type of F3 on an 1812 footprint.
    ("Qwiic PTC", [("C151160", "all"), ("C315900", 1, 1)], 0),
]


def scenario_revb(inp: Inputs) -> Inputs:
    """The board after the revision B package."""
    parts = dict(inp.parts)
    parts.update(load_parts(DATA / "revb-parts.csv"))
    delta: dict[str, dict[str, int] | str] = {}
    joints = 0
    for _name, changes, dj in REVB_PACKAGE:
        joints += dj
        for code, *q in changes:
            if q == ["all"]:
                delta[code] = "all"
            else:
                d = delta.setdefault(code, {b: 0 for b in BUILDS})
                for b, dq in zip(BUILDS, q):
                    d[b] += dq
    lines, seen = [], set()
    for ln in inp.lines:
        seen.add(ln.lcsc)
        d = delta.get(ln.lcsc)
        if d == "all":
            lines.append(replace(ln, qty={b: 0 for b in BUILDS}))
        elif d:
            lines.append(replace(ln, qty={b: ln.qty[b] + d[b] for b in BUILDS}))
        else:
            lines.append(ln)
    for code, d in delta.items():
        if code not in seen:
            lines.append(Line(refs="revision B", mpn=parts[code].mpn, lcsc=code, qty=dict(d), kind="smt"))
    new = inp.copy(lines=lines, parts=parts)
    for b in BUILDS:
        new.joints[b] += joints
    return new


def scenario_selftap(inp: Inputs) -> Inputs:
    new = inp.copy()
    new.a["fastener"] = "selftap"
    return new


def scenario_issue31(inp: Inputs) -> Inputs:
    lines = []
    for ln in inp.lines:
        q = dict(ln.qty)
        if ln.lcsc in (SNUBBER_R, SNUBBER_C, LED_R):
            q = {b: q[b] - CHANNELS[b] for b in BUILDS}
        lines.append(replace(ln, qty=q))
    new = inp.copy(lines=lines)
    # Each removed part is a two-terminal part: two joints.
    for b in BUILDS:
        new.joints[b] -= 2 * 3 * CHANNELS[b]
    return new


SCENARIOS = {
    "selftap": ("Self-tapping screws in place of the seven heat-set inserts", scenario_selftap),
    "revb": (
        "The revision B package: seven extended types removed by changes to the circuit or the layout",
        scenario_revb,
    ),
    "needs_decision": (
        "Four more extended types whose replacement costs function or margin; each needs a decision",
        scenario_needs_decision,
    ),
    "issue31": (
        "Issue #31: no output snubbers; zone LED in series with the opto-triac LED",
        scenario_issue31,
    ),
}


# Two passes replaced extended part numbers by basic or preferred ones: five
# on 2026-11-11 (pull request #47) and four more, with one that stays
# extended, on 2026-11-12. data/replaced-parts.csv keeps the snapshot rows of
# the old numbers, fetched on the same day as the new ones, with the pass and
# the number that replaced each. From it the model prices the board as it was
# before either pass, so that the savings on the page are calculated and not
# copied.
@dataclass(frozen=True)
class Replaced:
    step: int  # 1: the first pass, 2: the second
    new: str  # the LCSC number that is on the BOM now
    old: Part
    qty: dict[str, int] | None  # how many parts of the new line were this part; None: the whole line


def load_replaced(path: Path = DATA / "replaced-parts.csv") -> list[Replaced]:
    old = load_parts(path)
    out = []
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            qty = {b: int(r[QTY_COLUMN[b]]) for b in BUILDS} if r["qty_8zone"] else None
            out.append(Replaced(step=int(r["pass"]), new=r["replaced_by"], old=old[r["lcsc"]], qty=qty))
    return out


def before(inp: Inputs, steps=(1, 2)) -> Inputs:
    """The same order with the part numbers that the given passes replaced put back."""
    by_new = {r.new: r for r in load_replaced() if r.step in steps}
    lines = []
    for ln in inp.lines:
        r = by_new.get(ln.lcsc)
        if r is None:
            lines.append(ln)
        elif r.qty is None:
            lines.append(replace(ln, lcsc=r.old.lcsc, mpn=r.old.mpn))
        else:
            # Part of the line was the old number: the zone and status LEDs
            # joined the line of the fault LED.
            lines.append(replace(ln, qty={b: ln.qty[b] - r.qty[b] for b in BUILDS}))
            refs = f"{ln.refs} (as {r.old.mpn})"
            lines.append(replace(ln, refs=refs, lcsc=r.old.lcsc, mpn=r.old.mpn, qty=dict(r.qty)))
    parts = dict(inp.parts)
    parts.update({r.old.lcsc: r.old for r in by_new.values()})
    return inp.copy(lines=lines, parts=parts)


def stages(inp: Inputs) -> list[tuple[str, str, Inputs]]:
    """(key, what it is, inputs) for the board at each stage of the work on extended part types."""
    return [
        ("designed", "Revision A as designed", before(inp, (1, 2))),
        ("pass1", "After the first pass, 2026-11-11", before(inp, (2,))),
        ("now", "After the second pass: the board as it is", inp),
        ("revb", "After the revision B package", scenario_revb(inp)),
    ]


def fee_types(inp: Inputs, build: str) -> int:
    """SMT part types of a build that pay the Economic feeder fee."""
    smt = {ln.lcsc for ln in inp.lines if ln.qty[build] > 0 and ln.kind == "smt"}
    return sum(1 for c in smt if inp.fee_extended(c))


def with_price_factor(inp: Inputs, lcsc: str, factor: float) -> Inputs:
    pf = dict(inp.price_factor)
    pf[lcsc] = factor
    return inp.copy(price_factor=pf)


def with_assumption(inp: Inputs, name: str, value) -> Inputs:
    new = inp.copy()
    if name not in new.a:
        raise KeyError(name)
    new.a[name] = value
    return new
