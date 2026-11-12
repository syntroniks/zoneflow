# SPDX-License-Identifier: Apache-2.0
"""The hand-off file of the review walkthrough: read it, check it, record it in the repository.

The walkthrough page writes one Markdown file with the owner's decisions: an
instruction block for a coding agent, the decisions as a fenced JSON block, and
the same as text. ``reviewwalk record`` takes that file, checks the JSON against
the schema and against the board and the walkthrough as they are in the
repository now, and writes a dated record under ``docs/design/reviews/``.

The schema is ``docs/hardware/review/handoff.schema.json``. ``validate`` below
states the same rules in Python, so that recording needs no package, and a test
holds the two against each other.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

from . import spec as specmod
from .boardtext import BoardText, sha256

SCHEMA = "zoneflow-review-handoff"
VERSION = 1
BOARD_FILE = "hardware/zoneflow-a/zoneflow-a.kicad_pcb"
RECORDS = specmod.ROOT / "docs" / "design" / "reviews"
INDEX_MARKER = "<!-- zf:review-records -->"
DECISIONS = {"check": ("pass", "fail", "not_sure", "not_answered"), "decision": ("accepted", "rejected", "not_sure", "not_answered")}
DONE = ("pass", "accepted")
NEEDS_CHANGE = ("fail", "rejected")
CHOICES = ("agent", "listed", "own")
SUMMARY_KEYS = ("steps", "pass", "accepted", "fail", "rejected", "not_sure", "not_answered")
CONTEXT_KEYS = ("designators", "nets", "position", "pictures", "files", "source", "findings")
WORD = {"pass": "pass", "accepted": "accepted", "fail": "fail", "rejected": "rejected", "not_sure": "not sure",
        "not_answered": "not answered"}
_FENCE = re.compile(r"^```json[ \t]*\n(.*?)\n```[ \t]*$", re.S | re.M)
_HEX64 = re.compile(r"^[0-9a-f]{64}$")
_DATE = re.compile(r"^\d{4}-\d{2}-\d{2}$")
_COMMIT = re.compile(r"^([0-9a-f]{40}( with local changes)?|unknown)$")


class HandoffError(Exception):
    pass


def extract(text: str) -> dict:
    """The decisions out of a hand-off file: the JSON itself, or the fenced JSON block of the Markdown."""
    candidates = [text] if text.lstrip().startswith("{") else _FENCE.findall(text)
    for candidate in candidates:
        try:
            doc = json.loads(candidate)
        except json.JSONDecodeError:
            continue
        if isinstance(doc, dict) and doc.get("schema") == SCHEMA:
            return doc
    raise HandoffError(f'no JSON block with "schema": "{SCHEMA}" in the file')


def _is_str_list(value) -> bool:
    return isinstance(value, list) and all(isinstance(v, str) for v in value)


def validate(doc) -> list[str]:
    """The rules of handoff.schema.json. A list of sentences, empty when the document keeps them."""
    if not isinstance(doc, dict):
        return ["the document is not an object"]
    bad = []
    if doc.get("schema") != SCHEMA:
        bad.append(f'schema is not "{SCHEMA}"')
    if doc.get("schema_version") != VERSION or isinstance(doc.get("schema_version"), bool):
        bad.append(f"schema_version is not {VERSION}: this program reads version {VERSION} only")
    if not isinstance(doc.get("repository"), str) or not doc.get("repository"):
        bad.append("repository is missing")
    if not isinstance(doc.get("issue"), int) or isinstance(doc.get("issue"), bool):
        bad.append("issue is not a number")
    board = doc.get("board")
    if not isinstance(board, dict) or not isinstance(board.get("revision"), str) or not isinstance(board.get("file"), str) \
            or not isinstance(board.get("sha256"), str) or not _HEX64.match(board["sha256"]):
        bad.append("board needs revision, file and a sha256 of 64 hex digits")
    if not isinstance(doc.get("site_commit"), str) or not _COMMIT.match(doc["site_commit"]):
        bad.append('site_commit is not a commit hash or "unknown"')
    if not isinstance(doc.get("reviewed_on"), str) or not _DATE.match(doc["reviewed_on"]):
        bad.append("reviewed_on is not a date as YYYY-MM-DD")
    summary = doc.get("summary")
    if not isinstance(summary, dict) or any(not isinstance(summary.get(k), int) or isinstance(summary.get(k), bool) or summary[k] < 0
                                            for k in SUMMARY_KEYS):
        bad.append("summary needs a count for each of " + ", ".join(SUMMARY_KEYS))
    decisions = doc.get("decisions")
    if not isinstance(decisions, list):
        return bad + ["decisions is not a list"]
    seen = set()
    for i, d in enumerate(decisions):
        name = f"decisions[{i}]"
        if not isinstance(d, dict):
            bad.append(f"{name} is not an object")
            continue
        if isinstance(d.get("step"), str) and d["step"]:
            name = f"decision {d['step']}"
            if d["step"] in seen:
                bad.append(f"{name} is there twice")
            seen.add(d["step"])
        else:
            bad.append(f"{name} has no step")
        for key in ("title", "group", "note"):
            if not isinstance(d.get(key), str):
                bad.append(f"{name}: {key} is not text")
        kind, decision = d.get("kind"), d.get("decision")
        if kind not in DECISIONS:
            bad.append(f"{name}: kind is not check or decision")
            continue
        if decision not in DECISIONS[kind]:
            bad.append(f"{name}: a {kind} is one of {', '.join(DECISIONS[kind])}, not {decision!r}")
            continue
        if decision in DONE:
            continue
        # What an agent acts on carries its context.
        if not _is_str_list(d.get("designators")) or not _is_str_list(d.get("nets")) or not _is_str_list(d.get("files")) \
                or not _is_str_list(d.get("findings")) or not isinstance(d.get("source"), str) or not isinstance(d.get("pictures"), list):
            bad.append(f"{name}: a step that is not passed needs {', '.join(CONTEXT_KEYS)}")
        position = d.get("position", "missing")
        if position is not None and not (isinstance(position, dict) and all(
                isinstance(position.get(k), (int, float)) and not isinstance(position.get(k), bool)
                for k in ("x_mm", "y_mm", "kicad_x_mm", "kicad_y_mm"))):
            bad.append(f"{name}: position is null or four numbers in millimetres")
        if decision in NEEDS_CHANGE:
            remedy = d.get("remedy")
            if not isinstance(remedy, dict) or remedy.get("choice") not in CHOICES or not isinstance(remedy.get("text"), str):
                bad.append(f"{name}: a {decision} step needs a remedy with choice ({', '.join(CHOICES)}) and text")
            elif remedy["choice"] == "listed" and not (isinstance(remedy.get("id"), str) and remedy["id"]):
                bad.append(f"{name}: a listed remedy needs its id")
            elif remedy["choice"] == "own" and not remedy["text"].strip():
                bad.append(f"{name}: the owner's own remedy has no text")
    return bad


def count(doc) -> dict:
    out = {k: 0 for k in SUMMARY_KEYS}
    out["steps"] = len(doc["decisions"])
    for d in doc["decisions"]:
        out[d["decision"]] += 1
    return out


def validate_against(doc, spec, board=specmod.BOARD, allow_other_board=False) -> list[str]:
    """The document against the walkthrough and the board as they are now. Call after ``validate``."""
    bad = []
    if doc["board"]["file"] != BOARD_FILE:
        bad.append(f"the board file is {doc['board']['file']}, and this repository reviews {BOARD_FILE}")
    now = sha256(board)
    if doc["board"]["sha256"] != now and not allow_other_board:
        bad.append(f"the review was made of a board file with SHA-256 {doc['board']['sha256'][:16]}, and the board file in the "
                   f"repository now has {now[:16]}: check out the commit the review was made of, or pass --allow-other-board "
                   "to record it as a review of another board")
    steps = {s["id"]: s for s in spec["step"]}
    text = BoardText(board)
    for d in doc["decisions"]:
        step = steps.get(d["step"])
        if step is None:
            bad.append(f"decision {d['step']}: the walkthrough has no such step")
            continue
        kind = step.get("kind", "check")
        if d["kind"] != kind:
            bad.append(f"decision {d['step']}: the step is a {kind}, the file says {d['kind']}")
        if d["decision"] in DONE:
            continue
        for ref in d["designators"]:
            if not text.has_ref(ref):
                bad.append(f"decision {d['step']}: the board has no part {ref}")
        for net in d["nets"]:
            if not text.has_net(net):
                bad.append(f"decision {d['step']}: the board has no net {net}")
        for path in d["files"]:
            if not (specmod.ROOT / path).is_file():
                bad.append(f"decision {d['step']}: the repository has no file {path}")
        remedy = d.get("remedy")
        if remedy and remedy["choice"] == "listed" and remedy["id"] not in {r["id"] for r in specmod.step_remedies(spec, step)}:
            bad.append(f"decision {d['step']}: the step offers no remedy {remedy['id']}")
    missing = [sid for sid in steps if sid not in {d["step"] for d in doc["decisions"]}]
    if missing:
        bad.append("the file has no decision for: " + ", ".join(missing))
    if doc["summary"] != count(doc):
        bad.append("the summary does not count the decisions")
    return bad


def _line(text: str) -> str:
    return " ".join(str(text).split())


def remedy_text(d) -> str:
    remedy = d.get("remedy")
    if not remedy:
        return ""
    if remedy["choice"] == "agent":
        return "Not chosen. The agent proposes one."
    who = "The owner's own" if remedy["choice"] == "own" else f"Chosen from the list (`{remedy['id']}`)"
    return f"{who}: {_line(remedy['text'])}"


def detail(d) -> list[str]:
    """One decision that is not passed, as Markdown lines. The page writes the same text."""
    lines = [f"### {d['title']}", "", f"- **Step:** `{d['step']}`, {d.get('group_title') or d['group']}",
             f"- **Decision:** {WORD[d['decision']]}", f"- **Owner's note:** {_line(d['note']) or 'none'}"]
    if d["decision"] in NEEDS_CHANGE:
        lines.append(f"- **Remedy:** {remedy_text(d)}")
    if d["designators"]:
        lines.append("- **Parts:** " + ", ".join(d["designators"]))
    if d["nets"]:
        lines.append("- **Nets:** " + ", ".join(f"`{n}`" for n in d["nets"]))
    p = d["position"]
    if p:
        lines.append(f"- **Position:** ({p['x_mm']:g}, {p['y_mm']:g}) mm from the lower left corner of the board. "
                     f"KiCad's default display shows ({p['kicad_x_mm']:g}, {p['kicad_y_mm']:g})")
    if d["files"]:
        lines.append("- **Files a change would be made in:** " + ", ".join(f"`{f}`" for f in d["files"]))
    if d["pictures"]:
        lines.append("- **Pictures:** " + ", ".join(f"`{p['file']}`" for p in d["pictures"]))
    lines.append(f"- **Comes from:** {_line(d['source'])}" + (". Findings " + ", ".join(d["findings"]) if d["findings"] else ""))
    lines.append("")
    return lines


def readable(doc) -> list[str]:
    """The decisions as text, in the order an agent works: changes, questions, open steps, the rest."""
    lines = []
    for title, want, empty in (
        ("Needs a change", NEEDS_CHANGE, "No step failed and none was rejected."),
        ("Not sure: ask the owner", ("not_sure",), "The owner was sure of every answer given."),
        ("Not answered", ("not_answered",), "Every step has an answer."),
    ):
        mine = [d for d in doc["decisions"] if d["decision"] in want]
        lines += [f"## {title} ({len(mine)})", ""]
        if not mine:
            lines += [empty, ""]
        for d in mine:
            lines += detail(d)
    done = [d for d in doc["decisions"] if d["decision"] in DONE]
    lines += [f"## Passed and accepted: change nothing ({len(done)})", ""]
    for d in done:
        note = _line(d["note"])
        lines.append(f"- {WORD[d['decision']]}: {d['title']} (`{d['step']}`)" + (f". Note: {note}" if note else ""))
    lines.append("")
    return lines


def record_markdown(doc, other_board: bool) -> str:
    """The page for docs/design/reviews/."""
    s = doc["summary"]
    date = doc["reviewed_on"]
    lines = [f"# Pre-order review of revision {doc['board']['revision']}, {date}", "",
             '!!! warning "One person\'s look at design files"',
             "    No board has been made. Each decision below is the owner's answer to a step of the",
             "    [review walkthrough](../../hardware/review/index.md): a look at the board file, a print, a",
             "    datasheet or an order page. A pass does not show that the board works.", ""]
    if other_board:
        lines += ['!!! danger "A review of another board file"',
                  "    When this was recorded, the board file in the repository was not the one that was reviewed.",
                  "    The parts, nets and files named here were checked against the newer board.", ""]
    lines += [f"Recorded by `reviewwalk record` from a hand-off file of the walkthrough. The decisions as data: "
              f"[`{date}-pre-order.json`]({date}-pre-order.json).", "",
              "| | |", "|---|---|",
              f"| Board file | `{doc['board']['file']}` |",
              f"| SHA-256 of the board file | `{doc['board']['sha256']}` |",
              f"| Commit the walkthrough page was built from | `{doc['site_commit']}` |",
              f"| Tracked in | [issue #{doc['issue']}](https://github.com/{doc['repository']}/issues/{doc['issue']}) |", "",
              "| Steps | Pass | Accepted | Fail | Rejected | Not sure | Not answered |", "|---|---|---|---|---|---|---|",
              f"| {s['steps']} | {s['pass']} | {s['accepted']} | {s['fail']} | {s['rejected']} | {s['not_sure']} | {s['not_answered']} |", ""]
    ready = s["pass"] + s["accepted"] == s["steps"]
    lines += ["Every step is passed or accepted. What an order still needs is the owner's decision." if ready else
              "Not every step is passed or accepted. The board is not ready to order.", ""]
    return "\n".join(lines + readable(doc))


def record(path, out_dir=None, board=specmod.BOARD, data=specmod.DATA, force=False, allow_other_board=False):
    """Check a hand-off file and write its record. Returns the two paths written."""
    out_dir = RECORDS if out_dir is None else out_dir
    doc = extract(Path(path).read_text(encoding="utf-8"))
    bad = validate(doc)
    if not bad:
        bad = validate_against(doc, specmod.load(data), board, allow_other_board)
    if bad:
        raise HandoffError("\n".join(bad))
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    name = f"{doc['reviewed_on']}-pre-order"
    page, raw = out_dir / f"{name}.md", out_dir / f"{name}.json"
    if (page.exists() or raw.exists()) and not force:
        raise HandoffError(f"{page} is there already: pass --force to write it again")
    other = doc["board"]["sha256"] != sha256(board)
    page.write_text(record_markdown(doc, other), encoding="utf-8")
    raw.write_text(json.dumps(doc, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    index = out_dir / "index.md"
    if index.is_file():
        text = index.read_text(encoding="utf-8")
        s = doc["summary"]
        entry = (f"- [{doc['reviewed_on']}]({name}.md): {s['pass']} pass, {s['accepted']} accepted, {s['fail']} fail, "
                 f"{s['rejected']} rejected, {s['not_sure']} not sure, {s['not_answered']} not answered")
        if INDEX_MARKER in text and f"]({name}.md)" not in text:
            index.write_text(text.replace(INDEX_MARKER, f"{INDEX_MARKER}\n{entry}", 1), encoding="utf-8")
    return page, raw
