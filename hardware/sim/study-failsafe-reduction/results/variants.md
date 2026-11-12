# Variants against revision A: parts, cost, area

Study, not the product. Written by `cost.py`; do not edit.

8-zone build. Revision A by the same model: 121.22 USD per controller at 5, 70.57 USD per controller at 25, 53.71 USD per controller at 100
(board, parts, assembly, freight, duty, enclosure and the builder's time, as `docs/hardware/cost.md`). The board is 114 x 110 mm,
12540 mm2, with 840 SMT joints and 35 extended part types that pay the feeder fee.
Prices: the snapshot of `tools/cost` of 2026-11-11, and `new-parts.csv` of 2026-11-12 for the one-shot. A negative number is a saving.

| variant | what | parts removed | parts added | net parts | extended part types that pay the fee | SMT joints | landed cost per controller at 5, USD | landed cost per controller at 25, USD | landed cost per controller at 100, USD | of which parts at 5, USD | of which parts at 25, USD | of which parts at 100, USD | courtyard area freed, mm2 | courtyard area of the new parts, mm2 | net area, mm2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| V1 | One-chip detector resets the expander. No LED supply switch, no relay, no over-current latch | 40 | 8 | -32 | 0 | -70 | -1.01 | -0.86 | -0.76 | -0.64 | -0.52 | -0.36 | 498 | 62 | 436 |
| V2 | One-chip detector: expander reset, LED supply switch, K1 on the expander bit. No second detector, no latch | 29 | 9 | -20 | 0 | -47 | +0.33 | +0.21 | +0.12 | +0.32 | +0.23 | +0.24 | 232 | 76 | 157 |
| V3 | V2 with the relay detector of revision A kept: two layers. No latch | 19 | 9 | -10 | 0 | -25 | +0.57 | +0.47 | +0.32 | +0.47 | +0.39 | +0.34 | 164 | 76 | 89 |
| D | Revision A with the relay detector removed and nothing else changed | 10 | 0 | -10 | 0 | -22 | -0.29 | -0.26 | -0.22 | -0.18 | -0.16 | -0.12 | 68 | 0 | 68 |
| L | Revision A with the over-current latch removed and nothing else changed | 5 | 0 | -5 | 0 | -10 | -0.10 | -0.05 | -0.08 | -0.06 | -0.02 | -0.02 | 23 | 0 | 23 |

| variant | removed | added |
|---|---|---|
| V1 | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q3 R25 R26 C21 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 K1 D5 Q1 R11 R12 D23 JP1 R52 C33 R54 D8 R87 | U_WD (74LVC1G123DP,125 retriggerable one-shot, TSSOP-8, C513291), C_DEC (100 nF 50 V X7R 0603, C14663), R_T1 (100k 1% 0603, C25803), R_T2 (100k 1% 0603, C25803), C_T (4.7 uF 25 V X5R 0805, C1779), R_S (1k 1% 0603, C21190), R_PD (100k 1% 0603, C25803), U_SV (TLV803EA30DBZR supervisor, as U12, SOT-23, C5218924) |
| V2 | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 R52 C33 R54 D8 R87 | U_WD (74LVC1G123DP,125 retriggerable one-shot, TSSOP-8, C513291), C_DEC (100 nF 50 V X7R 0603, C14663), R_T1 (100k 1% 0603, C25803), R_T2 (100k 1% 0603, C25803), C_T (4.7 uF 25 V X5R 0805, C1779), R_S (1k 1% 0603, C21190), R_PD (100k 1% 0603, C25803), U_SV (TLV803EA30DBZR supervisor, as U12, SOT-23, C5218924), Q_INV (AO3400A N-MOSFET SOT-23, C20917) |
| V3 | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 R52 C33 R54 D8 R87 | U_WD (74LVC1G123DP,125 retriggerable one-shot, TSSOP-8, C513291), C_DEC (100 nF 50 V X7R 0603, C14663), R_T1 (100k 1% 0603, C25803), R_T2 (100k 1% 0603, C25803), C_T (4.7 uF 25 V X5R 0805, C1779), R_S (1k 1% 0603, C21190), R_PD (100k 1% 0603, C25803), U_SV (TLV803EA30DBZR supervisor, as U12, SOT-23, C5218924), Q_INV (AO3400A N-MOSFET SOT-23, C20917) |
| D | Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 | nothing |
| L | R52 C33 R54 D8 R87 | nothing |
