<!-- Written by hardware/sim/failsafe/failsafe.py warmup. Do not edit. -->

tA / tB in seconds: the time from the stop until the gate-drive read-back is low and until the relay read-back is high, after the keep-alive has toggled at 5 ms edges for the time in the first column. No valve and no relay.

| Toggled for | typical parts, 27 C | latest release: 70 C, 3.6 V, large capacitors, Q2 at 0.65 V | earliest release: 70 C, 3.0 V, small capacitors, Q2 at 1.45 V, leaky diodes | slowest to charge: -20 C, 3.0 V, large reservoirs, small pump capacitors, Q2 at 1.45 V |
|---|---|---|---|---|
| 0.1 s | 0.66 / 0.51 | 0.81 / 1.35 | 0.22 / 0.12 | 0.61 / 0.08 |
| 0.15 s | 0.67 / 0.55 | 0.82 / 1.41 | 0.22 / 0.13 | 0.66 / 0.14 |
| 0.2 s | 0.67 / 0.57 | 0.82 / 1.43 | 0.22 / 0.13 | 0.67 / 0.17 |
| 0.3 s | 0.67 / 0.57 | 0.82 / 1.44 | 0.22 / 0.13 | 0.68 / 0.19 |
| 0.5 s | 0.67 / 0.57 | 0.83 / 1.45 | 0.22 / 0.13 | 0.68 / 0.20 |
| 1 s | 0.67 / 0.57 | 0.83 / 1.45 | 0.22 / 0.13 | 0.68 / 0.20 |
| 2 s | 0.67 / 0.57 | 0.83 / 1.45 | 0.22 / 0.13 | 0.68 / 0.20 |

Solver check: 0 of 28 runs show the over-current latch setting with no cause.

