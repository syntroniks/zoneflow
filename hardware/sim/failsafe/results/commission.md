<!-- Written by hardware/sim/failsafe/failsafe.py commission. Do not edit. -->

5544 runs, 0 did not simulate. Solver check: 0 runs show the over-current latch setting with no cause.

A open: R30 open (the same as R91 open). B open: R14 open (the same as R90 open). Times in seconds, read at 1 ms; the firmware reads at 5 ms.

## What a commissioning cycle can measure, board at 10 to 40 C

| Boards | State | tA | tB | tB / tA | tA high stop / tA | tB high stop / tB | tA / enable time of A | enable time of A (ms) | enable time of B (ms) |
|---|---|---|---|---|---|---|---|---|---|
| Monte Carlo | healthy | 0.38 to 0.82 | 0.21 to 0.93 | 0.32 to 1.98 | 1.30 to 1.91 | 1.32 to 1.94 | 17.95 to 55.46 | 11.00 to 32.00 | 2.00 to 33.00 |
| Monte Carlo | A open | 0.89 to 1.99 | 0.21 to 0.93 | 0.14 to 0.91 | 1.29 to 1.89 | 1.32 to 1.94 | 42.43 to 131.62 | 11.00 to 32.00 | 2.00 to 33.00 |
| Monte Carlo | B open | 0.38 to 0.82 | 0.45 to 1.86 | 0.71 to 4.04 | 1.30 to 1.90 | 1.31 to 1.91 | 17.95 to 55.46 | 11.00 to 32.00 | 2.00 to 32.00 |
| grid of extremes | healthy | 0.37 to 0.83 | 0.14 to 1.23 | 0.18 to 3.10 | 1.50 to 1.56 | 1.50 to 1.64 | 17.52 to 57.00 | 12.00 to 23.00 | 2.00 to 42.00 |
| grid of extremes | A open | 0.79 to 2.01 | 0.14 to 1.23 | 0.07 to 1.36 | 1.49 to 1.54 | 1.50 to 1.64 | 42.14 to 138.71 | 12.00 to 23.00 | 2.00 to 42.00 |
| grid of extremes | B open | 0.37 to 0.83 | 0.32 to 2.51 | 0.42 to 6.33 | 1.50 to 1.56 | 1.49 to 1.58 | 17.52 to 57.00 | 13.00 to 23.00 | 2.00 to 41.00 |

## The learning limits: tA 0.33 to 0.85 s, tB 0.12 to 1.25 s

Share of the commissioning cycles (one board at one temperature of 10, 25 or 40 C) that a rule accepts. The last two rows add a limit on the ratio of the two times, which section 19.7 does not use.

| Boards | Rule | Healthy accepted | A open accepted | B open accepted |
|---|---|---|---|---|
| Monte Carlo | tA inside its learning limits | 600 of 600 | 0 of 600 | 600 of 600 |
| Monte Carlo | tB inside its learning limits | 600 of 600 | 600 of 600 | 434 of 600 |
| Monte Carlo | both: the rule of section 19.7 | 600 of 600 | 0 of 600 | 434 of 600 |
| Monte Carlo | both, and tB / tA from 0.3 to 2 | 600 of 600 | 0 of 600 | 359 of 600 |
| Monte Carlo | both, and tB / tA from 0.35 to 1.6 | 585 of 600 | 0 of 600 | 262 of 600 |
| grid of extremes | tA inside its learning limits | 192 of 192 | 16 of 192 | 192 of 192 |
| grid of extremes | tB inside its learning limits | 192 of 192 | 192 of 192 | 144 of 192 |
| grid of extremes | both: the rule of section 19.7 | 192 of 192 | 16 of 192 | 144 of 192 |
| grid of extremes | both, and tB / tA from 0.3 to 2 | 150 of 192 | 14 of 192 | 101 of 192 |
| grid of extremes | both, and tB / tA from 0.35 to 1.6 | 144 of 192 | 12 of 192 | 96 of 192 |

The same for the boards whose two reservoir capacitors, C23 and C50, are within 10 % of each other, as two parts from one reel may be:

| Boards | Rule | Healthy accepted | A open accepted | B open accepted |
|---|---|---|---|---|
| Monte Carlo | tA inside its learning limits | 186 of 186 | 0 of 186 | 186 of 186 |
| Monte Carlo | tB inside its learning limits | 186 of 186 | 186 of 186 | 119 of 186 |
| Monte Carlo | both: the rule of section 19.7 | 186 of 186 | 0 of 186 | 119 of 186 |
| Monte Carlo | both, and tB / tA from 0.3 to 2 | 186 of 186 | 0 of 186 | 101 of 186 |
| Monte Carlo | both, and tB / tA from 0.35 to 1.6 | 186 of 186 | 0 of 186 | 74 of 186 |
| grid of extremes | tA inside its learning limits | 96 of 96 | 8 of 96 | 96 of 96 |
| grid of extremes | tB inside its learning limits | 96 of 96 | 96 of 96 | 72 of 96 |
| grid of extremes | both: the rule of section 19.7 | 96 of 96 | 8 of 96 | 72 of 96 |
| grid of extremes | both, and tB / tA from 0.3 to 2 | 96 of 96 | 6 of 96 | 48 of 96 |
| grid of extremes | both, and tB / tA from 0.35 to 1.6 | 96 of 96 | 4 of 96 | 48 of 96 |

## The faulty boards that the rules accept

