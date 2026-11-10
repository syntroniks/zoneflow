# /// script
# requires-python = ">=3.11"
# dependencies = ["pyyaml>=6"]
# ///
"""Turn hardware/mechanical.yaml into enclosure/board_params.scad.

Run through `make` in enclosure/, or directly:

    uv run enclosure/tools/gen_params.py hardware/mechanical.yaml enclosure/board_params.scad

The script also checks the contract against itself: every part model must lie
where its x, y says, connectors must not overlap, listed parts must respect
the edge rules. It stops with a message if they do not.
"""

from __future__ import annotations

import sys
from pathlib import Path

import yaml


def fmt(v) -> str:
    """OpenSCAD literal for a Python value."""
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return f"{v:g}"
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(v, (list, tuple)):
        return "[" + ", ".join(fmt(x) for x in v) + "]"
    raise TypeError(f"cannot write {v!r} to OpenSCAD")


def part_prims(part: dict) -> list[list]:
    """Flatten a part's model into absolute boxes and cylinders.

    box -> ["box", cx, cy, sx, sy, z0, z1]; cyl -> ["cyl", cx, cy, d, 0, z0, z1].
    z values stay as heights above the mounting surface.
    """
    out = []
    for prim in part.get("model", []):
        if "box" in prim:
            dx, dy, sx, sy, z0, z1 = prim["box"]
            out.append(["box", part["x"] + dx, part["y"] + dy, sx, sy, z0, z1])
        elif "cyl" in prim:
            dx, dy, d, z0, z1 = prim["cyl"]
            out.append(["cyl", part["x"] + dx, part["y"] + dy, d, 0, z0, z1])
        else:
            raise SystemExit(f"{part['ref']}: unknown primitive {prim}")
    return out


