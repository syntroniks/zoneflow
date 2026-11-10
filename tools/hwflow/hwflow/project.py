# SPDX-License-Identifier: Apache-2.0
"""Write the files that surround the schematic: project file, library tables, DRC rules."""

from __future__ import annotations

import json
import tomllib
from pathlib import Path

from .design import Design

DEFAULT_NETCLASS = {
    "bus_width": 12,
    "clearance": 0.2,
    "diff_pair_gap": 0.25,
    "diff_pair_via_gap": 0.25,
    "diff_pair_width": 0.2,
    "line_style": 0,
    "microvia_diameter": 0.3,
    "microvia_drill": 0.1,
    "name": "Default",
    "pcb_color": "rgba(0, 0, 0, 0.000)",
    "priority": 2147483647,
    "schematic_color": "rgba(0, 0, 0, 0.000)",
    "track_width": 0.2,
    "tuning_profile": "",
    "via_diameter": 0.6,
    "via_drill": 0.3,
    "wire_width": 6,
}


def lib_tables(design: Design, out_dir: Path, footprints: set[str]) -> None:
    """Project-local library tables listing only the libraries the design uses.

    The build must not depend on the user's global KiCad configuration, and a
    fresh checkout has none.
    """
    sym_libs = sorted({p.lib_id.split(":")[0] for s in design.sheets.values() for p in s.parts.values()}
                      | {n.symbol.split(":")[0] for s in design.sheets.values()
                         for n in s.nets.values() if n.kind == "power"})
    lines = ["(sym_lib_table", "\t(version 7)"]
    for lib in sym_libs:
        local = [d for d in design.symbol_dirs if (Path(d) / f"{lib}.kicad_sym").exists()]
        if local:
            uri = f"${{KIPRJMOD}}/{Path(local[0]).name}/{lib}.kicad_sym"
        else:
            uri = f"${{KICAD10_SYMBOL_DIR}}/{lib}.kicad_sym"
        lines.append(f'\t(lib (name "{lib}") (type "KiCad") (uri "{uri}") (options "") (descr ""))')
    lines.append(")")
    (out_dir / "sym-lib-table").write_text("\n".join(lines) + "\n", encoding="utf-8")

    fp_libs = sorted({f.split(":")[0] for f in footprints if ":" in f})
    lines = ["(fp_lib_table", "\t(version 7)"]
    for lib in fp_libs:
        uri = f"${{KICAD10_FOOTPRINT_DIR}}/{lib}.pretty"
        lines.append(f'\t(lib (name "{lib}") (type "KiCad") (uri "{uri}") (options "") (descr ""))')
    lines.append(")")
    (out_dir / "fp-lib-table").write_text("\n".join(lines) + "\n", encoding="utf-8")


def project_file(design: Design, out_dir: Path, board_cfg: dict, fab_rules: dict,
                 netclass_of: dict[str, str]) -> None:
    """Write ``<name>.kicad_pro``: fab minimums, net classes and net-to-class assignments."""
    classes = []
    names = list(board_cfg.get("netclass", {}))
    for i, name in enumerate(names):
        cfg = board_cfg["netclass"][name]
        c = dict(DEFAULT_NETCLASS)
        c["name"] = name
        c["track_width"] = cfg["track_width"]
        c["clearance"] = cfg["clearance"]
        c["via_diameter"] = cfg["via_diameter"]
        c["via_drill"] = cfg["via_drill"]
        c["priority"] = 2147483647 if name == "Default" else i
        classes.append(c)
    if "Default" not in names:
        classes.insert(0, dict(DEFAULT_NETCLASS))
    b = fab_rules["board"]
    rules = {
        "min_clearance": b["min_clearance"],
        "min_track_width": b["min_track_width"],
        "min_through_hole_diameter": b["min_through_hole_diameter"],
        "min_via_diameter": b["min_via_diameter"],
        "min_via_annular_width": b["min_via_annular_width"],
        "min_hole_to_hole": b["min_hole_to_hole"],
        "min_hole_clearance": b["min_hole_clearance"],
        "min_copper_edge_clearance": b["min_copper_edge_clearance"],
        "min_text_height": b["min_text_height"],
        "min_text_thickness": b["min_text_thickness"],
        "min_silk_clearance": b["min_silk_clearance"],
    }
    pro = {
        "board": {
            "design_settings": {
                "rules": rules,
                "rule_severities": dict(board_cfg.get("drc_severity", {})),
            }
        },
        "erc": {"rule_severities": dict(board_cfg.get("erc_severity", {}))},
        "meta": {"filename": f"{design.name}.kicad_pro", "version": 3},
        "net_settings": {
            "classes": classes,
            "meta": {"version": 5},
            "net_colors": {},
            "netclass_assignments": {},
            "netclass_patterns": [
                {"netclass": cls, "pattern": net} for net, cls in sorted(netclass_of.items())
            ],
        },
        "sheets": [],
        "text_variables": {},
    }
    (out_dir / f"{design.name}.kicad_pro").write_text(json.dumps(pro, indent=2, sort_keys=True) + "\n",
                                                     encoding="utf-8")


def drc_rules(design: Design, out_dir: Path, rule_files: list[Path]) -> None:
    """Concatenate DRC rule files into ``<name>.kicad_dru`` (one ``(version 1)`` header)."""
    out = ["(version 1)", ""]
    for f in rule_files:
        body = [ln for ln in Path(f).read_text(encoding="utf-8").splitlines() if ln.strip() != "(version 1)"]
        out.append(f"# ---- from {Path(f).name} ----")
        out += body
        out.append("")
    (out_dir / f"{design.name}.kicad_dru").write_text("\n".join(out).rstrip() + "\n", encoding="utf-8")


def load_toml(path: Path) -> dict:
    with open(path, "rb") as fh:
        return tomllib.load(fh)
