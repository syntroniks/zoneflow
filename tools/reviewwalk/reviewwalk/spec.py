# SPDX-License-Identifier: Apache-2.0
"""The content of the walkthrough: groups, steps, findings and the pictures they ask for.

The content is TOML in ``docs/hardware/review/data/``. This module loads it,
checks its shape, and lists the pictures, the parts and the nets it names. It
needs Python only.
"""

from __future__ import annotations

import hashlib
import json
import re
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DATA = ROOT / "docs" / "hardware" / "review" / "data"
BOARD_DIR = ROOT / "hardware" / "zoneflow-a"
BOARD = BOARD_DIR / "zoneflow-a.kicad_pcb"
TOP_RENDER = BOARD_DIR / "output" / "zoneflow-a-top.png"
OUT = BOARD_DIR / "output" / "review"
MANIFEST = "manifest.json"

# Drawings that are made once and that any step may show.
SHARED = ("assembly-top.svg", "assembly-bottom.svg", "overview-overlay.svg")
STEP_TEXT = ("check", "why", "good", "bad", "how", "fact", "cannot")
STEP_REQUIRED = ("id", "group", "title", "source", "check", "why", "good", "bad", "how")
KINDS = ("check", "decision")
SEVERITIES = ("Blocking", "Major", "Minor", "Note")
STATUSES = ("fixed", "open", "accepted", "no-defect")
_ID = re.compile(r"^[a-z0-9][a-z0-9-]*$")
# A token that looks like a reference designator: one of the board's letter codes and a number.
_REF_TOKEN = re.compile(r"(?<![A-Za-z0-9_/.#-])(BT|RN|SW|TP|JP|DS|[RCDQUJKFLYH])(\d{1,3})(?![A-Za-z0-9_-]|\.\d)")


class SpecError(Exception):
    pass


def load(data_dir=DATA):
    """All content as one dictionary: meta, blocks, groups, steps, findings, earlier."""
    data_dir = Path(data_dir)
    out = {}
    for path in sorted(data_dir.glob("*.toml")):
        with path.open("rb") as f:
            for key, value in tomllib.load(f).items():
                if isinstance(value, list):
                    out.setdefault(key, []).extend(value)
                elif key in out:
                    raise SpecError(f"{path.name}: [{key}] is already in another file")
                else:
                    out[key] = value
    validate(out)
    return out


def validate(spec):
    for key in ("meta", "block", "group", "step", "finding", "earlier"):
        if key not in spec:
            raise SpecError(f"the content has no [{key}]")
    groups = [g["id"] for g in spec["group"]]
    seen = set()
    for s in spec["step"]:
        for k in STEP_REQUIRED:
            if not str(s.get(k, "")).strip():
                raise SpecError(f"step {s.get('id', '?')}: no {k}")
        if not _ID.match(s["id"]):
            raise SpecError(f"step id {s['id']!r}: lower-case letters, digits and hyphens only")
        if s["id"] in seen:
            raise SpecError(f"step {s['id']} is there twice")
        seen.add(s["id"])
        if s["group"] not in groups:
            raise SpecError(f"step {s['id']}: no group {s['group']}")
        if s.get("kind", "check") not in KINDS:
            raise SpecError(f"step {s['id']}: kind must be one of {', '.join(KINDS)}")
    for owner in spec["step"] + spec["group"]:
        ids = set()
        for r in owner.get("remedies", []):
            if not _ID.match(str(r.get("id", ""))) or not str(r.get("text", "")).strip():
                raise SpecError(f"{owner['id']}: a remedy needs an id of lower-case letters, digits and hyphens, and a text")
            if r["id"] in ids or r["id"] in ("agent", "own"):
                raise SpecError(f"{owner['id']}: remedy id {r['id']} is taken")
            ids.add(r["id"])
        if not all(isinstance(f, str) and f and not f.startswith("/") for f in owner.get("files", [])):
            raise SpecError(f"{owner['id']}: files are paths from the root of the repository")
    finding_ids = set()
    for f in spec["finding"]:
        for k in ("id", "severity", "where", "found", "action", "status", "check"):
            if not str(f.get(k, "")).strip():
                raise SpecError(f"finding {f.get('id', '?')}: no {k}")
        if f["severity"] not in SEVERITIES:
            raise SpecError(f"finding {f['id']}: severity {f['severity']!r}")
        if f["status"] not in STATUSES:
            raise SpecError(f"finding {f['id']}: status {f['status']!r}")
        if f["id"] in finding_ids:
            raise SpecError(f"finding {f['id']} is there twice")
        finding_ids.add(f["id"])
        for sid in f.get("steps", []):
            if sid not in seen:
                raise SpecError(f"finding {f['id']}: no step {sid}")
    for s in spec["step"]:
        for fid in s.get("findings", []):
            if fid not in finding_ids:
                raise SpecError(f"step {s['id']}: no finding {fid}")
    names = set()
    for owner, fig in all_figures(spec):
        if not _ID.match(str(fig.get("id", ""))):
            raise SpecError(f"{owner}: a figure needs an id of lower-case letters, digits and hyphens")
        if not str(fig.get("alt", "")).strip() or not str(fig.get("caption", "")).strip():
            raise SpecError(f"{owner}, figure {fig['id']}: alt and caption are both needed")
        if "asset" in fig:
            if fig["asset"] not in SHARED:
                raise SpecError(f"{owner}, figure {fig['id']}: asset must be one of {', '.join(SHARED)}")
            continue
        name = figure_name(owner, fig)
        if name in names:
            raise SpecError(f"picture {name} is asked for twice")
        names.add(name)


