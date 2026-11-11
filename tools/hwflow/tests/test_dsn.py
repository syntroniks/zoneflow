# SPDX-License-Identifier: Apache-2.0
"""Edits to the Specctra file: nets kept from the autorouter, layer settings, keep-outs."""

import pytest

from hwflow import dsn

TEXT = '''(pcb "x.dsn"
  (structure
    (layer F.Cu
      (type signal)
    )
    (boundary
      (path pcb 0  0 0  1000 0)
    )
    (via "Via[0-3]_600:300_um")
    (rule
      (width 200)
    )
  )
  (network
    (net GND
      (pins U1-1 U1-9@1
        C1-2)
    )
    (net /Z1/T2
      (pins F101-2 Q101-2)
    )
    (net "/OUT Z1"
      (pins J2-4 F101-1)
    )
    (net /SIG
      (pins U1-2 R1-1)
    )
    (class kicad_default GND /SIG
      (rule
        (width 200)
      )
    )
  )
  (wiring
    (wire (path F.Cu 300  0 0  10 10)(net GND)(type protect))
  )
)
'''


def test_fixed_nets_move_to_a_class_of_their_own_and_keep_their_pins(tmp_path):
    f = tmp_path / "a.dsn"
    f.write_text(TEXT)
    assert dsn.fix_nets(f, ["GND", "/*/T2", "/OUT*"]) == (1, ["kicad_default__fixed"])
    out = f.read_text()
    assert "(pins U1-1 U1-9@1" in out and "(pins U1-2 R1-1)" in out      # every net keeps its pins
    assert "(class kicad_default /SIG\n" in out
    assert "(class kicad_default__fixed GND\n" in out
    assert out.count("(width 200)") == 3                                 # the rule went with it
    assert "(net GND)(type protect)" in out
    assert out.count("(") == out.count(")")


def test_a_class_with_every_net_fixed_is_replaced(tmp_path):
    f = tmp_path / "a.dsn"
    f.write_text(TEXT.replace("(class kicad_default GND /SIG", '(class "Odd (x)" GND "/A (b)"'))
    assert dsn.fix_nets(f, ["GND", "/A*"]) == (2, ["Odd (x)__fixed"])
    out = f.read_text()
    assert '(class "Odd (x)__fixed" GND "/A (b)"' in out and '(class "Odd (x)" ' not in out


def test_no_pattern_changes_nothing(tmp_path):
    f = tmp_path / "a.dsn"
    f.write_text(TEXT)
    assert dsn.fix_nets(f, []) == (0, [])
    assert dsn.fix_nets(f, ["/NOPE"]) == (0, [])
    assert f.read_text() == TEXT


def test_a_plane_layer_gets_its_type_and_its_plane(tmp_path):
    f = tmp_path / "a.dsn"
    f.write_text(TEXT)
    assert dsn.add_planes(f, [{"layer": "F.Cu", "net": "GND"}], (0, -900, 1000, 0)) == 1
    out = f.read_text()
    assert "(layer F.Cu\n      (type power)" in out
    assert "(plane GND (polygon F.Cu 0  0 -900  1000 -900  1000 0  0 0  0 -900))" in out
    assert out.index("(plane GND") < out.index('(via "Via[0-3]')
    assert out.count("(") == out.count(")")


def test_a_plane_on_a_layer_the_file_does_not_have_is_refused(tmp_path):
    f = tmp_path / "a.dsn"
    f.write_text(TEXT)
    with pytest.raises(ValueError):
        dsn.add_planes(f, [{"layer": "In1.Cu", "net": "GND"}], (0, 0, 1, 1))
    assert dsn.add_planes(f, [], (0, 0, 1, 1)) == 0
    assert f.read_text() == TEXT


def test_router_settings_name_the_ignored_classes():
    s = dsn.router_settings(["F.Cu", "B.Cu"], {}, ["b__fixed", "a__fixed"])
    assert s["autorouter"] == {"ignore_net_classes": ["a__fixed", "b__fixed"]}
    assert "autorouter" not in dsn.router_settings(["F.Cu", "B.Cu"], {})


def test_keepouts_go_before_the_via_list(tmp_path):
    f = tmp_path / "a.dsn"
    f.write_text(TEXT)
    keepouts = [{"layers": ["B.Cu", "In2.Cu"], "rect": [1.0, 2.0, 3.0, 4.0]}]
    assert dsn.add_keepouts(f, keepouts, lambda x, y: (x * 1000, -y * 1000)) == 2
    out = f.read_text()
    assert out.index("(keepout") < out.index('(via "Via[0-3]')
    assert '(keepout "" (rect B.Cu 1000 -4000 3000 -2000))' in out
    assert '(keepout "" (rect In2.Cu 1000 -4000 3000 -2000))' in out
    assert "(pins U1-2 R1-1)" in out


def test_no_keepout_changes_nothing_and_a_file_without_vias_is_refused(tmp_path):
    f = tmp_path / "a.dsn"
    f.write_text(TEXT)
    assert dsn.add_keepouts(f, [], lambda x, y: (x, y)) == 0
    assert f.read_text() == TEXT
    f.write_text(TEXT.replace('    (via "Via[0-3]_600:300_um")\n', ""))
    with pytest.raises(ValueError):
        dsn.add_keepouts(f, [{"layers": ["F.Cu"], "rect": [0, 0, 1, 1]}], lambda x, y: (x, y))


def test_router_settings_name_the_layers_the_autorouter_may_use():
    route = {"layers": ["F.Cu", "In2.Cu", "B.Cu"], "layer_cost": {"In2.Cu": 2.5}}
    s = dsn.router_settings(["F.Cu", "In1.Cu", "In2.Cu", "B.Cu"], route)
    assert s["fanout"] == {"enabled": False}
    assert [layer["routable"] for layer in s["layers"]] == [True, False, True, True]
    assert s["layers"][2]["preferred_direction_trace_cost"] == 2.5
    assert s["layers"][2]["undesired_direction_trace_cost"] == 5.0
    assert "preferred_direction_trace_cost" not in s["layers"][0]
    assert [layer["preferred_direction_horizontal"] for layer in s["layers"]] == [True, False, True, False]
    # With no layer list the autorouter keeps its own choice of layers; fan-out is still off.
    assert dsn.router_settings(["F.Cu", "B.Cu"], {}) == {"fanout": {"enabled": False}}
    assert dsn.router_settings(["F.Cu", "B.Cu"], {"fanout": True})["fanout"] == {"enabled": True}


def test_the_autorouter_settings_file_turns_usage_reporting_off(tmp_path):
    import json

    from hwflow import route

    path = route.write_settings(tmp_path, {"fanout": {"enabled": False}})
    assert path == tmp_path / "freerouting" / "freerouting.json"
    s = json.loads(path.read_text())
    assert s["usage_and_diagnostic_data"]["disable_analytics"] is True
    assert s["profile"]["allow_telemetry"] is False and s["profile"]["allow_contact"] is False
    assert s["profile"]["email"] == ""
    assert s["version"] == route.VERSION
    assert s["router"] == {"fanout": {"enabled": False}}
