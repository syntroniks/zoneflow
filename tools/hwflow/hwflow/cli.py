# SPDX-License-Identifier: Apache-2.0
"""Command line for the schematic half of the flow (runs without pcbnew)."""

from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

from . import committed, netlist, parity, project, schlint, schwriter
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

    if getattr(design, "capture", None) is not None:
        # The drawing came from a design netlist. KiCad's netlist must equal it, pin for pin.
        rc = check_parity(design, out, build, root, nl, net_file)
        if rc:
            return rc

    classes = netlist.net_classes(flat, nl)
    project.project_file(design, out, board_cfg, fab, classes)
    seen: set[str] = set()
    model = {
        "name": design.name,
        # One entry per reference. A multi-unit part is listed under its lowest-numbered unit.
        "parts": [
            {"ref": p.ref, "local_ref": p.part.ref, "sheet": p.part.sheet.name, "sheet_path": p.sheet_path,
             "footprint": p.part.footprint, "value": p.part.value, "dnp": p.part.dnp}
            for p in sorted(flat.parts, key=lambda fp: fp.part.unit)
            if not p.part.virtual and p.part.on_board and not (p.ref in seen or seen.add(p.ref))
        ],
        "netclass": classes,
        "nets": len([n for n in nl.nets.values() if len(n) >= 2]),
        "footprint_dirs": [str(Path(d).resolve().relative_to(out.resolve())) for d in design.footprint_dirs],
        "variants": {name: sorted(v["dnp"]) for name, v in design.variants.items()},
    }
    (build / "model.json").write_text(json.dumps(model, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    real = {p.ref for p in flat.parts if not p.part.virtual}
    print(f"schematic: {len(design.sheets)} sheet files, {len(real)} parts, "
          f"{model['nets']} nets; KiCad's netlist matches the circuit description")
    return 0


def check_parity(design: Design, out: Path, build: Path, root: Path, nl, net_file: Path) -> int:
    """KiCad's netlist and BOM exports against the design netlist the capture was drawn from."""
    cap = design.capture
    problems = parity.compare(cap.exp, nl, cap.extra_refs)
    fields = "Reference,Value,Footprint,LCSC,Spec"
    builds = {"": set(cap.base_fitted)}  # the base design: no KiCad variant
    builds.update({name: set(v["fitted"]) for name, v in design.variants.items() if "fitted" in v})
    for name, fitted in builds.items():
        dest = build / f"bom-check-{name or 'base'}.csv"
        cmd = ["kicad-cli", "sch", "export", "bom", "--fields", fields, "--labels", fields, "--exclude-dnp",
               "--ref-range-delimiter", "", "-o", str(dest)]
        if name:
            cmd += ["--variant", name]
        run(cmd + [str(root)])
        with open(dest, newline="", encoding="utf-8") as fh:
            rows = list(csv.DictReader(fh))
        problems += parity.compare_bom(cap.exp, rows, fitted, f"build {name or 'base'}")
    if problems:
        sys.stderr.write(f"the schematic does not match the design netlist ({len(problems)} differences):\n")
        for p in problems[:80]:
            sys.stderr.write(f"  {p}\n")
        if len(problems) > 80:
            sys.stderr.write(f"  ... and {len(problems) - 80} more\n")
        return 1
    # KiCad's own netlist, without the build machine's path and the time, is committed so that
    # a checker without KiCad can repeat the comparison.
    dest = out / "output" / f"{design.name}.net"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(parity.normalise_netlist(net_file.read_text(encoding="utf-8")), encoding="utf-8")
    print(f"parity: {len(cap.exp.comps)} components and {len(cap.exp.nets)} nets of the design netlist "
          f"match KiCad's netlist pin for pin; fitted parts match in {len(builds)} build(s)")
    return 0


def cmd_lint(args) -> int:
    """Plot every sheet and look for text and symbols that collide."""
    design_dir, out = Path(args.design_dir), Path(args.project_dir)
    design = load_design(design_dir)
    design.flatten()
    svg_dir = out / "build" / "lint-svg"
    if svg_dir.exists():
        for f in svg_dir.glob("*.svg"):
            f.unlink()
    run(["kicad-cli", "sch", "export", "svg", "-o", str(svg_dir), str(out / f"{design.name}.kicad_sch")])
    problems = schlint.lint(design, svg_dir)
    for p in problems:
        sys.stderr.write(f"  {p}\n")
    if problems:
        sys.stderr.write(f"schematic lint: {len(problems)} collision(s)\n")
        return 1
    print(f"schematic lint: {len(list(svg_dir.glob('*.svg')))} plotted pages, no text or symbol collides")
    return 0


def cmd_committed(args) -> int:
    """The committed netlist and BOMs of a board against the design netlist. Needs no KiCad."""
    import yaml

    comps = yaml.safe_load(Path(args.netlist).read_text(encoding="utf-8"))["components"]
    problems, summary = committed.check(Path(args.project_dir), comps)
    for p in problems[:80]:
        sys.stderr.write(f"  {p}\n")
    if problems:
        sys.stderr.write(f"committed outputs differ from the design netlist: {len(problems)} difference(s). "
                         f"Run make in {args.project_dir} and commit the result.\n")
        return 1
    print(summary)
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="hwflow")
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("committed", help="check a board's committed netlist and BOMs against the design netlist")
    s.add_argument("project_dir")
    s.add_argument("--netlist", required=True, help="the design netlist (YAML)")
    s.set_defaults(fn=cmd_committed)
    s = sub.add_parser("lint", help="plot the schematic and fail on overlapping text or symbols")
    s.add_argument("design_dir")
    s.add_argument("project_dir")
    s.set_defaults(fn=cmd_lint)
    s = sub.add_parser("sch", help="generate the schematic and project files, then verify the netlist")
    s.add_argument("design_dir")
    s.add_argument("project_dir")
    s.add_argument("--fab-rules", required=True, help="fab limits TOML; the .kicad_dru beside it is used too")
    s.add_argument("--no-normalise", action="store_true")
    s.set_defaults(fn=cmd_sch)
    args = ap.parse_args(argv)
    try:
        return args.fn(args)
    except (DesignError, parity.ParityError) as e:
        sys.stderr.write(f"design error: {e}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
