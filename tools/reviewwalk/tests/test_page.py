# SPDX-License-Identifier: Apache-2.0
"""The page builder, and the page logic under Node."""

import re
import subprocess
from html.parser import HTMLParser

import pytest
from conftest import TOOL, needs_node

from reviewwalk import page, spec


@pytest.fixture(scope="module")
def builder():
    return page.Builder(spec.load(), page.load_manifest(), "../../")


class Outline(HTMLParser):
    VOID = {"img", "input", "link", "br", "hr", "meta"}

    def __init__(self):
        super().__init__()
        self.stack, self.ids, self.images, self.errors = [], [], [], []

    def handle_starttag(self, tag, attrs):
        a = dict(attrs)
        if "id" in a:
            self.ids.append(a["id"])
        if tag == "img":
            self.images.append(a)
        if tag not in self.VOID:
            self.stack.append(tag)

    def handle_endtag(self, tag):
        if tag in self.VOID:
            return
        if not self.stack or self.stack[-1] != tag:
            self.errors.append(f"</{tag}> closes {self.stack[-1] if self.stack else 'nothing'}")
        else:
            self.stack.pop()


def test_the_walkthrough_is_well_formed_and_complete(builder):
    html = builder.walkthrough()
    o = Outline()
    o.feed(html)
    assert o.errors == [] and o.stack == []
    assert len(o.ids) == len(set(o.ids)), "an id is there twice"
    assert sum(i.startswith("step-") for i in o.ids) == 47
    assert sum(i.startswith("finding-") for i in o.ids) == 30
    for img in o.images:
        assert img.get("alt") is not None, img
    # Each step has three answers and a labelled note.
    assert html.count('type="radio"') == 47 * 3
    assert html.count('class="zf-note"') == 47 == html.count('class="zf-note-label" for="note-')


def test_every_link_inside_the_page_has_a_target(builder):
    html = builder.walkthrough()
    ids = set(re.findall(r'id="([^"]+)"', html))
    for target in re.findall(r'href="#([^"]+)"', html):
        assert target in ids, target


def test_links_and_coordinates_are_resolved(builder):
    assert builder.url("doc", "design/hardware-layout-review.md", "#2-findings") == "../../design/hardware-layout-review/#2-findings"
    assert builder.url("doc", "hardware/index.md") == "../../hardware/"
    assert builder.url("out", "zoneflow-a-pin1.csv") == "../../hardware/zoneflow-a/zoneflow-a-pin1.csv"
    assert builder.url("repo", "hardware/netlist.yaml").endswith("/blob/main/hardware/netlist.yaml")
    step = next(s for s in builder.steps if s["id"] == "k-u1-vias")
    box = builder.kicad_box(step)
    assert "(21.5, 33)" in box and "(111.5, 127)" in box  # contract coordinates and KiCad's page coordinates
    html = builder.walkthrough()
    assert not any(form in html for form in ("(doc:", "(out:", "(repo:", "\"doc:", "\"out:"))


def test_the_bench_sheet_has_both_sides_the_pin_table_and_every_step(builder):
    html = builder.bench_sheet(spec.BOARD_DIR / "output" / "zoneflow-a-pin1.csv")
    o = Outline()
    o.feed(html)
    assert o.errors == [] and o.stack == []
    assert 'style="width:142mm;height:160mm"' in html
    assert html.count("assembly-top.svg") == 1 and html.count("assembly-bottom.svg") == 1
    assert ">D25</th>" in html and ">U4</th>" in html
    assert len(re.findall(r"<tr><td>\d+</td>", html)) == 47


@needs_node
def test_the_page_logic_under_node():
    result = subprocess.run(["node", "--test", str(TOOL / "tests" / "js" / "review.test.cjs")], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
