#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyyaml>=6,<7"]
# ///
"""Keep the rev A paper design consistent.

`hardware/netlist.yaml` is the source. This tool

- writes the component tables, the test pad table and the cost summary in
  `docs/design/hardware-design.md` from it, between marker comments,
- writes the reference and quantity columns of `hardware/bom-preliminary.csv`,
- checks the netlist itself, the BOM and `hardware/pinmap.yaml` against it.

    uv run tools/designsync/designsync.py check    # exit 1 on any difference
    uv run tools/designsync/designsync.py write    # rewrite the generated parts

`check` changes nothing. `write` never invents a BOM line: a part that is in
the netlist and not in the BOM is an error, because its maker, price and stock
have to come from a distributor page that a person read.
"""

from __future__ import annotations

import argparse
import csv
import io
import re
import sys
from collections import Counter, OrderedDict
from pathlib import Path

import yaml

REPO = Path(__file__).resolve().parents[2]
NETLIST = REPO / "hardware" / "netlist.yaml"
PINMAP = REPO / "hardware" / "pinmap.yaml"
BOM = REPO / "hardware" / "bom-preliminary.csv"
DOC = REPO / "docs" / "design" / "hardware-design.md"

VARIANTS = ("all", "8", "dnf")
REQUIRED = ("ref", "value", "package", "lcsc", "block", "variant", "pins")
BEGIN = re.compile(r"<!-- designsync:begin (?P<kind>[a-z]+)(?P<args>(?: [a-z]+=\"[^\"]*\")*) -->")
END = "<!-- designsync:end -->"
# A BOM word that is not a designator. "J3 plug" is the loose plug for J3 and
# follows J3's build variant. A word by itself, "cell", is one per board.
ACCESSORY = re.compile(r"^[a-z]+$")
MODULE = "U4"
EXPANDER = "U5"


class Problems:
    def __init__(self) -> None:
        self.items: list[str] = []

    def add(self, where: str, what: str) -> None:
        self.items.append(f"{where}: {what}")


def natkey(ref: str) -> tuple[str, int, str]:
    m = re.match(r"([A-Z]+)(\d+)(.*)", ref)
    return (m.group(1), int(m.group(2)), m.group(3)) if m else (ref, 0, "")


# ---------------------------------------------------------------- netlist


def load_netlist(problems: Problems) -> list[dict]:
    data = yaml.safe_load(NETLIST.read_text())
    comps = data["components"]
    seen: set[str] = set()
    part_of: dict[str, tuple[str, str]] = {}
    for c in comps:
        ref = c.get("ref", "?")
        for key in REQUIRED:
            if key not in c:
                problems.add("netlist", f"{ref} has no '{key}'")
        if ref in seen:
            problems.add("netlist", f"{ref} is defined twice")
        seen.add(ref)
        if c.get("variant") not in VARIANTS:
            problems.add("netlist", f"{ref} has variant {c.get('variant')!r}, expected one of {VARIANTS}")
        if not c.get("pins"):
            problems.add("netlist", f"{ref} has no pins")
        if c.get("lcsc"):
            part = (c["value"], c["package"])
            if part_of.setdefault(c["lcsc"], part) != part:
                problems.add("netlist", f"{ref}: {c['lcsc']} is {part}, elsewhere {part_of[c['lcsc']]}")
        elif c.get("variant") != "dnf" and not re.match(r"^(TP|JP|J)\d+$", ref):
            problems.add("netlist", f"{ref} is fitted and has no LCSC number")
    count = Counter(net for c in comps for net in c["pins"].values())
    for net, uses in sorted(count.items()):
        if uses == 1 and net != "NC":
            problems.add("netlist", f"net {net} has a single connection")
    return comps


# ---------------------------------------------------------------- document


def connections(c: dict) -> str:
    pins = c["pins"]
    if len(pins) > 30:
        return "see pin map"
    if len(pins) <= 2 and set(pins) <= {"1", "2"}:
        return " to ".join(f"`{net}`" for net in pins.values())
    return ", ".join(f"{pin} NC" if net == "NC" else f"{pin} `{net}`" for pin, net in pins.items())


