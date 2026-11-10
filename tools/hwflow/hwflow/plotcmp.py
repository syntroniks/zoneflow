# SPDX-License-Identifier: Apache-2.0
"""Compare two SVG plots of a schematic page by what they draw, not by their bytes.

Two builds of the same KiCad release plot the same page with the same lines,
arcs and text, and write them in a different order: where KiCad sorts the
items of a page, items of equal rank keep whatever order the C++ library's
sort leaves them in. The files then differ on almost every line and show the
same picture.

``canonical`` reduces a plot to the multiset of what it draws: every drawing
element with the style it inherits from its groups, and every text string.
Two plots are the same picture when their multisets are equal. The time of
export and the order of drawing are left out; nothing else is.

The build uses this to keep a committed plot whenever the new plot draws the
same, so that the plots change in git only when the picture changes:

    python -m hwflow.plotcmp stash <output dir> <scratch dir>   # before plotting
    python -m hwflow.plotcmp keep <scratch dir> <output dir>    # after plotting
    python -m hwflow.plotcmp compare <dir of SVGs> <dir of SVGs>

Only the standard library is used.
"""

from __future__ import annotations

import re
import shutil
import sys
import xml.etree.ElementTree as ET
from collections import Counter
from pathlib import Path

DRAWN = {"path", "circle", "ellipse", "rect", "line", "polyline", "polygon", "image", "text"}
_SPACE = re.compile(r"\s+")


def _local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _style(text: str | None) -> dict[str, str]:
    out = {}
    for item in (text or "").split(";"):
        if ":" in item:
            key, value = item.split(":", 1)
            out[key.strip()] = value.strip()
    return out


def canonical(path: Path) -> Counter:
    """What one plot draws: a count of every (element, inherited style, attributes, text)."""
    found: Counter = Counter()

    def walk(node, style: dict[str, str], transform: tuple[str, ...]) -> None:
        tag = _local(node.tag)
        style = {**style, **_style(node.get("style"))}
        if node.get("transform"):
            transform = (*transform, _SPACE.sub(" ", node.get("transform").strip()))
        if tag in ("title", "metadata"):
            return  # the file name and the time of export
        if tag == "desc":
            found[("desc", _SPACE.sub(" ", (node.text or "").strip()))] += 1
            return
        if tag in DRAWN:
            attrs = tuple(sorted((k, _SPACE.sub(" ", v.strip())) for k, v in node.attrib.items()
                                 if k not in ("style", "transform")))
            text = _SPACE.sub(" ", "".join(node.itertext()).strip())
            found[(tag, tuple(sorted(style.items())), transform, attrs, text)] += 1
            return
        for child in node:
            walk(child, style, transform)

    root = ET.parse(path).getroot()
    size = sorted((k, v) for k, v in root.attrib.items() if _local(k) in ("width", "height", "viewBox"))
    found[("page", tuple(size))] += 1
    walk(root, {}, ())
    return found


def compare(old: Path, new: Path) -> list[str]:
    """Differences between two plots of one page. Empty means they draw the same."""
    a, b = canonical(old), canonical(new)
    if a == b:
        return []
    gone, added = a - b, b - a
    problems = [f"{new.name}: {sum(gone.values())} drawn element(s) only in the committed plot, "
                f"{sum(added.values())} only in the new one"]
    for label, items in (("committed only", gone), ("new only", added)):
        for key, n in list(items.items())[:3]:
            problems.append(f"  {label}, {n} time(s): {str(key)[:200]}")
    return problems


def compare_dirs(old_dir: Path, new_dir: Path) -> list[str]:
    old = {p.name: p for p in Path(old_dir).glob("*.svg")}
    new = {p.name: p for p in Path(new_dir).glob("*.svg")}
    problems = [f"{n}: committed and not plotted now" for n in sorted(set(old) - set(new))]
    problems += [f"{n}: plotted now and not committed" for n in sorted(set(new) - set(old))]
    for name in sorted(set(old) & set(new)):
        problems += compare(old[name], new[name])
    return problems


SVG_DIR = "schematic-svg"


def stash(out_dir: Path, scratch: Path) -> int:
    """Move the plots of the last build (the SVG directory and the PDFs) out of the way."""
    out_dir, scratch = Path(out_dir), Path(scratch)
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    moved = 0
    for item in [out_dir / SVG_DIR, *sorted(out_dir.glob("*.pdf"))]:
        if item.exists():
            shutil.move(str(item), str(scratch / item.name))
            moved += 1
    return moved


def keep(scratch: Path, out_dir: Path) -> tuple[int, int]:
    """Put back each stashed plot whose new version draws the same. Returns (kept, redrawn).

    The PDF holds every page, so it is put back only when no page was redrawn,
    none was added and none was removed.
    """
    scratch, out_dir = Path(scratch), Path(out_dir)
    old = {p.name: p for p in (scratch / SVG_DIR).glob("*.svg")}
    new = {p.name: p for p in (out_dir / SVG_DIR).glob("*.svg")}
    kept = 0
    for name, path in sorted(new.items()):
        if name in old and canonical(old[name]) == canonical(path):
            shutil.copyfile(old[name], path)
            kept += 1
    if kept == len(new) == len(old):
        for pdf in sorted(scratch.glob("*.pdf")):
            if (out_dir / pdf.name).exists():
                shutil.copyfile(pdf, out_dir / pdf.name)
    return kept, len(new) - kept


def main(argv=None) -> int:
    args = sys.argv[1:] if argv is None else argv
    if len(args) != 3 or args[0] not in ("stash", "keep", "compare"):
        sys.stderr.write(__doc__)
        return 2
    if args[0] == "stash":
        stash(Path(args[1]), Path(args[2]))
        return 0
    if args[0] == "keep":
        kept, redrawn = keep(Path(args[1]), Path(args[2]))
        print(f"plots: {kept + redrawn} pages, {kept} draw the same as before and keep their file, "
              f"{redrawn} redrawn")
        return 0
    args = args[1:]
    problems = compare_dirs(Path(args[0]), Path(args[1]))
    for p in problems:
        sys.stderr.write(p + "\n")
    if problems:
        sys.stderr.write("plots differ in what they draw\n")
        return 1
    n = len(list(Path(args[1]).glob("*.svg")))
    print(f"plotcmp: {n} plotted pages draw the same elements as the committed ones")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
