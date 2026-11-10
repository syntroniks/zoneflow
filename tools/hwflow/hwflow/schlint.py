# SPDX-License-Identifier: Apache-2.0
"""Find drawing faults in a generated schematic that ERC does not look for.

Symbols are placed by coordinate and nothing in KiCad says when a reference
lands on a wire or two symbols sit on top of each other. Two checks do:

- **On the plotted page.** ``kicad-cli sch export svg`` draws every text as
  strokes inside ``<g class="stroked-text">``. That gives the real extent of
  each text, whatever its font, justification or the transform of its symbol.
  A text must not overlap another text, must not be crossed by any line that
  is not part of it (a wire, a symbol outline, a frame), and must be on the
  page.
- **On the model.** No symbol body may overlap another, and no wire may run
  through a symbol body.

Each finding names the sheet, what collides and where, in page millimetres.
"""

from __future__ import annotations

import re
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

from .design import Design

SVG = "{http://www.w3.org/2000/svg}"
MARGIN = 12.0  # inner frame of KiCad's default drawing sheet, mm from the page edge
TITLE_BLOCK = (108.0, 32.0)  # width and height of the title block, bottom right
SHRINK = 0.12  # a line may come this close to a text's extent, mm
OVERLAP = 0.15  # two texts must overlap by more than this in both axes to count, mm


@dataclass
class Text:
    text: str
    box: tuple[float, float, float, float]  # x0, y0, x1, y1


def _numbers(d: str) -> list[tuple[str, list[float]]]:
    out = []
    for cmd, args in re.findall(r"([MLHVZCAQmlhvzcaq])([^MLHVZCAQmlhvzcaq]*)", d):
        out.append((cmd, [float(v) for v in re.findall(r"-?\d+(?:\.\d+)?(?:e-?\d+)?", args)]))
    return out


def _polyline(d: str) -> list[tuple[float, float]]:
    """Vertices of a path. Curves are reduced to their end points, which is enough to find a crossing."""
    pts: list[tuple[float, float]] = []
    for cmd, v in _numbers(d):
        if cmd in "ML":
            pts += list(zip(v[0::2], v[1::2]))
        elif cmd == "C":
            pts += list(zip(v[4::6], v[5::6]))
        elif cmd == "Q":
            pts += list(zip(v[2::4], v[3::4]))
        elif cmd == "A":
            pts += list(zip(v[5::7], v[6::7]))
        elif cmd in "Zz" and pts:
            pts.append(pts[0])
    return pts


def read_svg(path: Path) -> tuple[list[Text], list[tuple], tuple[float, float]]:
    """Texts, other line segments, and the page size of one plotted sheet."""
    root = ET.parse(path).getroot()
    vb = [float(v) for v in root.get("viewBox").split()]
    page = (vb[2], vb[3])
    texts: list[Text] = []
    segments: list[tuple] = []

    def walk(el, in_text):
        tag = el.tag.replace(SVG, "")
        if tag == "g" and el.get("class") == "stroked-text":
            desc = el.find(f"{SVG}desc")
            pts = [p for path_el in el.iter(f"{SVG}path") for p in _polyline(path_el.get("d", ""))]
            if pts:
                xs, ys = [p[0] for p in pts], [p[1] for p in pts]
                texts.append(Text(desc.text if desc is not None and desc.text else "",
                                  (min(xs), min(ys), max(xs), max(ys))))
            return
        if tag == "path":
            pts = _polyline(el.get("d", ""))
            segments.extend(zip(pts, pts[1:]))
        elif tag == "rect":
            x, y, w, h = (float(el.get(k)) for k in ("x", "y", "width", "height"))
            if w < page[0] - 1:  # not the page background
                c = [(x, y), (x + w, y), (x + w, y + h), (x, y + h), (x, y)]
                segments.extend(zip(c, c[1:]))
        elif tag == "circle":
            cx, cy, r = (float(el.get(k)) for k in ("cx", "cy", "r"))
            c = [(cx - r, cy), (cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)]
            segments.extend(zip(c, c[1:]))
        for child in el:
            walk(child, in_text)

    walk(root, False)
    return texts, segments, page


def _crosses(seg, box) -> bool:
    """Whether a segment passes through a rectangle (Liang-Barsky clip)."""
    (x0, y0), (x1, y1) = seg
    bx0, by0, bx1, by1 = box
    if bx0 >= bx1 or by0 >= by1:
        return False
    dx, dy = x1 - x0, y1 - y0
    t0, t1 = 0.0, 1.0
    for p, q in ((-dx, x0 - bx0), (dx, bx1 - x0), (-dy, y0 - by0), (dy, by1 - y0)):
        if abs(p) < 1e-12:
            if q < 0:
                return False
            continue
        t = q / p
        if p < 0:
            t0 = max(t0, t)
        else:
            t1 = min(t1, t)
        if t0 > t1:
            return False
    return True


