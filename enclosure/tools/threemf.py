"""Write 3MF files and print plates from STL files, the same bytes every time.

    python3 tools/threemf.py single part.stl part.3mf --title "ZoneFlow base"
    python3 tools/threemf.py binary ascii.stl binary.stl
    python3 tools/threemf.py plates plates.toml output
    python3 tools/threemf.py scenes plates.toml output build/plates

OpenSCAD's own 3MF export stamps each file with the time and fresh random
identifiers, so a rebuild changed every committed 3MF although no triangle
had moved. This writes the mesh of the STL and nothing that varies.

`plates` reads plates.toml: for each plate, the parts to print together. It
lays them out in rows on the bed, each already in print orientation, and
writes one 3MF with one object per part. It fails if a plate does not fit
its bed with the margin and the gap plates.toml gives. A plate is plain 3MF:
positions only, no slicer settings.

`scenes` writes one OpenSCAD file per plate that shows the same layout on
its bed, with the bed outline and the margin drawn, for a picture from above.

Only the standard library is used.
"""

from __future__ import annotations

import argparse
import struct
import sys
import tomllib
import zipfile
from pathlib import Path

Tri = tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]]

CONTENT_TYPES = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">\n'
    ' <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>\n'
    ' <Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/>\n'
    '</Types>\n'
)
RELS = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">\n'
    ' <Relationship Target="/3D/3dmodel.model" Id="rel0" '
    'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/>\n'
    '</Relationships>\n'
)


def read_stl(path: Path) -> list[Tri]:
    data = Path(path).read_bytes()
    if data[:5] == b"solid" and b"facet" in data[:1000]:
        tris, cur = [], []
        for line in data.decode("ascii", "replace").splitlines():
            parts = line.split()
            if parts[:1] == ["vertex"]:
                cur.append(tuple(float(v) for v in parts[1:4]))
                if len(cur) == 3:
                    tris.append(tuple(cur))
                    cur = []
        return tris
    (n,) = struct.unpack_from("<I", data, 80)
    tris = []
    for i in range(n):
        v = struct.unpack_from("<12f", data, 84 + 50 * i)
        tris.append((v[3:6], v[6:9], v[9:12]))
    return tris


def write_stl(path: Path, tris: list[Tri], header: str = "") -> None:
    """Binary STL. Normals are left zero: every reader derives them from the winding."""
    out = bytearray(header.encode("ascii", "replace")[:80].ljust(80, b" "))
    out += struct.pack("<I", len(tris))
    for a, b, c in tris:
        out += struct.pack("<12fH", 0.0, 0.0, 0.0, *a, *b, *c, 0)
    Path(path).write_bytes(bytes(out))


def bounds(tris: list[Tri]) -> tuple[list[float], list[float]]:
    lo = [min(p[k] for t in tris for p in t) for k in range(3)]
    hi = [max(p[k] for t in tris for p in t) for k in range(3)]
    return lo, hi


def turned(tris: list[Tri], quarter_turns: int) -> tuple[list[Tri], tuple[float, float, float]]:
    """Turn about z by quarter turns and put the lowest corner back on the origin.

    Returns the triangles and the shift that put the corner back.
    """
    q = quarter_turns % 4
    rot = [lambda p: p, lambda p: (-p[1], p[0], p[2]), lambda p: (-p[0], -p[1], p[2]), lambda p: (p[1], -p[0], p[2])][q]
    out = [tuple(rot(p) for p in t) for t in tris]
    lo, _ = bounds(out)
    return [tuple((p[0] - lo[0], p[1] - lo[1], p[2] - lo[2]) for p in t) for t in out], (-lo[0], -lo[1], -lo[2])


def _num(v: float) -> str:
    s = f"{v:.6f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def _esc(s: str) -> str:
    return s.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;").replace(">", "&gt;")