def table(comps: list[dict], block: str, channel: str | None) -> str:
    rows = ["| Ref | Part | Package | LCSC | Connections | Note |", "|---|---|---|---|---|---|"]
    for c in comps:
        if c["block"] != block:
            continue
        if channel and not re.fullmatch(rf"[A-Z]+{channel}\d\d", c["ref"]):
            continue
        note = c.get("note", "")
        if c["variant"] == "8" and not channel:
            note += " (8-zone build only)"
        if c["variant"] == "dnf":
            note += " (not fitted)"
        cells = (c["ref"], c["value"], c["package"], c["lcsc"] or "none", connections(c), note.strip())
        rows.append("| " + " | ".join(cells) + " |")
    return "\n".join(rows)


def testpads(comps: list[dict]) -> str:
    pads = [(c["ref"], next(iter(c["pins"].values()))) for c in comps if re.fullmatch(r"TP\d+", c["ref"])]
    pads.sort(key=lambda p: natkey(p[0]))
    rows = ["| Pad | Net | Pad | Net | Pad | Net |", "|---|---|---|---|---|---|"]
    for i in range(0, len(pads), 3):
        cells = [f"{ref} | `{net}`" for ref, net in pads[i : i + 3]]
        cells += [" | "] * (3 - len(cells))
        rows.append("| " + " | ".join(cells) + " |")
    return "\n".join(rows)


def tier_price(row: dict, pieces: int) -> float | None:
    """Price each at the largest listed break (10, 100, 1000) the order reaches."""
    for qty in (1000, 100, 10):
        if pieces >= qty or qty == 10:
            text = row[f"usd_each_at_{qty}"]
            return float(text) if text else None
    return None


def cost(bom: list[dict]) -> str:
    priced = [r for r in bom if r["usd_each_at_10"]]
    unpriced = [r for r in bom if not r["usd_each_at_10"]]
    basic = sum(1 for r in bom if r["jlcpcb_library"].startswith("basic"))
    extended = sum(1 for r in bom if r["jlcpcb_library"].startswith("extended"))
    other = len(bom) - basic - extended
    out = [
        f"`hardware/bom-preliminary.csv` has {len(bom)} lines: {basic} basic parts, {extended} extended parts"
        + (f" and {other} that JLCPCB does not fit." if other else "."),
        "",
        "| Build | 10 boards | 100 boards |",
        "|---|---|---|",
    ]
    for name, col in (("8-zone", "qty_8zone"), ("4-zone", "qty_4zone")):
        cells = []
        for boards in (10, 100):
            total = sum(int(r[col]) * tier_price(r, int(r[col]) * boards) for r in priced if int(r[col]))
            cells.append(f"{total:.2f} USD")
        out.append(f"| {name} | {cells[0]} | {cells[1]} |")
    if unpriced:
        names = ", ".join(r["value"] for r in unpriced)
        out += ["", f"Not in the totals, because the line has no price: {names}."]
    lines = sorted(priced, key=lambda r: -int(r["qty_8zone"]) * tier_price(r, int(r["qty_8zone"]) * 100))
    out += ["", "The six most expensive lines on an 8-zone board at the 100-board price:", ""]
    out += ["| Line | Quantity | USD per board |", "|---|---|---|"]
    for r in lines[:6]:
        qty = int(r["qty_8zone"])
        out.append(f"| {r['mpn']} | {qty} | {qty * tier_price(r, qty * 100):.2f} |")
    stocked = [r for r in bom if r["jlcpcb_stock"] and int(r["qty_8zone"])]
    stocked.sort(key=lambda r: int(r["jlcpcb_stock"]) / int(r["qty_8zone"]))
    out += ["", "Lines with the least stock against a 100-board order of the 8-zone build:", ""]
    out += ["| Part | Stock on the date checked | Needed for 100 boards |", "|---|---|---|"]
    for r in stocked[:8]:
        out.append(f"| {r['mpn']} | {r['jlcpcb_stock']} | {int(r['qty_8zone']) * 100} |")
    return "\n".join(out)


