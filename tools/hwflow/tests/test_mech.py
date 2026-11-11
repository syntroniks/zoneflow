# SPDX-License-Identifier: Apache-2.0
"""The mechanical-contract check, on a small made-up board. Needs no KiCad."""

import copy

from hwflow import mech

CONTRACT = {
    "revision": 9,
    "board": {"outline": [[0, 0], [50, 0], [50, 40], [0, 40]], "layers": 4, "thickness": 1.6, "edge_keepout": 1.0},
    "holes": [
        {"id": "A", "x": 5, "y": 35, "d": 3.4, "kind": "screw", "top_keepout": 8.0, "bottom_keepout": 10.0},
        {"id": "E", "x": 40, "y": 30, "d": 0, "kind": "foot", "top_keepout": 6.0, "bottom_keepout": 0},
    ],
    "parts": [
        {"ref": "J1", "side": "top", "x": 10.0, "y": 2.0, "pin1": [8.0, 5.0],
         "model": [{"box": [0, 0, 8.0, 9.0, 0, 7]}]},
        {"ref": "SW1", "side": "top", "x": 25.0, "y": 20.0, "model": [{"box": [0, 0, 8.6, 6.0, 0, 3]}]},
        {"ref": "C1", "side": "bottom", "x": 20.0, "y": 10.0, "pads": [[30, 8], [30, 12]],
         "model": [{"box": [0, 0, 20.0, 12.0, 0, 13]}]},
        {"ref": "DS1", "side": "top", "x": 1.0, "y": 1.0},  # not on the board
    ],
    "zones": [
        {"id": "T_GENERAL", "side": "top", "rect": [0, 0, 50, 40], "max_height": 9.0},
        {"id": "T_LOW", "side": "top", "rect": [0, 15, 50, 18], "max_height": 1.0},
        {"id": "T_NONE", "side": "top", "rect": [10, 30, 20, 38], "max_height": 0},
        {"id": "B_GENERAL", "side": "bottom", "rect": [0, 0, 50, 40], "max_height": 17.0},
    ],
}
HEIGHTS = {"footprint": {"R_0603": {"height": 0.55}, "IC": {"height": 1.75}, "TP": {"height": 0},
                         "Hole": {"height": 0}}}


def _pad(number, x, y, size=1.0, drill=0.0, plated=True):
    return {"number": number, "net": "", "at": [x, y], "size": [size, size], "drill": drill, "plated": plated,
            "through": drill > 0}


def _part(ref, footprint, x, y, w, h, side="top", pads=None):
    box = [x - w / 2, y - h / 2, x + w / 2, y + h / 2]
    return {"ref": ref, "footprint": f"Lib:{footprint}", "value": "", "side": side, "at": [x, y], "rot": 0,
            "dnp": False, "body": box, "courtyard": [box[0] - 0.25, box[1] - 0.25, box[2] + 0.25, box[3] + 0.25],
            "pads": pads if pads is not None else [_pad("1", x - 0.5, y), _pad("2", x + 0.5, y)]}


def _board():
    return {
        "outline": [-0.05, -0.05, 50.05, 40.05], "layers": 4, "thickness": 1.6,
        "parts": [
            _part("H1", "Hole", 5, 35, 3.4, 3.4, pads=[_pad("", 5, 35, 3.4, 3.4, plated=False)]),
            _part("J1", "Header", 10.0, 2.0, 8.0, 9.0,
                  pads=[_pad("1", 8.0, 5.0, 2.3, 1.4), _pad("2", 12.0, 5.0, 2.3, 1.4)]),
            _part("SW1", "Switch", 25.0, 20.0, 6.0, 6.0),
            _part("C1", "CapLying", 20.0, 10.0, 22.0, 12.0, side="bottom",
                  pads=[_pad("1", 30, 12, 2.0, 1.0), _pad("2", 30, 8, 2.0, 1.0)]),
            _part("R1", "R_0603", 30.0, 16.5, 1.6, 0.8),     # in the low zone, low enough
            _part("U1", "IC", 40.0, 22.0, 5.0, 4.0),
            _part("TP1", "TP", 15.0, 34.0, 1.5, 1.5, pads=[_pad("1", 15.0, 34.0, 1.5)]),  # no body: allowed anywhere
        ],
    }


def _check(board, contract=CONTRACT, heights=HEIGHTS):
    return mech.check(board, contract, heights)


