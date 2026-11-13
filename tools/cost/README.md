# zfcost

Landed cost of ZoneFlow controllers, from dated price snapshots. It writes the
tables of [`docs/hardware/cost.md`](../../docs/hardware/cost.md).

This is an estimate. It has never been compared with a real order.

```sh
cd tools/cost
uv run zfcost table                                   # both builds at 5, 10, 25 and 100
uv run zfcost table --qty 3 40 --build 4-zone --set us_tariff_rate=0.25
uv run zfcost render ../../docs/hardware/cost.md      # rewrite the tables on the page
uv run zfcost render ../../docs/hardware/cost.md --check
uv run pytest -q
```

## Files

| File | What |
|---|---|
| `data/jlcpcb-parts.csv` | One row per LCSC number of `hardware/bom-preliminary.csv`: stock, price breaks, basic or extended, preferred, the two quantities JLCPCB uses for minimum order and attrition, and the date. Written by `zfcost fetch`. |
| `data/replaced-parts.csv` | The snapshot rows of the part numbers that the two passes on extended part types replaced (five on 2026-11-11, four on 2026-11-12), each with its pass, the number that replaced it and, where only part of a BOM line changed, how many parts. The model prices the board as it was from them, for the table of stages. `zfcost fetch --side` refetches it. |
| `data/revb-parts.csv` | The snapshot rows of the three basic parts that the revision B package would add and that are not on the BOM today. `zfcost fetch --side` refetches it. |
| `data/sourced.toml` | Facts read from a source, each with the source and the date: the board from the layout, the JLCPCB board and freight quotes, the assembly price list, the coin cell, filament and inserts, US duty, end-user items, retail prices. |
| `data/assumptions.toml` | Guesses and choices, each named, with what it means. Change one there or with `--set name=value`. |
| `zfcost/model.py` | The rules: how many parts JLCPCB buys, which price break applies, assembly fees, board quantity, freight, duty, enclosure, scenarios, and the stages of the work on extended part types (`stages`, `scenario_revb`). |
| `zfcost/report.py` | The Markdown tables. |
| `zfcost/fetch.py` | Refreshes `data/jlcpcb-parts.csv` from the JLCPCB parts search. |

## Refreshing the prices

1. `uv run zfcost fetch` rewrites `data/jlcpcb-parts.csv` with today's date.
   The endpoint is the one the JLCPCB parts search page uses; it is not a
   documented API.
   `uv run zfcost fetch --side` does the same for `data/replaced-parts.csv`
   and `data/revb-parts.csv`. Run both on one day, or the stages on the cost
   page compare prices of different days.
2. The board and freight quotes in `data/sourced.toml` come from the JLCPCB
   online quote page (no login). Enter the board in `[board]` there, read the
   price at each quantity, and write the date.
3. Every other entry of `data/sourced.toml` names its page. Read it again and
   change the date.
4. `uv run zfcost render ../../docs/hardware/cost.md`, then change the date
   at the top of that page.

CI runs the tests and `render --check`, so the page cannot drift from the data.
