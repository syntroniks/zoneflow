<!-- Written by hardware/sim/failsafe/failsafe.py floatload. Do not edit. -->

288 runs, 0 did not simulate. Corners: typical parts, 27 C; Q2 at 0.65 V, 27 C; Q2 at 0.65 V, -4.5 mV/K, 70 C; Q2 at 0.65 V, -4.5 mV/K, 70 C, low-leakage diodes, 3V3 at 3.4 V.

A push-pull net of 3.3 V behind 30 ohm is bridged to `KA`. Second column: what the net does. Third: in how many of the four corners the valve stays on when the pin goes high-impedance with the valve running. Fourth: in how many the self-test reports the bridge, and by which step.

| Bridge | Net | Valve stays on, pin high-impedance | Detector held, valve off | Reported by the self-test |
|---|---|---|---|---|
| 0 ohm | 50 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 1 idle |
| 0 ohm | 50 % high, 2 kHz | 4 of 4 | 0 of 4 | 4 of 4: 1 idle |
| 0 ohm | 25 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 1 idle |
| 0 ohm | 10 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 1 idle |
| 0 ohm | 90 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 1 idle |
| 0 ohm | steady high | 0 of 4 | 0 of 4 | 4 of 4: 1 idle, 2b float |
| 470 ohm | 50 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 470 ohm | 50 % high, 2 kHz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 470 ohm | 25 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 470 ohm | 10 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 470 ohm | 90 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 470 ohm | steady high | 0 of 4 | 0 of 4 | 4 of 4: 2b float |
| 1 kohm | 50 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 1 kohm | 50 % high, 2 kHz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 1 kohm | 25 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 1 kohm | 10 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 1 kohm | 90 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 1 kohm | steady high | 0 of 4 | 0 of 4 | 1 of 4: 2b float |
| 2.2 kohm | 50 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 2.2 kohm | 50 % high, 2 kHz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 2.2 kohm | 25 % high, 500 Hz | 4 of 4 | 0 of 4 | 4 of 4: 2b float |
| 2.2 kohm | 10 % high, 500 Hz | 1 of 4 | 3 of 4 | 3 of 4: 2b float |
| 2.2 kohm | 90 % high, 500 Hz | 1 of 4 | 3 of 4 | 3 of 4: 2b float |
| 2.2 kohm | steady high | 0 of 4 | 0 of 4 | 0 of 4 |
| 3.3 kohm | 50 % high, 500 Hz | 1 of 4 | 2 of 4 | 3 of 4: 2b float |
| 3.3 kohm | 50 % high, 2 kHz | 1 of 4 | 2 of 4 | 3 of 4: 2b float |
| 3.3 kohm | 25 % high, 500 Hz | 0 of 4 | 3 of 4 | 3 of 4: 2b float |
| 3.3 kohm | 10 % high, 500 Hz | 0 of 4 | 3 of 4 | 3 of 4: 2b float |
| 3.3 kohm | 90 % high, 500 Hz | 0 of 4 | 3 of 4 | 3 of 4: 2b float |
| 3.3 kohm | steady high | 0 of 4 | 0 of 4 | 0 of 4 |
| 4.7 kohm | 50 % high, 500 Hz | 0 of 4 | 3 of 4 | 3 of 4: 2b float |
| 4.7 kohm | 50 % high, 2 kHz | 0 of 4 | 3 of 4 | 3 of 4: 2b float |
| 4.7 kohm | 25 % high, 500 Hz | 0 of 4 | 3 of 4 | 3 of 4: 2b float |
| 4.7 kohm | 10 % high, 500 Hz | 0 of 4 | 2 of 4 | 1 of 4: 2b float |
| 4.7 kohm | 90 % high, 500 Hz | 0 of 4 | 2 of 4 | 2 of 4: 2b float |
| 4.7 kohm | steady high | 0 of 4 | 0 of 4 | 0 of 4 |

Cases in which a valve stays on and the self-test, run with the net doing the same thing, reports nothing: 0.

Bridges that hold the valve on with the net at some duty and that a self-test with the net at 50 % does not report: 0.