A board that commissions with one bleed resistor open keeps the times it measured. The absolute limits of step 9 still apply to it at every stop test.

| Boards | State | Accepted | Their Q2 threshold at 25 C (V) | Their reservoir, times nominal | Longest tA and tB of an accepted board, -20 to 70 C | Of them outside the absolute limits at some temperature |
|---|---|---|---|---|---|---|
| Monte Carlo | A open | 0 of 200 boards | | | | |
| Monte Carlo | B open | 151 of 200 boards | 0.74 to 1.45 | 0.61 to 1.14 | tA 0.82, tB 1.54 | 0 of 151 |
| grid of extremes | A open | 16 of 64 boards | 0.65 to 1.45 | 0.60 to 0.60 | tA 0.95, tB 1.14 | 0 of 16 |
| grid of extremes | B open | 48 of 64 boards | 0.65 to 1.45 | 0.60 to 1.15 | tA 0.83, tB 1.46 | 0 of 48 |

## The band of step 9

Each release time of a board at each temperature from -20 to 70 C, divided by the time the same healthy board stored when it was commissioned.

The band is 0.4 to 1.7. The column in brackets is the band of review A2, 0.5 to 1.5.

| Boards | Commissioned at | Healthy tA / stored | Healthy tB / stored | Healthy stop tests outside the band | (outside 0.5 to 1.5) | A opens later: tA / stored | of them inside the band (not found at that temperature) | boards never found at any temperature | B opens later: tB / stored | of them inside the band | boards never found at any temperature |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Monte Carlo | 10 C | 0.58 to 1.05 | 0.67 to 1.52 | 0 of 1400 | (1) | 0.86 to 2.54 | 75 of 1400 | 0 of 200 | 0.96 to 3.23 | 54 of 1400 | 0 of 200 |
| Monte Carlo | 25 C | 0.58 to 1.03 | 0.63 to 1.34 | 0 of 1400 | (0) | 0.86 to 2.50 | 77 of 1400 | 0 of 200 | 0.91 to 2.85 | 146 of 1400 | 0 of 200 |
| Monte Carlo | 40 C | 0.61 to 1.05 | 0.58 to 1.20 | 0 of 1400 | (0) | 0.90 to 2.54 | 75 of 1400 | 0 of 200 | 0.90 to 2.54 | 243 of 1400 | 0 of 200 |
| grid of extremes | 10 C | 0.55 to 1.05 | 0.57 to 1.62 | 0 of 448 | (7) | 0.82 to 2.54 | 48 of 448 | 0 of 64 | 0.81 to 3.47 | 50 of 448 | 0 of 64 |
| grid of extremes | 25 C | 0.55 to 1.03 | 0.55 to 1.41 | 0 of 448 | (0) | 0.82 to 2.50 | 48 of 448 | 0 of 64 | 0.79 to 3.02 | 76 of 448 | 0 of 64 |
| grid of extremes | 40 C | 0.58 to 1.06 | 0.54 to 1.24 | 0 of 448 | (0) | 0.86 to 2.56 | 48 of 448 | 0 of 64 | 0.82 to 2.66 | 88 of 448 | 0 of 64 |

Where a bleed resistor that opens after commissioning stays inside the band, by board temperature at the stop test. Each cell is for boards commissioned at 10, 25 and 40 C.

| Boards | Stop test at | A open inside the band | B open inside the band |
|---|---|---|---|
| Monte Carlo | -20 C | 0, 0, 0 of 200 | 0, 74, 129 of 200 |
| Monte Carlo | 0 C | 0, 0, 0 of 200 | 0, 0, 27 of 200 |
| Monte Carlo | 10 C | 0, 0, 0 of 200 | 0, 0, 0 of 200 |
| Monte Carlo | 25 C | 0, 0, 0 of 200 | 0, 0, 0 of 200 |
| Monte Carlo | 40 C | 0, 0, 0 of 200 | 0, 0, 0 of 200 |
| Monte Carlo | 55 C | 8, 8, 6 of 200 | 3, 12, 14 of 200 |
| Monte Carlo | 70 C | 67, 69, 69 of 200 | 51, 60, 73 of 200 |
| grid of extremes | -20 C | 0, 0, 0 of 64 | 4, 28, 30 of 64 |
| grid of extremes | 0 C | 0, 0, 0 of 64 | 0, 0, 8 of 64 |
| grid of extremes | 10 C | 0, 0, 0 of 64 | 0, 0, 2 of 64 |
| grid of extremes | 25 C | 0, 0, 0 of 64 | 0, 0, 0 of 64 |
| grid of extremes | 40 C | 0, 0, 0 of 64 | 0, 0, 0 of 64 |
| grid of extremes | 55 C | 16, 16, 16 of 64 | 14, 16, 16 of 64 |
| grid of extremes | 70 C | 32, 32, 32 of 64 | 32, 32, 32 of 64 |

## How far the stored value moves with the temperature at commissioning

The release time of a healthy board at 10 C and at 40 C, divided by its time at 25 C.

| Boards | tA at 10 C | tA at 40 C | tB at 10 C | tB at 40 C |
|---|---|---|---|---|
| Monte Carlo | 0.98 to 1.00 | 0.96 to 1.02 | 0.88 to 0.97 | 1.00 to 1.12 |
| grid of extremes | 0.98 to 1.00 | 0.95 to 1.02 | 0.87 to 0.99 | 0.96 to 1.14 |

