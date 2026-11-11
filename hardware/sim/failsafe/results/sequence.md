<!-- Written by hardware/sim/failsafe/failsafe.py sequence. Do not edit. -->

The self-test sequence of design section 19.7 on a board with no fault, with step 2 holding the pin high for 2 s: 96 corners, 0 did not simulate, 0 fail a step.

Release times in step 9 over the corners that pass: tA 0.22 to 0.83 s, tB 0.10 to 1.45 s.

## One rising edge from rest, then the pin stays high

How long the relay read-back does not read high after the edge. Step 2 must hold the pin for longer than this before it reads. The gate-drive read-back stays low in every corner unless the last column says otherwise.

| Temperature | Relay read-back not high for, longest (s) | Corner | Gate of Q2, highest (V) | Corners in which the gate-drive read-back left low |
|---|---|---|---|---|
| -20 C | 0.68 | -20 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -1.5 mV/K, Schottky IS 6.6e-07 | 1.07 | 0 of 32 |
| 27 C | 0.83 | 27 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 1.5e-08 | 1.10 | 0 of 32 |
| 70 C | 1.33 | 70 C, 3V3 3.6 V, large capacitors, Vth 0.65 V at -4.5 mV/K, Schottky IS 1.5e-08 | 1.10 | 0 of 32 |

Corners in which it is longer than the 1.0 s that review A2 gave step 2: 2 of 96 (1.09, 1.33 s).
Solver check: 0 of 192 runs show the over-current latch setting with no cause.

