# SPDX-License-Identifier: Apache-2.0
"""Plots compared by what they draw, and the netlist normalised across builds of KiCad."""

from hwflow import parity, plotcmp

HEAD = ('<?xml version="1.0" standalone="no"?>\n<svg xmlns="http://www.w3.org/2000/svg" width="297mm" '
        'height="210mm" viewBox="0 0 297 210">\n')
RED = '<g style="fill:none; stroke:#840000; stroke-width:0.15">\n'
BLUE = '<g style="fill:none; stroke:#000084; stroke-width:0.15">\n'
A = '<path d="M 10.0,10.0\n20.0,10.0\n" />\n'
B = '<path d="M 30.0,10.0\n30.0,40.0\n" />\n'
TEXT = '<g class="stroked-text"><desc>R1</desc>\n<path d="M 5.0,5.0 L 6.0,6.0" />\n</g>\n'


def _svg(path, title, *body):
    path.write_text(HEAD + f"<title>SVG Image created as x.svg date {title} </title>\n" + "".join(body) + "</svg>\n")
    return path


def test_order_and_export_time_do_not_matter(tmp_path):
    one = _svg(tmp_path / "a.svg", "2026-11-10T10:00:00", RED, A, B, "</g>\n", TEXT)
    # The same elements in another order, split over two groups, at another time.
    two = _svg(tmp_path / "b.svg", "2026-11-11T11:11:11", TEXT, RED, B, "</g>\n", RED, A, "</g>\n")
    assert plotcmp.canonical(one) == plotcmp.canonical(two)
    assert plotcmp.compare(one, two) == []


def test_a_moved_line_a_changed_style_and_a_changed_text_are_found(tmp_path):
    base = _svg(tmp_path / "a.svg", "t", RED, A, B, "</g>\n", TEXT)
    moved = _svg(tmp_path / "b.svg", "t", RED, A.replace("20.0,10.0", "20.0,10.1"), B, "</g>\n", TEXT)
    assert plotcmp.compare(base, moved)
    # The same path under another group style is another drawing.
    styled = _svg(tmp_path / "c.svg", "t", RED, A, "</g>\n", BLUE, B, "</g>\n", TEXT)
    assert plotcmp.compare(base, styled)
    renamed = _svg(tmp_path / "d.svg", "t", RED, A, B, "</g>\n", TEXT.replace("R1", "R2"))
    assert plotcmp.compare(base, renamed)
    missing = _svg(tmp_path / "e.svg", "t", RED, A, "</g>\n", TEXT)
    assert plotcmp.compare(base, missing)
    doubled = _svg(tmp_path / "f.svg", "t", RED, A, B, B, "</g>\n", TEXT)
    assert plotcmp.compare(base, doubled)


def test_keep_puts_back_only_the_plots_that_draw_the_same(tmp_path):
    out = tmp_path / "output"
    (out / "schematic-svg").mkdir(parents=True)
    same_old = _svg(out / "schematic-svg" / "same.svg", "old", RED, A, B, "</g>\n").read_bytes()
    _svg(out / "schematic-svg" / "changed.svg", "old", RED, A, "</g>\n")
    (out / "x.pdf").write_bytes(b"old pdf")
    scratch = tmp_path / "build" / "previous-plots"
    plotcmp.stash(out, scratch)
    assert not (out / "schematic-svg").exists() and not (out / "x.pdf").exists()

    (out / "schematic-svg").mkdir()
    _svg(out / "schematic-svg" / "same.svg", "new", RED, B, A, "</g>\n")
    changed_new = _svg(out / "schematic-svg" / "changed.svg", "new", RED, B, "</g>\n").read_bytes()
    (out / "x.pdf").write_bytes(b"new pdf")
    assert plotcmp.keep(scratch, out) == (1, 1)
    assert (out / "schematic-svg" / "same.svg").read_bytes() == same_old
    assert (out / "schematic-svg" / "changed.svg").read_bytes() == changed_new
    assert (out / "x.pdf").read_bytes() == b"new pdf"  # a page was redrawn, so the PDF is the new one

    # Nothing redrawn: every plot and the PDF keep their old bytes.
    plotcmp.stash(out, scratch)
    (out / "schematic-svg").mkdir()
    _svg(out / "schematic-svg" / "same.svg", "newer", RED, A, B, "</g>\n")
    _svg(out / "schematic-svg" / "changed.svg", "newer", RED, B, "</g>\n")
    (out / "x.pdf").write_bytes(b"newer pdf")
    assert plotcmp.keep(scratch, out) == (2, 0)
    assert (out / "schematic-svg" / "same.svg").read_bytes() == same_old
    assert (out / "x.pdf").read_bytes() == b"new pdf"


def test_netlist_unit_ids_are_sorted(tmp_path):
    a, b, c = ("391e8978-4e27-511e-a19b-5884af5289cd", "e55e1585-d02d-583c-a168-b3f3538b063f",
               "0b4b3991-50ce-5839-a3ae-c81dc0c62efe")
    one = f'(comp (ref "U6") (sheetpath (names "/A/") (tstamps "/{c}/"))\n (tstamps "{a}" "{b}"\n\t"{c}"\n ))'
    two = f'(comp (ref "U6") (sheetpath (names "/A/") (tstamps "/{c}/"))\n (tstamps "{b}" "{c}"\n\t"{a}"\n ))'
    assert parity.normalise_netlist(one) == parity.normalise_netlist(two)
    assert f'(tstamps "{c}" "{a}" "{b}")' in parity.normalise_netlist(one)
    assert f'(tstamps "/{c}/")' in parity.normalise_netlist(one)  # a sheet path is left alone
    single = f'(tstamps "{a}")'
    assert parity.normalise_netlist(single) == single
