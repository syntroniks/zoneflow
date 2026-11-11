<!-- Written by hardware/sim/failsafe/failsafe.py ldo. Do not edit. -->

945 runs, 0 did not simulate. Solver check: 0 runs show the over-current latch setting with no cause.

U3 shorted input to output: `3V3` is joined to `V5`. Zone 1 runs when the fault appears, unless the row says otherwise. "Without U13" is the board as review A2 left it.

`3V3` after the fault: 4.82 to 5.07 V over the corners.

## The processor is dead: the pin stops for good 0.5 s after the fault

| Detector | Pin stops | Valve still on 5 s later | Valve current ends | Gate drive ends | Relay coil current ends |
|---|---|---|---|---|---|
| with U13 | low | 0 of 48 corners | 0.5 ms after the fault | 0.5 ms after the fault | 0.00 s after the fault |
| with U13 | high | 0 of 48 corners | 0.5 ms after the fault | 0.5 ms after the fault | 0.00 s after the fault |
| with U13 | hiz | 0 of 48 corners | 0.5 ms after the fault | 0.5 ms after the fault | 0.00 s after the fault |
| without U13 | low | 0 of 48 corners | 0.23 to 0.84 s after the stop | 0.27 to 0.84 s after the stop | 0.23 to 0.84 s after the stop |
| without U13 | high | 0 of 48 corners | 0.36 to 1.25 s after the stop | 0.42 to 1.25 s after the stop | 0.36 to 1.25 s after the stop |
| without U13 | hiz | 0 of 48 corners | 0.23 to 0.84 s after the stop | 0.27 to 0.84 s after the stop | 0.23 to 0.84 s after the stop |

## The processor is erratic: the pin keeps toggling, nothing writes the expander

| Detector | Keep-alive after the fault, high / low | Valve still on 5.5 s after the fault | Valve current ends after the fault |
|---|---|---|---|
| with U13 | 5 ms / 5 ms | 0 of 4 corners | 0.5 ms |
| with U13 | 20 ms / 20 ms | 0 of 4 corners | 0.5 ms |
| with U13 | 1 ms / 20 ms | 0 of 4 corners | 0.5 ms |
| with U13 | 100 ms / 100 ms | 0 of 4 corners | 0.5 ms |
| with U13 | 0.5 ms / 0.5 ms | 0 of 4 corners | 0.5 ms |
| without U13 | 5 ms / 5 ms | 4 of 4 corners | never |
| without U13 | 20 ms / 20 ms | 4 of 4 corners | never |
| without U13 | 1 ms / 20 ms | 4 of 4 corners | never |
| without U13 | 100 ms / 100 ms | 3 of 4 corners | 0.39 s |
| without U13 | 0.5 ms / 0.5 ms | 4 of 4 corners | never |

## The pin is joined to its supply and the rail swings between 2.0 V and `V5`

| Detector | Rail pulled down at | Valve still on 5.5 s after the fault | Valve current ends after the fault |
|---|---|---|---|
| with U13 | 2 Hz | 0 of 4 corners | 0.5 ms |
| with U13 | 10 Hz | 0 of 4 corners | 0.5 ms |
| with U13 | 50 Hz | 0 of 4 corners | 0.5 ms |
| with U13 | 200 Hz | 0 of 4 corners | 0.5 ms |
| without U13 | 2 Hz | 0 of 4 corners | 0.20 to 0.70 s |
| without U13 | 10 Hz | 1 of 4 corners | 0.22 to 0.82 s |
| without U13 | 50 Hz | 4 of 4 corners | never |
| without U13 | 200 Hz | 4 of 4 corners | never |

## No edges: U3 shorted from power-up, no valve asked for

