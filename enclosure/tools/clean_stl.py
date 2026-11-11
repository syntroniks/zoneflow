# /// script
# requires-python = ">=3.11"
# dependencies = [
#     "numpy==2.5.4",
#     "manifold3d==3.5.4",
# ]
# ///
"""Rewrite an STL from OpenSCAD as a clean, closed mesh.

    uv run tools/clean_stl.py raw.stl clean.stl

An STL stores 32-bit coordinates. OpenSCAD's export of some parts has pairs
of vertices that differ in the last bit, and triangles of zero area between
them: the wiring cover was not a closed mesh by its own vertices, and the
fascia and the knob carried degenerate triangles. A slicer welds these, but
a print file should not need repair.

This snaps every coordinate to GRID, drops what collapses, lets Manifold
rebuild the surface, and writes the triangles in a fixed order so that the
file is the same on every run. It stops if the result is not one closed
solid, or if the volume moved by more than a thousandth.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

import manifold3d as m3
import numpy as np

GRID = 1e-4   # mm


def read(path: Path) -> np.ndarray:
    data = path.read_bytes()
    n = int(np.frombuffer(data, dtype="<u4", count=1, offset=80)[0])
    rec = np.frombuffer(data, dtype=np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")]), count=n, offset=84)
    return rec["v"].astype(np.float64)


def volume(tris: np.ndarray) -> float:
    return abs(float(np.einsum("ij,ij->i", tris[:, 0], np.cross(tris[:, 1], tris[:, 2])).sum() / 6.0))


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    src, dest = Path(argv[1]), Path(argv[2])
    raw = read(src)
    snapped = np.round(raw / GRID) * GRID
    verts, inv = np.unique(snapped.reshape(-1, 3), axis=0, return_inverse=True)
    faces = inv.reshape(-1, 3)
    faces = faces[(faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & (faces[:, 0] != faces[:, 2])]
    solid = m3.Manifold(m3.Mesh(vert_properties=verts.astype(np.float32), tri_verts=faces.astype(np.uint32)))
    if solid.status() != m3.Error.NoError or solid.is_empty():
        raise SystemExit(f"{src}: not a closed solid ({solid.status()})")
    if len(solid.decompose()) != 1:
        raise SystemExit(f"{src}: {len(solid.decompose())} separate bodies")
    mesh = solid.to_mesh()
    v = np.asarray(mesh.vert_properties)[:, :3].astype(np.float32)
    tris = v[np.asarray(mesh.tri_verts)]
    # A fixed order: each triangle starts at its lowest vertex, triangles sorted by coordinates.
    keys = tris[:, :, 0].astype(np.float64) * 1e6 + tris[:, :, 1].astype(np.float64) * 1e3 + tris[:, :, 2]
    first = np.argmin(keys, axis=1)
    tris = np.take_along_axis(tris, ((first[:, None] + np.arange(3)[None, :]) % 3)[:, :, None], axis=1)
    flat = tris.reshape(len(tris), 9)
    tris = tris[np.lexsort(flat.T[::-1])]
    a, b, c = (tris[:, k].astype(np.float64) for k in range(3))
    area = np.linalg.norm(np.cross(b - a, c - a), axis=1) / 2
    if (area < 1e-9).any():
        raise SystemExit(f"{src}: {int((area < 1e-9).sum())} triangles of zero area are left")
    before, after = volume(raw), volume(tris.astype(np.float64))
    if abs(after - before) > max(0.01, before * 1e-3):
        raise SystemExit(f"{src}: volume moved from {before:.3f} to {after:.3f} mm3")
    out = bytearray(b"ZoneFlow, cleaned by enclosure/tools/clean_stl.py".ljust(80, b" "))
    out += struct.pack("<I", len(tris))
    rec = np.zeros(len(tris), dtype=np.dtype([("n", "<f4", 3), ("v", "<f4", (3, 3)), ("a", "<u2")]))
    rec["v"] = tris
    out += rec.tobytes()
    dest.write_bytes(bytes(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
