# SPDX-License-Identifier: Apache-2.0
"""Command line for the schematic half of the flow (runs without pcbnew)."""

from __future__ import annotations

import argparse
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

from . import netlist, project, schwriter
from .design import Design, DesignError


def load_design(design_dir: Path) -> Design:
    src = design_dir / "circuit.py"
    spec = importlib.util.spec_from_file_location("circuit", src)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(design_dir))
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.path.pop(0)
    return mod.build()


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"command failed: {' '.join(cmd)}")
    return r


def cmd_sch(args) -> int:
    design_dir, out = Path(args.design_dir), Path(args.project_dir)
    build = out / "build"
    build.mkdir(parents=True, exist_ok=True)
    design = load_design(design_dir)
    board_cfg = project.load_toml(design_dir / "board.toml")
    fab = project.load_toml(Path(args.fab_rules))
    design.netclass_priority = [n for n in board_cfg.get("netclass", {}) if n != "Default"]

    flat = schwriter.write(design, out)
    footprints = {p.part.footprint for p in flat.parts if p.part.footprint}
    project.lib_tables(design, out, footprints)
    rule_files = [Path(args.fab_rules).with_suffix(".kicad_dru")]
    extra = design_dir / "rules.kicad_dru"
    if extra.exists():
        rule_files.append(extra)
    project.drc_rules(design, out, rule_files)
    project.project_file(design, out, board_cfg, fab, {})

    root = out / f"{design.name}.kicad_sch"
    if not args.no_normalise:
        # Let KiCad rewrite every sheet in its own formatting, so that opening
        # and saving the project in the GUI later gives a small diff.
        for sheet in design.sheets.values():
            run(["kicad-cli", "sch", "upgrade", "--force", str(out / f"{sheet.name}.kicad_sch")])

    net_file = build / f"{design.name}.net"
    run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadsexpr", "-o", str(net_file), str(root)])
    nl = netlist.read(net_file)
    problems = netlist.compare(flat, nl)
    if problems:
        sys.stderr.write("schematic does not match the circuit description:\n")
        for p in problems:
            sys.stderr.write(f"  {p}\n")
        return 1

    classes = netlist.net_classes(flat, nl)
    project.project_file(design, out, board_cfg, fab, classes)
    model = {
        "name": design.name,
        "parts": [
            {"ref": p.ref, "local_ref": p.part.ref, "sheet": p.part.sheet.name, "sheet_path": p.sheet_path,
             "footprint": p.part.footprint, "value": p.part.value, "dnp": p.part.dnp}
            for p in flat.parts if not p.part.virtual
        ],
        "netclass": classes,
        "nets": len([n for n in nl.nets.values() if len(n) >= 2]),
    }
    (build / "model.json").write_text(json.dumps(model, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    real = [p for p in flat.parts if not p.part.virtual]
    print(f"schematic: {len(design.sheets)} sheet files, {len(real)} parts, "
          f"{model['nets']} nets; KiCad's netlist matches the circuit description")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hwflow")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sch", help="generate the schematic and project files, then verify the netlist")
    s.add_argument("design_dir")
    s.add_argument("project_dir")
    s.add_argument("--fab-rules", required=True, help="fab limits TOML; the .kicad_dru beside it is used too")
    s.add_argument("--no-normalise", action="store_true")
    s.set_defaults(fn=cmd_sch)
    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except DesignError as e:
        sys.stderr.write(f"design error: {e}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
