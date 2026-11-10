# SPDX-License-Identifier: Apache-2.0
"""Multi-unit parts, variants, netlist-driven capture, the parity check and the page lint."""

import pytest
from conftest import kicad_cli, needs_kicad

from hwflow import netlist, parity, schlint, schwriter, symlib
from hwflow.capture import Capture
from hwflow.design import Design, DesignError

pytestmark = pytest.mark.skipif(not symlib.symbol_dir().exists(), reason="KiCad symbol libraries not installed")

R0603 = "Resistor_SMD:R_0603_1608Metric"


def _parts(**over):
    table = {
        "C1": parity.PartSpec("C1", "Device:R", R0603, "10k", "0603", {}),
        "C2": parity.PartSpec("C2", "Device:D", "Diode_SMD:D_SMA", "M7", "SMA", {"A": ["2"], "K": ["1"]}),
        "C3": parity.PartSpec("C3", "Device:LED", "LED_SMD:LED_0603_1608Metric", "red", "0603",
                              {"A": ["2"], "K": ["1"]}),
    }
    table.update(over)
    return table


def _comp(ref, value, package, lcsc, pins, variant="all"):
    return {"ref": ref, "value": value, "package": package, "lcsc": lcsc, "block": "t", "variant": variant,
            "pins": pins}


def _design_netlist():
    """Supply, R1, D1 to ground, and one LED channel used twice."""
    comps = [
        _comp("R1", "10k 1%", "0603", "C1", {"1": "VIN", "2": "MID"}),
        _comp("D1", "Rectifier", "SMA", "C2", {"A": "MID", "K": "GND"}),
        _comp("R2", "10k 1%", "0603", "C1", {"1": "MID", "2": "VIN"}),
    ]
    for n, variant in ((1, "all"), (2, "8")):
        comps += [
            _comp(f"R{n}01", "10k 1%", "0603", "C1", {"1": "MID", "2": f"LEDA_Z{n}"}, variant),
            _comp(f"D{n}01", "LED red", "0603", "C3", {"A": f"LEDA_Z{n}", "K": f"ZD_Z{n}_N"}, variant),
            _comp(f"R{n}02", "10k 1%", "0603", "C1", {"1": f"ZD_Z{n}_N", "2": "GND"}, variant),
        ]
    return comps


def _capture(comps, parts=None, break_wire=False):
    d = Design("cap", "capture test")
    cap = Capture(d, comps, parts or _parts(), power={"GND": "power:GND"})
    d.capture = cap
    cap.assign("main", ["R1", "D1", "R2"])
    cap.template("chan", {"Z1": 100, "Z2": 200}, ["R101", "D101", "R102"])

    def main(s):
        r1 = s.place("R1", (50.8, 50.8), rot=90)
        d1 = s.attach("D1", "A", (76.2, 50.8), (1, 0))
        s.wire(r1[2], d1["A"], label=3.81)
        s.place("R2", (50.8, 76.2), rot=90)
        if break_wire:
            s.wire(r1[1], d1["K"], route="vh")

    def chan(s):
        r1 = s.place("R101", (50.8, 50.8), rot=90)
        d1 = s.attach("D101", "A", (76.2, 50.8), (1, 0))
        s.wire(r1[2], d1["A"], label=3.81)
        r2 = s.place("R102", (101.6, 50.8), rot=90)
        s.wire(d1["K"], r2[1], label=3.81)

    cap.sheet("main", "main", build=main)
    cap.sheet("chan", "chan", build=chan)
    cap.instantiate("main", "Main", (25.4, 25.4))
    cap.instantiate("chan", "Z1", (25.4, 76.2))
    cap.instantiate("chan", "Z2", (25.4, 101.6))
    cap.finish()
    d.variant("small", dnp={c["ref"] for c in comps if c["variant"] == "8"}, fitted={"all"})
    return d, cap


