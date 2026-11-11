# SPDX-License-Identifier: Apache-2.0
"""The committed content, and the rules that keep it tied to the board."""

import copy
import json
import shutil

import pytest
from conftest import run_tool

from reviewwalk import check, spec
from reviewwalk.__main__ import same_picture
from reviewwalk.boardtext import BoardText


@pytest.fixture(scope="module")
def content():
    return spec.load()


def test_the_committed_content_loads_and_names_only_what_is_on_the_board(content):
    assert check.problems(content) == []


def test_every_checklist_item_of_the_review_has_a_step(content):
    sources = " ".join(s["source"] for s in content["step"])
    for item in range(1, 8):
        assert f"section 17, item {item}" in sources or f"items 2 and {item}" in sources, f"item {item}"


def test_thirty_findings_in_order(content):
    assert [f["id"] for f in content["finding"]] == [f"L{i}" for i in range(1, 31)]
    assert sum(f["severity"] == "Blocking" for f in content["finding"]) == 1
    assert sum(f["severity"] == "Major" for f in content["finding"]) == 9


def test_every_step_has_a_picture_or_says_why_not(content):
    for s in content["step"]:
        assert s.get("figure") or str(s.get("cannot", "")).strip(), s["id"]


def test_board_text_reads_parts_nets_and_origin():
    board = BoardText(spec.BOARD)
    assert {"U1", "D25", "Q101", "J7", "BT1", "TP8", "RN2", "JP1"} <= board.refs
    assert board.has_net("/KA") and board.has_net("KA") and board.has_net("GND")
    assert not board.has_net("NO_SUCH_NET")
    assert board.origin == (90.0, 160.0)


def test_a_part_that_is_not_on_the_board_is_reported(content):
    broken = copy.deepcopy(content)
    broken["step"][0]["check"] += " Then look at R999."
    found = check.problems(broken)
    assert any("R999" in line and "no part" in line for line in found)


def test_a_net_that_is_not_on_the_board_is_reported(content):
    broken = copy.deepcopy(content)
    broken["step"][1]["figure"][0]["marks"] = [{"nets": ["/NOT_A_NET"], "label": "x"}]
    assert any("NOT_A_NET" in line for line in check.problems(broken))


def test_a_finding_id_and_a_listed_word_are_not_taken_for_parts(content):
    named = spec.named_refs(content)
    assert "L5" not in named and "L27" not in named  # finding ids
    assert "DS1" not in named  # the display: in the BOM, not on the board
    assert "L1" in named  # the inductor, which a step does name


def test_pictures_drawn_from_another_board_are_refused(content, tmp_path):
    other = tmp_path / "board.kicad_pcb"
    other.write_text(spec.BOARD.read_text(encoding="utf-8") + "\n", encoding="utf-8")
    found = check.problems(content, board=other)
    assert any("not drawn from the committed board" in line for line in found)


def test_a_changed_picture_description_makes_the_pictures_stale(content):
    changed = copy.deepcopy(content)
    changed["step"][1]["figure"][0]["box"][0] += 1.0
    assert spec.inputs_hash(changed) != spec.inputs_hash(content)
    reworded = copy.deepcopy(content)
    reworded["step"][1]["why"] += " One more sentence."
    assert spec.inputs_hash(reworded) == spec.inputs_hash(content)  # text alone does not redraw anything


def test_a_missing_and_a_stray_picture_are_reported(content, tmp_path):
    out = tmp_path / "review"
    shutil.copytree(spec.OUT, out)
    (out / "k-u1-vias-pad.svg").unlink()
    (out / "left-over.svg").write_text("<svg/>", encoding="utf-8")
    found = check.problems(content, out_dir=out)
    assert any("k-u1-vias-pad.svg is missing" in line for line in found)
    assert any("left-over.svg" in line and "belongs to no step" in line for line in found)


def test_bad_content_is_refused(content):
    for mutate, message in (
        (lambda c: c["step"][0].pop("why"), "no why"),
        (lambda c: c["step"][0].update(group="nowhere"), "no group"),
        (lambda c: c["step"].append(copy.deepcopy(c["step"][0])), "twice"),
        (lambda c: c["finding"][0].update(severity="Huge"), "severity"),
        (lambda c: c["step"][0].update(findings=["L99"]), "no finding L99"),
        (lambda c: c["step"][1]["figure"][0].pop("alt"), "alt and caption"),
    ):
        broken = copy.deepcopy(content)
        mutate(broken)
        with pytest.raises(spec.SpecError, match=message):
            spec.validate(broken)


def test_the_manifest_lists_every_drawn_picture(content):
    manifest = json.loads((spec.OUT / spec.MANIFEST).read_text(encoding="utf-8"))
    names = {spec.figure_name(owner, fig) for owner, fig in spec.figures(content)}
    assert names == set(manifest["figures"])
    assert manifest["board"] == {"width": 114.0, "height": 110.0, "kicad_origin": [90.0, 160.0]}
    for meta in manifest["figures"].values():
        x0, y0, x1, y1 = meta["box"]
        assert x1 > x0 and y1 > y0 and 0.999 <= (x1 - x0) / (y1 - y0) <= 2.501


def test_pictures_compare_with_a_tolerance():
    a = '<path d="M1.25,3.5L10,20"/><text>x 16 to 27</text>'
    assert same_picture(a, a, 0.0)
    assert same_picture(a, a.replace("1.25", "1.26"), 0.02)
    assert not same_picture(a, a.replace("1.25", "1.35"), 0.02)
    assert not same_picture(a, a.replace("L10", "L10,11"), 0.02)  # another shape, not another number


def test_the_command_line_check_passes_on_the_repository():
    result = run_tool("check")
    assert result.returncode == 0, result.stderr
    assert "47 steps, 30 findings" in result.stdout
