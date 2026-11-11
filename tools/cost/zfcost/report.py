"""Markdown tables for docs/hardware/cost.md and the terminal."""

from __future__ import annotations

import re

from . import model as m

QTYS = (5, 10, 25, 100)
LABEL = {
    "smt_parts": "Parts fitted by JLCPCB",
    "hand_parts": "Parts bought loose: headers, C1, K1, plugs, display, spare fuse",
    "cell": "Coin cell (US distributor)",
    "pcb": "Bare board",
    "assembly": "Assembly fees",
    "freight": "Freight to the US",
    "tariff": "US duty",
    "enclosure": "Enclosure materials",
    "labour": "Builder's time (0 unless a rate is set)",
}


def usd(x: float) -> str:
    return f"{x:,.2f}"


def table(header: list[str], rows: list[list[str]]) -> str:
    out = ["| " + " | ".join(header) + " |", "|" + "|".join("---" for _ in header) + "|"]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(out)


def headline(inp: m.Inputs, qtys=QTYS) -> str:
    rows = []
    for b in m.BUILDS:
        cs = [m.batch_cost(n, b, inp) for n in qtys]
        rows.append([f"{b}, board landed"] + [usd(c.board_total / c.n) for c in cs])
        rows.append([f"{b}, with enclosure"] + [usd(c.per_unit()) for c in cs])
    return table(["USD per controller"] + [f"{n} units" for n in qtys], rows)


def breakdown(inp: m.Inputs, build: str, qtys=QTYS) -> str:
    cs = [m.batch_cost(n, build, inp) for n in qtys]
    rows = []
    for cat in m.Cost.CATEGORIES:
        if cat == "labour" and not any(c.labour for c in cs):
            continue
        rows.append([LABEL[cat]] + [usd(c.per_unit(cat)) for c in cs])
        if cat == "assembly":
            rows.append(["&nbsp;&nbsp;of which for extended parts"] + [usd(c.extended_fee / c.n) for c in cs])
    rows.append(["**Total per controller**"] + [f"**{usd(c.per_unit())}**" for c in cs])
    rows.append(["Assembly service"] + [" + ".join(f"{s} {k}" for k, s in c.orders) for c in cs])
    rows.append(["Boards ordered"] + [str(c.pcb_boards) for c in cs])
    return table([f"{build}, USD per controller"] + [f"{n} units" for n in qtys], rows)


def order_totals(inp: m.Inputs, qtys=QTYS) -> str:
    rows = []
    for b in m.BUILDS:
        cs = [m.batch_cost(n, b, inp) for n in qtys]
        rows.append([b] + [usd(c.total) for c in cs])
    return table(["USD for the whole batch"] + [f"{n} units" for n in qtys], rows)


def extended_fees(inp: m.Inputs, qtys=QTYS) -> str:
    rows = []
    build = "8-zone"
    for n in qtys:
        c = m.batch_cost(n, build, inp)
        duty = c.extended_fee * inp.a["us_tariff_rate"]
        share = (c.extended_fee + duty) / c.total * 100
        rows.append(
            [
                str(n),
                " + ".join(s for _, s in c.orders),
                usd(c.extended_fee),
                usd((c.extended_fee + duty) / n),
                f"{share:.0f} %",
            ]
        )
    return table(
        [
            "Units",
            "Service",
            "Extended-part fees, USD per batch",
            "With duty, USD per controller",
            "Share of the total",
        ],
        rows,
    )


def type_counts(inp: m.Inputs) -> str:
    rows = []
    for b in m.BUILDS:
        smt = sorted({ln.lcsc for ln in inp.lines if ln.qty[b] > 0 and ln.kind == "smt"})
        basic = sum(1 for c in smt if inp.parts[c].library == "basic")
        pref = sum(1 for c in smt if inp.parts[c].library == "extended" and inp.parts[c].preferred)
        ext = sum(1 for c in smt if inp.parts[c].fee_extended)
        parts = sum(ln.qty[b] for ln in inp.lines if ln.kind == "smt")
        rows.append([b, str(parts), str(inp.joints[b]), str(len(smt)), str(basic), str(pref), str(ext)])
    return table(
        ["Build", "Parts placed", "Joints", "Part types", "Basic", "Preferred extended", "Extended, fee charged"],
        rows,
    )