def model_xml(objects: list[tuple[str, list[Tri], tuple[float, float, float]]], title: str, description: str) -> str:
    """objects: name, triangles, and where the object's origin goes on the bed."""
    out = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">',
        f' <metadata name="Title">{_esc(title)}</metadata>',
        f' <metadata name="Description">{_esc(description)}</metadata>',
        ' <metadata name="Application">ZoneFlow enclosure/tools/threemf.py</metadata>',
        " <resources>",
    ]
    for i, (name, tris, _) in enumerate(objects, start=1):
        index: dict[tuple[float, float, float], int] = {}
        faces = []
        for t in tris:
            f = tuple(index.setdefault(tuple(p), len(index)) for p in t)
            if len(set(f)) == 3:
                faces.append(f)
        out.append(f'  <object id="{i}" type="model" name="{_esc(name)}">')
        out.append("   <mesh>")
        out.append("    <vertices>")
        out += [f'     <vertex x="{_num(x)}" y="{_num(y)}" z="{_num(z)}"/>' for x, y, z in index]
        out.append("    </vertices>")
        out.append("    <triangles>")
        out += [f'     <triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in faces]
        out.append("    </triangles>")
        out.append("   </mesh>")
        out.append("  </object>")
    out.append(" </resources>")
    out.append(" <build>")
    for i, (_, _, at) in enumerate(objects, start=1):
        out.append(f'  <item objectid="{i}" transform="1 0 0 0 1 0 0 0 1 {_num(at[0])} {_num(at[1])} {_num(at[2])}"/>')
    out.append(" </build>")
    out.append("</model>")
    return "\n".join(out) + "\n"


def write_3mf(path: Path, objects, title: str, description: str = "") -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w") as z:
        for name, text in (("[Content_Types].xml", CONTENT_TYPES), ("_rels/.rels", RELS),
                           ("3D/3dmodel.model", model_xml(objects, title, description))):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, text.encode("utf-8"), compresslevel=9)


def bed_of(cfg: dict, plate: dict) -> tuple[list[float], float, float]:
    """Bed size, edge margin and gap of a plate: its own, or the defaults of the file."""
    return plate.get("bed", cfg["bed"]), plate.get("margin", cfg["margin"]), plate.get("gap", cfg["gap"])


def layout(plate: dict, bed: list[float], margin: float, gap: float, root: Path) -> list[dict]:
    """Rows from the front of the bed to the back, tallest footprint first. Fails if the plate is full."""
    items = []
    for part in plate["parts"]:
        quarter = int(part.get("turn", 0)) // 90
        tris, shift = turned(read_stl(root / f"{part['file']}.stl"), quarter)
        lo, hi = bounds(tris)
        for n in range(int(part.get("count", 1))):
            name = Path(part["file"]).name + (f" ({n + 1})" if part.get("count", 1) > 1 else "")
            items.append({"name": name, "file": part["file"], "tris": tris, "size": [hi[k] - lo[k] for k in range(3)],
                          "quarter": quarter, "shift": shift})
    order = sorted(range(len(items)), key=lambda i: (-items[i]["size"][1], -items[i]["size"][0], i))
    x = y = margin
    row_h = 0.0
    for i in order:
        w, h = items[i]["size"][0], items[i]["size"][1]
        if x > margin and x + w > bed[0] - margin + 1e-6:
            x, y, row_h = margin, y + row_h + gap, 0.0
        if x + w > bed[0] - margin + 1e-6 or y + h > bed[1] - margin + 1e-6:
            raise SystemExit(f"plate {plate['name']}: {items[i]['name']} ({w:.1f} x {h:.1f} mm) does not fit the "
                             f"{bed[0]:g} x {bed[1]:g} mm bed with a {margin:g} mm margin")
        items[i]["at"] = (x, y, 0.0)
        x += w + gap
        row_h = max(row_h, h)
    return items


def load_plates(config: Path) -> dict:
    with open(config, "rb") as fh:
        return tomllib.load(fh)


def cmd_plates(args) -> int:
    cfg = load_plates(args.config)
    root = Path(args.root)
    for plate in cfg["plate"]:
        bed, margin, gap = bed_of(cfg, plate)
        items = layout(plate, bed, margin, gap, root)
        dest = root / f"{plate['name']}.3mf"
        write_3mf(dest, [(it["name"], it["tris"], it["at"]) for it in items], plate["title"],
                  f"{plate['title']}. Positions only, for a {bed[0]:g} x {bed[1]:g} mm bed. "
                  "No slicer settings. Nothing has been printed by the project.")
        print(f"{dest}: {len(items)} objects")
    return 0


