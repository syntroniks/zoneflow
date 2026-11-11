<!-- Written by hardware/sim/failsafe/failsafe.py bridges. Do not edit. -->

| Net bridged to `KA` | Bridge | Pin driven low after the stop | Pin high-impedance after the stop | Reported by the sequence as merged | Reported by the sequence with step 2b |
|---|---|---|---|---|---|
| push-pull, 3.3 V | 0 ohm | **valve stays on** in 4 of 4 corners | **valve stays on** in 4 of 4 corners | every corner: 1 idle | every corner: 1 idle |
| push-pull, 3.3 V | 100 ohm | detector B held, valve off | **valve stays on** in 4 of 4 corners | 2 of 4 corners: 1 idle | every corner: 1 idle, 2b float |
| push-pull, 3.3 V | 470 ohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 1 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 2.2 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 3.3 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 4.7 kohm | both let go | **valve stays on** in 4 of 4 corners | not reported | every corner: 2b float |
| push-pull, 3.3 V | 10 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 0 ohm | both let go | **valve stays on** in 3 of 4 corners | 2 of 4 corners: 2 pin-high | 3 of 4 corners: 2 pin-high, 2b float |
| zero-cross signal AC_ZC | 100 ohm | both let go | **valve stays on** in 2 of 4 corners | 1 of 4 corners: 2 pin-high | 3 of 4 corners: 2 pin-high, 2b float |
| zero-cross signal AC_ZC | 470 ohm | both let go | **valve stays on** in 3 of 4 corners | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 1 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 2.2 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 3.3 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 4.7 kohm | both let go | detector B held, valve off | not reported | 3 of 4 corners: 2b float |
| zero-cross signal AC_ZC | 10 kohm | both let go | detector B held, valve off | not reported | 1 of 4 corners: 2b float |

Corners: typical parts, 27 C; Q2 at 0.65 V, 27 C; Q2 at 0.65 V, -4.5 mV/K, 70 C; Q2 at 0.65 V, -4.5 mV/K, 70 C, low-leakage diodes, 3V3 at 3.4 V.

A step counts as reporting the bridge only if the same step passes at the same corner with no bridge. The band of step 9 is taken around the release times of the same corner with no bridge.

Cases in which a valve stays on and the sequence as merged reports nothing: 28 of the 64 combinations of net, bridge and corner.
Cases in which a valve stays on and the sequence with step 2b reports nothing: 0.

Solver check: 0 of 192 runs show the over-current latch setting with no cause.

