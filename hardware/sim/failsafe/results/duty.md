<!-- Written by hardware/sim/failsafe/failsafe.py duty. Do not edit. -->

Gate of Q2 above its threshold while toggling, at the worst corner for detector B, and whether the relay is fully driven (Q2 drops 0.1 V or less).

| Keep-alive, high / low | 3V3 at 3.0 V | 3V3 at 3.2 V, the lowest U3 delivers | 3V3 at 3.0 V, R89 220 ohm |
|---|---|---|---|
| 1 / 1 ms | +0.42 V, held | +0.60 V, held | +0.55 V, held |
| 2.5 / 2.5 ms | +0.41 V, held | +0.58 V, held | +0.53 V, held |
| 5 / 5 ms | +0.38 V, held | +0.55 V, held | +0.48 V, held |
| 10 / 10 ms | +0.29 V, held | +0.45 V, held | +0.35 V, held |
| 20 / 20 ms | +0.08 V, held | +0.22 V, held | +0.10 V, held |
| 1 / 20 ms | -0.25 V, **not held** | -0.13 V, **not held** | +0.13 V, held |
| 2 / 20 ms | -0.00 V, **not held** | +0.13 V, held | +0.22 V, held |
| 2.5 / 20 ms | +0.05 V, held | +0.19 V, held | +0.24 V, held |
| 3 / 20 ms | +0.09 V, held | +0.23 V, held | +0.24 V, held |
| 5 / 20 ms | +0.15 V, held | +0.30 V, held | +0.24 V, held |
| 20 / 1 ms | -0.22 V, **not held** | -0.10 V, **not held** | +0.18 V, held |
| 20 / 2 ms | +0.03 V, **not held** | +0.17 V, held | +0.28 V, held |
| 20 / 2.5 ms | +0.09 V, held | +0.23 V, held | +0.29 V, held |
| 20 / 3 ms | +0.12 V, held | +0.27 V, held | +0.30 V, held |
| 20 / 5 ms | +0.19 V, held | +0.34 V, held | +0.30 V, held |
| 1 / 9 ms | +0.10 V, held | +0.25 V, held | +0.36 V, held |
| 2.5 / 7.5 ms | +0.32 V, held | +0.48 V, held | +0.45 V, held |
| 2.5 / 17.5 ms | +0.10 V, held | +0.25 V, held | +0.28 V, held |
| 17.5 / 2.5 ms | +0.13 V, held | +0.28 V, held | +0.32 V, held |

Solver check: 0 of 57 runs show the over-current latch setting with no cause.

