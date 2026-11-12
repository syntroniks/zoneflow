# SPDX-License-Identifier: Apache-2.0
"""The hand-off file: what the page writes, what ``reviewwalk record`` makes of it."""

import copy
import json
import re
import subprocess

import jsonschema
import pytest
from conftest import REPO, TOOL, needs_node, run_tool

from reviewwalk import handoff, page, spec

SCHEMA = json.loads((REPO / "docs" / "hardware" / "review" / "handoff.schema.json").read_text(encoding="utf-8"))
DATE = "2026-11-12"


@pytest.fixture(scope="module")
def content():
    return spec.load()


@pytest.fixture(scope="module")
def meta(content):
    html = page.Builder(content, page.load_manifest(), "../../", commit="c" * 40).walkthrough()
    return json.loads(re.search(r'<script type="application/json" id="zf-review-meta">(.*?)</script>', html, re.S).group(1))


def answers(meta):
    """A review as an owner might leave it: most steps passed, some failed, rejected, unsure, one left open."""
    out = {s["id"]: {"answer": "pass"} for s in meta["steps"]}
    out["k-u1-vias"] = {"answer": "fail", "note": "I count five vias,\nnot six.", "remedy": "add-vias"}
    out["k-dev-adc"] = {"answer": "fail", "note": "Too close to the antenna for me."}
    out["s-u5"] = {"answer": "fail", "remedy": "own", "remedyText": "Ask TI which sheet revision applies."}
    out["k-ka-n0"] = {"answer": "unsure", "note": "What does the float test see here?"}
    out["k-hole-b"]["note"] = "Fine for a prototype."
    del out["o-ask"]
    return out


