# V1 with one thing put back at a time: parts, cost, area

Study, not the product. Written by `review_options.py`; do not edit.

8-zone build. Revision A on main has 313 soldered parts by the bill of materials (840 SMT joints) and costs
121.22 USD per controller at 5, 70.57 USD per controller at 25, 53.71 USD per controller at 100 by the model of `tools/cost`.
JP1 is a pad pattern and not a soldered part, so V1 removes 39 soldered parts and 40 designators. A negative number is a saving.

| option | what | soldered parts removed | parts added | net parts | soldered parts on the board | SMT joints | against revision A at 5, USD | against revision A at 25, USD | against revision A at 100, USD | against V1 at 5, USD | against V1 at 25, USD | against V1 at 100, USD | courtyard area, mm2 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| V1 | One-chip detector resets the expander. No LED supply switch, no relay, no over-current latch | 39 | 8 | -31 | 282 | -70 | -1.01 | -0.86 | -0.76 | +0.00 | +0.00 | +0.00 | -436 |
| V1+T | V1 with the over-current comparator on the clear input of the one-shot: R52 and C33 stay | 37 | 8 | -29 | 284 | -66 | -0.97 | -0.84 | -0.72 | +0.04 | +0.02 | +0.04 | -427 |
| V1+R | V1 with K1 kept on an expander bit: K1, D5, Q1, R11, R12, D23, JP1 stay | 33 | 8 | -25 | 288 | -59 | -0.02 | -0.04 | -0.10 | +1.00 | +0.82 | +0.66 | -197 |
| V1+S | V1 with the LED supply switch kept: Q3, R25, R26, C21 stay, Q_INV added | 35 | 9 | -26 | 287 | -58 | -0.67 | -0.63 | -0.55 | +0.35 | +0.23 | +0.21 | -396 |
| V1+T+R | V1 with the comparator on the clear input and K1 | 31 | 8 | -23 | 290 | -55 | +0.02 | -0.02 | -0.06 | +1.04 | +0.84 | +0.70 | -188 |
| V1+T+R+S | V1 with all three: V2 of the study with the fast trip put back | 27 | 9 | -18 | 295 | -43 | +0.37 | +0.23 | +0.16 | +1.38 | +1.09 | +0.92 | -148 |
| V1-H | V1 with the high-gain current channel removed as well: one amplifier, one ADC pin | 47 | 8 | -39 | 274 | -86 | -1.18 | -0.98 | -0.90 | -0.16 | -0.12 | -0.14 | -473 |
| V1+T-H | V1 with the comparator on the clear input, and one current channel | 45 | 8 | -37 | 276 | -82 | -1.14 | -0.96 | -0.86 | -0.12 | -0.10 | -0.10 | -463 |

| option | removed | added |
|---|---|---|
| V1 | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q3 R25 R26 C21 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 K1 D5 Q1 R11 R12 D23 JP1 R52 C33 R54 D8 R87 | U_WD, C_DEC, R_T1, R_T2, C_T, R_S, R_PD, U_SV |
| V1+T | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q3 R25 R26 C21 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 K1 D5 Q1 R11 R12 D23 JP1 R54 D8 R87 | U_WD, C_DEC, R_T1, R_T2, C_T, R_S, R_PD, U_SV |
| V1+R | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q3 R25 R26 C21 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 R52 C33 R54 D8 R87 | U_WD, C_DEC, R_T1, R_T2, C_T, R_S, R_PD, U_SV |
| V1+S | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 K1 D5 Q1 R11 R12 D23 JP1 R52 C33 R54 D8 R87 | U_WD, C_DEC, R_T1, R_T2, C_T, R_S, R_PD, U_SV, Q_INV |
| V1+T+R | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q3 R25 R26 C21 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 R54 D8 R87 | U_WD, C_DEC, R_T1, R_T2, C_T, R_S, R_PD, U_SV |
| V1+T+R+S | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 R54 D8 R87 | U_WD, C_DEC, R_T1, R_T2, C_T, R_S, R_PD, U_SV, Q_INV |
| V1-H | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q3 R25 R26 C21 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 K1 D5 Q1 R11 R12 D23 JP1 R52 C33 R54 D8 R87 R42 R43 C28 R44 R45 C29 R47 C31 | U_WD, C_DEC, R_T1, R_T2, C_T, R_S, R_PD, U_SV |
| V1+T-H | R29 C22 D7 C23 R30 R91 R31 R32 C24 R34 R35 R36 U7 C20 Q3 R25 R26 C21 Q2 R89 C51 D22 R14 R90 C50 R93 R94 R95 K1 D5 Q1 R11 R12 D23 JP1 R54 D8 R87 R42 R43 C28 R44 R45 C29 R47 C31 | U_WD, C_DEC, R_T1, R_T2, C_T, R_S, R_PD, U_SV |