def top_lines(inp: m.Inputs, build="8-zone", qtys=(5, 100), count=10) -> str:
    per = {}
    for n in qtys:
        c = m.batch_cost(n, build, inp)
        for mpn, _kind, _need, _qty, cost in c.lines:
            per.setdefault(mpn, {})[n] = per.get(mpn, {}).get(n, 0.0) + cost / n
    ranked = sorted(per.items(), key=lambda kv: -kv[1].get(qtys[-1], 0.0))[:count]
    rows = [[mpn] + [usd(v.get(n, 0.0)) for n in qtys] for mpn, v in ranked]
    return table(["Part"] + [f"USD per controller at {n}" for n in qtys], rows)


def stock(inp: m.Inputs, n=100, build="8-zone", count=10) -> str:
    rows = []
    for ln in inp.lines:
        if ln.kind == "cell" or ln.qty[build] == 0:
            continue
        p = inp.parts[ln.lcsc]
        need = ln.qty[build] * n
        if ln.kind in ("smt",):
            need = m.assembled_order_qty(need, p, inp.a["attrition_fields"])
        rows.append((p.stock / need, ln.mpn, ln.lcsc, p.stock, need, p.library))
    rows.sort()
    out = []
    for ratio, mpn, lcsc, st, need, lib in rows[:count]:
        flag = "**below the need**" if st < need else ("thin: under 5 batches" if ratio < 5 else "")
        out.append([mpn, lcsc, lib, f"{st:,}", f"{need:,}", f"{ratio:.1f}", flag])
    return table(["Part", "LCSC", "Library", "JLCPCB stock", f"Needed for {n}", "Batches in stock", "Flag"], out)


def sensitivity(inp: m.Inputs, qtys=(5, 25, 100)) -> str:
    rows = []
    base = {n: m.batch_cost(n, "8-zone", inp).per_unit() for n in qtys}
    rows.append(["Base case"] + [usd(base[n]) for n in qtys])
    for rate in (0.0, 0.25, 0.60):
        alt = m.with_assumption(inp, "us_tariff_rate", rate)
        rows.append([f"US duty {rate * 100:.0f} %"] + [usd(m.batch_cost(n, "8-zone", alt).per_unit()) for n in qtys])
    for lcsc, name, factors in (
        ("C5329582", "Display panel", (0.5, 1.5)),
        ("C2913202", "ESP32-S3 module", (0.75, 1.25)),
    ):
        for f in factors:
            alt = m.with_price_factor(inp, lcsc, f)
            rows.append([f"{name} price x {f:g}"] + [usd(m.batch_cost(n, "8-zone", alt).per_unit()) for n in qtys])
    return table(["8-zone, USD per controller"] + [f"{n} units" for n in qtys], rows)


def cuts(inp: m.Inputs, qtys=(5, 25, 100)) -> str:
    rows = []
    for key, (desc, fn) in m.SCENARIOS.items():
        alt = fn(inp)
        sav = [m.batch_cost(n, "8-zone", inp).per_unit() - m.batch_cost(n, "8-zone", alt).per_unit() for n in qtys]
        rows.append([f"`{key}`", desc] + [usd(s) for s in sav])
    return table(["Scenario", "Change"] + [f"Saves USD per controller at {n}" for n in qtys], rows)


def batch_size(inp: m.Inputs) -> str:
    rows = []
    for b in m.BUILDS:
        c5 = m.batch_cost(5, b, inp).per_unit()
        c25 = m.batch_cost(25, b, inp).per_unit()
        c100 = m.batch_cost(100, b, inp).per_unit()
        rows.append([b, usd(c5), usd(c25), usd(c5 - c25), usd(c100), usd(c25 - c100)])
    return table(["Build", "5 units", "25 units", "Saved 5 to 25", "100 units", "Saved 25 to 100"], rows)


def time_table(inp: m.Inputs, qtys=QTYS) -> str:
    rows = []
    for n in qtys:
        c = m.batch_cost(n, "8-zone", inp)
        rows.append(
            [
                str(n),
                f"{c.hours_print:.0f}",
                f"{n * inp.sourced['enclosure']['print_hours_slow']:.0f}",
                f"{c.hours_builder:.1f}",
                f"{c.hours_builder * 60 / n:.0f}",
            ]
        )
    return table(
        [
            "Units",
            "Printer hours, fast printer",
            "Printer hours, slow printer",
            "Builder hours",
            "Builder minutes per controller",
        ],
        rows,
    )