def write(meta, given, tmp_path, name="handoff.md"):
    m, a = tmp_path / "meta.json", tmp_path / "answers.json"
    m.write_text(json.dumps(meta), encoding="utf-8")
    a.write_text(json.dumps(given), encoding="utf-8")
    result = subprocess.run(["node", str(TOOL / "tests" / "js" / "make_handoff.cjs"), str(m), str(a), DATE], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    out = tmp_path / name
    out.write_text(result.stdout, encoding="utf-8")
    return out


@pytest.fixture
def file(meta, tmp_path):
    return write(meta, answers(meta), tmp_path)


def test_every_step_gives_an_agent_its_context(meta, content):
    assert len(meta["steps"]) == 47
    assert meta["commit"] == "c" * 40 and re.fullmatch(r"[0-9a-f]{64}", meta["board"])
    for s in meta["steps"]:
        assert s["kind"] in ("check", "decision") and s["source"] and s["files"], s["id"]
        for f in s["files"]:
            assert (REPO / f).is_file(), f"{s['id']}: {f}"
        assert s["pictures"] or s["id"] in ("o-commands", "o-options"), s["id"]
    by_id = {s["id"]: s for s in meta["steps"]}
    u1 = by_id["k-u1-vias"]
    assert u1["designators"] == ["U1"] and u1["nets"] == ["GND"] and u1["findings"] == ["L2", "L8"]
    assert u1["position"] == {"x_mm": 21.5, "y_mm": 33.0, "kicad_x_mm": 111.5, "kicad_y_mm": 127.0}
    assert u1["pictures"][0]["file"] == "hardware/zoneflow-a/output/review/k-u1-vias-pad.svg"
    assert "hardware/zoneflow-a/design/layout.py" in u1["files"]
    assert [r["id"] for r in u1["remedies"]] == ["add-vias"]
    # A datasheet step offers its group's remedies after its own, and a step can decline them.
    assert [r["id"] for r in by_id["s-d25"]["remedies"]] == ["rotation-row", "netlist", "other-part"]
    assert by_id["p-scale"]["remedies"] == []
    assert sorted(s["id"] for s in meta["steps"] if s["kind"] == "decision") == [
        "k-dev-adc", "k-dev-j7", "k-dev-r33", "k-hole-b", "k-ka-n0", "k-open-risks"]


def test_a_decision_step_is_answered_with_accept_or_reject(content):
    html = page.Builder(content, page.load_manifest(), "../../").walkthrough()
    step = html[html.index('id="step-k-dev-adc"'):html.index('id="step-k-open-risks"')]
    assert "> Accept</label>" in step and "> Reject</label>" in step and "> Pass</label>" not in step
    assert '<option value="">Let the agent propose a remedy</option>' in step and '<option value="pin-map">' in step
    assert '<option value="own">' in step
    check = html[html.index('id="step-k-u1-vias"'):html.index('id="step-k-screws"')]
    assert "> Pass</label>" in check and "> Fail</label>" in check


def test_a_file_that_names_a_missing_source_is_refused(content):
    from reviewwalk import check

    broken = copy.deepcopy(content)
    broken["step"][0]["files"] = ["hardware/no-such-file.yaml"]
    assert any("no-such-file.yaml" in line for line in check.problems(broken))
    for mutate, message in (
        (lambda c: c["step"][0].update(kind="vote"), "kind must be"),
        (lambda c: c["step"][0].update(remedies=[{"id": "own", "text": "x"}]), "is taken"),
        (lambda c: c["step"][0].update(remedies=[{"id": "a", "text": ""}]), "a remedy needs"),
        (lambda c: c["group"][0].update(files=["/etc/passwd"]), "paths from the root"),
    ):
        bad = copy.deepcopy(content)
        mutate(bad)
        with pytest.raises(spec.SpecError, match=message):
            spec.validate(bad)


@needs_node
def test_the_page_writes_a_file_that_keeps_the_schema(file, meta):
    text = file.read_text(encoding="utf-8")
    doc = handoff.extract(text)
    jsonschema.Draft202012Validator.check_schema(SCHEMA)
    jsonschema.validate(doc, SCHEMA, cls=jsonschema.Draft202012Validator)
    assert handoff.validate(doc) == []
    assert doc["summary"] == {"steps": 47, "pass": 38, "accepted": 4, "fail": 2, "rejected": 1, "not_sure": 1, "not_answered": 1}
    assert doc["board"]["sha256"] == meta["board"] and doc["site_commit"] == "c" * 40
    by_id = {d["step"]: d for d in doc["decisions"]}
    assert list(by_id) == [s["id"] for s in meta["steps"]]
    assert by_id["k-open"] == {"step": "k-open", "title": "Open the board and run the design rule check yourself", "group": "kicad",
                               "group_title": "In KiCad", "kind": "check", "decision": "pass", "note": ""}
    assert by_id["k-hole-b"]["decision"] == "accepted" and by_id["k-hole-b"]["note"] == "Fine for a prototype."
    assert by_id["k-dev-adc"]["decision"] == "rejected" and by_id["k-dev-adc"]["remedy"] == {"choice": "agent", "text": ""}
    assert by_id["k-u1-vias"]["remedy"]["choice"] == "listed" and by_id["k-u1-vias"]["remedy"]["id"] == "add-vias"
    assert by_id["s-u5"]["remedy"] == {"choice": "own", "text": "Ask TI which sheet revision applies."}
    assert by_id["k-ka-n0"]["decision"] == "not_sure" and "remedy" not in by_id["k-ka-n0"]
    assert by_id["o-ask"]["decision"] == "not_answered" and by_id["o-ask"]["files"]


@needs_node
def test_the_text_of_the_file_is_the_text_the_record_command_writes(file):
    text = file.read_text(encoding="utf-8")
    doc = handoff.extract(text)
    assert text.split("# Decisions as text\n\n", 1)[1] == "\n".join(handoff.readable(doc))


@needs_node
def test_record_writes_the_page_and_the_data(file, tmp_path):
    out = tmp_path / "reviews"
    out.mkdir()
    (out / "index.md").write_text(f"# Records\n\n{handoff.INDEX_MARKER}\n", encoding="utf-8")
    result = run_tool("record", str(file), "--out", str(out))
    assert result.returncode == 0, result.stderr
    assert f"{DATE}-pre-order.md" in result.stdout
    md = (out / f"{DATE}-pre-order.md").read_text(encoding="utf-8")
    assert md.startswith(f"# Pre-order review of revision A, {DATE}\n")
    assert "No board has been made." in md and "A pass does not show that the board works." in md
    assert "| 47 | 38 | 4 | 2 | 1 | 1 | 1 |" in md
    assert "Not every step is passed or accepted. The board is not ready to order." in md
    assert "## Needs a change (3)" in md and "### Six vias under the buck converter" in md
    assert "- **Owner's note:** I count five vias, not six." in md
    assert "- **Remedy:** Chosen from the list (`add-vias`): Add the missing vias under the pad of U1" in md
    assert "- **Remedy:** Not chosen. The agent proposes one." in md
    assert "- **Remedy:** The owner's own: Ask TI which sheet revision applies." in md
    assert "- **Position:** (21.5, 33) mm from the lower left corner of the board. KiCad's default display shows (111.5, 127)" in md
    assert "## Not sure: ask the owner (1)" in md and "## Not answered (1)" in md
    assert "## Passed and accepted: change nothing (42)" in md
    assert "- accepted: Five tracks under the display carrier's leg at hole B (`k-hole-b`). Note: Fine for a prototype." in md
    assert "another board file" not in md
    data = json.loads((out / f"{DATE}-pre-order.json").read_text(encoding="utf-8"))
    assert data == handoff.extract(file.read_text(encoding="utf-8"))
    index = (out / "index.md").read_text(encoding="utf-8")
    assert f"- [{DATE}]({DATE}-pre-order.md): 38 pass, 4 accepted, 2 fail, 1 rejected, 1 not sure, 1 not answered" in index
    # A second record of the same date is not written over without being asked.
    again = run_tool("record", str(file), "--out", str(out))
    assert again.returncode == 1 and "is there already" in again.stderr
    assert run_tool("record", str(file), "--out", str(out), "--force").returncode == 0
    assert (out / "index.md").read_text(encoding="utf-8").count(f"({DATE}-pre-order.md)") == 1
    # The JSON alone is taken too.
    alone = tmp_path / "alone.json"
    alone.write_text(json.dumps(data), encoding="utf-8")
    assert run_tool("record", str(alone), "--out", str(tmp_path / "other")).returncode == 0


@needs_node
def test_a_review_of_another_board_is_refused_unless_asked_for(file, tmp_path):
    doc = handoff.extract(file.read_text(encoding="utf-8"))
    doc["board"]["sha256"] = "f" * 64
    other = tmp_path / "other.json"
    other.write_text(json.dumps(doc), encoding="utf-8")
    out = tmp_path / "reviews"
    result = run_tool("record", str(other), "--out", str(out))
    assert result.returncode == 1 and "the board file in the repository now has" in result.stderr
    assert not out.exists() or not list(out.iterdir())
    result = run_tool("record", str(other), "--out", str(out), "--allow-other-board")
    assert result.returncode == 0, result.stderr
    assert "A review of another board file" in (out / f"{DATE}-pre-order.md").read_text(encoding="utf-8")


@needs_node
def test_decisions_that_do_not_fit_the_walkthrough_or_the_board_are_refused(file, content):
    good = handoff.extract(file.read_text(encoding="utf-8"))
    assert handoff.validate_against(good, content) == []

    def against(mutate):
        doc = copy.deepcopy(good)
        mutate(doc, {d["step"]: d for d in doc["decisions"]})
        assert handoff.validate(doc) == [], "the mutation must keep the schema, so that the second check is what refuses it"
        return " ".join(handoff.validate_against(doc, content))

    assert "no such step" in against(lambda doc, d: d["k-open"].update(step="k-opened"))
    assert "no decision for: k-open" in against(lambda doc, d: doc["decisions"].remove(d["k-open"]))
    assert "the board has no part U999" in against(lambda doc, d: d["k-u1-vias"]["designators"].append("U999"))
    assert "the board has no net /NOPE" in against(lambda doc, d: d["k-u1-vias"]["nets"].append("/NOPE"))
    assert "no file hardware/gone.py" in against(lambda doc, d: d["k-u1-vias"]["files"].append("hardware/gone.py"))
    assert "offers no remedy burn-it" in against(lambda doc, d: d["k-u1-vias"]["remedy"].update(id="burn-it"))
    assert "the step is a decision" in against(lambda doc, d: d["k-dev-adc"].update(kind="check", decision="fail"))
    assert "summary does not count" in against(lambda doc, d: doc["summary"].update({"pass": 40}))
    assert "this repository reviews" in against(lambda doc, d: doc["board"].update(file="hardware/other.kicad_pcb"))


BREAKS = {
    "another schema": lambda doc, d: doc.update(schema="something-else"),
    "a later version": lambda doc, d: doc.update(schema_version=2),
    "no repository": lambda doc, d: doc.pop("repository"),
    "a short hash": lambda doc, d: doc["board"].update(sha256="abc"),
    "a date in words": lambda doc, d: doc.update(reviewed_on="12 November"),
    "a commit that is not one": lambda doc, d: doc.update(site_commit="main"),
    "a summary without a count": lambda doc, d: doc["summary"].pop("rejected"),
    "decisions that are no list": lambda doc, d: doc.update(decisions={}),
    "a decision without a step": lambda doc, d: d["k-open"].pop("step"),
    "a decision without a note": lambda doc, d: d["k-open"].pop("note"),
    "a kind that is none": lambda doc, d: d["k-open"].update(kind="vote"),
    "a check that is accepted": lambda doc, d: d["k-open"].update(decision="accepted"),
    "a decision that is failed": lambda doc, d: d["k-hole-b"].update(decision="fail"),
    "a failed step without its context": lambda doc, d: d["k-u1-vias"].pop("files"),
    "an unsure step without its context": lambda doc, d: d["k-ka-n0"].pop("designators"),
    "a failed step without a remedy": lambda doc, d: d["k-u1-vias"].pop("remedy"),
    "a remedy with no choice": lambda doc, d: d["k-u1-vias"]["remedy"].update(choice="maybe"),
    "a listed remedy without its id": lambda doc, d: d["k-u1-vias"]["remedy"].pop("id"),
    "own words that are empty": lambda doc, d: d["s-u5"]["remedy"].update(text="  "),
    "a position in words": lambda doc, d: d["k-u1-vias"].update(position="top left"),
    "a position with a number missing": lambda doc, d: d["k-u1-vias"]["position"].pop("kicad_y_mm"),
}


@needs_node
@pytest.mark.parametrize("name", sorted(BREAKS))
def test_the_schema_and_the_program_refuse_the_same_files(file, name):
    doc = handoff.extract(file.read_text(encoding="utf-8"))
    BREAKS[name](doc, {d["step"]: d for d in doc["decisions"]})
    assert handoff.validate(doc) != [], "the program accepts it"
    assert list(jsonschema.Draft202012Validator(SCHEMA).iter_errors(doc)) != [], "the schema accepts it"


def test_what_is_not_a_hand_off_file_is_refused(tmp_path):
    for text in ("# Notes\n\nNothing here.\n", '```json\n{"schema": "other"}\n```\n', "{ not json"):
        with pytest.raises(handoff.HandoffError, match="no JSON block"):
            handoff.extract(text)
    notes = tmp_path / "notes.md"
    notes.write_text("# Notes\n", encoding="utf-8")
    result = run_tool("record", str(notes), "--out", str(tmp_path / "out"))
    assert result.returncode == 1 and "no JSON block" in result.stderr
    assert run_tool("record", str(tmp_path / "missing.md")).returncode == 1


def test_the_repository_has_the_page_that_lists_the_records():
    index = (handoff.RECORDS / "index.md").read_text(encoding="utf-8")
    assert handoff.INDEX_MARKER in index and "reviewwalk record" in index
