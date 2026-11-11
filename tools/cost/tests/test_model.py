import math

import pytest

from zfcost import model as m
from zfcost import report


@pytest.fixture(scope="module")
def inp():
    return m.Inputs.load()


def part(least=0, loss=0, min_purchase=1, breaks=((1, 1.0),), library="basic", preferred=False):
    return m.Part("C1", "X", library, preferred, 1000, least, loss, min_purchase, breaks)


# --- Rules on made-up numbers ---


def test_price_at_picks_the_last_break_reached():
    breaks = ((1, 3.0), (10, 2.0), (100, 1.0))
    assert m.price_at(breaks, 1) == 3.0
    assert m.price_at(breaks, 9) == 3.0
    assert m.price_at(breaks, 10) == 2.0
    assert m.price_at(breaks, 5000) == 1.0
    # Below the first break the first price applies.
    assert m.price_at(((20, 0.5),), 3) == 0.5


def test_assembled_order_adds_attrition_then_applies_minimum():
    p = part(least=20, loss=10)
    assert m.assembled_order_qty(5, p, True) == 20  # 5 + 10 < 20
    assert m.assembled_order_qty(15, p, True) == 25
    assert m.assembled_order_qty(15, p, False) == 15
    assert m.assembled_order_qty(0, p, True) == 0


def test_loose_order_respects_minimum_purchase():
    assert m.loose_order_qty(3, part(min_purchase=4)) == 4
    assert m.loose_order_qty(7, part(min_purchase=4)) == 7


def test_marginal_tiers():
    tiers = [[100, 1.0], [200, 0.5]]
    assert m.tiered(50, tiers) == 50
    assert m.tiered(150, tiers) == 100 + 25
    # Past the last tier the last rate continues.
    assert m.tiered(300, tiers) == 100 + 50 + 50


def test_interpolate():
    xs, ys = [100, 200, 400], [10.0, 20.0, 30.0]
    assert m.interpolate(50, xs, ys) == 10.0
    assert m.interpolate(150, xs, ys) == 15.0
    assert m.interpolate(300, xs, ys) == 25.0
    assert m.interpolate(600, xs, ys) == 40.0


def test_split_orders():
    assert m.split_orders(25, "auto", 50) == [(25, "economic")]
    assert m.split_orders(100, "auto", 50) == [(100, "standard")]
    assert m.split_orders(101, "economic", 50) == [(34, "economic"), (34, "economic"), (33, "economic")]


def test_pcb_rounds_up_to_a_quoted_quantity():
    fab = {"quantities": [5, 10, 20], "via_0p2": [50.0, 60.0, 80.0]}
    assert m.pcb_order(5, fab, "0p2", True) == (5, 50.0)
    assert m.pcb_order(6, fab, "0p2", True) == (10, 60.0)
    q, usd = m.pcb_order(40, fab, "0p2", True)
    assert q == 40 and usd == pytest.approx(160.0)


# --- The data agree with the layout ---


def test_bom_places_what_the_placement_file_places(inp):
    for build, n in inp.sourced["board"]["placements"].items():
        assert sum(ln.qty[build] for ln in inp.lines if ln.kind == "smt") == n


def test_every_priced_line_is_in_the_snapshot(inp):
    for ln in inp.lines:
        if ln.kind != "cell":
            assert ln.lcsc in inp.parts, ln.mpn
    assert m.SPARE_FUSE_LCSC in inp.parts


def test_extended_type_count(inp):
    smt = {ln.lcsc for ln in inp.lines if ln.kind == "smt" and ln.qty["8-zone"]}
    assert len(smt) == 89
    assert sum(1 for c in smt if inp.parts[c].fee_extended) == 40


# --- Whole-batch arithmetic, redone by hand from the parts of the result ---


def test_batch_total_is_the_sum_of_its_parts(inp):
    c = m.batch_cost(25, "8-zone", inp)
    assert c.total == pytest.approx(sum(getattr(c, k) for k in m.Cost.CATEGORIES))
    assert c.smt_parts + c.hand_parts == pytest.approx(sum(line[4] for line in c.lines))


def test_economic_fees_by_hand(inp):
    a = inp.sourced["assembly"]
    c = m.batch_cost(10, "8-zone", inp)
    expect = a["economic_setup"] + a["economic_stencil"] + a["confirm_placement"]
    expect += 850 * 10 * a["economic_joint"] + 40 * a["economic_feeder_extended"]
    assert c.assembly == pytest.approx(expect)
    assert c.extended_fee == pytest.approx(40 * 3.07)


def test_duty_is_rate_times_goods(inp):
    c = m.batch_cost(10, "4-zone", inp)
    goods = c.smt_parts + c.hand_parts + c.pcb + c.assembly
    assert c.tariff == pytest.approx(inp.a["us_tariff_rate"] * goods)
    zero = m.batch_cost(10, "4-zone", m.with_assumption(inp, "us_tariff_rate", 0.0))
    assert zero.tariff == 0.0


def test_cell_minimum_order(inp):
    c = m.batch_cost(5, "8-zone", inp)
    assert c.cell == pytest.approx(20 * 0.2855 + inp.a["us_distributor_shipping"])


def test_enclosure_whole_spools_and_insert_packs(inp):
    a, e = inp.a, inp.sourced["enclosure"]
    n = 10
    spools = math.ceil(n * e["petg_g_per_unit"] * a["petg_waste_factor"] / 1000)
    expect = (
        spools * e["petg_usd_per_kg"] + e["insert_pack_usd"] + n * 7 * a["screw_usd_each"] + n * a["label_usd_each"]
    )
    assert m.enclosure_cost(n, inp) == pytest.approx(expect)


def test_scenarios_save_money_and_issue31_removes_parts(inp):
    for _desc, fn in m.SCENARIOS.values():
        alt = fn(inp)
        assert m.batch_cost(25, "8-zone", alt).total <= m.batch_cost(25, "8-zone", inp).total
    alt = m.scenario_issue31(inp)
    removed = sum(ln.qty["8-zone"] for ln in inp.lines) - sum(ln.qty["8-zone"] for ln in alt.lines)
    assert removed == 27
    assert alt.joints["8-zone"] == 850 - 54


def test_unit_cost_falls_with_quantity(inp):
    for b in m.BUILDS:
        costs = [m.batch_cost(n, b, inp).per_unit() for n in (5, 10, 25, 100)]
        assert costs == sorted(costs, reverse=True)


def test_render_replaces_only_marked_blocks(inp):
    text = "before\n<!-- zfcost:begin types -->\nold\n<!-- zfcost:end -->\nafter\n"
    out = report.render(text, inp)
    assert out.startswith("before\n<!-- zfcost:begin types -->\n")
    assert "| Build |" in out and "old" not in out and out.endswith("<!-- zfcost:end -->\nafter\n")
    assert report.render(out, inp) == out