def check(m: dict) -> list[str]:
    """Consistency checks on the contract. Returns a list of problems."""
    bad = []
    by = {p["ref"]: p for p in m["parts"]}
    xs = [pt[0] for pt in m["board"]["outline"]]
    ys = [pt[1] for pt in m["board"]["outline"]]
    w, h = max(xs), max(ys)
    face_y = m["stack"]["header_face_y"]

    # Terminal headers: length, pin 1 and mating face follow from the drawing.
    prev_end = None
    for ref in ("J1", "J2", "J3", "J4"):
        j = by[ref]
        length = j["poles"] * 3.81 + 0.8
        _, cx, cy, sx, sy, _, _ = part_prims(j)[0]
        x0, x1 = cx - sx / 2, cx + sx / 2
        if abs(sx - length) > 0.011:
            bad.append(f"{ref}: body {sx} is not poles x 3.81 + 0.8 = {length:.2f}")
        if abs((x0 + (3.81 + 0.8) / 2) - j["pin1"][0]) > 0.011:
            bad.append(f"{ref}: pin 1 x {j['pin1'][0]} does not match the body")
        if abs((cy - sy / 2) - face_y) > 0.011:
            bad.append(f"{ref}: mating face is not at header_face_y")
        if abs((face_y + 7.9) - j["pin1"][1]) > 0.011:
            bad.append(f"{ref}: pin row y {j['pin1'][1]} is not 7.9 behind the face")
        if len(j["labels"]) != j["poles"]:
            bad.append(f"{ref}: {len(j['labels'])} labels for {j['poles']} poles")
        if prev_end is not None and x0 - prev_end < 0.5:
            bad.append(f"{ref}: closer than 0.5 mm to the block on its left")
        prev_end = x1
        plug = by[f"{ref}-plug"]
        if abs(plug["x"] - j["x"]) > 0.011:
            bad.append(f"{ref}-plug: not in line with {ref}")

    # Display: outline, active area and FPC tail agree with each other.
    d = by["DS1"]
    ox, oy = d["outline_origin"]
    aw, ah = d["active_area"]
    if abs(ox + d["aa_to_edge_opposite_fpc"] + aw / 2 - d["x"]) > 0.011:
        bad.append("DS1: x is not the centre of the active area")
    if abs(oy + d["aa_to_long_edge"] + ah / 2 - d["y"]) > 0.011:
        bad.append("DS1: y is not the centre of the active area")
    tail_c = oy + 15.28 + d["fpc_tail_width"] / 2
    if abs(tail_c - d["fpc_tail_centre_y"]) > 0.011:
        bad.append("DS1: fpc_tail_centre_y does not follow from the outline")
    j7 = by["J7"]
    if abs(j7["y"] - tail_c) > 0.011:
        bad.append("J7: not centred on the FPC tail")
    if abs(j7["x"] + j7["body"][0] / 2 - j7["fpc_entry_x"]) > 0.011:
        bad.append("J7: fpc_entry_x is not the +x face of the body")
    fitted = [o for o in m["display_options"] if o["fitted"]]
    if len(fitted) != 1 or fitted[0]["outline"] != d["outline"] or fitted[0]["centre"] != [d["x"], d["y"]]:
        bad.append("display_options: the fitted option does not match DS1")
    if abs(j7["pin1_y"] - (oy + 15.28 + 0.5)) > 0.011:
        bad.append("J7: pin1_y does not follow from the outline")
    st = m["stack"]
    if abs(st["panel_front_z"] - d["thickness"] - st["panel_back_z"]) > 0.011:
        bad.append("stack: panel_back_z is not panel_front_z - thickness")

    # Antenna end is off the board; nothing metallic within the clearance.
    u4 = by["U4"]
    ax0, ay0, ax1, ay1 = u4["antenna_rect"]
    if ay0 < h - 0.011:
        bad.append("U4: antenna area overlaps the board")
    clr = m["antenna"]["clearance"]
    for hole in m["holes"]:
        if hole["kind"] != "screw":
            continue
        dx = max(ax0 - hole["x"], 0, hole["x"] - ax1)
        dy = max(ay0 - hole["y"], 0, hole["y"] - ay1)
        if (dx * dx + dy * dy) ** 0.5 < clr:
            bad.append(f"hole {hole['id']}: screw within {clr} mm of the antenna")

    # Fuse holders stand inside their well, clear of its 1.2 mm walls.
    well = next(z for z in m["zones"] if z["id"] == "T_FUSEWELL")["rect"]
    for ref in ("F1", "F4"):
        _, cx, cy, sx, sy, _, _ = part_prims(by[ref])[0]
        if (cx - sx / 2 < well[0] + 1.6 or cx + sx / 2 > well[2] - 1.6
                or cy - sy / 2 < well[1] + 1.6 or cy + sy / 2 > well[3] - 1.6):
            bad.append(f"{ref}: closer than 0.4 mm to the wall of the fuse well")
    a, b = (part_prims(by[r])[0] for r in ("F4", "F1"))
    if (b[1] - b[3] / 2) - (a[1] + a[3] / 2) < 1.0:
        bad.append("F1 and F4: pads closer than 1.0 mm")

    # C1 lies on the bottom side with its pads beyond the end of the can.
    c1 = by["C1"]
    can = part_prims(c1)[0]
    for px, py in c1["pads"]:
        if not (px > can[1] + can[3] / 2 + 2.0 and abs(py - c1["y"]) == 2.5):
            bad.append("C1: pads do not sit 5.0 mm apart beyond the end of the can")
    if m["assembly"]["through_hole"][2]["refs"] != "C1" or c1["side"] != "bottom":
        bad.append("C1: assembly section and part disagree")

    # Everything on the board stays on the board, except edge connectors.
    edge_ok = {"J1", "J2", "J3", "J4", "J5", "J6", "U4"}
    for p in m["parts"]:
        if p.get("group") == "mate" or p["ref"] in edge_ok or p["ref"].startswith("DS1"):
            continue
        for prim in part_prims(p):
            _, cx, cy, sx, sy, _, _ = prim
            ry = sy / 2 if prim[0] == "box" else sx / 2
            if cx - sx / 2 < 0 or cx + sx / 2 > w or cy - ry < 0 or cy + ry > h:
                bad.append(f"{p['ref']}: model leaves the board outline")
    for z in m["zones"]:
        x0, y0, x1, y1 = z["rect"]
        if not (0 <= x0 < x1 <= w and 0 <= y0 < y1 <= h):
            bad.append(f"zone {z['id']}: rectangle is not on the board")
    return bad