| Detector | Keep-alive pin | Drive enabled at any time | Expander out of reset at any time | Reservoir of A, highest (V) | Gate of Q2, highest (V) |
|---|---|---|---|---|---|
| with U13 | pin low from power-up | 0 of 24 corners | 0 of 24 | 0.02 | 0.00 |
| with U13 | pin high from power-up | 0 of 24 corners | 0 of 24 | 1.57 | 1.53 |
| with U13 | pin high-impedance from power-up | 0 of 24 corners | 0 of 24 | 0.02 | 0.00 |
| with U13 | one rising edge, then high | 0 of 24 corners | 0 of 24 | 1.57 | 1.53 |
| with U13 | one pulse of 60 us (a power-up glitch) | 0 of 24 corners | 0 of 24 | 0.14 | 0.03 |
| with U13 | one pulse of 100 ms | 0 of 24 corners | 0 of 24 | 1.57 | 1.53 |
| with U13 | 0.5 Hz | 0 of 24 corners | 0 of 24 | 1.62 | 1.58 |
| with U13 | 1 Hz | 0 of 24 corners | 0 of 24 | 1.86 | 1.81 |
| with U13 | 2 Hz | 0 of 24 corners | 0 of 24 | 2.33 | 2.27 |
| with U13 | 3 Hz | 0 of 24 corners | 0 of 24 | 2.88 | 2.63 |
| with U13 | 5 Hz | 0 of 24 corners | 0 of 24 | 3.32 | 3.10 |
| without U13 | pin low from power-up | 0 of 24 corners | 0 of 24 | 0.02 | 0.00 |
| without U13 | pin high from power-up | 0 of 24 corners | 0 of 24 | 1.57 | 1.53 |
| without U13 | pin high-impedance from power-up | 0 of 24 corners | 0 of 24 | 0.02 | 0.00 |
| without U13 | one rising edge, then high | 0 of 24 corners | 0 of 24 | 1.57 | 1.53 |
| without U13 | one pulse of 60 us (a power-up glitch) | 0 of 24 corners | 0 of 24 | 0.14 | 0.03 |
| without U13 | one pulse of 100 ms | 0 of 24 corners | 0 of 24 | 1.57 | 1.53 |
| without U13 | 0.5 Hz | 0 of 24 corners | 0 of 24 | 1.62 | 1.58 |
| without U13 | 1 Hz | 0 of 24 corners | 0 of 24 | 1.86 | 1.81 |
| without U13 | 2 Hz | 0 of 24 corners | 0 of 24 | 2.33 | 2.27 |
| without U13 | 3 Hz | 10 of 24 corners | 10 of 24 | 2.88 | 2.63 |
| without U13 | 5 Hz | 20 of 24 corners | 20 of 24 | 3.32 | 3.10 |

## The step itself: the keep-alive stopped 3.4 s before U3 shorts

| Detector | Pin | Drive enabled after the fault | Valve on after the fault | Reservoir of A, highest (V) | Gate of Q2, highest (V) |
|---|---|---|---|---|---|
| with U13 | driven low | 0 of 4 corners | 0 of 4 | 0.03 | 0.01 |
| with U13 | joined to its supply | 0 of 4 corners | 0 of 4 | 0.61 | 0.55 |
| with U13 | high-impedance | 0 of 4 corners | 0 of 4 | 0.03 | 0.00 |
| without U13 | driven low | 0 of 4 corners | 0 of 4 | 0.03 | 0.01 |
| without U13 | joined to its supply | 0 of 4 corners | 0 of 4 | 0.61 | 0.55 |
| without U13 | high-impedance | 0 of 4 corners | 0 of 4 | 0.03 | 0.00 |

## Where U13 acts

The regulator's set point is stepped to the level in the first column while zone 1 runs with typical parts. Each cell gives the voltage `3V3` then has at the board, a little under the set point, and what the valve does: "off" means the valve current ended, after the time shown.

| Set point (V) | U13 and divider: trips lowest | U13 and divider: typical | U13 and divider: trips highest |
|---|---|---|---|
| 3.6 | 3.587 V: runs on | 3.587 V: runs on | 3.587 V: runs on |
| 3.7 | 3.687 V: runs on | 3.687 V: runs on | 3.687 V: runs on |
| 3.8 | 3.787 V: runs on | 3.787 V: runs on | 3.787 V: runs on |
| 3.9 | 3.886 V: runs on | 3.886 V: runs on | 3.886 V: runs on |
| 3.95 | 3.938 V: off after 3.5 ms | 3.936 V: runs on | 3.936 V: runs on |
| 4 | 3.988 V: off after 2.5 ms | 3.986 V: runs on | 3.986 V: runs on |
| 4.05 | 4.037 V: off after 2.0 ms | 4.036 V: runs on | 4.036 V: runs on |
| 4.1 | 4.087 V: off after 2.0 ms | 4.087 V: off after 3.0 ms | 4.085 V: runs on |
| 4.15 | 4.137 V: off after 2.0 ms | 4.137 V: off after 2.5 ms | 4.135 V: runs on |
| 4.2 | 4.187 V: off after 1.5 ms | 4.187 V: off after 2.0 ms | 4.187 V: off after 4.0 ms |
| 4.3 | 4.287 V: off after 1.5 ms | 4.287 V: off after 2.0 ms | 4.287 V: off after 2.5 ms |