SCENE = """// GENERATED by enclosure/tools/threemf.py from plates.toml. Do not edit.
// {title}
// The middle of the bed is at the origin, so that one camera suits every bed.
use <{fonts}/LiberationSans-Bold.ttf>
bed = [{bx:g}, {by:g}];
margin = {margin:g};
module frame(inset, w) difference() {{
    translate([inset, inset]) square([bed[0] - 2 * inset, bed[1] - 2 * inset]);
    translate([inset + w, inset + w]) square([bed[0] - 2 * inset - 2 * w, bed[1] - 2 * inset - 2 * w]);
}}
translate([-bed[0] / 2, -bed[1] / 2, 0]) {{
// The bed, a line every 50 mm, its edge, and the margin the layout keeps free.
color("#ffffff") translate([0, 0, -1.2]) cube([bed[0], bed[1], 1]);
color("#dcdfe4") translate([0, 0, -0.2]) linear_extrude(0.1) {{
    for (x = [50 : 50 : bed[0] - 1]) translate([x - 0.2, 0]) square([0.4, bed[1]]);
    for (y = [50 : 50 : bed[1] - 1]) translate([0, y - 0.2]) square([bed[0], 0.4]);
}}
color("#30343a") translate([0, 0, -0.2]) linear_extrude(0.2) frame(0, 1.2);
color("#d2542c") translate([0, 0, -0.2]) linear_extrude(0.2) frame(margin, 0.7);
color("#30343a") translate([0, -9, -0.2]) linear_extrude(0.2)
    text("{label}", size = 5, font = "Liberation Sans:style=Bold");
color("#30343a") translate([0, -17, -0.2]) linear_extrude(0.2)
    text("{name}", size = 5, font = "Liberation Sans:style=Bold");
"""


def cmd_scenes(args) -> int:
    cfg = load_plates(args.config)
    root, dest_dir = Path(args.root).resolve(), Path(args.dest).resolve()
    dest_dir.mkdir(parents=True, exist_ok=True)
    fonts = Path(__file__).resolve().parent.parent / "fonts"
    for plate in cfg["plate"]:
        bed, margin, gap = bed_of(cfg, plate)
        items = layout(plate, bed, margin, gap, root)
        label = f"{bed[0]:g} x {bed[1]:g} mm bed.  Red line: {margin:g} mm from the edge.  Parts {gap:g} mm apart."
        out = [SCENE.format(title=plate["title"], fonts=fonts.as_posix(), bx=bed[0], by=bed[1], margin=margin, label=label,
                            name=Path(plate["name"]).name + ".3mf")]
        for it in items:
            x, y, z = (it["at"][k] + it["shift"][k] for k in range(3))
            out.append(f'color("{plate.get("color", "#8a9097")}") translate([{_num(x)}, {_num(y)}, {_num(z)}]) '
                       f'rotate([0, 0, {90 * it["quarter"]}]) import("{(root / it["file"]).as_posix()}.stl");\n')
        out.append("}\n")
        dest = dest_dir / f"{Path(plate['name']).name}.scad"
        dest.write_text("".join(out), encoding="utf-8")
        print(f"{dest}: {len(items)} objects")
    return 0


def cmd_single(args) -> int:
    write_3mf(args.dest, [(Path(args.src).stem, read_stl(args.src), (0.0, 0.0, 0.0))], args.title or Path(args.src).stem,
              args.description)
    return 0


def cmd_binary(args) -> int:
    write_stl(args.dest, read_stl(args.src), args.header)
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("single", help="one STL to one 3MF")
    s.add_argument("src", type=Path)
    s.add_argument("dest", type=Path)
    s.add_argument("--title", default="")
    s.add_argument("--description", default="")
    s.set_defaults(fn=cmd_single)
    s = sub.add_parser("binary", help="rewrite an STL as binary STL")
    s.add_argument("src", type=Path)
    s.add_argument("dest", type=Path)
    s.add_argument("--header", default="")
    s.set_defaults(fn=cmd_binary)
    s = sub.add_parser("plates", help="one 3MF per plate of plates.toml")
    s.add_argument("config", type=Path)
    s.add_argument("root", type=Path, help="directory the part files and the plates are in")
    s.set_defaults(fn=cmd_plates)
    s = sub.add_parser("scenes", help="one OpenSCAD scene per plate of plates.toml, with the bed drawn")
    s.add_argument("config", type=Path)
    s.add_argument("root", type=Path, help="directory the part files are in")
    s.add_argument("dest", type=Path, help="directory for the .scad files")
    s.set_defaults(fn=cmd_scenes)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
