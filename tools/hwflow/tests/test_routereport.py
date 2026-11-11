# SPDX-License-Identifier: Apache-2.0
"""The routing report's tables, from made-up numbers. Reading a board needs KiCad and is not tested here."""

from hwflow import routereport


def data():
    row = {"net": "/A", "class": "Default", "class_width": 0.2, "length": 40.0, "auto_length": 40.0, "vias": 2,
           "layers": ["B.Cu", "F.Cu"], "min_width": 0.2, "span": 10.0, "plane_length": 0.0}
    return {"rows": [row,
                     dict(row, net="/THIN", **{"class": "Power"}, class_width=0.4, min_width=0.2, length=12.0),
                     dict(row, net="/SLOT", length=8.0, plane_length=3.5, layers=["In2.Cu"])],
            "per_layer": {"F.Cu": {"hand": 100.0, "auto": 50.0}, "In2.Cu": {"hand": 0.0, "auto": 3.5}},
            "vias_in_pads": [("/A", "R1", "2", "/A")], "hand_nets": 7, "hand_vias": 20, "auto_vias": 4}


def test_each_thing_to_look_at_is_listed():
    text = routereport.markdown(data(), ["In2.Cu"])
    assert "- `/THIN` (Power): 0.2 mm, class width 0.4 mm" in text
    assert "- `/A`: a via in pad 2 of R1" in text
    assert "- `/A`: 40 mm of track for 10 mm" in text          # four times as long as its pins are apart
    assert "`/THIN`: 12 mm of track" not in text               # under the length worth a look
    assert "- `/SLOT`: 3.5 mm" in text and "Total: 4 mm in 1 nets." in text
    assert "| F.Cu | 100 | 50 |" in text
    assert "7 nets are routed by hand alone, 3 have autorouted copper." in text


def test_nothing_to_list_says_none():
    d = data()
    d["rows"] = d["rows"][:0]
    d["vias_in_pads"] = []
    text = routereport.markdown(d, [])
    assert text.count("None.") == 3 and "slot in the pour" not in text