def render(kind: str, args: dict, comps: list[dict], bom: list[dict]) -> str:
    if kind == "table":
        return table(comps, args["block"], args.get("channel"))
    if kind == "testpads":
        return testpads(comps)
    if kind == "cost":
        return cost(bom)
    raise ValueError(f"unknown generated region '{kind}'")


def sync_document(comps: list[dict], bom: list[dict], problems: Problems, write: bool) -> None:
    lines = DOC.read_text().split("\n")
    out: list[str] = []
    blocks_seen: set[str] = set()
    i = 0
    while i < len(lines):
        m = BEGIN.fullmatch(lines[i])
        if not m:
            if lines[i].startswith("<!-- designsync:") and lines[i] != END:
                problems.add("document", f"line {i + 1}: malformed marker")
            out.append(lines[i])
            i += 1
            continue
        try:
            j = lines.index(END, i)
        except ValueError:
            problems.add("document", f"line {i + 1}: marker is not closed")
            out.extend(lines[i:])
            break
        args = dict(re.findall(r'([a-z]+)="([^"]*)"', m.group("args")))
        # Blank lines inside the region: Markdown needs one between a comment and a table.
        want = ["", *render(m.group("kind"), args, comps, bom).split("\n"), ""]
        if m.group("kind") == "table":
            blocks_seen.add(args["block"])
            if len(want) == 4:
                problems.add("document", f"line {i + 1}: no component is in block '{args['block']}'")
        have = lines[i + 1 : j]
        if have != want and not write:
            pairs = enumerate(zip(have, want, strict=False))
            diff = next((k for k, (a, b) in pairs if a != b), min(len(have), len(want)))
            problems.add(
                "document",
                f"line {i + 2 + diff}: the {m.group('kind')} region {args or ''} differs from the netlist or BOM. "
                "Run 'designsync.py write'.",
            )
        out.append(lines[i])
        out.extend(want if write else have)
        out.append(END)
        i = j + 1
    for block in OrderedDict.fromkeys(c["block"] for c in comps):
        if block not in blocks_seen and not block.startswith("T "):
            problems.add("document", f"block '{block}' has no table in the document")
    if write:
        DOC.write_text("\n".join(out))


# ---------------------------------------------------------------- BOM


def read_bom() -> tuple[list[str], list[dict]]:
    with BOM.open(newline="") as f:
        reader = csv.DictReader(f)
        return list(reader.fieldnames or []), list(reader)


def sync_bom(comps: list[dict], problems: Problems, write: bool) -> list[dict]:
    fields, rows = read_bom()
    groups: OrderedDict[str, dict] = OrderedDict()
    for c in comps:
        if not c["lcsc"] or c["variant"] == "dnf":
            continue
        g = groups.setdefault(c["lcsc"], {"all": [], "8": [], "value": c["value"], "package": c["package"]})
        g[c["variant"]].append(c["ref"])
    by_lcsc = {}
    for r in rows:
        if r["lcsc"] in by_lcsc:
            problems.add("BOM", f"{r['lcsc']} is on two lines")
        by_lcsc[r["lcsc"]] = r
    for lcsc, g in groups.items():
        r = by_lcsc.get(lcsc)
        if r is None:
            problems.add(
                "BOM", f"{lcsc} ({g['value']}, {' '.join(g['all'] + g['8'])}) is in the netlist and has no line"
            )
            continue
        want = {
            "refs": " ".join(sorted(g["all"] + g["8"], key=natkey)),
            "qty_8zone": str(len(g["all"]) + len(g["8"])),
            "qty_4zone": str(len(g["all"])),
            "value": g["value"],
            "package": g["package"],
        }
        for key, value in want.items():
            if r[key] != value:
                if write and key in ("refs", "qty_8zone", "qty_4zone"):
                    r[key] = value
                else:
                    problems.add("BOM", f"{lcsc} {key}: line says {r[key]!r}, netlist gives {value!r}")
    variant_of = {c["ref"]: c["variant"] for c in comps}
    for r in rows:
        if r["lcsc"] in groups:
            continue
        name = r["lcsc"] or r["value"]
        tokens = r["refs"].split()
        if not any(ACCESSORY.match(t) for t in tokens):
            problems.add("BOM", f"line {name} ({r['refs']}) matches nothing in the netlist")
            continue
        q8 = q4 = 0
        owner = None
        for t in tokens:
            if not ACCESSORY.match(t):
                if owner or t not in variant_of:
                    problems.add("BOM", f"line {name}: cannot read '{r['refs']}'")
                owner = t
                continue
            variant = variant_of.get(owner, "all") if owner else "all"
            q8 += variant in ("all", "8")
            q4 += variant == "all"
            owner = None
        if (r["qty_8zone"], r["qty_4zone"]) != (str(q8), str(q4)):
            problems.add("BOM", f"line {name} ({r['refs']}): quantities should be {q8} and {q4}")
    if write:
        buf = io.StringIO(newline="")
        writer = csv.DictWriter(buf, fieldnames=fields)  # default line ending is CRLF, as the file has
        writer.writeheader()
        writer.writerows(rows)
        BOM.write_bytes(buf.getvalue().encode())
    return rows