def main() -> None:
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    m = yaml.safe_load(src.read_text())
    if m.get("schema") != 1:
        raise SystemExit("mechanical.yaml: unknown schema")
    problems = check(m)
    if problems:
        raise SystemExit("mechanical.yaml is inconsistent:\n  " + "\n  ".join(problems))

    by = {p["ref"]: p for p in m["parts"]}
    b, st = m["board"], m["stack"]
    xs = [pt[0] for pt in b["outline"]]
    ys = [pt[1] for pt in b["outline"]]
    d, enc, usb, qw, u4 = by["DS1"], by["SW3"], by["J5"], by["J6"], by["U4"]
    well = next(z for z in m["zones"] if z["id"] == "T_FUSEWELL")["rect"]
    c1 = by["C1"]
    leds = [p for p in m["parts"] if "label" in p]

    prims = []
    for p in m["parts"]:
        group = p.get("group", "board")
        if p["ref"].startswith("DS1"):
            group = "display"
        for prim in part_prims(p):
            prims.append([p["ref"], group, p["side"], *prim])

    v = {
        "board_outline": b["outline"],
        "board_w": max(xs) - min(xs),
        "board_h": max(ys) - min(ys),
        "board_t": b["thickness"],
        "board_edge_keepout": b["edge_keepout"],
        "z_fascia_outer": st["fascia_outer_z"],
        "z_fascia_inner": st["fascia_inner_z"],
        "z_cover_inner": st["cover_inner_z"],
        "bottom_clearance": st["bottom_clearance"],
        "z_panel_front": st["panel_front_z"],
        "z_panel_back": st["panel_back_z"],
        "panel_pad": st["panel_pad"],
        "deck_t": st["deck_thickness"],
        "header_face_y": st["header_face_y"],
        "top_gap": st["top_gap"],
        # id, x, y, hole diameter, kind, top keep-out, bottom keep-out
        "holes": [[h["id"], h["x"], h["y"], h["d"], h["kind"],
                   h["top_keepout"], h["bottom_keepout"]] for h in m["holes"]],
        "disp_window_margin": d["window_margin"],
        # id, active x, active y, outline x, outline y, active area to the
        # edge opposite the FPC, to each long edge, thickness, centre x,
        # centre y, FPC root from and to (measured from the outline's y origin)
        "panel_options": [[o["id"], *o["active_area"], *o["outline"],
                           o["aa_to_edge_opposite_fpc"], o["aa_to_long_edge"], o["thickness"],
                           *o["centre"], *o["fpc_root_y_from_origin"]]
                          for o in m["display_options"]],
        "panel_fitted": next(o["id"] for o in m["display_options"] if o["fitted"]),
        "fuse_well": well,
        "fuse_c": [[by[r]["x"], by[r]["y"]] for r in ("F4", "F1")],
        "c1_c": [c1["x"], c1["y"]],
        "c1_can": [part_prims(c1)[0][3], part_prims(c1)[0][4]],
        "j7_c": [by["J7"]["x"], by["J7"]["y"]],
        "j7_entry_x": by["J7"]["fpc_entry_x"],
        "enc_c": [enc["x"], enc["y"]],
        "enc_shaft_top_z": enc["shaft_top_z"],
        "enc_bushing_d": enc["bushing_d"],
        "enc_bushing_top_z": enc["bushing_top_z"],
        "enc_shaft_d": enc["shaft_d"],
        "enc_flat_across": enc["shaft_flat_across"],
        "enc_flat_len": enc["shaft_flat_length"],
        "enc_travel": enc["push_travel"],
        "btn_c": [[by[r]["x"], by[r]["y"]] for r in ("SW4", "SW5")],
        "btn_top_z": by["SW4"]["actuator_top_z"],
        "btn_travel": by["SW4"]["travel"],
        "btn_act_d": 3.5,
        "led_c": [[p["x"], p["y"]] for p in leds],
        "led_label": [p["label"] for p in leds],
        "led_h": 0.8,
        # ref, x0, x1 of each header body
        "terminals": [[r, by[r]["x"] - (by[r]["poles"] * 3.81 + 0.8) / 2,
                       by[r]["x"] + (by[r]["poles"] * 3.81 + 0.8) / 2]
                      for r in ("J1", "J2", "J3", "J4")],
        "terminal_labels": [lab for r in ("J1", "J2", "J3", "J4") for lab in by[r]["labels"]],
        "terminal_pin_x": [by[r]["pin1"][0] + 3.81 * i
                           for r in ("J1", "J2", "J3", "J4") for i in range(by[r]["poles"])],
        "header_h": 7.0,
        "plug_top_z": 11.6,
        "plug_len": 8.9,
        "usb_x": usb["x"],
        "usb_z": usb["opening_centre_z"],
        "qwiic_x": qw["x"],
        "antenna_rect": u4["antenna_rect"],
        "antenna_clearance": m["antenna"]["clearance"],
        # ref, group, side, type, cx, cy, sx or d, sy, z0, z1
        "part_prims": prims,
        # id, side, x0, y0, x1, y1, max height
        "zones": [[z["id"], z["side"], *z["rect"], z["max_height"]] for z in m["zones"]],
    }

    lines = [
        "// GENERATED from hardware/mechanical.yaml by enclosure/tools/gen_params.py.",
        "// Do not edit. Change the YAML and run `make` in enclosure/.",
        "",
    ]
    for name, val in v.items():
        if isinstance(val, list) and val and isinstance(val[0], list) and len(val) > 3:
            lines.append(f"{name} = [")
            lines += [f"    {fmt(row)}," for row in val]
            lines.append("];")
        else:
            lines.append(f"{name} = {fmt(val)};")
    dst.write_text("\n".join(lines) + "\n")
    print(f"{dst}: {len(v)} values, {len(prims)} model primitives, contract consistent")


if __name__ == "__main__":
    main()
