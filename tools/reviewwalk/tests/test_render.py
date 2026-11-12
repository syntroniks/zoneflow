# SPDX-License-Identifier: Apache-2.0
"""The picture generator against the committed board. Needs KiCad's Python; skipped without it."""

import json
import xml.etree.ElementTree as ET

import pytest
from conftest import KICAD_PY, REPO, needs_kicad, run_tool

from reviewwalk import png, spec

SVG = "{http://www.w3.org/2000/svg}"


@pytest.fixture(scope="module")
def drawn(tmp_path_factory):
    out = tmp_path_factory.mktemp("review")
    result = run_tool("generate", "--out", str(out), python=KICAD_PY)
    assert result.returncode == 0, result.stderr
    return out


@needs_kicad
def test_drawing_again_gives_the_committed_pictures(drawn):
    result = run_tool("compare", str(drawn), str(spec.OUT))
    assert result.returncode == 0, result.stderr


@needs_kicad
def test_the_buck_converter_picture_has_its_six_vias_and_its_labels(drawn):
    root = ET.parse(drawn / "k-u1-vias-pad.svg").getroot()
    text = [t.text for t in root.iter(f"{SVG}text")]
    for label in ("VIN_U1", "SW", "FB", "GND", "six vias in the pad"):
        assert label in text
    meta = json.loads((drawn / "manifest.json").read_text())["figures"]["k-u1-vias-pad.svg"]
    x0, y0, x1, y1 = meta["box"]
    s = meta["width"] / (x1 - x0)
    holes = root.find(f".//{SVG}g[@id='holes']")
    inside = 0
    for c in holes.iter(f"{SVG}circle"):
        x, y = x0 + float(c.get("cx")) / s, y1 - float(c.get("cy")) / s
        if 20.2 <= x <= 22.8 and 31.4 <= y <= 34.6:  # the exposed pad of U1, 2.4 x 3.1 mm at (21.5, 33.0)
            inside += 1
    assert inside == 6


@needs_kicad
def test_each_shape_says_what_the_board_file_says_of_it(drawn):
    root = ET.parse(drawn / "k-u1-vias-pad.svg").getroot()
    layer = root.find(f".//{SVG}g[@data-layer='F.Cu']")
    assert layer is not None and layer.get("id") == "layer-F-Cu"
    pads = {}
    for group in layer.iter(f"{SVG}g"):
        for pad in group.findall(f"{SVG}path[@data-p]"):
            pads.setdefault(pad.get("data-p"), set()).add(group.get("data-n"))
    # The nets the netlist puts on the pins of U1.
    want = {"U1.1": "GND", "U1.2": "/Power/VIN_U1", "U1.3": "/Power/UVLO", "U1.4": "/Power/RON", "U1.5": "/Power/FB",
            "U1.6": "/PGOOD", "U1.7": "/Power/BST", "U1.8": "/Power/SW", "U1.9": "GND"}
    assert {k: v for k, v in pads.items() if k.startswith("U1.")} == {k: {v} for k, v in want.items()}
    assert layer.find(f"{SVG}path[@data-zone='1'][@data-n='GND']") is not None
    # Annotation does not take a press away from the copper under it.
    for name in ("grid", "holes", "pad-labels", "notes"):
        assert root.find(f".//{SVG}g[@id='{name}']").get("pointer-events") == "none", name
    silk = ET.parse(drawn / "s-d25-pads.svg").getroot().find(f".//{SVG}g[@data-layer='F.Fab']")
    assert silk.find(f".//{SVG}g[@data-r='D25']") is not None


@needs_kicad
def test_a_bottom_view_is_mirrored(drawn):
    meta = json.loads((drawn / "manifest.json").read_text())["figures"]["p-bottom-parts.svg"]
    assert meta["view"] == "bottom"
    assert "Seen from below (mirrored)" in (drawn / "p-bottom-parts.svg").read_text()


@needs_kicad
def test_the_assembly_drawing_is_at_true_size(drawn):
    root = ET.parse(drawn / "assembly-top.svg").getroot()
    assert root.get("width") == "142mm" and root.get("height") == "160mm"
    assert root.get("viewBox") == "0 0 142 160"  # one unit of the drawing is one millimetre
    outline = [r for r in root.iter(f"{SVG}rect") if r.get("width") == "114" and r.get("height") == "110"]
    assert len(outline) == 1


@needs_kicad
def test_a_part_that_is_gone_stops_the_generator(tmp_path):
    data = tmp_path / "data"
    data.mkdir()
    for f in spec.DATA.glob("*.toml"):
        text = f.read_text(encoding="utf-8")
        if f.name == "20-sheets.toml":
            text = text.replace('around = ["U11"]', 'around = ["U999"]')
        (data / f.name).write_text(text, encoding="utf-8")
    result = run_tool("generate", "--data", str(data), "--out", str(tmp_path / "out"), python=KICAD_PY)
    assert result.returncode != 0
    assert "no part U999" in result.stderr


def test_the_board_is_found_in_the_top_render():
    calib = png.find_board(spec.TOP_RENDER, 114.0, 110.0)
    kx = (calib["right"] - calib["left"]) / 114.0
    ky = (calib["bottom"] - calib["top"]) / 110.0
    assert abs(kx / ky - 1) < 0.008
    assert 0 < calib["left"] < calib["right"] < calib["width"]
    # The module overhangs the top edge, so the board edge is below the top of the picture.
    assert calib["top"] > 5
    assert (REPO / "hardware" / "zoneflow-a" / "output" / "review" / "overview-overlay.svg").is_file()
