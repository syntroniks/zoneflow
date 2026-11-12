# SPDX-License-Identifier: Apache-2.0
"""Command line: ``python -m reviewwalk generate|check|compare|record``.

generate  draw every picture from the board (needs KiCad's Python)
check     the content and the committed pictures against the committed board (Python only)
compare   two directories of pictures, numbers within a tolerance (Python only)
record    check a hand-off file of the walkthrough and write its record under docs/design/reviews/ (Python only)
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

from . import check as checkmod
from . import spec as specmod


def generate(args):
    from . import png, render  # needs pcbnew

    spec = specmod.load(args.data)
    board = render.Board(args.board)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    wanted = set()
    manifest = {"inputs": specmod.inputs_hash(spec, args.board, args.top_render),
                "board": {"width": board.width, "height": board.height,
                          "kicad_origin": list(board.kicad_xy(0, 0))},
                "figures": {}}
    for owner, fig in specmod.figures(spec):
        name = specmod.figure_name(owner, fig)
        try:
            svg, meta = render.render_figure(board, fig)
        except render.RenderError as e:
            sys.exit(f"reviewwalk: {name}: {e}")
        (out / name).write_text(svg, encoding="utf-8")
        manifest["figures"][name] = meta
        wanted.add(name)
    calib = png.find_board(args.top_render, board.width, board.height)
    manifest["overview"] = calib
    (out / "overview-overlay.svg").write_text(render.render_overlay(calib, spec["block"], board.width, board.height),
                                              encoding="utf-8")
    wanted.add("overview-overlay.svg")
    manifest["assembly"] = {}
    for side in ("top", "bottom"):
        svg, meta = render.render_assembly(board, side, spec["meta"].get("print_note", ""))
        (out / f"assembly-{side}.svg").write_text(svg, encoding="utf-8")
        manifest["assembly"][side] = meta
        wanted.add(f"assembly-{side}.svg")
    for old in out.glob("*.svg"):
        if old.name not in wanted:
            old.unlink()
    (out / specmod.MANIFEST).write_text(json.dumps(manifest, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(f"reviewwalk: {len(wanted)} pictures in {out}")


def check(args):
    spec = specmod.load(args.data)
    found = checkmod.problems(spec, args.board, args.out, args.top_render)
    for line in found:
        print(f"reviewwalk: {line}", file=sys.stderr)
    if found:
        sys.exit(1)
    print(f"reviewwalk: {len(spec['step'])} steps, {len(spec['finding'])} findings, "
          f"{sum(1 for _ in specmod.figures(spec))} pictures: all parts and nets are on the board, and the pictures are of it")


_NUMBER = re.compile(r"-?\d+(?:\.\d+)?")


def same_picture(a: str, b: str, tolerance: float) -> bool:
    """Two SVG texts with the same structure and numbers no further apart than the tolerance."""
    if a == b:
        return True
    if _NUMBER.sub("#", a) != _NUMBER.sub("#", b):
        return False
    return all(abs(float(x) - float(y)) <= tolerance for x, y in zip(_NUMBER.findall(a), _NUMBER.findall(b)))


def compare(args):
    a, b = Path(args.a), Path(args.b)
    names_a, names_b = {p.name for p in a.glob("*.svg")}, {p.name for p in b.glob("*.svg")}
    bad = [f"only in {a}: {n}" for n in sorted(names_a - names_b)] + [f"only in {b}: {n}" for n in sorted(names_b - names_a)]
    for name in sorted(names_a & names_b):
        if not same_picture((a / name).read_text(encoding="utf-8"), (b / name).read_text(encoding="utf-8"), args.tolerance):
            bad.append(f"{name} differs")
    for line in bad:
        print(f"reviewwalk: {line}", file=sys.stderr)
    if bad:
        sys.exit(1)
    print(f"reviewwalk: {len(names_a)} pictures, the same in both")


def record(args):
    from . import handoff

    try:
        page, raw = handoff.record(args.file, args.out, args.board, args.data, args.force, args.allow_other_board)
    except handoff.HandoffError as e:
        for line in str(e).splitlines():
            print(f"reviewwalk: {line}", file=sys.stderr)
        sys.exit(1)
    except OSError as e:
        sys.exit(f"reviewwalk: {e}")
    print(f"reviewwalk: recorded in {page} and {raw.name}")


def main(argv=None):
    p = argparse.ArgumentParser(prog="reviewwalk", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    for name, fn in (("generate", generate), ("check", check)):
        s = sub.add_parser(name)
        s.add_argument("--data", default=str(specmod.DATA))
        s.add_argument("--board", default=str(specmod.BOARD))
        s.add_argument("--top-render", default=str(specmod.TOP_RENDER))
        s.add_argument("--out", default=str(specmod.OUT))
        s.set_defaults(fn=fn)
    s = sub.add_parser("compare")
    s.add_argument("a")
    s.add_argument("b")
    s.add_argument("--tolerance", type=float, default=0.02)
    s.set_defaults(fn=compare)
    s = sub.add_parser("record")
    s.add_argument("file", help="the hand-off file the walkthrough page wrote, Markdown or its JSON alone")
    s.add_argument("--out", default=None, help="where the record goes; docs/design/reviews by default")
    s.add_argument("--data", default=str(specmod.DATA))
    s.add_argument("--board", default=str(specmod.BOARD))
    s.add_argument("--force", action="store_true", help="write over a record of the same date")
    s.add_argument("--allow-other-board", action="store_true",
                   help="record a review of another board file than the one in the repository, and say so in the record")
    s.set_defaults(fn=record)
    args = p.parse_args(argv)
    args.fn(args)


if __name__ == "__main__":
    main()
