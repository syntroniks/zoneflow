# SPDX-License-Identifier: Apache-2.0
"""Placement files per build variant and the pin-1 table. Needs no KiCad."""

import csv
import json

from hwflow import fab, stable

POS = "Ref,Val,Package,PosX,PosY,Rot,Side\nR1,10k,R_0603_1608Metric,10.0,20.0,90,top\n" \
      "U1,IC,SOIC-8_3.9x4.9mm_P1.27mm,30.0,40.0,0,top\nR501,10k,R_0603_1608Metric,50.0,20.0,0,top\n"


def _rows(path):
    with open(path, newline="") as fh:
        return list(csv.DictReader(fh))


def test_cpl_applies_rotations_and_leaves_out_a_variants_parts(tmp_path):
    (tmp_path / "pos.csv").write_text(POS)
    (tmp_path / "rot.csv").write_text("footprint_regex,rotation,dx,dy\n^SOIC-,270,0,0\n")
    (tmp_path / "model.json").write_text(json.dumps({"variants": {"4-zone": ["R501"]}}))
    base = tmp_path / "cpl.csv"
    assert fab.main(["cpl", str(tmp_path / "pos.csv"), str(base), "--rotations", str(tmp_path / "rot.csv")]) == 0
    rows = _rows(base)
    assert [r["Designator"] for r in rows] == ["R1", "U1", "R501"]
    assert rows[1]["Rotation"] == "270" and rows[0]["Rotation"] == "90"
    small = tmp_path / "cpl-4.csv"
    assert fab.main(["cpl", str(tmp_path / "pos.csv"), str(small), "--model", str(tmp_path / "model.json"),
                     "--variant", "4-zone"]) == 0
    assert [r["Designator"] for r in _rows(small)] == ["R1", "U1"]
    assert fab.main(["cpl", str(tmp_path / "pos.csv"), str(small), "--model", str(tmp_path / "model.json"),
                     "--variant", "none"]) == 1


def test_pin_1_table_lists_polarised_parts_only(tmp_path):
    def part(ref, fp, pads, body, rot=0, dnp=False):
        return {"ref": ref, "footprint": fp, "side": "top", "rot": rot, "at": [0, 0], "dnp": dnp, "body": body,
                "courtyard": body, "pads": [{"number": n, "at": at} for n, at in pads]}

    placement = {"parts": [
        part("D1", "Diode_SMD:D_SMA", [("1", [8.0, 10.0]), ("2", [12.0, 10.0])], [8.5, 9.0, 11.5, 11.0]),
        part("U1", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", [("1", [18.0, 22.0]), ("8", [22.0, 22.0])],
             [18.0, 18.0, 22.0, 22.5], rot=90),
        part("R1", "Resistor_SMD:R_0603_1608Metric", [("1", [0, 0]), ("2", [1, 0])], [0, 0, 1, 1]),
        part("D9", "Diode_SMD:D_SMA", [("1", [0, 0]), ("2", [1, 0])], [0, 0, 1, 1], dnp=True),
    ]}
    (tmp_path / "placement.json").write_text(json.dumps(placement))
    out = tmp_path / "pin1.csv"
    assert fab.main(["polarity", str(tmp_path / "placement.json"), str(out)]) == 0
    rows = {r["Designator"]: r for r in _rows(out)}
    assert set(rows) == {"D1", "U1"}                      # no resistor, no part that is not fitted
    assert rows["D1"]["Pad is at"] == "left" and rows["D1"]["Pad"] == "1"
    assert rows["U1"]["Pad is at"] == "upper left" and rows["U1"]["Rotation"] == "90"


def test_export_times_are_taken_out_of_gerber_and_drill_headers(tmp_path):
    g = tmp_path / "a.gbr"
    g.write_text("%TF.GenerationSoftware,KiCad,Pcbnew,10.0.8*%\n%TF.CreationDate,2026-11-10T18:03:11-06:00*%\nG04 x*\n")
    d = tmp_path / "a.drl"
    d.write_text("M48\n; DRILL file {KiCad 10.0.8} date 2026-11-10T18:03:12-0600\n"
                 "; #@! TF.CreationDate,2026-11-10T18:03:12-06:00\n")
    assert stable.normalise(g, "2026-11-10") and stable.normalise(d, "2026-11-10")
    assert "%TF.CreationDate,2026-11-10T00:00:00+00:00*%" in g.read_text()
    text = d.read_text()
    assert "date 2026-11-10T00:00:00+0000" in text and "TF.CreationDate,2026-11-10T00:00:00+00:00" in text
    assert not stable.normalise(g, "2026-11-10")          # a second pass changes nothing


def test_export_times_without_a_time_zone_are_taken_out_too(tmp_path):
    g = tmp_path / "a.gbr"
    g.write_text("G04 Created by KiCad (PCBNEW 10.0.8) date 2026-11-11 07:36:22*\n%MOMM*%\n")
    d = tmp_path / "a.drl"
    d.write_text("M48\n; DRILL file KiCad 10.0.8 date 2026-11-11T07:36:23\n; FORMAT={-:-/ absolute / metric}\n")
    r = tmp_path / "drc.rpt"
    r.write_text("** Drc report for a.kicad_pcb **\n** Created on 2026-11-11T07:35:59 **\n** Found 0 violations **\n")
    s = tmp_path / "a.step"
    s.write_text("ISO-10303-21;\nHEADER;\nFILE_NAME('a.step','2026-11-11T07:36:34',('Pcbnew'),('Kicad'),\n#1 = X;\n")
    for f in (g, d, r, s):
        assert stable.normalise(f, "2026-11-10")
        assert not stable.normalise(f, "2026-11-10")      # a second pass changes nothing
    assert "date 2026-11-10 00:00:00*" in g.read_text()
    assert "date 2026-11-10T00:00:00\n" in d.read_text()
    assert "** Created on 2026-11-10T00:00:00 **" in r.read_text()
    assert "FILE_NAME('a.step','2026-11-10T00:00:00',('Pcbnew')" in s.read_text() and "#1 = X;" in s.read_text()
