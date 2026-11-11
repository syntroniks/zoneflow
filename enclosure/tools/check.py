# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "numpy==2.5.4",
#     "manifold3d==3.5.4",
#     "trimesh==5.2.0",
#     "lxml==6.1.3",
#     "networkx==3.7",
# ]
# ///
"""Interference, clearance and print-file check for the ZoneFlow enclosure.

    uv run tools/check.py --openscad openscad --out output/check-report.md

Run through `make` in enclosure/. It does five things and fails the build if
any of them fails.

1. Interference between modelled solids. For every pair in PAIRS, OpenSCAD
   intersects the two solids in their assembled position and this script
   measures what is left. A pair passes under LIMIT. The pairs cover the
   contract's placeholder board, the printable test-fit kit (board, C1, K1,
   plug pieces, display panel) and the soldered lead ends, each against every
   printed part.
2. Interference with the board as KiCad models it: every body of the
   detailed export against every printed part.
3. Clearances at the features a person checks by hand in a dry fit. Each is
   measured on the exported meshes, not computed from the parameters, and
   compared with the value the test-fit guide tells the reader to expect.
4. Every print file: closed mesh, one body, sits on the bed, fits the bed.
5. Every 3MF and every plate, read back with a second library.

What this proves: the modelled solids do not overlap, and the modelled gaps
are the stated ones. Nothing here has been printed, and no board exists.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import re
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

import manifold3d as m3
import numpy as np
import trimesh

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE / "tools"))
import threemf  # noqa: E402

LIMIT = 0.5  # mm3. Faces that only touch leave slivers below this.
BED = (220.0, 220.0)
TF = "test-fit"
DETAIL = f"{TF}/view-only/zoneflow-a-board-detailed-view-only"

PRINTED = ["base", "fascia", "cover", "bridge", "knob", "buttons", "lightpipe"]
PAIRS: list[tuple[str, str]] = (
    # The placeholder board against every printed part.
    [("board", p) for p in PRINTED]
    # The display panel and its folded FPC.
    + [("display", p) for p in PRINTED]
    # Mated terminal plugs.
    + [("plugs", p) for p in ("base", "fascia", "cover")]
    # The free volume that the height zones promise to the layout engineer.
    + [("zones", p) for p in PRINTED]
    # Printed parts against each other.
    + [("base", p) for p in PRINTED[1:]]
    + [("fascia", p) for p in PRINTED[2:]]
    + [("cover", p) for p in PRINTED[3:]]
    + [("bridge", p) for p in PRINTED[4:]]
    + [("knob", "buttons"), ("knob", "lightpipe"), ("buttons", "lightpipe")]
)
# The test-fit kit as it is printed, in its assembled position. `tfboard` is
# the board with C1 and K1 in their slots, `tfplugs` the four plug pieces,
# `tfpanel` the display panel without its cable, `tails` the lead ends that
# are not printed.
TF_PAIRS: list[tuple[str, str]] = (
    [("tfboard", p) for p in PRINTED]
    + [("tfplugs", p) for p in PRINTED]
    + [("tfpanel", p) for p in PRINTED]
    + [("tails", p) for p in PRINTED]
    + [("tfmain", "tfaddons"), ("tfboard", "tfplugs"), ("tfboard", "tfpanel"), ("tfboard", "display")]
)
# The same checks that matter for the larger panel option. The board model
# is laid out for the fitted panel, so only the display itself, the carrier
# and the fascia are tested against each other and against the free zones.
VARIANT = "2.4"
VARIANT_PAIRS: list[tuple[str, str]] = [
    ("display", "fascia"), ("display", "bridge"), ("display", "buttons"), ("display", "base"),
    ("display", "knob"), ("display", "lightpipe"), ("fascia", "bridge"), ("fascia", "buttons"),
    ("bridge", "buttons"), ("bridge", "base"), ("bridge", "knob"), ("bridge", "lightpipe"),
    ("zones", "fascia"), ("zones", "bridge"),
]
# The knob's bore has a flat and the placeholder shaft is a plain cylinder, so
# these two overlap by the missing flat. Anything larger is a real clash. The
# test-fit board's shaft has the flat, and nothing is allowed there.
EXPECTED = {("board", "knob"): 36.5}
# The contract's volume for the folded display cable reaches 0.2 mm into the
# connector it plugs into. Anything larger is a part standing in the cable's way.
TF_EXPECTED = {("tfboard", "display"): 2.0}

# Print files: name of the OpenSCAD part, file under output/, quantity.
PRINTS = [
    ("base", "zoneflow-base", 1), ("fascia", "zoneflow-fascia", 1), ("cover", "zoneflow-cover", 1),
    ("bridge", "zoneflow-bridge", 1), ("knob", "zoneflow-knob", 1), ("button_back", "zoneflow-button-back", 1),
    ("button_home", "zoneflow-button-home", 1), ("lightpipe", "zoneflow-lightpipe", 1),
]
TF_PRINTS = [
    ("tf_board", f"{TF}/zoneflow-testfit-board", 1), ("tf_c1", f"{TF}/zoneflow-testfit-c1", 1),
    ("tf_k1", f"{TF}/zoneflow-testfit-k1", 1), ("tf_plug_2p", f"{TF}/zoneflow-testfit-plug-2p", 1),
    ("tf_plug_4p", f"{TF}/zoneflow-testfit-plug-4p", 1), ("tf_plug_7p", f"{TF}/zoneflow-testfit-plug-7p", 2),
    ("tf_panel", f"{TF}/zoneflow-testfit-panel", 1),
]
PETG_DENSITY = 1.27  # g/cm3

# Tolerances assumed in the clearance table, millimetres. One term for all the
# printed surfaces in a loop taken together; this is an assumption about a
# tuned FDM printer, not a measurement.
TOL = {
    "print": (0.2, "the printed parts in the loop, taken together (assumed)"),
    "thickness": (0.16, "board thickness, `board.thickness_tol` of the contract"),
    "outline": (0.2, "board outline, `board.outline_tol` of the contract"),
    "place": (0.1, "a reflowed part on its lands (assumed)"),
    "hand": (0.2, "a hand-soldered through-hole part in its holes (assumed)"),
    "panel": (0.2, "display outline, `DS1.outline` of the contract"),
    "panel_t": (0.05, "display thickness, `DS1.thickness` of the contract"),
}
MATCH = 0.015   # mm. A measured value may differ from the expected one by this much.
# USB-C plug overmold: 12.35 x 6.5 mm. `unconfirmed.usb_overmold` of the contract, which
# says it is from memory of the USB Type-C specification. Not in the kit; try a real cable.
USB_OVERMOLD = (12.35, 6.5)
M3_SCREW = 3.0


# ---- Meshes ---------------------------------------------------------------------
def stl_arrays(path: Path) -> np.ndarray:
    """Triangles of a binary STL as an (n, 3, 3) array."""
    data = Path(path).read_bytes()
    if data[:5] == b"solid" and b"facet" in data[:1000]:
        return np.array(threemf.read_stl(path), dtype=np.float64).reshape(-1, 3, 3)
    n = int(np.frombuffer(data, dtype="<u4", count=1, offset=80)[0])
    rec = np.frombuffer(data, dtype=np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")]), count=n, offset=84)
    return rec["v"].astype(np.float64)


def indexed(tris: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    verts, inv = np.unique(tris.reshape(-1, 3), axis=0, return_inverse=True)
    faces = inv.reshape(-1, 3)
    keep = (faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & (faces[:, 0] != faces[:, 2])
    return verts, faces[keep]


def measure(tris: np.ndarray) -> tuple[float, list[float], list[float]]:
    """Volume by the divergence theorem, and the bounding box."""
    if len(tris) == 0:
        return 0.0, [0, 0, 0], [0, 0, 0]
    a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
    vol = float(np.einsum("ij,ij->i", a, np.cross(b, c)).sum() / 6.0)
    pts = tris.reshape(-1, 3)
    return abs(vol), pts.min(axis=0).tolist(), pts.max(axis=0).tolist()


def components(faces: np.ndarray, n_verts: int) -> np.ndarray:
    """Label of the connected body each face belongs to (faces joined by a shared vertex)."""
    parent = np.arange(n_verts)

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for a, b, c in faces.tolist():
        ra, rb, rc = find(a), find(b), find(c)
        if rb != ra:
            parent[rb] = ra
        if rc != ra:
            parent[find(rc)] = ra
    roots = np.array([find(i) for i in faces[:, 0].tolist()])
    _, labels = np.unique(roots, return_inverse=True)
    return labels


def closed(faces: np.ndarray) -> bool:
    """Every edge is used once in each direction: a closed, consistently wound surface."""
    e = np.concatenate([faces[:, [0, 1]], faces[:, [1, 2]], faces[:, [2, 0]]])
    if len(np.unique(e, axis=0)) != len(e):
        return False
    return np.array_equal(np.unique(e, axis=0), np.unique(e[:, ::-1], axis=0))


def manifold(verts: np.ndarray, faces: np.ndarray) -> m3.Manifold | None:
    m = m3.Manifold(m3.Mesh(vert_properties=verts.astype(np.float32), tri_verts=faces.astype(np.uint32)))
    return m if m.status() == m3.Error.NoError and not m.is_empty() else None


def load(path: Path) -> m3.Manifold:
    verts, faces = indexed(stl_arrays(path))
    m = manifold(verts, faces)
    if m is None:
        raise SystemExit(f"{path}: not a closed solid")
    return m


def cube(lo, hi) -> m3.Manifold:
    return m3.Manifold.cube([hi[k] - lo[k] for k in range(3)]).translate(list(lo))


def clip(m: m3.Manifold, lo, hi) -> m3.Manifold:
    return m ^ cube(lo, hi)


def ext(m: m3.Manifold):
    """Bounding box as (lo, hi), or None for nothing."""
    if m.is_empty():
        return None
    b = m.bounding_box()
    return list(b[:3]), list(b[3:])


# ---- OpenSCAD ---------------------------------------------------------------------
def run(openscad: str, defs: dict[str, str], out: Path) -> bool:
    """Run OpenSCAD. Returns False when the result is empty."""
    cmd = [openscad, "--backend=manifold", "-o", str(out), "--export-format", "binstl"]
    for k, v in defs.items():
        cmd += ["-D", f'{k}="{v}"']
    cmd.append(str(HERE / "zoneflow.scad"))
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        if "top level object is empty" in r.stderr or "empty" in r.stderr.lower():
            return False
        raise SystemExit(f"OpenSCAD failed for {defs}:\n{r.stderr}")
    return out.exists() and out.stat().st_size > 84


def scad_vars(path: Path) -> dict:
    """The `name = value;` lines of a generated .scad file. Values are numbers, strings and lists."""
    text = re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))
    out = {}
    for name, value in re.findall(r"^(\w+)\s*=\s*(.*?);", text, flags=re.S | re.M):
        out[name] = json.loads(re.sub(r",\s*\]", "]", value))
    return out


# ---- Clearances ----------------------------------------------------------------------
class Probe:
    """Measurements on the solids in their assembled position."""

    def __init__(self, solids: dict[str, m3.Manifold]):
        self.s = solids

    def part(self, name: str, lo=None, hi=None) -> m3.Manifold:
        return self.s[name] if lo is None else clip(self.s[name], lo, hi)

    @staticmethod
    def axis(a: m3.Manifold, b: m3.Manifold, axis: int, b_side: int) -> float | None:
        """Gap along one axis between a and b, with b on the low (-1) or the high (+1) side of a."""
        ea, eb = ext(a), ext(b)
        if ea is None or eb is None:
            return None
        return ea[0][axis] - eb[1][axis] if b_side < 0 else eb[0][axis] - ea[1][axis]

    @staticmethod
    def near(a: m3.Manifold, b: m3.Manifold, search: float = 8.0) -> float | None:
        """Smallest distance between two solids, None if further apart than `search`."""
        if a.is_empty() or b.is_empty():
            return None
        g = a.min_gap(b, search)
        return None if g >= search - 1e-9 else g

    def hole(self, name: str, lo, hi):
        """Bounding box of the empty space of a solid inside a box: a hole, a bore, a window."""
        return ext(cube(lo, hi) - self.s[name])

    @staticmethod
    def offset(ea, eb, axes=(0, 1)) -> float | None:
        if ea is None or eb is None:
            return None
        return math.hypot(*[(ea[0][k] + ea[1][k]) / 2 - (eb[0][k] + eb[1][k]) / 2 for k in axes])


def clearances(s: dict[str, m3.Manifold], bp: dict, tf: dict) -> tuple[list[dict], list[dict]]:
    """The gaps and the alignments a person checks in the dry fit.

    Returns two lists of rows. Every row has what was measured on the meshes
    and what the guide says to expect; the caller compares them.
    """
    P = Probe(s)
    t = tf["tf_thickness"]
    zF, zFi = bp["z_fascia_outer"], bp["z_fascia_inner"]
    W, H = bp["board_w"], bp["board_h"]
    prims: dict[str, list] = {}
    for p in tf["tf_prims"]:
        prims.setdefault(p[0], []).append(p)

    def pbox(p, grow=0.0):
        """Box of a stand-in primitive in assembled coordinates."""
        sx, sy = (p[5], p[6]) if p[2] == "box" else (p[5], p[5])
        z0, z1 = (p[7], p[8]) if p[1] == "top" else (-t - p[8], -t - p[7])
        return ([p[3] - sx / 2 - grow, p[4] - sy / 2 - grow, z0 - grow], [p[3] + sx / 2 + grow, p[4] + sy / 2 + grow, z1 + grow])

    def standin(ref, i=0, solid="tfmain"):
        lo, hi = pbox(prims[ref][i], 0.001)
        return clip(s[solid], lo, hi)

    gaps: list[dict] = []
    aligns: list[dict] = []

    def gap(section, what, got, want, tol=(), kind="gap", note=""):
        gaps.append({"section": section, "what": what, "got": got, "want": want, "tol": list(tol), "kind": kind, "note": note})

    def align(section, what, got, want=0.0, note=""):
        aligns.append({"section": section, "what": what, "got": got, "want": want, "note": note})

    slab = clip(s["tfmain"], [-1, -1, -t], [W + 1, H + 1, 0])
    holes = {h[0]: h for h in bp["holes"]}

    # ---- Board in the base -----------------------------------------------------
    sec = "Board in the base"
    gap(sec, "Left board edge to the side wall", P.axis(slab, P.part("base", [-4, 45, -t], [0, 75, 0]), 0, -1), 1.1,
        ("outline", "print"), note="`pcb_gap`")
    gap(sec, "Right board edge to the side wall", P.axis(slab, P.part("base", [W, 45, -t], [W + 4, 75, 0]), 0, +1), 1.1,
        ("outline", "print"), note="`pcb_gap`")
    gap(sec, "Bottom board edge to the partition", P.axis(slab, P.part("base", [30, -2.5, -t], [50, 0, -0.4]), 1, -1), 0.4,
        ("outline", "print"), note="`part_gap`")
    gap(sec, "Fascia lip above the board edge, left",
        P.axis(slab, P.part("fascia", [-1.5, 45, 0], [1.5, 75, 3]), 2, +1), 0.6, ("print", "thickness"), note="`lip_z0`")
    for h in bp["holes"]:
        if h[4] == "screw":
            x, y = h[1], h[2]
            bh = P.hole("tfmain", [x - 1.9, y - 1.9, -t + 0.6], [x + 1.9, y + 1.9, -0.2])
            boss = P.hole("base", [x - 2.4, y - 2.4, -t - 4], [x + 2.4, y + 2.4, -t - 1])
            align(sec, f"Board hole {h[0]} to the insert hole of its boss", P.offset(bh, boss))
            gap(sec, f"M3 screw in board hole {h[0]}, all round", (bh[1][0] - bh[0][0] - M3_SCREW) / 2 if bh else None, 0.2,
                ("print",), kind="play", note="3.4 mm hole. This play is what lines the board up")
            gap(sec, f"Board underside on boss {h[0]}",
                P.axis(slab, P.part("base", [x - 3, y - 3, -t - 3], [x + 3, y + 3, -t + 0.5]), 2, -1), 0.0, kind="contact")
        if h[4] == "post":
            gap(sec, f"Board underside on post {h[0]}, under the encoder",
                P.axis(slab, P.part("base", [h[1] - 2, h[2] - 2, -t - 3], [h[1] + 2, h[2] + 2, -t + 0.5]), 2, -1), 0.0,
                kind="contact")

    # ---- Bottom side -------------------------------------------------------------
    sec = "Bottom side"
    c1 = clip(s["tfaddons"], *pbox(prims["C1"][0], 0.001))
    k1 = clip(s["tfaddons"], *pbox(prims["K1"][0], 0.001))
    c1b, k1b = pbox(prims["C1"][0]), pbox(prims["K1"][0])
    gap(sec, "C1 to the two ribs under it",
        P.axis(c1, P.part("base", [c1b[0][0], c1b[0][1], -t - 19], [c1b[1][0], c1b[1][1], c1b[0][2] - 0.01]), 2, -1), 0.3,
        ("print",), note="`c1_rib_gap`. The ribs carry the can once the board is in")
    gap(sec, "K1 to the back wall",
        P.axis(k1, P.part("base", [k1b[0][0], k1b[0][1], -t - 20], [k1b[1][0], k1b[1][1], k1b[0][2] - 0.01]), 2, -1), 2.7,
        ("print",))
    gap(sec, "K1 to the left side wall", P.axis(k1, P.part("base", [-4, k1b[0][1], -16], [k1b[0][0] - 0.01, k1b[1][1], -4]), 0, -1),
        2.3, ("print", "hand"))
    gap(sec, "K1 to the boss of hole C", P.near(k1, P.part("base", [-0.5, 6.5, -t - 15], [9.5, k1b[0][1] - 0.01, -t])), 1.6,
        ("print", "hand"))
    best = None
    for tail in tf["tf_tails"]:
        z0 = 0.0 if tail[4] == "top" else -t - tf["tf_tail_len"]
        one = clip(s["tails"], [tail[1] - 1.5, tail[2] - 1.5, z0 - 0.01], [tail[1] + 1.5, tail[2] + 1.5, z0 + tf["tf_tail_len"] + 0.01])
        for name in PRINTED:
            g = P.near(one, s[name], 6.0)
            if g is not None and (best is None or g < best[0]):
                best = (g, tail, name)
    gap(sec, f"Nearest soldered lead end to a printed part: {best[1][0]} at ({best[1][1]:g}, {best[1][2]:g}), "
             f"{best[1][4]} side, to the {best[2]}", best[0], 0.49, ("print", "hand"),
        note="A K1 pin that is soldered on the top side, beside the corner of the fuse well. Lead ends are 2.5 mm long in "
             "the model, which is assumed")

    # ---- Terminal headers and plugs ------------------------------------------------
    sec = "Terminals"
    hdr = tf["tf_headers"]
    for i, h in enumerate(hdr):
        ref, x0, x1, yf, yb, hh, _ = h
        nose = clip(s["tfplugs"], [x0 - 0.001, yf - 0.001, 0], [x1 + 0.001, 0, hh + 0.001])
        block = clip(s["tfmain"], [x0 - 0.001, 0, 0.001], [x1 + 0.001, yb + 0.001, hh + 0.001])
        left = P.axis(nose, P.part("fascia", [x0 - 3.4, yf, 0.4], [x0 - 0.001, -0.5, hh]), 0, -1)
        right = P.axis(nose, P.part("fascia", [x1 + 0.001, yf, 0.4], [x1 + 3.4, -0.5, hh]), 0, +1)
        open_left = i > 0 and x0 - hdr[i - 1][2] < 0.8
        open_right = i + 1 < len(hdr) and hdr[i + 1][1] - x1 < 0.8
        gap(sec, f"{ref} to the left edge of its opening", left, None if open_left else 0.4, ("print", "hand"),
            note="No wall here: one opening with the block beside it" if open_left else "`comp_clear`")
        gap(sec, f"{ref} to the right edge of its opening", right, None if open_right else 0.4, ("print", "hand"),
            note="No wall here: one opening with the block beside it" if open_right else "`comp_clear`")
        gap(sec, f"{ref} to the top of its opening", P.axis(nose, P.part("fascia", [x0, yf, hh], [x1, -0.5, hh + 2]), 2, +1), 0.4,
            ("print", "thickness"), note="`comp_clear`")
        gap(sec, f"{ref} underside to the top of the base partition",
            P.axis(nose, P.part("base", [x0, yf, -3], [x1, -0.3, 0]), 2, -1), 0.3, ("print", "thickness"),
            note="A thin board sits lower on its bosses and closes this")
        part_face = ext(P.part("base", [x0, yf - 2, -3], [x1, 0, -0.4]))
        align(sec, f"{ref} mating face to the wiring side of the partition",
              abs(ext(nose)[0][1] - part_face[0][1]) if part_face else None, note="Flush by design")
        align(sec, f"{ref} block on the board to its plug piece, side to side", P.offset(ext(block), ext(nose), axes=(0,)))
    plugs = tf["tf_plugs"]
    for (pref, px0, px1, py0, py1, pz0, pz1), h in zip(plugs, hdr):
        body = clip(s["tfplugs"], [px0 - 0.001, py0 - 0.001, pz0], [px1 + 0.001, py1 + 0.001, pz1 + 0.001])
        if pref == "J2-plug":
            gap(sec, "Plug top to the top wall of the wiring cover",
                P.axis(body, P.part("cover", [px0, py1 - 2.6, pz1 - 0.5], [px1, py1 - 0.6, pz1 + 3]), 2, +1), 0.3,
                ("print", "thickness"), note="Plug height is unconfirmed in the contract (`plug_height`)")
            gap(sec, "Plug top to the inside of the wiring cover",
                P.axis(body, P.part("cover", [px0, py0, pz1 - 0.5], [px1, py1 - 2.7, pz1 + 3]), 2, +1), 0.9,
                ("print", "thickness"))
            gap(sec, "Plug shoulder against the wiring side of the fascia's partition",
                P.axis(body, P.part("fascia", [px0 + 2, py1, h[5] + 0.5], [px1 - 2, 0, pz1]), 1, +1), 0.0, kind="contact",
                note="Touching by design: nothing spare if the partition prints thick")
    for x, (a, b) in zip(bp_hooks(s, hdr), ((0, 1), (2, 3))):
        pa = clip(s["tfplugs"], [plugs[a][1], plugs[a][3], 0], [plugs[a][2], plugs[a][4], 12])
        pb = clip(s["tfplugs"], [plugs[b][1], plugs[b][3], 0], [plugs[b][2], plugs[b][4], 12])
        hook = P.part("fascia", [plugs[a][2] + 0.01, -4.6, -5], [plugs[b][1] - 0.01, -2.05, 8])
        gap(sec, f"{plugs[a][0]} to the fascia hook on its right", P.axis(pa, hook, 0, +1), 0.5, ("print", "hand"))
        gap(sec, f"{plugs[b][0]} to the fascia hook on its left", P.axis(pb, hook, 0, -1), 0.5, ("print", "hand"))

    # ---- USB-C and Qwiic --------------------------------------------------------------
    sec = "USB-C and Qwiic"
    u = tf["tf_usb"]
    usb = standin("J5")
    usb_z = u[4] / 2
    gap(sec, "USB-C receptacle to the left edge of its opening",
        P.axis(usb, P.part("fascia", [u[0] - 4, -2, 0.4], [u[0], -0.5, u[4]]), 0, -1), 2.03, ("print", "place"))
    gap(sec, "USB-C receptacle to the top of its opening",
        P.axis(usb, P.part("fascia", [u[0], -2, u[4]], [u[1], -0.5, u[4] + 4]), 2, +1), 2.07, ("print", "thickness"))
    ow, oh = USB_OVERMOLD
    cx = (u[0] + u[1]) / 2
    mold = cube([cx - ow / 2, -9, usb_z - oh / 2], [cx + ow / 2, -0.5, usb_z + oh / 2])
    gap(sec, "USB-C plug overmold to the left edge of the opening",
        P.axis(mold, P.part("fascia", [cx - ow / 2 - 3, -2, 0.4], [cx - ow / 2, -0.5, 5]), 0, -1), 0.33, ("print", "place"),
        note="Overmold 12.35 x 6.5 mm from the contract, unconfirmed. Not in the kit: try a real cable")
    gap(sec, "USB-C plug overmold to the top of the opening",
        P.axis(mold, P.part("fascia", [cx - ow / 2, -2, usb_z + oh / 2], [cx + ow / 2, -0.5, 9]), 2, +1), 0.45,
        ("print", "thickness"), note="As above")
    gap(sec, "USB-C plug overmold to the cut in the base partition below it",
        P.axis(mold, P.part("base", [cx - ow / 2, -2, -6], [cx + ow / 2, -0.5, usb_z - oh / 2]), 2, -1), 0.45,
        ("print", "thickness"), note="As above")
    gap(sec, "USB-C plug overmold to the 24 VAC plug", P.axis(mold, clip(s["tfplugs"], [plugs[0][1], -11, 0], [plugs[0][2], -2, 12]), 0, +1),
        1.33, ("hand", "place"), note="As above")
    gap(sec, "USB-C receptacle to the 24 VAC header on its right",
        P.axis(usb, clip(s["tfmain"], [hdr[0][1] - 0.001, 0, 0.001], [hdr[0][2], hdr[0][4], hdr[0][5]]), 0, +1), 3.03,
        ("hand", "place"), note="No wall between them: the opening runs on into the one of J1")
    q = standin("J6")
    qb = pbox(prims["J6"][0])
    gap(sec, "Qwiic connector to the left edge of its opening",
        P.axis(q, P.part("fascia", [qb[0][0] - 3, -2, 0.4], [qb[0][0], -0.5, 4]), 0, -1), 0.75, ("print", "place"))
    gap(sec, "Qwiic connector to the right edge of its opening",
        P.axis(q, P.part("fascia", [qb[1][0], -2, 0.4], [qb[1][0] + 3, -0.5, 4]), 0, +1), 0.75, ("print", "place"))
    gap(sec, "Qwiic connector to the top of its opening",
        P.axis(q, P.part("fascia", [qb[0][0], -2, qb[1][2]], [qb[1][0], -0.5, qb[1][2] + 4]), 2, +1), 1.45, ("print", "thickness"))

    # ---- Fuse well -----------------------------------------------------------------
    sec = "Fuse well"
    fw = bp["fuse_well"]
    f4, f1 = standin("F4"), standin("F1")
    f4b, f1b = pbox(prims["F4"][0]), pbox(prims["F1"][0])
    gap(sec, "Fuse holder F4 to the left wall of the well",
        P.axis(f4, P.part("fascia", [fw[0], f4b[0][1], 0.3], [f4b[0][0] - 0.01, f4b[1][1], 3.5]), 0, -1), 0.49, ("print", "place"),
        note="The stand-in is as long as the land pattern, 10.62 mm; the holder body is 9.73 mm")
    gap(sec, "Fuse holder F1 to the right wall of the well",
        P.axis(f1, P.part("fascia", [f1b[1][0] + 0.01, f1b[0][1], 0.3], [fw[2], f1b[1][1], 3.5]), 0, +1), 0.49, ("print", "place"),
        note="As above")
    gap(sec, "Fuse holders to the lower wall of the well",
        P.axis(f4, P.part("fascia", [f4b[0][0], fw[1], 0.3], [f4b[1][0], f4b[0][1] - 0.01, 3.5]), 1, -1), 1.385, ("print", "place"))
    gap(sec, "Fuse holders to the upper wall of the well",
        P.axis(f4, P.part("fascia", [f4b[0][0], f4b[1][1] + 0.01, 0.3], [f4b[1][0], fw[3], 3.5]), 1, +1), 1.585, ("print", "place"))
    gap(sec, "Walls of the well to the board",
        P.axis(slab, P.part("fascia", [fw[0], fw[1], 0], [fw[2], fw[3], 2]), 2, +1), 0.3, ("print", "thickness"),
        note="A thick board sits higher and closes this")
    flap = P.part("cover", [fw[0], fw[1], zF], [fw[2], fw[3] + 6, zF + 3])
    gap(sec, "Wiring cover flap to the face of the fascia",
        P.axis(P.part("fascia", [fw[0], fw[3] - 1.1, zF - 1], [fw[2], fw[3] + 6, zF + 0.001]), flap, 2, +1), 0.15, ("print",),
        kind="fit", note="The flap is meant to lie on the fascia")
    align(sec, "Wiring cover flap past the upper wall of the well", ext(flap)[1][1] - fw[3] if ext(flap) else None, 2.0,
          note="An overlap, not an offset: how far the flap reaches beyond the opening")

    # ---- Display ----------------------------------------------------------------------
    sec = "Display"
    opt = next(o for o in bp["panel_options"] if o[0] == bp["panel_fitted"])
    aw, ah, ow_, oh_, ex, ey, _, cxd, cyd = opt[1:10]
    ax0, ay0, ax1, ay1 = cxd - aw / 2, cyd - ah / 2, cxd + aw / 2, cyd + ah / 2
    dx0, dy0 = ax0 - ex, ay0 - ey
    dx1, dy1 = dx0 + ow_, dy0 + oh_
    pan = s["tfpanel"]
    zb, zf = bp["z_panel_back"], bp["z_panel_front"]
    gap(sec, "Panel to the locating rib on the left",
        P.axis(pan, P.part("fascia", [dx0 - 2.5, cyd - 9, 9.5], [dx0 - 0.01, cyd + 9, zFi]), 0, -1), 0.4, ("print", "panel"),
        note="`display_fit`")
    gap(sec, "Panel to the locating ribs on the right, the cable side",
        P.axis(pan, P.part("fascia", [dx1 + 0.01, dy0, 9.5], [dx1 + 2.5, dy1, zFi]), 0, +1), 0.4, ("print", "panel"),
        note="`display_fit`")
    gap(sec, "Panel to the locating rib below",
        P.axis(pan, P.part("fascia", [dx0, dy0 - 2.5, 9.5], [dx1, dy0 - 0.01, zFi]), 1, -1), 0.4, ("print", "panel"),
        note="`display_fit`")
    gap(sec, "Panel to the locating rib above",
        P.axis(pan, P.part("fascia", [dx0, dy1 + 0.01, 9.5], [dx1, dy1 + 2.5, zFi]), 1, +1), 0.4, ("print", "panel"),
        note="`display_fit`")
    gap(sec, "Glass to the bezel lip in front of it",
        P.axis(pan, P.part("fascia", [dx0 + 0.3, cyd - 5, zf], [ax0 - 0.8, cyd + 5, zF]), 2, +1), 0.5,
        ("print", "thickness", "panel_t"), note="`display_gap`. Nothing clamps the glass")
    gap(sec, "Panel back to the carrier deck", P.axis(pan, P.part("bridge", [dx0 + 5, dy0 + 5, 6], [dx1 - 12, dy1 - 5, zb]), 2, -1),
        0.1, ("print", "panel_t"), kind="fit", note="`panel_pad`: the tape")
    win = P.hole("fascia", [ax0 - 6, ay0 - 6, zF - 1.0], [ax1 + 6, ay1 + 6, zF - 0.999])
    for name, got in (("left", ax0 - win[0][0]), ("right", win[1][0] - ax1), ("bottom", ay0 - win[0][1]), ("top", win[1][1] - ay1)):
        gap(sec, f"Window edge outside the active area, {name}", got, 0.6, ("print", "panel"),
            note="`window_margin`. The groove on the panel stand-in fills this strip")
    align(sec, "Middle of the window to the middle of the active area",
          P.offset(win, ([ax0, ay0, 0], [ax1, ay1, 0])))
    gap(sec, "Display connector J7 to the underside of the carrier deck, beside the cable opening",
        P.axis(standin("J7"), P.part("bridge", [dx0, dy0, 3], [dx1, dy1, zb]), 2, +1), 5.65, ("print",))
    for hid in ("A", "B"):
        h = holes[hid]
        bh = P.hole("tfmain", [h[1] - 1.9, h[2] - 1.9, -t + 0.6], [h[1] + 1.9, h[2] + 1.9, -0.2])
        leg = P.hole("bridge", [h[1] - 1.9, h[2] - 1.9, 1], [h[1] + 1.9, h[2] + 1.9, 3])
        align(sec, f"Carrier leg at hole {hid} to the board hole", P.offset(leg, bh))
    for hid in ("E", "F"):
        h = holes[hid]
        gap(sec, f"Carrier foot {hid} on the board",
            P.axis(slab, P.part("bridge", [h[1] - 3, h[2] - 3, -0.5], [h[1] + 3, h[2] + 3, 2]), 2, +1), 0.0, kind="contact")

    # ---- Buttons -------------------------------------------------------------------------
    sec = "Buttons"
    for i, (name, ref) in enumerate((("Back", "SW4"), ("Home", "SW5"))):
        cxb, cyb = bp["btn_c"][i]
        act = clip(s["tfmain"], [cxb - 1.8, cyb - 1.8, 8.05], [cxb + 1.8, cyb + 1.8, 9.95])
        cap = P.part("buttons", [cxb - 12, cyb - 7, 7], [cxb + 12, cyb + 7, 15])
        gap(sec, f"{name} button: actuator in the socket of its cap, all round",
            P.near(act, clip(cap, [cxb - 4, cyb - 4, 8.05], [cxb + 4, cyb + 4, 9.95])), 0.4, ("print", "place"), kind="play",
            note="`btn_socket_d` 4.3 on a 3.5 mm actuator: loose by design")
        sock = P.hole("buttons", [cxb - 1, cyb - 1, 8.5], [cxb + 1, cyb + 1, 11.5])
        top = ext(clip(s["tfmain"], [cxb - 1.8, cyb - 1.8, 5], [cxb + 1.8, cyb + 1.8, 11]))
        gap(sec, f"{name} button: cap resting on the actuator", sock[1][2] - top[1][2] if sock and top else None, 0.0,
            kind="contact")
        gap(sec, f"{name} button: cap flange to the inside of the fascia",
            P.axis(clip(cap, [cxb - 10.5, cyb - 2, 7.9], [cxb - 8.4, cyb + 2, 10.4]),
                   P.part("fascia", [cxb - 10.5, cyb - 2, 10.0], [cxb - 8.4, cyb + 2, zF]), 2, +1), 0.6,
            ("print", "thickness"), note="`btn_flange_gap`: the cap never loads the switch")
        gap(sec, f"{name} button: cap in its hole, all round",
            P.near(clip(cap, [cxb - 12, cyb - 7, 11], [cxb + 12, cyb + 7, 12]),
                   P.part("fascia", [cxb - 12, cyb - 7, 11], [cxb + 12, cyb + 7, 12])), 0.25, ("print",),
            kind="fit", note="`fit`: two printed parts that slide")
        align(sec, f"{name} button: actuator to the socket of its cap", P.offset(ext(act), sock))

    # ---- Encoder and knob --------------------------------------------------------------------
    sec = "Encoder and knob"
    ex_, ey_ = bp["enc_c"]
    shaft_top, bush_top = bp["enc_shaft_top_z"], bp["enc_bushing_top_z"]
    shaft = clip(s["tfmain"], [ex_ - 3.2, ey_ - 3.2, bush_top + 3.5], [ex_ + 3.2, ey_ + 3.2, shaft_top - 0.5])
    knob = s["knob"]
    gap(sec, "Encoder shaft in the bore of the knob, all round and at the flat",
        P.near(shaft, clip(knob, [ex_ - 5.4, ey_ - 5.4, bush_top + 3.5], [ex_ + 5.4, ey_ + 5.4, shaft_top - 0.5])), 0.1,
        ("print",), kind="fit", note="`knob_bore_fit`: meant as a push fit")
    bore = P.hole("knob", [ex_ - 2, ey_ - 2, shaft_top - 4], [ex_ + 2, ey_ + 1, shaft_top + 1.8])
    gap(sec, "Knob seated on the end of the shaft", bore[1][2] - shaft_top if bore else None, 0.0, kind="contact",
        note="The roof of the knob closes the bore at the shaft's end, so the shaft sets the knob's height")
    bush = clip(s["tfmain"], [ex_ - 4.6, ey_ - 4.6, zFi], [ex_ + 4.6, ey_ + 4.6, zF])
    gap(sec, "Encoder bushing in the hole of the fascia, all round",
        P.near(bush, P.part("fascia", [ex_ - 9, ey_ - 9, zFi], [ex_ + 9, ey_ + 9, zF])), 2.5125, ("print", "place"), kind="play",
        note="`enc_hole_d` 14: large so the fascia can swing shut over the shaft")
    gap(sec, "Knob underside to the face of the fascia, knob not pressed",
        P.axis(knob, P.part("fascia", [ex_ - 18, ey_ - 18, zFi], [ex_ + 18, ey_ + 18, zF + 0.001]), 2, -1), 2.0,
        ("print", "thickness"), note="`knob_gap`. Pressing the knob takes 1.5 mm of it")
    hub = clip(knob, [ex_ - 5.6, ey_ - 5.6, bush_top], [ex_ + 5.6, ey_ + 5.6, shaft_top])
    bushing = clip(s["tfmain"], [ex_ - 4.6, ey_ - 4.6, 5], [ex_ + 4.6, ey_ + 4.6, bush_top + 0.001])
    gap(sec, "Hub of the knob to the top of the bushing, knob not pressed", P.axis(hub, bushing, 2, -1), 3.1, ("print",),
        note="1.5 mm of push travel and 1.6 mm spare")
    fh = P.hole("fascia", [ex_ - 7.5, ey_ - 7.5, zFi + 0.5], [ex_ + 7.5, ey_ + 7.5, zF - 0.5])
    align(sec, "Shaft axis to the middle of the hole in the fascia", P.offset(ext(bush), fh))

    # ---- LEDs and light pipes -----------------------------------------------------------------
    sec = "LEDs and light pipes"
    zg, off, side = [], [], []
    for cxl, cyl in bp["led_c"]:
        led = clip(s["tfmain"], [cxl - 1.0, cyl - 0.6, 0.001], [cxl + 1.0, cyl + 0.6, 1.2])
        pipe = P.part("lightpipe", [cxl - 1.5, cyl - 1.5, 1.0], [cxl + 1.5, cyl + 1.5, 7.5])
        zg.append(P.axis(led, pipe, 2, +1))
        off.append(P.offset(ext(led), ext(pipe)))
        side.append(P.axis(P.part("lightpipe", [cxl - 1.5, cyl - 1.5, zF - 1.5], [cxl + 1.5, cyl + 1.5, zF - 0.5]),
                           P.part("fascia", [cxl - 3, cyl - 1, zF - 1.5], [cxl - 1.21, cyl + 1, zF - 0.5]), 0, -1))
    ok = all(v is not None for v in zg + off + side)
    gap(sec, "LED top to the end of its light pipe, smallest of the ten", min(zg) if ok else None, 0.7,
        ("print", "thickness"), note="`lp_bottom_z` 1.5 over a 0.8 mm LED")
    gap(sec, "Light pipe in its hole in the fascia, each side, smallest of the ten", min(side) if ok else None, 0.25,
        ("print",), kind="fit", note="2.4 mm pipe in a 2.9 mm hole")
    align(sec, "LED to the axis of its light pipe, largest of the ten", max(off) if ok else None)

    # ---- Other parts under the fascia ------------------------------------------------------------
    sec = "Module, cell and the rest"
    m = tf["tf_module"]
    mod = clip(s["tfmain"], [m[0] - 0.001, H, 0.001], [m[1] + 0.001, m[3] + 0.001, m[4] + 0.001])
    gap(sec, "Antenna end of the module to the top wall of the base",
        P.axis(mod, P.part("base", [m[0], m[3], 0], [m[0] + 8, m[3] + 8, 3]), 1, +1), 4.0, ("print", "outline", "place"),
        note="Plastic, not metal; the contract's 15 mm is for metal")
    gap(sec, "Antenna corner of the module to the rounded corner of the base",
        P.near(mod, P.part("base", [m[0], H, 0], [W + 4, m[3] + 8, 3])), 6 - math.hypot(2.9, 2.0), ("print", "outline", "place"),
        note="The inside corner of the base has a 6 mm radius and comes closer than the top wall")
    bt = standin("BT1")
    gap(sec, "Cell holder to the inside of the fascia",
        P.axis(bt, P.part("fascia", [m[0] - 20, 58, 6], [m[1], 72, zFi + 0.5]), 2, +1), 5.0, ("print", "thickness"))
    top_all = clip(s["tfmain"], [1.5, 20, 0.001], [W - 1.5, 55, 9.5])
    gap(sec, "Tallest part between the terminals and the buttons to the inside of the fascia",
        zFi - ext(top_all)[1][2], 5.7, ("print", "thickness"), note="The opto-triacs, 4.8 mm")
    return gaps, aligns


def dimensions(s: dict[str, m3.Manifold], bp: dict, tf: dict) -> list[dict]:
    """What a person can measure with callipers on the printed pieces, taken from the meshes."""
    P = Probe(s)
    t = tf["tf_thickness"]
    zF, zFi = bp["z_fascia_outer"], bp["z_fascia_inner"]
    W, H = bp["board_w"], bp["board_h"]
    rows: list[dict] = []

    def row(piece, what, got, want, note=""):
        rows.append({"piece": piece, "what": what, "got": got, "want": want, "note": note})

    def size(e, k):
        return e[1][k] - e[0][k] if e else None

    tb = "Test-fit board"
    slab = ext(clip(s["tfmain"], [-1, -1, -t + 0.6], [W + 1, H, -0.5]))
    row(tb, "Board, width", size(slab, 0), 114.0)
    row(tb, "Board, height, without the tab under the antenna", size(slab, 1), 110.0)
    row(tb, "Board, thickness", size(ext(clip(s["tfmain"], [2, 80, -t - 1], [8, 90, 0.001])), 2), 1.6, "8 layers of 0.2 mm")
    holes = {h[0]: h for h in bp["holes"]}
    hb = {k: P.hole("tfmain", [holes[k][1] - 1.9, holes[k][2] - 1.9, -t + 0.6], [holes[k][1] + 1.9, holes[k][2] + 1.9, -0.2])
          for k in "ABCD"}
    row(tb, "Mounting hole, diameter", size(hb["C"], 0), 3.4, "Prints small on most printers")
    row(tb, "Hole C to hole D, centre to centre", P.offset(hb["C"], hb["D"]), 105.0, "The lower pair")
    row(tb, "Hole A to hole B, centre to centre", P.offset(hb["A"], hb["B"]), math.hypot(69.5, 1.0), "The upper pair")
    row(tb, "Hole C to hole A, centre to centre", P.offset(hb["C"], hb["A"]), math.hypot(1.0, 93.0), "The left pair")
    for h in tf["tf_headers"]:
        if h[0] in ("J1", "J2", "J3"):
            row(tb, f"Header block {h[0]}, width", size(ext(clip(s["tfmain"], [h[1] - 0.01, 0, 0.5], [h[2] + 0.01, h[4], h[5]])), 0),
                h[6] * 3.81 + 0.8, f"{h[6]} poles x 3.81 + 0.8")
    h = tf["tf_headers"][0]
    row(tb, "Header block, top above the underside of the board",
        ext(clip(s["tfmain"], [h[1], 0, 0.5], [h[2], h[4], h[5] + 1]))[1][2] + t, 8.6, "7.0 above the top surface")
    ex_, ey_ = bp["enc_c"]
    top = ext(clip(s["tfmain"], [ex_ - 3.2, ey_ - 3.2, bp["enc_shaft_top_z"] - 3], [ex_ + 3.2, ey_ + 3.2, bp["enc_shaft_top_z"] + 1]))
    row(tb, "Encoder shaft, diameter", size(top, 0), 5.975)
    row(tb, "Encoder shaft, across the flat", size(top, 1), 4.45, "The flat is 7 mm long")
    row(tb, "Encoder shaft, top above the underside of the board", top[1][2] + t, 26.1, "24.5 above the top surface")
    row(tb, "Encoder bushing, diameter",
        size(ext(clip(s["tfmain"], [ex_ - 4.6, ey_ - 4.6, 10], [ex_ + 4.6, ey_ + 4.6, 12])), 0), 8.975)
    bx, by = bp["btn_c"][0]
    act = ext(clip(s["tfmain"], [bx - 1.8, by - 1.8, 6], [bx + 1.8, by + 1.8, 11]))
    row(tb, "Button actuator, diameter", size(act, 0), 3.5)
    row(tb, "Button actuator, top above the underside of the board", act[1][2] + t, 11.6, "10.0 above the top surface")
    lx, ly = bp["led_c"][0]
    row(tb, "Left board edge to the middle of the first LED bump", lx, 12.0, "From the contract; the bump is 1.6 x 0.8 x 0.8")
    row(tb, "Bottom board edge to the middle of the LED row", ly, 42.0, "From the contract")

    pn = "Display panel stand-in"
    pe = ext(s["tfpanel"])
    row(pn, "Panel, long side", size(pe, 0), 51.8)
    row(pn, "Panel, short side", size(pe, 1), 36.2)
    row(pn, "Panel, thickness", size(pe, 2), 2.05, "Ten layers of 0.2 mm give 2.0")
    row(pn, "Cable strip, length beyond the panel edge", tf["tf_panel_tail"][0], 20.7, "From the contract, tolerance 0.5")

    pl = "Plug pieces"
    p = tf["tf_plugs"][0]
    body = ext(clip(s["tfplugs"], [p[1] - 0.01, p[3] - 0.01, 0], [p[2] + 0.01, p[4], 12]))
    row(pl, "Plug body, height", size(body, 2), 11.6)
    row(pl, "Plug body, depth", size(body, 1), 8.9)
    row(pl, "Nose, height: the header's mating face", size(ext(clip(s["tfplugs"], [p[1], p[4] + 0.2, 0], [p[2], -0.2, 12])), 2), 7.0)

    en = "Enclosure"
    inner = P.hole("base", [-2.9, 50, -1.0], [W + 2.9, 60, -0.9])
    row(en, "Base, inside width at the board", size(inner, 0), 116.2, "114 + 2 x 1.1")
    c = holes["C"]
    boss = ext(P.part("base", [c[1] - 3, c[2] - 3, -t - 3], [c[1] + 3, c[2] + 3, -t + 0.5]))
    rim = ext(P.part("base", [-4, 40, -2], [0, 80, 20]))
    row(en, "Base, top of a board boss below the rim of the side wall", rim[1][2] - boss[1][2], 7.6, "Depth gauge of the callipers")
    opt = next(o for o in bp["panel_options"] if o[0] == bp["panel_fitted"])
    aw, ah, cxd, cyd = opt[1], opt[2], opt[8], opt[9]
    win = P.hole("fascia", [cxd - aw / 2 - 6, cyd - ah / 2 - 6, zF - 1.0], [cxd + aw / 2 + 6, cyd + ah / 2 + 6, zF - 0.999])
    row(en, "Fascia, window width at its narrowest, behind the chamfer", size(win, 0), 42.02, "Active area 40.8 + 2 x 0.6")
    row(en, "Fascia, window height at its narrowest", size(win, 1), 31.82, "Active area 30.6 + 2 x 0.6")
    eh = P.hole("fascia", [ex_ - 7.5, ey_ - 7.5, zFi + 0.5], [ex_ + 7.5, ey_ + 7.5, zF - 0.5])
    row(en, "Fascia, hole for the encoder", size(eh, 0), 14.0)
    lh = P.hole("fascia", [lx - 2, ly - 2, zFi + 0.5], [lx + 2, ly + 2, zFi + 1.0])
    row(en, "Fascia, hole for a light pipe, square", size(lh, 0), 2.9)
    bh = P.hole("fascia", [bx - 10, by - 5, zFi + 0.5], [bx + 10, by + 5, zF - 0.5])
    row(en, "Fascia, hole for a button cap, length", size(bh, 0), 16.5)
    row(en, "Fascia, hole for a button cap, width", size(bh, 1), 8.5)
    fw = bp["fuse_well"]
    fh = P.hole("fascia", [fw[0] + 0.5, fw[1] + 0.5, 4], [fw[2] - 0.5, fw[3] - 0.5, 5])
    row(en, "Fascia, fuse well, inside length", size(fh, 0), 23.6)
    row(en, "Fascia, fuse well, inside width", size(fh, 1), 8.0)
    j4 = tf["tf_headers"][3]
    oh = P.hole("fascia", [j4[1] - 2, -1.5, 3], [j4[2] + 1.5, -1.0, 4])
    row(en, "Fascia, opening for J4, width", size(oh, 0), 28.27, "27.47 + 2 x 0.4")
    kb = P.hole("knob", [ex_ - 3.2, ey_ - 3.2, bp["enc_shaft_top_z"] - 4], [ex_ + 3.2, ey_ + 3.2, bp["enc_shaft_top_z"] - 3])
    row(en, "Knob, bore diameter", size(kb, 0), 6.175, "Shaft 5.975 + 2 x 0.1")
    row(en, "Knob, bore across the flat", size(kb, 1), 4.65, "4.45 + 2 x 0.1")
    return rows


def bp_hooks(s, hdr) -> list[float]:
    """x of the two fascia hooks: the middles of the two wide gaps between header blocks."""
    return [(hdr[i][2] + hdr[i + 1][1]) / 2 for i in range(len(hdr) - 1) if hdr[i + 1][1] - hdr[i][2] > 2]


# ---- The board as KiCad models it ---------------------------------------------------------------
def detailed_bodies(path: Path, thickness: float):
    """Split the detailed export into bodies. Returns (solids, counts).

    kicad-cli puts z = 0 at the bottom surface of the board; the enclosure puts
    it at the top surface, so everything moves down by the board thickness.
    A closed body is used as it is. An open body with a volume is replaced by
    its convex hull, which can only be larger. A flat body has no volume and
    is left out.
    """
    tris = stl_arrays(path)
    tris[:, :, 2] -= thickness
    verts, faces = indexed(tris)
    labels = components(faces, len(verts))
    order = np.argsort(labels, kind="stable")
    cuts = np.flatnonzero(np.diff(labels[order])) + 1
    solids, n_closed, n_hull, n_flat = [], 0, 0, 0
    for idx in np.split(order, cuts):
        f = faces[idx]
        used, inv = np.unique(f, return_inverse=True)
        v = verts[used]
        size = v.max(axis=0) - v.min(axis=0)
        if size.min() < 1e-4:
            n_flat += 1
            continue
        m = manifold(v, inv.reshape(-1, 3)) if closed(f) else None
        if m is not None:
            n_closed += 1
        else:
            m = m3.Manifold.hull_points(v)
            n_hull += 1
        solids.append((m, v.min(axis=0), v.max(axis=0)))
    return solids, {"triangles": len(tris), "bodies": int(labels.max()) + 1, "closed": n_closed, "hull": n_hull, "flat": n_flat}


def ref_of(lo, hi, layout: dict) -> str:
    """The footprint a body belongs to: the smallest courtyard or outline its middle lies in."""
    if hi[0] - lo[0] > 100 and hi[1] - lo[1] > 100:
        return "the bare board"
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    best = None
    for f in layout["footprints"]:
        b = f["courtyard"] or f["body"]
        if b and b[0] - 0.3 <= cx <= b[2] + 0.3 and b[1] - 0.3 <= cy <= b[3] + 0.3:
            area = (b[2] - b[0]) * (b[3] - b[1])
            if best is None or area < best[0]:
                best = (area, f["ref"])
    return best[1] if best else "?"


# ---- Print files ----------------------------------------------------------------------------------
def mesh_report(path: Path) -> dict:
    tris = stl_arrays(path)
    verts, faces = indexed(tris)
    vol, lo, hi = measure(tris)
    a, b, c = tris[:, 0], tris[:, 1], tris[:, 2]
    area = np.linalg.norm(np.cross(b - a, c - a), axis=1) / 2
    m = manifold(verts, faces)
    return {
        "volume": vol, "lo": lo, "hi": hi, "triangles": len(tris),
        "bodies": int(components(faces, len(verts)).max()) + 1,
        "closed": closed(faces),
        "degenerate": int((area < 1e-9).sum()),
        "solid": m is not None and abs(m.volume() - vol) < max(1.0, vol * 1e-4),
    }


def num(v: float | None, digits: int = 2) -> str:
    if v is None:
        return "open"
    r = round(v, digits)
    return f"{0.0 if r == 0 else r:.{digits}f}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--openscad", default="openscad")
    ap.add_argument("--out", type=Path, default=HERE / "output" / "check-report.md")
    args = ap.parse_args()
    out_dir = args.out.parent
    bp = scad_vars(HERE / "board_params.scad")
    tf = scad_vars(HERE / "testfit_params.scad")
    layout = json.loads((HERE / "board_layout.json").read_text(encoding="utf-8"))
    failed: list[str] = []

    with tempfile.TemporaryDirectory() as tmp:
        tmpd = Path(tmp)

        def pair(ab, panel=None):
            a, b = ab
            out = tmpd / f"x_{a}_{b}_{panel}.stl"
            defs = {"part": "check", "check_a": a, "check_b": b}
            if panel:
                defs["panel"] = panel
            if not run(args.openscad, defs, out):
                return ab, 0.0, None
            vol, lo, hi = measure(stl_arrays(out))
            return ab, vol, (lo, hi)

        def printed(item):
            name, _, _ = item
            out = tmpd / f"p_{name}.stl"
            run(args.openscad, {"part": f"print_{name}"}, out)
            return name, measure(stl_arrays(out))

        def solid(name):
            out = tmpd / f"s_{name}.stl"
            if not run(args.openscad, {"part": "solid", "check_a": name}, out):
                raise SystemExit(f"solid {name} is empty")
            return name, load(out)

        with concurrent.futures.ThreadPoolExecutor(8) as ex:
            pairs = list(ex.map(pair, PAIRS))
            tfpairs = list(ex.map(pair, TF_PAIRS))
            vpairs = list(ex.map(lambda ab: pair(ab, VARIANT), VARIANT_PAIRS))
            fresh = dict(ex.map(printed, PRINTS + TF_PRINTS))
            solids = dict(ex.map(solid, PRINTED + ["tfmain", "tfaddons", "tfplugs", "tfpanel", "tails"]))

    lines = [
        "# Enclosure check report",
        "",
        "Generated by `enclosure/tools/check.py`. Do not edit.",
        "",
        "Nothing here has been printed or fitted, and no board exists. The check",
        "works on the modelled solids in their assembled position.",
        "",
        f"Board file revision {layout['revision']} of {layout['date']}; mechanical contract revision {tf['tf_contract_rev']}.",
        "",
        "## Interference",
        "",
        "`board`, `display` and `plugs` are the placeholder models from",
        "`hardware/mechanical.yaml`; `zones` is the free volume that file promises",
        f"for components. A pair passes below {LIMIT} mm3.",
        "",
        "| A | B | Overlap, mm3 | Result |",
        "|---|---|---|---|",
    ]

    def rows(results, tag="", expected=None):
        for (a, b), vol, box in results:
            allow = (expected or {}).get((a, b))
            if vol < LIMIT:
                res = "pass"
            elif allow is not None and vol <= allow[0]:
                res = f"pass (expected: {allow[1]}, allowed {allow[0]:g})"
            else:
                lo, hi = box
                where = " ".join(f"{k}={lo[i]:.1f}..{hi[i]:.1f}" for i, k in enumerate("xyz"))
                res = f"**FAIL** at {where}"
                failed.append(f"{tag}{a} / {b}: {vol:.2f} mm3 at {where}")
            lines.append(f"| {a} | {b} | {vol:.2f} | {res} |")

    rows(pairs, expected={k: (v, "flat on the shaft") for k, v in EXPECTED.items()})
    lines += [
        "",
        "## Interference with the test-fit kit",
        "",
        "The printable stand-ins, as they are printed, in their assembled position.",
        "They are generated from the board file: `output/test-fit/stand-ins.md` lists",
        "every one with its source.",
        "",
        "- `tfboard`: the test-fit board with C1 and K1 in their slots (`tfmain` and `tfaddons`).",
        "- `tfplugs`: the four plug pieces, each with the 2 mm of header that overhangs the board edge.",
        "- `tfpanel`: the display panel stand-in without its cable. The folded cable is in `display`.",
        f"- `tails`: {len(tf['tf_tails'])} soldered lead ends, {tf['tf_tail_len']:g} mm long, which is assumed. Not printed.",
        "",
        "| A | B | Overlap, mm3 | Result |",
        "|---|---|---|---|",
    ]
    rows(tfpairs, "test fit: ", {k: (v, "the cable enters J7") for k, v in TF_EXPECTED.items()})
    lines += [
        "",
        f"## Interference with the {VARIANT} inch panel option",
        "",
        f"Fascia and display carrier rebuilt with `panel = \"{VARIANT}\"`. The board",
        "placeholder is not part of this: it is laid out for the fitted panel.",
        "The panel's folded cable is a guessed box.",
        "",
        "| A | B | Overlap, mm3 | Result |",
        "|---|---|---|---|",
    ]
    rows(vpairs, f"{VARIANT}: ")

    # ---- The board as KiCad models it ------------------------------------------------------
    detail = out_dir / f"{DETAIL}.stl"
    bodies, counts = detailed_bodies(detail, tf["tf_thickness"])
    missing_file = out_dir / f"{DETAIL}-missing-models.txt"
    missing = missing_file.read_text(encoding="utf-8").split() if missing_file.exists() else []
    lines += [
        "",
        "## Interference with the board as KiCad models it",
        "",
        f"`output/{DETAIL}.stl` is `kicad-cli pcb export stl --subst-models` of the board file: the",
        "same export as `hardware/zoneflow-a/output/zoneflow-a.step`, as triangles. It has",
        f"{counts['triangles']} triangles in {counts['bodies']} bodies. {counts['closed']} are closed solids and are used as they",
        f"are. {counts['hull']} are open and are replaced by their convex hull, which can only be larger.",
        f"{counts['flat']} are flat, have no volume and are left out. Every body is intersected with every",
        f"printed part and with the display panel stand-in; the limit is {LIMIT} mm3 per body.",
        "",
        "The closest body is the smallest distance between any body and that part, with the",
        "footprint the body belongs to. The bare board is left out of that column: it rests on",
        "its bosses and carries the feet of the display carrier.",
        "",
        "| Printed part | Bodies that overlap | Largest overlap, mm3 | Closest body | Distance, mm | Result |",
        "|---|---|---|---|---|---|",
    ]
    for name in PRINTED + ["tfpanel"]:
        part = solids[name]
        pb = part.bounding_box()
        hits, worst, nearest = [], 0.0, None
        for m, lo, hi in bodies:
            if any(lo[k] > pb[3 + k] + 3 or hi[k] < pb[k] - 3 for k in range(3)):
                continue
            g = m.min_gap(part, 3.0)
            if g < 3.0 - 1e-9 and (nearest is None or g < nearest[0]) and ref_of(lo, hi, layout) != "the bare board":
                nearest = (g, ref_of(lo, hi, layout))
            if g > 0:
                continue
            vol = (m ^ part).volume()
            worst = max(worst, vol)
            if vol >= LIMIT:
                hits.append((ref_of(lo, hi, layout), vol, lo, hi))
        for ref, vol, lo, hi in hits:
            where = " ".join(f"{k}={lo[i]:.1f}..{hi[i]:.1f}" for i, k in enumerate("xyz"))
            failed.append(f"detailed board: body of {ref} / {name}: {vol:.2f} mm3, body at {where}")
        res = "pass" if not hits else "**FAIL**: " + ", ".join(sorted({h[0] for h in hits}))
        near = (nearest[1], num(nearest[0])) if nearest else ("none within 3 mm", "")
        lines.append(f"| {name} | {len(hits)} | {worst:.2f} | {near[0]} | {near[1]} | {res} |")
    lines += [
        "",
        "What this export does not contain, so that only the stand-ins of the test-fit board",
        "cover it:",
        "",
        f"- **{len(missing)} parts whose model file is not in the KiCad library on the build machine**: "
        + (", ".join(missing) if missing else "none") + ".",
        "- **Parts whose footprint names no model**: " + ", ".join(
            f["ref"] for f in layout["footprints"]
            if not f["model"] and f["body"] and not f["ref"].startswith(("TP", "JP"))) + ".",
        "- **J8 is in it but is not fitted.** KiCad exports the debug header's model, 8.5 mm tall.",
        "",
    ]

    # ---- Clearances --------------------------------------------------------------------------
    gaps, aligns = clearances(solids, bp, tf)
    lines += [
        "## Clearances at the features checked by hand",
        "",
        "Measured on the exported meshes of the printed parts and the test-fit kit in their",
        "assembled position. Each value must equal the one `docs/enclosure/test-fit.md` tells the",
        f"reader to expect, within {MATCH} mm, or the build fails.",
        "",
        "The tolerance is what the gap has to absorb, added up without any allowance for",
        "errors cancelling:",
        "",
    ]
    lines += [f"- `{k}` {v[0]:g} mm: {v[1]}." for k, v in TOL.items()]
    lines += [
        "",
        "Not counted, because the assembler can use them: the board can move 0.2 mm each way on",
        "its screws, and the fascia 0.25 mm each way in the base. *Left over* is the gap less the",
        "tolerance. A negative figure means the parts can touch in the worst case; it does not",
        "mean they will. Printer accuracy varies and nothing has been printed.",
        "",
        "Kind: `gap` must stay open; `play` is a loose fit by design; `fit` is a close fit by design",
        "(push, slide or tape); `contact` parts rest on each other.",
        "",
    ]
    section = None
    for g in gaps:
        if g["section"] != section:
            section = g["section"]
            lines += ["", f"### {section}", "", "| Feature | Kind | Nominal, mm | Tolerance assumed, mm | Left over, mm | Note |",
                      "|---|---|---|---|---|---|"]
        got, want = g["got"], g["want"]
        if (got is None) != (want is None) or (got is not None and abs(got - want) > MATCH):
            failed.append(f"clearance: {g['what']}: measured {num(got, 3)}, expected {num(want, 3)}")
        tol = sum(TOL[k][0] for k in g["tol"])
        if got is None:
            cells = ("open", "", "")
        elif g["kind"] == "contact":
            cells = ("0.00", "", "")
        else:
            left = round(got - tol, 2) + 0.0
            cells = (num(got), f"{tol:.2f} ({' + '.join(g['tol'])})" if g["tol"] else "", f"{left:+.2f}" if g["tol"] else "")
        lines.append(f"| {g['what']} | {g['kind']} | {cells[0]} | {cells[1]} | {cells[2]} | {g['note']} |")
    lines += [
        "",
        "### Alignment",
        "",
        "Offsets between two things that should be in line, and two overlaps. The expected value",
        "is 0.00 unless the note says otherwise.",
        "",
        "| Feature | Measured, mm | Expected, mm | Note |",
        "|---|---|---|---|",
    ]
    for a in aligns:
        if a["got"] is None or abs(a["got"] - a["want"]) > MATCH:
            failed.append(f"alignment: {a['what']}: measured {num(a['got'], 3)}, expected {a['want']:.3f}")
        lines.append(f"| {a['section']}: {a['what']} | {num(a['got'])} | {a['want']:.2f} | {a['note']} |")
    open_gaps = sorted((g for g in gaps if g["kind"] == "gap" and g["got"] is not None), key=lambda g: g["got"])
    by_size: dict[str, list[str]] = {}
    for g in open_gaps:
        by_size.setdefault(num(g["got"]), []).append(g["what"])
    lines += [
        "",
        "### The tightest gaps",
        "",
        "Of the features that must stay open, the three smallest nominal gaps and every",
        "feature that has them:",
        "",
    ]
    lines += [f"- **{k} mm**: " + "; ".join(v) + "." for k, v in list(by_size.items())[:3]]
    closing = [g for g in gaps if g["kind"] == "gap" and g["got"] is not None and g["tol"]
               and round(g["got"] - sum(TOL[k][0] for k in g["tol"]), 2) < 0]
    lines += [
        "",
        "Gaps that the assumed tolerances can close, all of them stacked one way:",
        "",
    ]
    lines += [f"- {g['what']}: {num(g['got'])} mm against {sum(TOL[k][0] for k in g['tol']):.2f} mm." for g in closing] or ["- None."]

    dims = dimensions(solids, bp, tf)
    lines += [
        "",
        "## Dimensions to measure on the printed pieces",
        "",
        "Taken from the same meshes. A printed piece will differ from these by what the printer",
        "adds or takes; the difference is the thing to report.",
        "",
        "| Piece | Dimension | Model, mm | Note |",
        "|---|---|---|---|",
    ]
    for d in dims:
        if d["got"] is None or abs(d["got"] - d["want"]) > MATCH:
            failed.append(f"dimension: {d['piece']}: {d['what']}: measured {num(d['got'], 3)}, expected {d['want']:.3f}")
        lines.append(f"| {d['piece']} | {d['what']} | {num(d['got'])} | {d['note']} |")

    # ---- Print files ------------------------------------------------------------------------------
    lines += [
        "",
        "## Print files",
        "",
        f"Each STL in `output/` in its print orientation. Bed limit {BED[0]:g} x {BED[1]:g} mm. Mass is",
        f"solid volume times {PETG_DENSITY} g/cm3, so it is an upper bound for a part with infill.",
        "*Closed* means every edge of the mesh joins exactly two triangles, wound the same way,",
        "with no triangle of zero area, and that Manifold accepts the mesh as a solid of the",
        "same volume. A closed mesh has no face of zero thickness.",
        "",
        "",
        "Hours are the volume divided by an average flow of 5 mm3/s, an older bed-slinger, and",
        "of 10 mm3/s, a current fast printer. They are calculated, not sliced, and count each file once.",
        "",
        "| File | Quantity | Size x, y, z in mm | Volume, cm3 | Mass at 100 %, g | Hours at 5 | Hours at 10 | Bodies | Triangles | Closed | Fits bed |",
        "|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    totals, volumes = {}, {}
    for group, items in (("enclosure", PRINTS), ("test-fit kit", TF_PRINTS)):
        total = 0.0
        for name, file, q in items:
            path = out_dir / f"{file}.stl"
            r = mesh_report(path)
            size = [r["hi"][k] - r["lo"][k] for k in range(3)]
            fits = size[0] <= BED[0] and size[1] <= BED[1]
            good = r["closed"] and r["solid"] and r["degenerate"] == 0
            if not fits:
                failed.append(f"{file}: {size[0]:.1f} x {size[1]:.1f} does not fit the bed")
            if min(abs(r["lo"][k]) for k in range(3)) > 0.02 or abs(r["lo"][2]) > 0.02:
                failed.append(f"{file}: print model does not sit on the bed (z min {r['lo'][2]:.2f})")
            if not good:
                failed.append(f"{file}: mesh is not a closed solid (closed {r['closed']}, solid {r['solid']}, "
                              f"{r['degenerate']} degenerate triangles)")
            if r["bodies"] != 1:
                failed.append(f"{file}: {r['bodies']} separate bodies in one print file")
            fv, flo, fhi = fresh[name]
            if abs(fv - r["volume"]) > 0.5 or any(abs(flo[k] - r["lo"][k]) > 0.01 or abs(fhi[k] - r["hi"][k]) > 0.01 for k in range(3)):
                failed.append(f"{file}.stl is not what the source gives now: run make")
            mass = r["volume"] / 1000 * PETG_DENSITY
            total += mass * q
            lines.append(
                f"| `{file}.stl` | {q} | {size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f} | {r['volume'] / 1000:.1f} "
                f"| {mass:.1f} | {r['volume'] / 5 / 3600:.2f} | {r['volume'] / 10 / 3600:.2f} | {r['bodies']} | {r['triangles']} "
                f"| {'yes' if good else '**NO**'} | {'yes' if fits else '**NO**'} |")
            volumes[group] = volumes.get(group, 0.0) + r["volume"] * q
        totals[group] = total
    lines.append("")
    for group in totals:
        v = volumes[group]
        lines.append(f"- **{group.capitalize()}**, every piece in its quantity: {v / 1000:.1f} cm3, {totals[group]:.0f} g at 100 % fill, "
                     f"{v / 5 / 3600:.1f} h at 5 mm3/s, {v / 10 / 3600:.1f} h at 10 mm3/s.")
    lines.append("")

    # ---- 3MF files and plates, read back -----------------------------------------------------------
    lines += [
        "## 3MF files and plates",
        "",
        "Written by `enclosure/tools/threemf.py` from the STL files and read back here with",
        f"trimesh {trimesh.__version__}. A part's 3MF must hold the volume of its STL. A plate must hold every",
        "part, apart from each other, on the bed. No slicer has opened these files.",
        "",
        "| Plate | Parts | Extent on the bed, mm | Smallest gap between parts, mm | Result |",
        "|---|---|---|---|---|",
    ]
    for _, file, _ in PRINTS + TF_PRINTS:
        want = measure(stl_arrays(out_dir / f"{file}.stl"))[0]
        got = trimesh.load(out_dir / f"{file}.3mf", force="mesh")
        if abs(abs(got.volume) - want) > max(0.5, want * 1e-4):
            failed.append(f"{file}.3mf holds {abs(got.volume):.1f} mm3, its STL {want:.1f} mm3")
    cfg = threemf.load_plates(HERE / "plates.toml")
    for plate in cfg["plate"]:
        scene = trimesh.load(out_dir / f"{plate['name']}.3mf")
        meshes = scene.dump() if isinstance(scene, trimesh.Scene) else [scene]
        want_n = sum(int(p.get("count", 1)) for p in plate["parts"])
        lo = np.min([m.bounds[0] for m in meshes], axis=0)
        hi = np.max([m.bounds[1] for m in meshes], axis=0)
        gap_min = None
        for i in range(len(meshes)):
            for j in range(i + 1, len(meshes)):
                a, b = meshes[i].bounds, meshes[j].bounds
                d = max(max(a[0][k] - b[1][k], b[0][k] - a[1][k]) for k in range(2))
                gap_min = d if gap_min is None else min(gap_min, d)
        want_vol = sum(measure(stl_arrays(out_dir / f"{p['file']}.stl"))[0] * int(p.get("count", 1)) for p in plate["parts"])
        got_vol = sum(abs(m.volume) for m in meshes)
        ok = (len(meshes) == want_n and lo[0] >= -1e-6 and lo[1] >= -1e-6 and hi[0] <= cfg["bed"][0] + 1e-6
              and hi[1] <= cfg["bed"][1] + 1e-6 and abs(lo[2]) < 0.02 and (gap_min is None or gap_min >= cfg["gap"] - 1e-6)
              and abs(got_vol - want_vol) < max(1.0, want_vol * 1e-4))
        if not ok:
            failed.append(f"plate {plate['name']}: {len(meshes)} of {want_n} parts, extent {lo[:2]} to {hi[:2]}, gap {gap_min}")
        lines.append(f"| `{plate['name']}.3mf` | {len(meshes)} | {lo[0]:.1f}, {lo[1]:.1f} to {hi[0]:.1f}, {hi[1]:.1f} "
                     f"| {'one part' if gap_min is None else f'{gap_min:.1f}'} | {'pass' if ok else '**FAIL**'} |")
    lines.append("")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines), encoding="utf-8")
    everything = pairs + tfpairs + vpairs
    n_ok = sum(1 for ab, v, _ in everything if v < LIMIT or {**EXPECTED, **TF_EXPECTED}.get(ab, 0) >= v)
    print(f"{args.out}: {n_ok} of {len(everything)} pairs clear, {counts['closed'] + counts['hull']} bodies of the detailed "
          f"board, {len(gaps)} gaps and {len(aligns)} alignments measured, {len(PRINTS) + len(TF_PRINTS)} print files, "
          f"{len(cfg['plate'])} plates")
    if failed:
        print("FAILED:\n  " + "\n  ".join(failed), file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