def all_figures(spec):
    """Every picture a step or finding shows: (id of its owner, its description)."""
    for s in spec["step"]:
        for fig in s.get("figure", []):
            yield s["id"], fig
    for f in spec["finding"]:
        for fig in f.get("figure", []):
            yield f["id"].lower(), fig


def figures(spec):
    """The pictures that are drawn for one owner. A figure with "asset" shows a shared drawing instead."""
    for owner, fig in all_figures(spec):
        if "asset" not in fig:
            yield owner, fig


def figure_name(owner, fig):
    return f"{owner}-{fig['id']}.svg"


RENDER_KEYS = ("id", "view", "box", "around", "margin", "layers", "width", "zones", "holes", "grid", "marks", "pad_labels",
               "pad_label_mode", "callouts", "minimap", "values", "text")


def render_inputs(spec):
    """What the pictures are drawn from, apart from the board: the same text gives the same pictures."""
    figs = [[owner, {k: fig[k] for k in RENDER_KEYS if k in fig}] for owner, fig in figures(spec)]
    blocks = [{"box": b["box"], "badge": b.get("badge")} for b in spec["block"]]
    return json.dumps({"figures": figs, "blocks": blocks, "print_note": spec["meta"].get("print_note", "")},
                      sort_keys=True, separators=(",", ":"))


def inputs_hash(spec, board=BOARD, top_render=TOP_RENDER):
    """One hash over everything a picture depends on: board, picture descriptions, drawing code, and
    where the board is in the top render.

    Of the render only the place of the board counts, not its bytes: KiCad does not write the same
    file twice, and the numbered boxes depend on nothing else in it.
    """
    from . import png

    h = hashlib.sha256()
    here = Path(__file__).resolve().parent
    for path in (Path(board), here / "render.py", here / "png.py"):
        h.update(hashlib.sha256(path.read_bytes()).hexdigest().encode())
    h.update(json.dumps(png.find_edges(top_render), sort_keys=True).encode())
    h.update(render_inputs(spec).encode())
    return h.hexdigest()


def named_refs(spec):
    """Reference designators the content names: {designator: where it is named}.

    From the picture descriptions, and from every designator-like word in the text
    of the steps and findings. A finding id such as L5 is not a part, and neither
    is a word listed in meta.not_parts.
    """
    not_parts = set(spec["meta"].get("not_parts", [])) | {f["id"] for f in spec["finding"]}
    out = {}

    def add(ref, where):
        out.setdefault(ref, where)

    def scan(text, where):
        for m in _REF_TOKEN.finditer(str(text)):
            token = m.group(1) + m.group(2)
            if token not in not_parts:
                add(token, where)

    for owner, fig in figures(spec):
        where = f"picture {figure_name(owner, fig)}"
        for ref in fig.get("around", []) + fig.get("pad_labels", []):
            add(ref, where)
        for m in fig.get("marks", []):
            for ref in m.get("refs", []):
                add(ref, where)
        for c in fig.get("callouts", []):
            if "ref" in c:
                add(c["ref"], where)
            if "pad" in c:
                add(c["pad"].partition(".")[0], where)
            scan(c.get("text", ""), where)
        scan(fig.get("caption", ""), where)
    for s in spec["step"]:
        for k in ("title",) + STEP_TEXT:
            scan(s.get(k, ""), f"step {s['id']}, {k}")
        for ref in s.get("kicad", {}).get("find", []):
            add(ref, f"step {s['id']}, kicad.find")
        if "ibom" in s and "ref" in s["ibom"]:
            add(s["ibom"]["ref"], f"step {s['id']}, ibom")
    for f in spec["finding"]:
        for k in ("where", "found", "action", "check"):
            scan(f.get(k, ""), f"finding {f['id']}, {k}")
    for b in spec["block"]:
        scan(b.get("text", ""), f"block {b['name']}")
    return out