def one_off(inp: m.Inputs) -> str:
    asm = inp.sourced["assembly"]
    fab = inp.sourced["pcb_fab"]
    rows = [
        [
            "Economic assembly: setup, stencil, placement confirmation",
            usd(asm["economic_setup"] + asm["economic_stencil"] + asm["confirm_placement"]),
            "Every order",
        ],
        [
            "Standard assembly: setup, stencil, placement confirmation",
            usd(asm["standard_setup_single"] + asm["standard_stencil_single"] + asm["confirm_placement"]),
            "Every order",
        ],
        [
            "Economic extended-part fees, 8-zone",
            usd(
                sum(
                    1
                    for c in {ln.lcsc for ln in inp.lines if ln.kind == "smt" and ln.qty["8-zone"]}
                    if inp.fee_extended(c)
                )
                * asm["economic_feeder_extended"]
            ),
            "Every order",
        ],
        [
            "Bare boards, 5 pieces, 0.2 mm holes",
            usd(fab["via_0p2"][0]),
            "Every order; 25.00 of it is the engineering fee",
        ],
    ]
    proto = m.batch_cost(5, "8-zone", inp)
    rows.append(["Prototype run: 5 eight-zone controllers, landed, with enclosures", usd(proto.total), "Once"])
    return table(["Item", "USD", "When"], rows)


def assumptions(inp: m.Inputs) -> str:
    notes = m.assumption_notes()

    def show(v) -> str:
        return str(v).lower() if isinstance(v, bool) else str(v)

    rows = [[f"`{k}`", f"`{show(inp.a[k])}`", notes[k]] for k in notes]
    return table(["Name", "Value", "What it means"], rows)


def end_user(inp: m.Inputs) -> str:
    e = inp.sourced["end_user"]
    rows = [
        ["24 VAC transformer", f"{usd(e['transformer_low'])} to {usd(e['transformer_high'])}", e["transformer"]],
        ["Valve cable, 250 ft roll", f"{usd(e['cable_low'])} to {usd(e['cable_high'])}", e["cable"]],
    ]
    return table(["Item", "USD", "Source"], rows)


def retail(inp: m.Inputs) -> str:
    rows = [[r["name"], r["price"], r["source"]] for r in inp.sourced["retail"]]
    return table(["Controller", "Retail price", "Source"], rows)


def pcb_quotes(inp: m.Inputs) -> str:
    fab = inp.sourced["pcb_fab"]
    rows = [[str(q), usd(a), usd(b), usd(b / q)] for q, a, b in zip(fab["quantities"], fab["via_0p3"], fab["via_0p2"])]
    return table(
        ["Boards", "USD, smallest hole 0.3 mm", "USD, smallest hole 0.2 mm (this layout)", "USD per board, 0.2 mm"],
        rows,
    )


def tht_options(inp: m.Inputs, qtys=(5, 25, 100)) -> str:
    rows = []
    for who, label in (("builder", "Builder solders them"), ("assembler", "JLCPCB fits them (Standard service)")):
        alt = m.with_assumption(inp, "tht_by", who)
        cs = [m.batch_cost(n, "8-zone", alt) for n in qtys]
        rows.append([label] + [f"{usd(c.per_unit())} ({c.hours_builder * 60 / c.n:.0f} min)" for c in cs])
    return table(["8-zone, USD per controller (builder minutes per controller)"] + [f"{n} units" for n in qtys], rows)


BLOCKS = {
    "headline": headline,
    "breakdown-8": lambda i: breakdown(i, "8-zone"),
    "breakdown-4": lambda i: breakdown(i, "4-zone"),
    "orders": order_totals,
    "types": type_counts,
    "extfee": extended_fees,
    "toplines": top_lines,
    "stock": stock,
    "sensitivity": sensitivity,
    "cuts": cuts,
    "batch": batch_size,
    "time": time_table,
    "oneoff": one_off,
    "enduser": end_user,
    "retail": retail,
    "pcbfab": pcb_quotes,
    "tht": tht_options,
    "assumptions": assumptions,
}

MARK = re.compile(r"(<!-- zfcost:begin (?P<name>[\w-]+) -->\n)(?P<body>.*?)(<!-- zfcost:end -->)", re.S)


def render(text: str, inp: m.Inputs) -> str:
    def sub(mt: re.Match) -> str:
        name = mt.group("name")
        if name not in BLOCKS:
            raise KeyError(f"unknown block {name}")
        return mt.group(1) + "\n" + BLOCKS[name](inp) + "\n\n" + mt.group(4)

    return MARK.sub(sub, text)
