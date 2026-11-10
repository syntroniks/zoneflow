# SPDX-License-Identifier: Apache-2.0
"""Checks that need no KiCad executable (the stock symbol libraries must be installed)."""

import pytest

from hwflow import sexpr, symlib
from hwflow.design import Design, DesignError, offset_ref, transform
from hwflow.sexpr import Sym

pytestmark = pytest.mark.skipif(not symlib.symbol_dir().exists(), reason="KiCad symbol libraries not installed")


def test_sexpr_round_trip():
    text = '(a "quoted \\"x\\"" 1.5 (b c) (d "multi\\nline"))'
    tree = sexpr.loads(text)
    assert tree == [Sym("a"), 'quoted "x"', Sym("1.5"), [Sym("b"), Sym("c")], [Sym("d"), "multi\nline"]]
    assert sexpr.loads(sexpr.dumps(tree)) == tree


def test_transform_rotation_and_mirror():
    # A point above the origin in the library (y up) is above it on the page (y down).
    assert transform(0, 2.54, 0, None) == (0, -2.54)
    # Counter-clockwise rotation moves "up" to "left".
    assert transform(0, 2.54, 90, None) == (-2.54, 0)
    # Mirroring is applied after rotation, in the page frame.
    assert transform(2.54, 0, 90, "x") == (0, 2.54)
    assert transform(2.54, 0, 90, "y") == (0, -2.54)
    with pytest.raises(DesignError):
        transform(0, 0, 45, None)


def test_derived_symbol_is_flattened():
    sym = symlib.load("Regulator_Linear:AMS1117-3.3")
    assert sexpr.find(sym.node, "extends") is None
    assert {p.name for p in sym.pins} == {"GND", "VO", "VI"}
    assert sym.props["Footprint"] == "Package_TO_SOT_SMD:SOT-223-3_TabPin2"


def test_offset_ref():
    assert offset_ref("R1", 100) == "R101"
    assert offset_ref("#FLG2", 10) == "#FLG12"
    with pytest.raises(DesignError):
        offset_ref("R", 0)


def _two_channel(ref_offsets=(100, 200)):
    d = Design("t", "t")
    chan = d.sheet("chan", "chan")
    a = chan.port("A", "input")
    r = chan.part("R1", "Device:R", at=(50.8, 50.8), value="1k")
    chan.connect(a, r[1])
    chan.connect(chan.power("GND"), r[2])
    for i, off in enumerate(ref_offsets):
        d.root.instantiate(chan, f"CH{i + 1}", at=(50.8, 25.4 + 25.4 * i), connect={"A": d.root.net(f"IN{i + 1}")},
                           ref_offset=off)
    j = d.root.part("J1", "Connector_Generic:Conn_01x04", at=(25.4, 50.8))
    d.root.connect(d.root.net("IN1"), j[1])
    d.root.connect(d.root.net("IN2"), j[2])
    d.root.connect(d.root.power("GND"), j[3])
    d.root.nc(j[4])
    return d


def test_reused_sheet_gets_distinct_references_and_nets():
    flat = _two_channel().flatten()
    assert sorted(p.ref for p in flat.parts) == ["J1", "R101", "R201"]
    nets = {frozenset(n.nodes) for n in flat.nets}
    assert frozenset({("J1", "1"), ("R101", "1")}) in nets
    assert frozenset({("J1", "2"), ("R201", "1")}) in nets
    assert frozenset({("J1", "3"), ("R101", "2"), ("R201", "2")}) in nets
    assert flat.no_connects == {("J1", "4")}


def test_clashing_references_are_rejected():
    with pytest.raises(DesignError, match="duplicate references"):
        _two_channel(ref_offsets=(100, 100)).flatten()


def test_unconnected_pin_is_rejected():
    d = Design("t", "t")
    r = d.root.part("R1", "Device:R", at=(50.8, 50.8))
    d.root.connect(d.root.net("A"), r[1])
    with pytest.raises(DesignError, match="neither connected nor marked nc"):
        d.flatten()


def test_stacked_pins_are_one_node():
    d = Design("t", "t")
    u = d.root.part("U1", "RF_Module:ESP32-S3-WROOM-1", at=(101.6, 101.6))
    d.root.connect(d.root.power("GND"), u.pins("GND"))
    d.root.nc_rest(u)
    flat = d.flatten()
    gnd = next(n for n in flat.nets if "GND" in n.names)
    assert gnd.nodes == {("U1", "1"), ("U1", "40"), ("U1", "41")}


def test_off_grid_position_is_rejected():
    d = Design("t", "t")
    with pytest.raises(DesignError, match="grid"):
        d.root.part("R1", "Device:R", at=(50.0, 50.8))