def _unique(items):
    seen, out = set(), []
    for item in items:
        if item not in seen:
            seen.add(item)
            out.append(item)
    return out


def step_remedies(spec, step):
    """The remedies a step offers: its own, then those of its group."""
    group = next(g for g in spec["group"] if g["id"] == step["group"])
    own = list(step.get("remedies", []))
    shared = group.get("remedies", []) if step.get("group_remedies", True) else []
    taken = {r["id"] for r in own}
    return own + [r for r in shared if r["id"] not in taken]


def step_context(spec, step, kicad_origin):
    """What a coding agent needs to act on a step with no other context.

    The parts and nets the step is about, where on the board, the pictures, the source files a change
    would be made in, the remedies offered, and where the step comes from.
    """
    group = next(g for g in spec["group"] if g["id"] == step["group"])
    kicad = step.get("kicad", {})
    refs, nets = list(kicad.get("find", [])), list(kicad.get("nets", []))
    if "ref" in step.get("ibom", {}):
        refs.append(step["ibom"]["ref"])
    pictures = []
    for fig in step.get("figure", []):
        if "asset" in fig:
            pictures.append({"file": f"hardware/zoneflow-a/output/review/{fig['asset']}"})
            continue
        refs += fig.get("around", []) + fig.get("pad_labels", [])
        for m in fig.get("marks", []):
            refs += m.get("refs", [])
            nets += m.get("nets", [])
        picture = {"file": f"hardware/zoneflow-a/output/review/{figure_name(step['id'], fig)}",
                   "view": fig.get("view", "top")}
        if "box" in fig:
            picture["box_mm"] = [float(v) for v in fig["box"]]
        pictures.append(picture)
    position = None
    if "at" in kicad:
        x, y = (float(v) for v in kicad["at"])
        position = {"x_mm": x, "y_mm": y, "kicad_x_mm": round(kicad_origin[0] + x, 3), "kicad_y_mm": round(kicad_origin[1] - y, 3)}
    return {
        "id": step["id"], "title": step["title"], "group": group["id"], "group_title": group["title"],
        "kind": step.get("kind", "check"), "designators": _unique(refs), "nets": _unique(nets), "position": position,
        "pictures": pictures, "files": _unique(list(step.get("files", [])) + list(group.get("files", []))),
        "remedies": [{"id": r["id"], "text": r["text"]} for r in step_remedies(spec, step)],
        "source": step["source"], "findings": list(step.get("findings", [])),
    }


def named_files(spec):
    """Files of the repository that steps and groups name for an implementer: {path: where}."""
    out = {}
    for g in spec["group"]:
        for f in g.get("files", []):
            out.setdefault(f, f"group {g['id']}")
    for s in spec["step"]:
        for f in s.get("files", []):
            out.setdefault(f, f"step {s['id']}")
    return out


def named_nets(spec):
    """Nets the pictures and the KiCad hints name: {net: where}."""
    out = {}
    for owner, fig in figures(spec):
        for m in fig.get("marks", []):
            for net in m.get("nets", []):
                out.setdefault(net, f"picture {figure_name(owner, fig)}")
    for s in spec["step"]:
        for net in s.get("kicad", {}).get("nets", []):
            out.setdefault(net, f"step {s['id']}, kicad.nets")
        if "ibom" in s and "net" in s["ibom"]:
            out.setdefault(s["ibom"]["net"], f"step {s['id']}, ibom")
    return out