def test_multi_unit_part_is_one_component_and_needs_every_unit():
    d = Design("mu", "units")
    r = d.root
    gnd, vcc = r.power("GND"), r.power("VCC")
    for u in (1, 2, 3, 4):
        p = r.part("U1", "Comparator:LM2901", at=(50.8, 25.4 * u), unit=u)
        r.connect(r.net(f"P{u}"), p["+"])
        r.connect(r.net(f"N{u}"), p["-"])
        r.connect(r.net(f"O{u}"), [q for q in p.all_pins() if q.pin.name == ""][0])
    with pytest.raises(DesignError, match=r"unit\(s\) \[5\] are not placed"):
        d.flatten()
    p = r.part("U1", "Comparator:LM2901", at=(101.6, 50.8), unit=5)
    r.connect(vcc, p["V+"])
    r.connect(gnd, p["V-"])
    flat = d.flatten()
    assert {fp.ref for fp in flat.parts if not fp.part.virtual} == {"U1"}
    assert len({n for net in flat.nets for n in net.nodes}) == 14


def test_capture_refuses_a_wire_between_two_nets():
    with pytest.raises(DesignError, match="would join"):
        _capture(_design_netlist(), break_wire=True)


def test_capture_rejects_a_template_instance_that_differs():
    comps = _design_netlist()
    next(c for c in comps if c["ref"] == "R202")["pins"]["2"] = "MID"
    with pytest.raises(DesignError, match="does not correspond"):
        _capture(comps)


def test_stands_for_accepts_only_the_design_name():
    assert parity.stands_for("/Fail-safe/KA_N2", "KA_N2")
    assert parity.stands_for("GND", "GND")
    assert parity.stands_for("/Z3/T2", "T2_Z3")
    assert parity.stands_for("/MV/ZD_N", "ZD_MV_N")
    assert not parity.stands_for("/Z3/T2", "T2_Z4")
    assert not parity.stands_for("/Fail-safe/Net-(R1-Pad1)", "KA_N2")


@needs_kicad
def test_parity_passes_and_catches_each_kind_of_difference(tmp_path):
    comps = _design_netlist()
    d, cap = _capture(comps)
    schwriter.write(d, tmp_path)
    root = tmp_path / "cap.kicad_sch"
    r = kicad_cli("sch", "export", "netlist", "-o", str(tmp_path / "out.net"), str(root), cwd=tmp_path)
    assert r.returncode == 0, r.stderr
    nl = netlist.read(tmp_path / "out.net")
    assert parity.compare(cap.exp, nl) == []
    assert nl.comps["R101"].fields["Spec"] == "10k 1%"

    def problems(mutate, parts=None):
        changed = [dict(c, pins=dict(c["pins"])) for c in comps]
        mutate(changed)
        return parity.compare(parity.expected(changed, parts or _parts()), nl)

    def by(ref, comps_):
        return next(c for c in comps_ if c["ref"] == ref)

    # A pin on another net.
    out = problems(lambda cs: by("R2", cs)["pins"].update({"2": "GND"}))
    assert any("joins design nets" in p or "pieces" in p for p in out)
    # A value changed in the design netlist only.
    out = problems(lambda cs: by("R1", cs).update(value="12k 1%"))
    assert out == ["R1 Spec: the schematic has '10k 1%', the design netlist needs '12k 1%'"]
    # A part number changed.
    out = problems(lambda cs: by("D1", cs).update(lcsc="C3", package="0603"))
    assert any(p.startswith("D1 footprint") for p in out) and any(p.startswith("D1 LCSC") for p in out)
    # A part added to, and one removed from, the design netlist.
    out = problems(lambda cs: cs.append(_comp("R9", "10k 1%", "0603", "C1", {"1": "MID", "2": "GND"})))
    assert "R9 is in the design netlist and not in the schematic" in out
    out = problems(lambda cs: cs.remove(by("R102", cs)))
    assert "R102 is in the schematic and not in the design netlist" in out
    # The part table maps the diode's pins the wrong way round.
    swapped = _parts(C2=parity.PartSpec("C2", "Device:D", "Diode_SMD:D_SMA", "M7", "SMA", {"A": ["1"], "K": ["2"]}))
    assert any("joins design nets" in p for p in problems(lambda cs: None, swapped))
    # A net renamed in the design netlist: connectivity is the same, the label is not.
    def rename(cs):
        for c in cs:
            c["pins"] = {k: ("MIDDLE" if v == "MID" else v) for k, v in c["pins"].items()}
    assert any("label it MIDDLE" in p for p in problems(rename))