# ---------------------------------------------------------------- pin map


def check_pinmap(comps: list[dict], problems: Problems) -> None:
    pm = yaml.safe_load(PINMAP.read_text())
    by_ref = {c["ref"]: c for c in comps}
    module = by_ref[MODULE]["pins"]
    listed: set[str] = set()

    def module_pin(gpio: int) -> str | None:
        name = {43: "TXD0", 44: "RXD0"}.get(gpio, f"IO{gpio}")
        return next((k for k in module if k.split()[-1] == name), None)

    for g in pm["gpio"]:
        key = module_pin(g["gpio"])
        if key is None:
            problems.add("pin map", f"GPIO{g['gpio']} is not a pin of {MODULE} in the netlist")
            continue
        listed.add(key)
        want = g.get("net", g["signal"])
        if module[key] != want:
            problems.add("pin map", f"GPIO{g['gpio']} is {want}, the netlist has {module[key]} on pin '{key}'")
    for key, net in module.items():
        name = key.split()[-1]
        if (name.startswith("IO") or name in ("TXD0", "RXD0")) and key not in listed and net != "NC":
            problems.add("pin map", f"module pin '{key}' carries {net} and is not in the pin map")
    en = next(k for k in module if k.split()[-1] == "EN")
    if module[en] != pm["enable_pin"].get("net", pm["enable_pin"]["signal"]):
        problems.add("pin map", f"enable_pin does not match net {module[en]}")

    expander = by_ref[EXPANDER]["pins"]
    bits = pm["io_expander"]["bits"]
    if len(bits) != 16:
        problems.add("pin map", f"io_expander lists {len(bits)} bits, expected 16")
    for b in bits:
        key = next((k for k in expander if k.split()[-1] == f"P{b['port']}{b['bit']}"), None)
        want = b.get("net", b["signal"])
        if key is None or expander[key] != want:
            got = expander.get(key) if key else None
            problems.add("pin map", f"expander P{b['port']}{b['bit']} is {want}, the netlist has {got}")


# ---------------------------------------------------------------- main


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("command", choices=("check", "write"), nargs="?", default="check")
    write = parser.parse_args().command == "write"
    problems = Problems()
    comps = load_netlist(problems)
    bom = sync_bom(comps, problems, write)
    sync_document(comps, bom, problems, write)
    check_pinmap(comps, problems)
    for item in problems.items:
        print(item)
    fitted = sum(1 for c in comps if c["variant"] != "dnf")
    print(
        f"designsync {'write' if write else 'check'}: {len(comps)} components ({fitted} fitted), "
        f"{len(bom)} BOM lines, {len(problems.items)} problem(s)"
    )
    return 1 if problems.items else 0


if __name__ == "__main__":
    sys.exit(main())
