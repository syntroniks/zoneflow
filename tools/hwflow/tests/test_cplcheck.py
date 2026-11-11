# SPDX-License-Identifier: Apache-2.0
"""The placement check against library footprints, on made-up footprints. Needs no network and no KiCad."""

import json

from hwflow import cplcheck


def _lib(pads, pins=None, origin=(4000.0, 3000.0)):
    """A library answer in the shape the service gives: pads in units of 10 mil, y down."""
    shape = [f"PAD~RECT~{origin[0] + x / 0.254}~{origin[1] - y / 0.254}~4~4~1~~{n}~0~~0~id" for n, x, y in pads]
    sym = [f"P~show~0~{n}~0~0~0~id~0^^0~0^^M 0 0 h 5~#880000^^0~0~0~0~{name}~start~~~#0000FF^^0~0~0~0~{n}~end"
           for n, name in (pins or {}).items()]
    return {"success": True, "result": {"dataStr": {"shape": sym}, "packageDetail": {
        "title": "made-up", "dataStr": {"head": {"x": origin[0], "y": origin[1]}, "shape": shape}}}}


def _part(footprint, pads, rot=0.0):
    return {"ref": "X1", "footprint": footprint, "rot": rot,
            "pads": [{"number": n, "at": [x, y], "plated": True, "through": False} for n, x, y in pads]}


SOT6_BOARD = [("1", 9.0, 21.0), ("2", 9.0, 20.0), ("3", 9.0, 19.0),
              ("4", 11.0, 19.0), ("5", 11.0, 20.0), ("6", 11.0, 21.0)]
# the same six pads as a library draws them: pins 1 to 3 along the bottom, from the left
SOT6_LIB = [("1", -1.0, -1.0), ("2", 0.0, -1.0), ("3", 1.0, -1.0), ("4", 1.0, 1.0), ("5", 0.0, 1.0), ("6", -1.0, 1.0)]


def test_library_answer_is_read_in_millimetres_with_y_up():
    lib = cplcheck.parse_library(_lib([("1", -1.0, 2.0), ("2", 1.0, -2.0)], {"1": "K", "2": "A"}))
    got = [(p["number"], round(p["x"], 3), round(p["y"], 3)) for p in lib["pads"]]
    assert got == [("1", -1.0, 2.0), ("2", 1.0, -2.0)]
    assert lib["pins"] == {"1": "K", "2": "A"}
    assert cplcheck.parse_library({"success": False}) is None


def test_pin_1_must_land_on_pin_1():
    lib = cplcheck.parse_library(_lib(SOT6_LIB))
    part = _part("Package_TO_SOT_SMD:SOT-23-6", SOT6_BOARD)
    assert cplcheck.check_part(part, lib, 10.0, 20.0, 270)["ok"]          # pins 1 to 3 down the left side
    wrong = cplcheck.check_part(part, lib, 10.0, 20.0, 90)                # the pads fit as a set, pin 1 opposite
    assert not wrong["ok"] and wrong["kind"] == "pin 1" and wrong["offset"] < 0.01
    assert not cplcheck.check_part(part, lib, 10.0, 20.0, 0)["ok"]        # a quarter turn: no pad fits


def test_cathode_is_found_by_the_library_pin_name():
    part = _part("Diode_SMD:D_SMA", [("1", 8.0, 5.0), ("2", 12.0, 5.0)])    # KiCad: pad 1, at the left, is the cathode
    k_is_1 = cplcheck.parse_library(_lib([("1", -2.0, 0.0), ("2", 2.0, 0.0)], {"1": "K", "2": "A"}))
    k_is_2 = cplcheck.parse_library(_lib([("1", -2.0, 0.0), ("2", 2.0, 0.0)], {"1": "A", "2": "K"}))
    assert cplcheck.check_part(part, k_is_1, 10.0, 5.0, 0)["ok"]
    assert not cplcheck.check_part(part, k_is_1, 10.0, 5.0, 180)["ok"]
    assert cplcheck.check_part(part, k_is_2, 10.0, 5.0, 180)["ok"]
    # a part with no polarity in its pin names is compared as a set of pads: both ways round fit
    plain = cplcheck.parse_library(_lib([("1", -2.0, 0.0), ("2", 2.0, 0.0)], {"1": "1", "2": "2"}))
    both = cplcheck.check_part(part, plain, 10.0, 5.0, 180)
    assert both["ok"] and both["kind"] == "set" and both["offset"] < 1e-6


