# SPDX-License-Identifier: Apache-2.0
"""Checks that run KiCad's own tools on generated files."""

import json
import shutil

import pytest
from conftest import TEST_BOARD, kicad_cli, needs_kicad

from hwflow import netlist, schwriter
from hwflow.design import Design

pytestmark = needs_kicad


def _export(design: Design, tmp_path):
    flat = schwriter.write(design, tmp_path)
    root = tmp_path / f"{design.name}.kicad_sch"
    r = kicad_cli("sch", "export", "netlist", "-o", str(tmp_path / "out.net"), str(root), cwd=tmp_path)
    assert r.returncode == 0, r.stderr
    return flat, netlist.read(tmp_path / "out.net"), root


def _orientations() -> Design:
    """One 3-pin part in all 12 rotation and mirror states, each pin on its own named net."""
    d = Design("xf", "orientation test")
    r = d.root
    i = 0
    for mi, mirror in enumerate([None, "x", "y"]):
        for ri, rot in enumerate([0, 90, 180, 270]):
            i += 1
            x, y = 38.1 + ri * 50.8, 38.1 + mi * 50.8
            u = r.part(f"U{i}", "Regulator_Linear:AMS1117-3.3", at=(x, y), rot=rot, mirror=mirror)
            c = r.part(f"C{i}", "Device:C", at=(x + 22.86, y))
            g = r.part(f"R{i}", "Device:R", at=(x - 20.32, y + 10.16), rot=90)
            r.connect(r.net(f"U{i}_VI"), u["VI"], c[1])
            r.connect(r.net(f"U{i}_VO"), u["VO"], c[2])
            r.connect(r.net(f"U{i}_GND"), u["GND"], g[2])
            r.connect(r.power("GND"), g[1])
    return d


def test_pin_positions_match_kicad_in_every_orientation(tmp_path):
    flat, nl, _ = _export(_orientations(), tmp_path)
    assert netlist.compare(flat, nl) == []
    assert nl.nets["/U6_VI"] == {("U6", "3"), ("C6", "1")}  # mirrored and rotated


def test_netlist_check_catches_a_wire_that_misses_its_pin(tmp_path):
    d = Design("miss", "broken drawing")
    r = d.root
    a = r.part("R1", "Device:R", at=(50.8, 50.8), rot=90)
    b = r.part("R2", "Device:R", at=(76.2, 50.8), rot=90)
    net = r.net("MID")
    r.wire(a[2], b[1], net=net)
    r.connect(r.net("A"), a[1])
    r.connect(r.net("B"), b[2])
    flat = d.flatten()
    # Move R2 after the wire was recorded: the drawing no longer reaches its pin.
    b.at = (76.2, 55.88)
    schwriter.write(d, tmp_path)
    r_ = kicad_cli("sch", "export", "netlist", "-o", str(tmp_path / "out.net"),
                   str(tmp_path / "miss.kicad_sch"), cwd=tmp_path)
    assert r_.returncode == 0, r_.stderr
    problems = netlist.compare(flat, netlist.read(tmp_path / "out.net"))
    assert any("R2" in p for p in problems), problems


def _regulator(with_flags: bool) -> Design:
    d = Design("erc", "ERC test")
    r = d.root
    gnd, vin, v33 = r.power("GND"), r.power("VBUS"), r.power("+3V3")
    u = r.part("U1", "Regulator_Linear:AMS1117-3.3", at=(101.6, 76.2))
    j = r.part("J1", "Connector_Generic:Conn_01x04", at=(50.8, 76.2))
    c = r.part("C1", "Device:C", at=(127, 86.36))
    r.connect(vin, u["VI"], j[1])
    r.connect(gnd, u["GND"], j[2], c[2])
    r.connect(v33, u["VO"], c[1], j[3])
    r.nc(j[4])
    if with_flags:
        r.connect(vin, r.part("#FLG1", "power:PWR_FLAG", at=(76.2, 101.6))[1])
        r.connect(gnd, r.part("#FLG2", "power:PWR_FLAG", at=(88.9, 101.6))[1])
    return d


def _erc(design: Design, tmp_path):
    from hwflow import project

    schwriter.write(design, tmp_path)
    project.lib_tables(design, tmp_path, set())
    r = kicad_cli("sch", "erc", "--severity-error", "--exit-code-violations", "--format", "json",
                  "-o", str(tmp_path / "erc.json"), str(tmp_path / f"{design.name}.kicad_sch"), cwd=tmp_path)
    data = json.loads((tmp_path / "erc.json").read_text())
    return r.returncode, [v["type"] for s in data["sheets"] for v in s["violations"]]


def test_erc_passes_on_a_correct_circuit(tmp_path):
    rc, types = _erc(_regulator(with_flags=True), tmp_path)
    assert (rc, types) == (0, [])


def test_erc_catches_an_undriven_power_input(tmp_path):
    rc, types = _erc(_regulator(with_flags=False), tmp_path)
    assert rc != 0
    assert "power_pin_not_driven" in types


def _copy_board(tmp_path):
    if not (TEST_BOARD / "toolchain_test.kicad_pcb").exists():
        pytest.skip("toolchain test board has not been built")
    dst = tmp_path / "board"
    shutil.copytree(TEST_BOARD, dst, ignore=shutil.ignore_patterns("build", "output", "route", "design"))
    return dst


def _drc(board_dir):
    r = kicad_cli("pcb", "drc", "--schematic-parity", "--severity-error", "--severity-warning",
                  "--exit-code-violations", "--format", "json", "-o", "drc.json", "toolchain_test.kicad_pcb",
                  cwd=board_dir)
    data = json.loads((board_dir / "drc.json").read_text())
    return r.returncode, data


def test_drc_passes_on_the_committed_board(tmp_path):
    rc, data = _drc(_copy_board(tmp_path))
    assert rc == 0
    assert data["violations"] == [] and data["unconnected_items"] == [] and data["schematic_parity"] == []


def test_drc_catches_a_short(tmp_path):
    board_dir = _copy_board(tmp_path)
    pcb = board_dir / "toolchain_test.kicad_pcb"
    text = pcb.read_text()
    # R11 sits at (112.5, 90) on the page; this track joins its two pads (CC1 and GND).
    short = ('\t(segment\n\t\t(start 111.675 90)\n\t\t(end 113.325 90)\n\t\t(width 0.2)\n\t\t(layer "F.Cu")\n'
             '\t\t(net "GND")\n\t\t(uuid "00000000-0000-4000-8000-000000000001")\n\t)\n')
    head, sep, tail = text.rpartition("\t(embedded_fonts")
    assert sep
    pcb.write_text(head + short + sep + tail)
    rc, data = _drc(board_dir)
    assert rc != 0
    # KiCad 10.0.8 reports this short as a crossing track plus a solder mask
    # bridge between the two nets, not as "shorting_items".
    assert {"tracks_crossing", "solder_mask_bridge"} <= {v["type"] for v in data["violations"]}


def test_drc_catches_a_part_missing_from_the_board(tmp_path):
    board_dir = _copy_board(tmp_path)
    pcb = board_dir / "toolchain_test.kicad_pcb"
    lines = pcb.read_text().split("\n")
    start = next(i for i, ln in enumerate(lines) if ln.startswith("\t(footprint ")
                 and any('"Reference" "R12"' in x for x in lines[i:i + 12]))
    end = next(i for i in range(start + 1, len(lines)) if lines[i] == "\t)")
    pcb.write_text("\n".join(lines[:start] + lines[end + 1:]))
    rc, data = _drc(board_dir)
    assert rc != 0
    assert "missing_footprint" in {v["type"] for v in data["schematic_parity"]}
