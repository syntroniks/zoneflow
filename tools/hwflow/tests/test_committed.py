# SPDX-License-Identifier: Apache-2.0
"""The check of a board's committed netlist and BOMs against the design netlist. Needs no KiCad."""

import pytest

from hwflow import committed, parity

PARTS = """
[builds]
base = ["all", "8"]
small = ["all"]

[mechanical.H1]
value = "A"
symbol = "Mechanical:MountingHole"
footprint = "Lib:Hole"

[part.C1]
value = "10k"
symbol = "Device:R"
footprint = "Resistor_SMD:R_0603_1608Metric"
package = "0603"

[part.C2]
value = "M7"
symbol = "Device:D"
footprint = "Diode_SMD:D_SMA"
package = "SMA"
pins = { "A" = "2", "K" = "1" }
"""

COMPS = [
    {"ref": "R1", "value": "10k 1%", "package": "0603", "lcsc": "C1", "block": "t", "variant": "all",
     "pins": {"1": "VIN", "2": "MID"}},
    {"ref": "R2", "value": "10k 1%", "package": "0603", "lcsc": "C1", "block": "t", "variant": "8",
     "pins": {"1": "MID", "2": "GND"}},
    {"ref": "D1", "value": "Rectifier", "package": "SMA", "lcsc": "C2", "block": "t", "variant": "all",
     "pins": {"A": "MID", "K": "GND"}},
    {"ref": "R3", "value": "10k 1%", "package": "0603", "lcsc": "C1", "block": "t", "variant": "all",
     "pins": {"1": "VIN", "2": "GND"}},
]


def _comp(ref, value, footprint, lcsc, spec):
    return (f'(comp (ref "{ref}") (value "{value}") (footprint "{footprint}") '
            f'(fields (field (name "LCSC") "{lcsc}") (field (name "Spec") "{spec}")) '
            f'(libsource (lib "L") (part "P")) (sheetpath (names "/") (tstamps "/")) (tstamps "t-{ref}"))')


def _net(name, *nodes):
    body = " ".join(f'(node (ref "{r}") (pin "{p}"))' for r, p in nodes)
    return f'(net (code "1") (name "{name}") {body})'


def _write(tmp_path, comps=None, nets=None, bom=None, bom_small=None):
    (tmp_path / "design").mkdir()
    (tmp_path / "output").mkdir()
    (tmp_path / "design" / "parts.toml").write_text(PARTS, encoding="utf-8")
    (tmp_path / "t.kicad_pro").write_text("{}", encoding="utf-8")
    r = "Resistor_SMD:R_0603_1608Metric"
    comps = comps or [_comp("R1", "10k", r, "C1", "10k 1%"), _comp("R2", "10k", r, "C1", "10k 1%"),
                      _comp("R3", "10k", r, "C1", "10k 1%"), _comp("D1", "M7", "Diode_SMD:D_SMA", "C2", "Rectifier"),
                      _comp("H1", "A", "Lib:Hole", "", "")]
    nets = nets or [_net("VIN", ("R1", "1"), ("R3", "1")), _net("MID", ("R1", "2"), ("R2", "1"), ("D1", "2")),
                    _net("GND", ("R2", "2"), ("D1", "1"), ("R3", "2"))]
    (tmp_path / "output" / "t.net").write_text(
        f'(export (components {" ".join(comps)}) (nets {" ".join(nets)}))', encoding="utf-8")
    head = "Comment,Designator,Footprint,LCSC Part #,Quantity\n"
    (tmp_path / "output" / "t-bom-jlcpcb.csv").write_text(
        head + (bom or '10k,"R1,R2,R3",R_0603_1608Metric,C1,3\nM7,D1,D_SMA,C2,1\n'), encoding="utf-8")
    (tmp_path / "output" / "t-bom-jlcpcb-small.csv").write_text(
        head + (bom_small or '10k,"R1,R3",R_0603_1608Metric,C1,2\nM7,D1,D_SMA,C2,1\n'), encoding="utf-8")
    return tmp_path


def test_committed_outputs_match(tmp_path):
    problems, summary = committed.check(_write(tmp_path), COMPS)
    assert problems == []
    assert "4 components and 3 nets" in summary


def test_netlist_change_without_regeneration_is_found(tmp_path):
    project = _write(tmp_path)
    moved = [dict(c) for c in COMPS]
    moved[3] = {**moved[3], "pins": {"1": "MID", "2": "GND"}}  # R3 pin 1 moves from VIN to MID
    problems, _ = committed.check(project, moved)
    assert problems == ["KiCad net VIN joins design nets ['MID', 'VIN']",
                        "design net VIN is not in the schematic"]
    added = COMPS + [{**COMPS[0], "ref": "R4"}]
    problems, _ = committed.check(project, added)
    assert any("R4 is in the design netlist and not in the schematic" in p for p in problems)
    assert any("R4 should be fitted and is not in the BOM" in p for p in problems)


def test_part_number_change_is_found(tmp_path):
    project = _write(tmp_path)
    other = [dict(c) for c in COMPS]
    other[2] = {**other[2], "lcsc": "C9"}
    with pytest.raises(parity.ParityError, match="C9"):  # not in the part table at all
        committed.check(project, other)


def test_bom_of_a_build_is_checked(tmp_path):
    # R2 is an 8-zone part: it must not be on the small build's BOM.
    project = _write(tmp_path, bom_small='10k,"R1,R2,R3",R_0603_1608Metric,C1,3\nM7,D1,D_SMA,C2,1\n')
    problems, _ = committed.check(project, COMPS)
    assert problems == ["BOM of build small: R2 is in the BOM and should not be fitted"]


def test_bom_quantity_and_number_are_checked(tmp_path):
    project = _write(tmp_path, bom='10k,"R1,R2,R3",R_0603_1608Metric,C1,2\nM7,D1,D_SMA,C7,1\n')
    problems, _ = committed.check(project, COMPS)
    assert any("3 designators and quantity 2" in p for p in problems)
    assert any("D1 has LCSC number 'C7'" in p for p in problems)


def test_missing_mechanical_part_is_found(tmp_path):
    r = "Resistor_SMD:R_0603_1608Metric"
    comps = [_comp("R1", "10k", r, "C1", "10k 1%"), _comp("R2", "10k", r, "C1", "10k 1%"),
             _comp("R3", "10k", r, "C1", "10k 1%"), _comp("D1", "M7", "Diode_SMD:D_SMA", "C2", "Rectifier")]
    problems, _ = committed.check(_write(tmp_path, comps=comps), COMPS)
    assert problems == ["H1 is expected as a mechanical part and is not in the schematic"]
