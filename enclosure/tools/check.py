"""Interference and bed-fit check for the ZoneFlow enclosure.

For every pair below, OpenSCAD intersects the two solids in their assembled
position and this script measures the volume of what is left. A pair passes
when that volume is under LIMIT. It also measures every printed part in its
print orientation and checks that it fits the bed.

What this proves: the modelled solids do not overlap. The board is the
placeholder from hardware/mechanical.yaml, so it proves the contract is free of
the enclosure, not that a real board fits. Nothing here has been printed.

    python3 tools/check.py --openscad openscad --out output/check-report.md
"""

from __future__ import annotations

import argparse
import concurrent.futures
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
LIMIT = 0.5  # mm3. Faces that only touch leave slivers below this.
BED = (220.0, 220.0)

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
# these two overlap by the missing flat. Anything larger is a real clash.
EXPECTED = {("board", "knob"): 36.5}

PRINTS = ["base", "fascia", "cover", "bridge", "knob", "button_back", "button_home", "lightpipe"]
QUANTITY: dict[str, int] = {}
PETG_DENSITY = 1.27  # g/cm3


def read_stl(path: Path) -> list[tuple[tuple[float, float, float], ...]]:
    data = path.read_bytes()
    if data[:5] == b"solid" and b"facet" in data[:400]:
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


def measure(tris) -> tuple[float, list[float], list[float]]:
    """Volume by the divergence theorem, and the bounding box."""
    if not tris:
        return 0.0, [0, 0, 0], [0, 0, 0]
    vol = 0.0
    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    for a, b, c in tris:
        vol += (
            a[0] * (b[1] * c[2] - b[2] * c[1])
            - a[1] * (b[0] * c[2] - b[2] * c[0])
            + a[2] * (b[0] * c[1] - b[1] * c[0])
        ) / 6.0
        for p in (a, b, c):
            for k in range(3):
                lo[k] = min(lo[k], p[k])
                hi[k] = max(hi[k], p[k])
    return abs(vol), lo, hi


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


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--openscad", default="openscad")
    ap.add_argument("--out", type=Path, default=HERE / "output" / "check-report.md")
    args = ap.parse_args()

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
            vol, lo, hi = measure(read_stl(out))
            return ab, vol, (lo, hi)

        def printed(name):
            out = tmpd / f"p_{name}.stl"
            run(args.openscad, {"part": f"print_{name}"}, out)
            return name, measure(read_stl(out))

        with concurrent.futures.ThreadPoolExecutor(8) as ex:
            pairs = list(ex.map(pair, PAIRS))
            vpairs = list(ex.map(lambda ab: pair(ab, VARIANT), VARIANT_PAIRS))
            prints = list(ex.map(printed, PRINTS))

    lines = [
        "# Enclosure check report",
        "",
        "Generated by `enclosure/tools/check.py`. Do not edit.",
        "",
        "Nothing here has been printed or fitted. The check intersects the",
        "modelled solids in their assembled position. `board`, `display` and",
        "`plugs` are the placeholder models from `hardware/mechanical.yaml`;",
        "`zones` is the free volume that file promises for components.",
        "",
        "## Interference",
        "",
        f"A pair passes below {LIMIT} mm3.",
        "",
        "| A | B | Overlap, mm3 | Result |",
        "|---|---|---|---|",
    ]
    failed = []

    def rows(results, tag=""):
        for (a, b), vol, box in results:
            allow = EXPECTED.get((a, b))
            if vol < LIMIT:
                res = "pass"
            elif allow is not None and vol <= allow:
                res = f"pass (expected: flat on the shaft, allowed {allow:g})"
            else:
                lo, hi = box
                where = " ".join(f"{k}={lo[i]:.1f}..{hi[i]:.1f}" for i, k in enumerate("xyz"))
                res = f"**FAIL** at {where}"
                failed.append(f"{tag}{a} / {b}: {vol:.2f} mm3 at {where}")
            lines.append(f"| {a} | {b} | {vol:.2f} | {res} |")

    rows(pairs)
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

    lines += [
        "",
        "## Printed parts in print orientation",
        "",
        f"Bed limit {BED[0]:g} x {BED[1]:g} mm. Mass is solid volume times",
        f"{PETG_DENSITY} g/cm3, so it is an upper bound for a part with infill.",
        "",
        "| Part | Quantity | Size x, y, z in mm | Volume, cm3 | Mass at 100 %, g | Fits bed |",
        "|---|---|---|---|---|---|",
    ]
    total = 0.0
    for name, (vol, lo, hi) in prints:
        size = [hi[k] - lo[k] for k in range(3)]
        fits = size[0] <= BED[0] and size[1] <= BED[1]
        if not fits:
            failed.append(f"{name}: {size[0]:.1f} x {size[1]:.1f} does not fit the bed")
        if abs(lo[2]) > 0.02:
            failed.append(f"{name}: print model does not sit on the bed (z min {lo[2]:.2f})")
        q = QUANTITY.get(name, 1)
        mass = vol / 1000 * PETG_DENSITY
        total += mass * q
        lines.append(
            f"| {name} | {q} | {size[0]:.1f} x {size[1]:.1f} x {size[2]:.1f} | {vol / 1000:.1f} "
            f"| {mass:.0f} | {'yes' if fits else '**NO**'} |"
        )
    lines += ["", f"Total at 100 % fill: {total:.0f} g.", ""]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines))
    everything = pairs + vpairs
    n_ok = sum(1 for ab, v, _ in everything if v < LIMIT or EXPECTED.get(ab, 0) >= v)
    print(f"{args.out}: {n_ok} of {len(everything)} pairs clear, {len(prints)} parts measured")
    if failed:
        print("FAILED:\n  " + "\n  ".join(failed), file=sys.stderr)
        raise SystemExit(1)


if __name__ == "__main__":
    main()
