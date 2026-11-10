# SPDX-License-Identifier: Apache-2.0
"""Check the committed outputs of a board against its design netlist, without KiCad.

A board directory commits what KiCad extracted from the generated schematic:
``output/<name>.net`` (the netlist) and one JLCPCB BOM per build. This module
compares them with ``hardware/netlist.yaml`` through the part table, so that
a machine with only Python can tell when the design netlist, the part table
or the schematic outputs have moved apart. It cannot tell whether the
committed ``.kicad_sch`` files are what the sources generate today: that
needs KiCad, and is what ``make`` in the board directory checks.

Only the standard library is used. The caller parses the YAML.
"""

from __future__ import annotations

import csv
import tomllib
from pathlib import Path

from . import netlist, parity

BASE = "base"


def load_table(path: Path) -> dict:
    with open(path, "rb") as fh:
        return tomllib.load(fh)


def builds(table: dict) -> dict[str, set[str]]:
    """Build name -> the design-netlist variants it fits. ``base`` is the design without a KiCad variant."""
    raw = table.get("builds") or {BASE: ["all", "8"]}
    if BASE not in raw:
        raise parity.ParityError(f"the part table's [builds] has no {BASE!r} entry")
    return {name: set(fitted) for name, fitted in raw.items()}


def bom_file(project_dir: Path, name: str, build: str) -> Path:
    suffix = "" if build == BASE else f"-{build}"
    return project_dir / "output" / f"{name}-bom-jlcpcb{suffix}.csv"


def compare_jlc_bom(exp: parity.Expected, rows: list[dict], fitted: set[str], what: str) -> list[str]:
    """One JLCPCB BOM (Comment, Designator, Footprint, LCSC Part #, Quantity) against the design netlist."""
    problems: list[str] = []
    want = {r: e for r, e in exp.comps.items() if e["comp"]["variant"] in fitted and e["spec"].in_bom}
    seen: dict[str, dict] = {}
    for row in rows:
        refs = [r for r in row["Designator"].split(",") if r]
        if len(refs) != int(row["Quantity"]):
            problems.append(f"{what}: line {row['LCSC Part #']} lists {len(refs)} designators and quantity "
                            f"{row['Quantity']}")
        for r in refs:
            if r in seen:
                problems.append(f"{what}: {r} is on two lines")
            seen[r] = row
    for r in sorted(set(want) - set(seen)):
        problems.append(f"{what}: {r} should be fitted and is not in the BOM")
    for r in sorted(set(seen) - set(want)):
        problems.append(f"{what}: {r} is in the BOM and should not be fitted")
    for ref in sorted(set(want) & set(seen)):
        spec, c, row = want[ref]["spec"], want[ref]["comp"], seen[ref]
        if row["LCSC Part #"] != (c["lcsc"] or ""):
            problems.append(f"{what}: {ref} has LCSC number {row['LCSC Part #']!r}, the design netlist "
                            f"{c['lcsc']!r}")
        if row["Footprint"] != spec.footprint.split(":")[-1]:
            problems.append(f"{what}: {ref} has footprint {row['Footprint']!r}, the part table "
                            f"{spec.footprint.split(':')[-1]!r}")
        if spec.value not in row["Comment"].split(" / "):
            problems.append(f"{what}: {ref} has value {row['Comment']!r}, the part table {spec.value!r}")
    return problems


def check(project_dir: Path, components: list[dict]) -> tuple[list[str], str]:
    """Differences between the committed outputs and the design netlist, and a one-line summary."""
    project_dir = Path(project_dir)
    pros = sorted(project_dir.glob("*.kicad_pro"))
    if len(pros) != 1:
        raise parity.ParityError(f"{project_dir} has {len(pros)} KiCad project files, expected one")
    name = pros[0].stem
    table_path = project_dir / "design" / "parts.toml"
    table = load_table(table_path)
    exp = parity.expected(components, parity.load_parts(table_path))
    mechanical = table.get("mechanical", {})

    net_file = project_dir / "output" / f"{name}.net"
    if not net_file.exists():
        return [f"{net_file} is missing: run make in {project_dir}"], ""
    nl = netlist.read(net_file)
    problems = parity.compare(exp, nl, set(mechanical))
    for ref, m in sorted(mechanical.items()):
        k = nl.comps.get(ref)
        if k and k.footprint != m["footprint"]:
            problems.append(f"{ref} footprint: the schematic has {k.footprint!r}, the part table {m['footprint']!r}")

    all_builds = builds(table)
    for build, fitted in all_builds.items():
        path = bom_file(project_dir, name, build)
        if not path.exists():
            problems.append(f"{path} is missing: run make in {project_dir}")
            continue
        with open(path, newline="", encoding="utf-8") as fh:
            problems += compare_jlc_bom(exp, list(csv.DictReader(fh)), fitted, f"BOM of build {build}")
    summary = (f"{name}: the committed netlist has the {len(exp.comps)} components and {len(exp.nets)} nets of "
               f"the design netlist, pin for pin; {len(all_builds)} BOM(s) list the fitted parts")
    return problems, summary