@needs_kicad
def test_variant_removes_its_parts_from_the_bom(tmp_path):
    d, cap = _capture(_design_netlist())
    schwriter.write(d, tmp_path)
    fields = "Reference,Value,Footprint,LCSC,Spec"
    import csv
    for name, fitted, n in (("", {"all", "8"}, 9), ("small", {"all"}, 6)):
        dest = tmp_path / f"bom{name}.csv"
        args = ["sch", "export", "bom", "--fields", fields, "--labels", fields, "--exclude-dnp",
                "--ref-range-delimiter", "", "-o", str(dest)] + (["--variant", name] if name else [])
        r = kicad_cli(*args, str(tmp_path / "cap.kicad_sch"), cwd=tmp_path)
        assert r.returncode == 0, r.stderr
        with open(dest, newline="") as fh:
            rows = list(csv.DictReader(fh))
        assert len(rows) == n
        assert parity.compare_bom(cap.exp, rows, fitted, name or "base") == []
        if name:
            wrong = parity.compare_bom(cap.exp, rows, {"all", "8"}, name)
            assert "small: R201 should be fitted and is not in KiCad's BOM" in wrong


def _svg(body: str) -> str:
    return ('<?xml version="1.0"?><svg xmlns="http://www.w3.org/2000/svg" width="297mm" height="210mm" '
            'viewBox="0 0 297 210"><g>' + body + "</g></svg>")


def _text(s, x, y, w=8.0, h=1.3):
    return (f'<g class="stroked-text"><desc>{s}</desc><path d="M{x} {y} L{x + w} {y + h}" />'
            f'<path d="M{x} {y + h} L{x + w} {y}" /></g>')


def test_page_lint_finds_text_on_text_text_on_a_line_and_text_off_the_page(tmp_path):
    clean = _svg(_text("R1", 50, 50) + _text("10k", 50, 54) + '<path d="M40 60 L80 60" />')
    (tmp_path / "ok.svg").write_text(clean)
    assert schlint.lint_svg(tmp_path / "ok.svg", "ok") == []
    bad = _svg(_text("R1", 50, 50) + _text("R2", 55, 50.5) + _text("C1", 100, 100)
               + '<path d="M90 100.6 L120 100.6" />' + _text("NOTE", 292, 100))
    (tmp_path / "bad.svg").write_text(bad)
    out = schlint.lint_svg(tmp_path / "bad.svg", "bad")
    assert len(out) == 3
    assert "text 'R1'" in out[1] and "overlaps text 'R2'" in out[1]
    assert "text 'C1'" in out[2] and "crossed by a line" in out[2]
    assert "'NOTE'" in out[0] and "off the page" in out[0]


def test_model_lint_finds_overlapping_symbols_and_a_wire_through_a_body():
    d = Design("lint", "lint")
    r = d.root
    a = r.part("R1", "Device:R", at=(50.8, 50.8))
    b = r.part("R2", "Device:R", at=(50.8, 52.07))
    c = r.part("R3", "Device:R", at=(76.2, 50.8))
    e = r.part("U1", "Timer_RTC:PCF8563T", at=(101.6, 50.8))
    r.nc_rest(a), r.nc_rest(b), r.nc_rest(c), r.nc_rest(e)
    r.wire((88.9, 50.8), (114.3, 50.8), net=r.net("X"))
    out = schlint.lint_model(d)
    assert any("symbol R1" in p and "overlaps symbol R2" in p for p in out)
    assert any("runs through symbol U1" in p for p in out)
    assert not any("R3" in p for p in out)