def test_an_origin_that_is_not_kicads_anchor_is_found(tmp_path):
    # library origin 1.5 mm above the middle of the pads: placed on KiCad's anchor it lands 1.5 mm off
    lib = cplcheck.parse_library(_lib([(n, x, y - 1.5) for n, x, y in SOT6_LIB]))
    part = _part("Package_TO_SOT_SMD:SOT-23-6", SOT6_BOARD)
    assert not cplcheck.check_part(part, lib, 10.0, 20.0, 270)["ok"]
    assert cplcheck.check_part(part, lib, 11.5, 20.0, 270)["ok"]           # moved by the offset, turned with the part

    # and through the command line, with the cache standing in for the service
    (tmp_path / "C1.json").write_text(json.dumps(_lib(SOT6_LIB)))
    (tmp_path / "placement.json").write_text(json.dumps({"parts": [dict(part, ref="U1")]}))
    (tmp_path / "bom.csv").write_text("Comment,Designator,Footprint,LCSC Part #,Quantity\nx,U1,SOT-23-6,C1,1\n")
    (tmp_path / "cpl.csv").write_text("Designator,Mid X,Mid Y,Layer,Rotation\nU1,10.0000mm,20.0000mm,Top,270\n")
    args = [str(tmp_path / "placement.json"), str(tmp_path / "cpl.csv"), str(tmp_path / "bom.csv"),
            "--cache", str(tmp_path), "--report", str(tmp_path / "r.md")]
    assert cplcheck.main(args) == 0
    (tmp_path / "cpl.csv").write_text("Designator,Mid X,Mid Y,Layer,Rotation\nU1,10.0000mm,20.0000mm,Top,180\n")
    assert cplcheck.main(args) == 1
    assert "**no**" in (tmp_path / "r.md").read_text()


def test_a_heat_tab_a_bidirectional_diode_and_a_switch_are_not_false_alarms():
    # DPAK: the library draws the tab 1.2 mm from where the board's land is; the two leads fit
    board = _part("Package_TO_SOT_SMD:TO-252-2", [("1", 7.7, 15.0), ("3", 12.3, 15.0), ("2", 10.0, 8.7)])
    data = _lib([("1", -2.3, -3.75), ("3", 2.3, -3.75)])
    data["result"]["packageDetail"]["dataStr"]["shape"].append("PAD~RECT~4000~2985.2~24~21~1~~2~0~~0~id")
    lib = cplcheck.parse_library(data)
    assert len(cplcheck.leads(lib["pads"])) == 2
    assert cplcheck.check_part(board, lib, 10.0, 18.75, 0)["ok"]
    assert not cplcheck.check_part(board, lib, 10.0, 18.75, 180)["ok"]

    # a bidirectional diode whose library pins are named K and A all the same
    tvs = _part("Diode_SMD:D_SMC", [("1", 8.0, 5.0), ("2", 12.0, 5.0)])
    data = _lib([("1", -2.0, 0.0), ("2", 2.0, 0.0)], {"1": "K", "2": "A"})
    data["result"]["packageDetail"]["title"] = "SMC_L7.1-W6.2-LS8.1-BI"
    assert cplcheck.check_part(tvs, cplcheck.parse_library(data), 10.0, 5.0, 180)["kind"] == "set"

    # a switch with two lands per contact: two board pads are numbered 1, the library counts 1 to 4
    sw = _part("Button_Switch_SMD:SW", [("1", 7.0, 12.0), ("1", 13.0, 12.0), ("2", 7.0, 8.0), ("2", 13.0, 8.0)])
    lib = cplcheck.parse_library(_lib([("1", -3.0, -2.0), ("2", 3.0, -2.0), ("3", -3.0, 2.0), ("4", 3.0, 2.0)]))
    r = cplcheck.check_part(sw, lib, 10.0, 10.0, 0)
    assert r["ok"] and r["kind"] == "set"