def test_a_board_that_agrees_passes():
    problems, summary = _check(_board())
    assert problems == []
    assert "revision 9" in summary and "3 placed parts" in summary


def _moved(ref, **change):
    b = _board()
    for p in b["parts"]:
        if p["ref"] == ref:
            for k, v in change.items():
                if k == "shift":
                    dx, dy = v
                    p["at"] = [p["at"][0] + dx, p["at"][1] + dy]
                    p["body"] = [p["body"][0] + dx, p["body"][1] + dy, p["body"][2] + dx, p["body"][3] + dy]
                    p["courtyard"] = [p["courtyard"][0] + dx, p["courtyard"][1] + dy, p["courtyard"][2] + dx,
                                      p["courtyard"][3] + dy]
                    for q in p["pads"]:
                        q["at"] = [q["at"][0] + dx, q["at"][1] + dy]
                else:
                    p[k] = v
    return b


def test_a_fixed_part_off_position_fails():
    assert any("J1 pin 1" in p for p in _check(_moved("J1", shift=(0.2, 0)))[0])
    assert any(p.startswith("SW1 is at") for p in _check(_moved("SW1", shift=(0, 0.1)))[0])
    assert any("C1 pads" in p for p in _check(_moved("C1", shift=(0.3, 0)))[0])
    # Inside the tolerance nothing is reported.
    assert _check(_moved("SW1", shift=(0.03, 0)))[0] == []


def test_wrong_side_outline_layers_and_missing_hole_fail():
    assert any("SW1 is on the bottom side" in p for p in _check(_moved("SW1", side="bottom"))[0])
    b = _board()
    b["outline"] = [-0.05, -0.05, 52.05, 40.05]
    assert any(p.startswith("outline") for p in _check(b)[0])
    b = _board()
    b["layers"] = 2
    assert any("copper layers" in p for p in _check(b)[0])
    b = _board()
    b["parts"] = [p for p in b["parts"] if p["ref"] != "H1"]
    assert any(p.startswith("hole A") for p in _check(b)[0])


def test_height_zones_take_the_last_listed_zone():
    # U1 (1.75 mm) into the 1.0 mm strip: the later, lower zone wins over T_GENERAL.
    problems = _check(_moved("U1", shift=(0, -5.0)))[0]
    assert problems == ["U1 is 1.75 mm high in zone T_LOW, which allows 1"]
    # A resistor in the zone that allows nothing.
    problems = _check(_moved("R1", shift=(-15.0, 18.0)))[0]
    assert problems == ["R1 is 0.55 mm high in zone T_NONE, which allows 0"]
    # A test pad has no body and may be there.
    assert _check(_board())[0] == []


def test_keep_outs_edge_bottom_side_and_unknown_height():
    assert any("keep-out of screw A" in p for p in _check(_moved("R1", shift=(-22.5, 16.0)))[0])
    assert any("keep-out of foot E" in p for p in _check(_moved("U1", shift=(0, 6.0)))[0])
    assert any("within 1 mm of the board edge" in p for p in _check(_moved("R1", shift=(19.0, 0)))[0])
    assert any("R1 is on the bottom side" in p for p in _check(_moved("R1", side="bottom"))[0])
    assert any("no height for footprint" in p for p in _check(_moved("U1", footprint="Lib:Unknown"))[0])


def test_soldering_access_around_the_pads_of_a_bottom_part():
    # C1's pads come up at (30, 8) and (30, 12). A 1.75 mm part 2 mm from a pad edge is in the way.
    problems = _check(_moved("U1", shift=(-4.5, -10.0)))[0]
    assert any("within 2.5 mm of pad" in p and "C1" in p for p in problems)
    # A low part in the same place is not.
    b = _moved("U1", shift=(-4.5, -10.0), footprint="Lib:R_0603")
    assert not any("within 2.5 mm" in p for p in _check(b)[0])


def test_zone_limit_subtracts_overlaps():
    zones = copy.deepcopy(CONTRACT["zones"])
    assert mech.zone_limit((1, 1, 2, 2), "top", zones) == (9.0, "T_GENERAL")
    assert mech.zone_limit((1, 14, 2, 16), "top", zones) == (1.0, "T_LOW")
    assert mech.zone_limit((1, 1, 2, 2), "bottom", zones) == (17.0, "B_GENERAL")
    assert mech.zone_limit((60, 60, 61, 61), "top", zones) is None
