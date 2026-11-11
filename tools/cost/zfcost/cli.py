"""zfcost: landed cost of ZoneFlow controllers.

uv run zfcost table                     # both builds at 5, 10, 25, 100
uv run zfcost table --qty 3 40 --build 4-zone --set us_tariff_rate=0.25
uv run zfcost render ../../docs/hardware/cost.md          # rewrite the tables
uv run zfcost render ../../docs/hardware/cost.md --check  # fail if out of date
uv run zfcost fetch                     # refresh data/jlcpcb-parts.csv (network)
"""

from __future__ import annotations

import argparse
import sys
import tomllib
from pathlib import Path

from . import model as m
from . import report


def parse_set(items: list[str]) -> dict:
    out = {}
    for item in items:
        name, _, raw = item.partition("=")
        # TOML parses numbers, booleans and quoted strings; bare words are strings.
        try:
            out[name] = tomllib.loads(f"v = {raw}")["v"]
        except tomllib.TOMLDecodeError:
            out[name] = raw
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        prog="zfcost", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="cmd", required=True)
    t = sub.add_parser("table", help="print the cost tables")
    t.add_argument("--qty", type=int, nargs="+", default=list(report.QTYS))
    t.add_argument("--build", choices=m.BUILDS, nargs="+", default=list(m.BUILDS))
    t.add_argument("--set", action="append", default=[], metavar="NAME=VALUE", help="change an assumption")
    r = sub.add_parser("render", help="rewrite the zfcost blocks of a Markdown page")
    r.add_argument("page", type=Path)
    r.add_argument("--check", action="store_true", help="only check that the page is current")
    f = sub.add_parser("fetch", help="refresh the JLCPCB parts snapshot (network)")
    f.add_argument("--out", type=Path, default=m.DATA / "jlcpcb-parts.csv")
    args = ap.parse_args(argv)

    if args.cmd == "fetch":
        from .fetch import fetch

        n = fetch(m.BOM_CSV, args.out)
        print(f"wrote {n} parts to {args.out}")
        return 0

    if args.cmd == "render":
        inp = m.Inputs.load()
        old = args.page.read_text()
        new = report.render(old, inp)
        if args.check:
            if new != old:
                print(f"{args.page} is out of date: run zfcost render {args.page}", file=sys.stderr)
                return 1
            print(f"{args.page} is current")
            return 0
        args.page.write_text(new)
        print(f"rewrote {args.page}")
        return 0

    inp = m.Inputs.load(parse_set(args.set))
    qtys = tuple(args.qty)
    print(f"Estimate from data dated {inp.sourced['snapshot_date']}. USD. Not a quote.\n")
    for b in args.build:
        print(report.breakdown(inp, b, qtys))
        print()
    print(report.order_totals(inp, qtys))
    return 0


if __name__ == "__main__":
    sys.exit(main())