def lint_svg(path: Path, name: str) -> list[str]:
    texts, segments, page = read_svg(path)
    w, h = page
    tb = (w - MARGIN - TITLE_BLOCK[0], h - MARGIN - TITLE_BLOCK[1], w - MARGIN, h - MARGIN)

    def in_title(box):
        return box[2] > tb[0] and box[3] > tb[1]

    def on_frame(box):
        return box[0] < MARGIN or box[1] < MARGIN or box[2] > w - MARGIN or box[3] > h - MARGIN

    body = []
    problems = []
    plotted = set()
    for t in texts:
        if in_title(t.box):
            continue
        # KiCad plots some texts twice at the same place (once per sheet instance they are shared with).
        key = (t.text, round(t.box[0], 2), round(t.box[1], 2), round(t.box[2], 2), round(t.box[3], 2))
        if key in plotted:
            continue
        plotted.add(key)
        if on_frame(t.box):
            # The frame's own zone letters and digits sit in the border; anything else there is off the page.
            if not re.fullmatch(r"[A-Z]|\d{1,2}", t.text):
                problems.append(f"{name}: text {t.text!r} at ({t.box[0]:.1f}, {t.box[1]:.1f}) is off the page")
            continue
        body.append(t)
    lines = []
    for a, b in segments:
        box = (min(a[0], b[0]), min(a[1], b[1]), max(a[0], b[0]), max(a[1], b[1]))
        if in_title(box):
            continue
        # The frame itself, and anything in the border band, is not part of the drawing.
        if box[0] < MARGIN + 0.01 or box[1] < MARGIN + 0.01 or box[2] > w - MARGIN - 0.01 or box[3] > h - MARGIN - 0.01:
            continue
        lines.append((a, b))

    seen = set()
    for i, a in enumerate(body):
        for b in body[i + 1:]:
            ox = min(a.box[2], b.box[2]) - max(a.box[0], b.box[0])
            oy = min(a.box[3], b.box[3]) - max(a.box[1], b.box[1])
            if ox > OVERLAP and oy > OVERLAP:
                key = (a.text, b.text, round(a.box[0], 1), round(a.box[1], 1))
                if key not in seen:
                    seen.add(key)
                    problems.append(f"{name}: text {a.text!r} at ({a.box[0]:.1f}, {a.box[1]:.1f}) overlaps "
                                    f"text {b.text!r} at ({b.box[0]:.1f}, {b.box[1]:.1f})")
    for t in body:
        box = (t.box[0] + SHRINK, t.box[1] + SHRINK, t.box[2] - SHRINK, t.box[3] - SHRINK)
        for seg in lines:
            if _crosses(seg, box):
                (x0, y0), (x1, y1) = seg
                problems.append(f"{name}: text {t.text!r} at ({t.box[0]:.1f}, {t.box[1]:.1f}) is crossed by a line "
                                f"from ({x0:.1f}, {y0:.1f}) to ({x1:.1f}, {y1:.1f})")
                break
    return problems


def lint_model(design: Design) -> list[str]:
    """Symbol bodies on top of each other, and wires through symbol bodies."""
    problems = []
    for sheet in design.sheets.values():
        boxes = []
        for part in sheet.parts.values():
            if part.virtual or not part.sym.pins:
                continue
            x0, y0, x1, y1 = part.body_box()
            boxes.append((part, (x0, y0, x1, y1)))
        for i, (pa, a) in enumerate(boxes):
            for pb, b in boxes[i + 1:]:
                ox = min(a[2], b[2]) - max(a[0], b[0])
                oy = min(a[3], b[3]) - max(a[1], b[1])
                if ox > 0.1 and oy > 0.1:
                    problems.append(f"{sheet.name}: symbol {pa.ref} at {pa.at} overlaps symbol {pb.ref} at {pb.at}")
        for w in sheet.wires:
            for p0, p1 in zip(w.points, w.points[1:]):
                for part, b in boxes:
                    # The outline of some symbols reaches a little past their pin ends (the arrows of an
                    # LED), so only the core of the body counts.
                    m = min(0.9, 0.45 * min(b[2] - b[0], b[3] - b[1]))
                    inner = (b[0] + m, b[1] + m, b[2] - m, b[3] - m)
                    if _crosses((p0, p1), inner):
                        problems.append(f"{sheet.name}: a wire of net {w.net.name} from {p0} to {p1} runs through "
                                        f"symbol {part.ref}")
    return problems


def lint(design: Design, svg_dir: Path) -> list[str]:
    problems = lint_model(design)
    files = sorted(svg_dir.glob("*.svg"))
    if not files:
        problems.append(f"no plotted sheets in {svg_dir}")
    seen = set()
    for f in files:
        for p in lint_svg(f, f.stem):
            # The instances of a reused sheet are the same drawing; report each fault once.
            key = re.sub(r"\d", "#", p.split(": ", 1)[1])
            if key not in seen:
                seen.add(key)
                problems.append(p)
    return problems
