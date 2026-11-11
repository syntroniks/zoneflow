# SPDX-License-Identifier: Apache-2.0
"""The layout rule check on made-up copper: each kind of rule must hold when met and fail when broken."""

from hwflow import layoutrules as lr

F, B = frozenset([0]), frozenset([2])
ALL = frozenset([0, 1, 2])


def seg(a, b, w=0.2):
    return ("seg", a, b, w)


def copper():
    return {
        "/KA": [("track", B, seg((0, 0), (20, 0), 0.25)), ("via", ALL, ("circle", (0, 0), 0.3)),
                ("pad", F, ("rect", (-1.0, -0.4, 0.0, 0.4)))],
        "/NEAR": [("track", B, seg((0, 1.0), (20, 1.0)))],          # 0.775 mm from the track
        "/FAR": [("track", B, seg((0, 3.0), (20, 3.0)))],
        "/CROSS": [("track", F, seg((10, -5), (10, 5)))],            # crosses on the other layer
        "/PADNEAR": [("pad", F, ("rect", (0.2, -0.4, 1.0, 0.4)))],   # 0.2 mm from the pad of /KA
        "GND": [("via", ALL, ("circle", (5, 0.6), 0.3))],
    }


def problems(rule):
    return lr.check(copper(), {}, {"keepaway": [rule]})[0]


def test_gap_of_shapes():
    assert lr.gap(seg((0, 0), (10, 0), 0.2), seg((0, 1), (10, 1), 0.2)) == 0.8
    assert lr.gap(seg((0, 0), (10, 0)), seg((5, -1), (5, 1))) == 0.0
    assert abs(lr.gap(("circle", (0, 0), 0.3), ("rect", (1.0, -1, 2, 1))) - 0.7) < 1e-9
    assert lr.gap(("rect", (0, 0, 2, 2)), seg((1, 1), (1, 1.5))) == 0.0     # inside the rectangle


def test_plan_view_catches_a_crossing_on_another_layer():
    p = problems({"net": "/KA", "from": ["/CROSS"], "distance": 1.0, "scope": "plan"})
    assert len(p) == 1 and "/CROSS" in p[0] and "plan view" in p[0]


def test_same_layer_lets_a_crossing_on_another_layer_pass():
    assert problems({"net": "/KA", "from": ["/CROSS"], "distance": 1.0, "scope": "layer"}) == []


def test_same_layer_distance():
    assert problems({"net": "/KA", "from": ["/FAR"], "distance": 1.0, "scope": "layer"}) == []
    # the via of /KA is on every layer and 0.3 mm in radius: 1.0 - 0.3 - 0.1
    p = problems({"net": "/KA", "from": ["/NEAR"], "distance": 1.0, "scope": "layer"})
    assert len(p) == 1 and "0.600" in p[0]
    # the track alone: 1.0 - 0.125 - 0.1
    p = problems({"net": "/KA", "from": ["/NEAR"], "distance": 1.0, "scope": "layer", "own_kinds": ["track"]})
    assert len(p) == 1 and "0.775" in p[0]


def test_wildcard_with_exception_and_kinds():
    rule = {"net": "/KA", "from": ["*"], "except": ["GND", "/NEAR", "/CROSS"], "distance": 1.0, "scope": "layer",
            "kinds": ["track", "via"], "own_kinds": ["track", "via"]}
    # /PADNEAR is a pad, and pads are left out: nothing is found
    assert problems(rule) == []
    rule["except"] = ["/NEAR", "/CROSS"]
    p = problems(rule)
    assert len(p) == 1 and "GND" in p[0]            # the via 0.6 mm away
    rule["kinds"] = ["track", "via", "pad"]
    rule["own_kinds"] = ["track", "via", "pad"]
    rule["except"] = ["GND", "/NEAR", "/CROSS"]
    assert any("/PADNEAR" in x for x in problems(rule))


def test_a_rule_that_names_no_net_of_the_board_fails():
    p = problems({"net": "/KA", "from": ["/TYPO"], "distance": 1.0})
    assert len(p) == 1 and "/TYPO" in p[0]
    p = problems({"net": "/NOPE", "from": ["/FAR"], "distance": 1.0})
    assert len(p) == 1 and "no copper" in p[0]


PARTS = {
    "R1": {"rot": 0.0, "pads": [("/A", (0, 0, 1, 1)), ("/B", (2, 0, 3, 1))]},
    "C1": {"rot": 90.0, "pads": [("/B", (0, 5, 1, 6))]},
    "C2": {"rot": 270.0, "pads": [("/C", (20, 0, 21, 1))]},
    "TP1": {"rot": 0.0, "pads": [("/B", (5, 5, 6, 6))]},
}


def test_apart():
    ok = lr.check({}, PARTS, {"apart": [{"a": ["R1"], "b": ["C2"], "distance": 10.0}]})
    assert ok[0] == [] and ok[1][0]["found"] == 17.0
    bad = lr.check({}, PARTS, {"apart": [{"a": ["R1"], "b": ["C1"], "distance": 10.0}]})
    assert len(bad[0]) == 1 and "4.00 mm apart" in bad[0][0]


def test_perpendicular():
    assert lr.check({}, PARTS, {"perpendicular": [{"parts": ["R1", "C1"]}]})[0] == []
    assert lr.check({}, PARTS, {"perpendicular": [{"parts": ["R1", "C2"]}]})[0] == []
    assert len(lr.check({}, PARTS, {"perpendicular": [{"parts": ["C1", "C2"]}]})[0]) == 1


def test_no_pad():
    assert lr.check({}, PARTS, {"no_pad": [{"nets": ["/A"], "refs": "TP*"}]})[0] == []
    bad = lr.check({}, PARTS, {"no_pad": [{"nets": ["/B"], "refs": "TP*"}]})[0]
    assert bad == ["no_pad: TP1 on /B"]
    assert "not nets" in lr.check({}, PARTS, {"no_pad": [{"nets": ["/TYPO"], "refs": "TP*"}]})[0][0]


def test_report_lists_every_row():
    _, rows = lr.check(copper(), PARTS, {
        "keepaway": [{"net": "/KA", "from": ["/FAR"], "distance": 1.0, "scope": "layer"}],
        "apart": [{"a": ["R1"], "b": ["C2"], "distance": 10.0}],
        "perpendicular": [{"parts": ["R1", "C1"]}],
        "no_pad": [{"nets": ["/A"], "refs": "TP*"}]})
    text = lr.report(rows)
    assert "`/FAR`" in text and "17 mm apart" in text and "90 degrees" in text and "none found" in text
